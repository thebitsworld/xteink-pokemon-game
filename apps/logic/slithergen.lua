-- Slitherlink puzzle generator, loaded only while a puzzle is made. Usage:
--   local clues = smudge.dofile("slithergen.lua")().reduce(n, T, clues, rand, millis, deadline, minClues)
--
-- A random loop, its clues and the grid's layout T come from slitherloop.lua,
-- which make.lua loads and drops before this module (the X3 cannot hold
-- both). Every cell gets its clue, then clues are taken away in random order as long as deduction
-- alone still finds the loop (the rules at each dot and number, plus "a line here -
-- or none - would break the puzzle"), so every puzzle has one solution and
-- needs no guessing. Once the time is up the clues still there stay: a slow
-- reader gets a puzzle with more clues, never none. The tests check with a
-- full search that the solution is unique.

return function()
local G = {}
local byte = string.byte

-- A solver for n x n puzzles: solver.deduce(clues) tells whether deduction
-- alone finds the solution (clues: one number per cell, -1 for none). T is
-- the grid's layout from slitherloop.lua's topology(n). The solver's pieces
-- are open for a full search, which the tests use to count solutions.
function G.solver(n, T)
    local V, E, C = (n + 1) * (n + 1), 2 * n * (n + 1), n * n
    local ev1, ev2, ec1, ec2, vinc, cedge = T.ev1, T.ev2, T.ec1, T.ec2, T.vinc, T.cedge

    -- st[e]: -1 open, 0 empty, 1 line. vd[v] = lines * 8 + open edges at a
    -- dot, cs[c] = lines * 8 + open sides of a cell; both + 64 while queued.
    -- trail: the edges set, undone by undo(). q: a ring of the dots and
    -- cells to look at, each in it once at most.
    local st, vd, cs, trail, q = {}, {}, {}, {}, {}
    local tn, qh, qt, QN = 0, 1, 0, V + C
    local odd, closed, closedAt, clue = 0, false, 0, nil

    -- Dots are 1..V in the queue, cells V + c.
    local function push(x)
        if x <= V then
            if vd[x] >= 64 then return end
            vd[x] = vd[x] + 64
        else
            if cs[x - V] >= 64 then return end
            cs[x - V] = cs[x - V] + 64
        end
        qt = qt + 1
        q[(qt - 1) % QN + 1] = x
    end
    local function unqueue(x)
        if x <= V then vd[x] = vd[x] % 64 else cs[x - V] = cs[x - V] % 64 end
    end
    local function clearQueue()
        for i = qh, qt do unqueue(q[(i - 1) % QN + 1]) end
        qh, qt = 1, 0
    end

    -- The far end of the path of lines from dot v (which has one line).
    local function farEnd(v)
        local from = 0
        while true do
            local nextE = 0
            for k = (v - 1) * 4 + 1, v * 4 do
                local f = byte(vinc, k)
                if f > 0 and f ~= from and st[f] == 1 then
                    nextE = f
                    break
                end
            end
            if nextE == 0 then return v end
            local a = byte(ev1, nextE)
            v = a == v and byte(ev2, nextE) or a
            from = nextE
        end
    end

    local function set(e, val)
        local s = st[e]
        if s ~= -1 then return s == val end
        local a, b, c1, c2 = byte(ev1, e), byte(ev2, e), byte(ec1, e), byte(ec2, e)
        if val == 1 then
            local da, db = vd[a] % 64 // 8, vd[b] % 64 // 8
            if closed or da >= 2 or db >= 2 then return false end
            local closing = da == 1 and db == 1 and farEnd(a) == b
            tn = tn + 1
            trail[tn], st[e] = e, 1
            vd[a], vd[b] = vd[a] + 7, vd[b] + 7
            odd = odd + (da == 0 and 1 or -1) + (db == 0 and 1 or -1)
            cs[c1] = cs[c1] + 7
            if c2 > 0 then cs[c2] = cs[c2] + 7 end
            if closing then
                -- The line closes a loop: only the final one, with no loose ends.
                closed, closedAt = true, tn
                if odd ~= 0 then return false end
            end
        else
            tn = tn + 1
            trail[tn], st[e] = e, 0
            vd[a], vd[b] = vd[a] - 1, vd[b] - 1
            cs[c1] = cs[c1] - 1
            if c2 > 0 then cs[c2] = cs[c2] - 1 end
        end
        push(a)
        push(b)
        push(V + c1)
        if c2 > 0 then push(V + c2) end
        return true
    end

    local function undo(mark)
        while tn > mark do
            local e = trail[tn]
            local a, b, c1, c2 = byte(ev1, e), byte(ev2, e), byte(ec1, e), byte(ec2, e)
            if st[e] == 1 then
                if closed and closedAt == tn then closed = false end
                vd[a], vd[b] = vd[a] - 7, vd[b] - 7
                odd = odd + (vd[a] % 64 // 8 == 1 and 1 or -1) + (vd[b] % 64 // 8 == 1 and 1 or -1)
                cs[c1] = cs[c1] - 7
                if c2 > 0 then cs[c2] = cs[c2] - 7 end
            else
                vd[a], vd[b] = vd[a] + 1, vd[b] + 1
                cs[c1] = cs[c1] + 1
                if c2 > 0 then cs[c2] = cs[c2] + 1 end
            end
            st[e] = -1
            tn = tn - 1
        end
    end

    -- Sets every open edge in slots base+1..base+4 of `list` to val.
    local function setOpen(list, base, val)
        for k = base + 1, base + 4 do
            local e = byte(list, k)
            if e > 0 and st[e] == -1 and not set(e, val) then return false end
        end
        return true
    end

    local function propagate()
        while qh <= qt do
            local x = q[(qh - 1) % QN + 1]
            qh = qh + 1
            unqueue(x)
            local ok = true
            if x <= V then
                local d, u = vd[x] // 8, vd[x] % 8
                if u > 0 then
                    if d == 2 or (d == 0 and u == 1) then
                        ok = setOpen(vinc, (x - 1) * 4, 0)
                    elseif d == 1 and u == 1 then
                        ok = setOpen(vinc, (x - 1) * 4, 1)
                    end
                elseif d == 1 then
                    ok = false
                end
            else
                local c = x - V
                local k = clue[c]
                if k >= 0 then
                    local on, u = cs[c] // 8, cs[c] % 8
                    if on > k or on + u < k then
                        ok = false
                    elseif u > 0 then
                        if on == k then
                            ok = setOpen(cedge, (c - 1) * 4, 0)
                        elseif on + u == k then
                            ok = setOpen(cedge, (c - 1) * 4, 1)
                        end
                    end
                end
            end
            if not ok then
                clearQueue()
                return false
            end
        end
        qh, qt = 1, 0
        return true
    end

    local function reset(clues)
        clearQueue()
        clue = clues
        for e = 1, E do st[e] = -1 end
        for v = 1, V do
            local u = 0
            for k = (v - 1) * 4 + 1, v * 4 do
                if byte(vinc, k) > 0 then u = u + 1 end
            end
            vd[v] = u
        end
        for c = 1, C do
            cs[c] = 4
            if clue[c] >= 0 then push(V + c) end
        end
        tn, odd, closed, closedAt = 0, 0, false, 0
    end

    -- With the loop closed, every other edge is empty.
    local function finish()
        for e = 1, E do
            if st[e] == -1 then set(e, 0) end
        end
        return propagate()
    end

    -- The pieces, for a search (the tests' count of solutions).
    local solver = { set = set, undo = undo, propagate = propagate, reset = reset, finish = finish,
                     st = st, vd = vd, cs = cs }
    function solver.mark() return tn end
    function solver.closed() return closed end

    -- Deduction: the local rules, then for each open edge "a line here (or
    -- none) leads to a contradiction", until the loop is found or nothing moves.
    function solver.deduce(clues)
        reset(clues)
        if not propagate() then return false end
        local progress = true
        while progress and not closed do
            progress = false
            for e = 1, E do
                if st[e] == -1 then
                    local mark = tn
                    local on = set(e, 1) and propagate()
                    undo(mark)
                    if not on then
                        -- No line here (a contradiction either way: no solution).
                        if not (set(e, 0) and propagate()) then return false end
                        progress = true
                    else
                        local off = set(e, 0) and propagate()
                        undo(mark)
                        if not off then
                            if not (set(e, 1) and propagate()) then return false end
                            progress = true
                        end
                    end
                end
            end
        end
        return closed and finish()
    end

    return solver
end

local function gcd(a, b)
    while b ~= 0 do a, b = b, a % b end
    return a
end

-- clues table -> string, "." for none.
local function clueString(clues)
    local out = {}
    for i = 1, #clues do out[i] = clues[i] >= 0 and tostring(clues[i]) or "." end
    return table.concat(out)
end

-- Takes clues away from a full set (clues: every cell's, from
-- slitherloop.lua; T: its topology(n)) while deduction still solves the
-- puzzle: the clue string, or nil when deduction cannot solve even the full
-- set. Stops at millis() > deadline, or when only minClues are left.
function G.reduce(n, T, clues, rand, millis, deadline, minClues)
    local solver = G.solver(n, T)
    if not solver.deduce(clues) then return nil end
    -- Each clue in turn, in a random order (a random start and a step with
    -- no factor in common with the cell count, which needs no table), goes
    -- if deduction still solves the puzzle.
    local N = n * n
    local step = rand(N - 1)
    while gcd(step, N) ~= 1 do step = rand(N - 1) end
    local i, left = rand(N), N
    for _ = 1, N do
        if left <= (minClues or 0) or millis() > deadline then break end
        i = (i + step - 1) % N + 1
        local saved = clues[i]
        clues[i] = -1
        if solver.deduce(clues) then left = left - 1 else clues[i] = saved end
    end
    return clueString(clues)
end

return G
end
