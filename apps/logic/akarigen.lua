-- Light Up puzzle generator, loaded only while a puzzle is made. Usage:
--   local G = smudge.dofile("akarigen.lua")()
--   local grid, sol = G.generate(size, rand, millis, capMs, easy)
--
-- Black cells are scattered at random (symmetric), then bulbs go into
-- unlit white cells at random until every cell is lit: that is the
-- solution. Every black cell first gets its number (the bulbs next to it);
-- the layout is kept only if those numbers allow one solution. Then the
-- numbers are taken away in random order as long as a single solution
-- stays (for an easy puzzle: as long as reasoning alone finds it). Grids
-- are akari.lua's: "." white, "#" black, "0".."4" a black cell's number.
-- While a puzzle is made it is one flat table g, cell by cell, row by row:
-- -1 a white cell, -3 one with a bulb in the solution, 0..4 a black cell's
-- number, 5 a black cell without one.

return function()
local G = {}
local byte, char, unpack = string.byte, string.char, table.unpack

-- t[1..n] as a string of bytes, a piece at a time: unpacking all at once
-- would grow the Lua stack (by kilobytes on the X3).
local function pack(t, n)
    local s = ""
    for a = 1, n, 32 do s = s .. char(unpack(t, a, math.min(n, a + 31))) end
    return s
end

-- A random layout and its solution: g. `density`: % of black cells.
function G.layout(n, rand, density)
    local g, N = {}, n * n
    for i = 1, N do g[i] = -1 end
    for i = 1, N do
        local j = N + 1 - i
        if i <= j and rand(100) <= density then g[i], g[j] = 5, 5 end
    end
    -- A white cell walled in on all sides turns black.
    local unlit = 0
    for i = 1, N do
        local c = (i - 1) % n
        if g[i] < 0 and (i <= n or g[i - n] >= 0) and (i > N - n or g[i + n] >= 0)
            and (c == 0 or g[i - 1] >= 0) and (c == n - 1 or g[i + 1] >= 0) then
            g[i] = 5
        end
        if g[i] < 0 then unlit = unlit + 1 end
    end
    -- Bulbs in random unlit cells (-1) until none is left; lit cells are -2
    -- meanwhile.
    while unlit > 0 do
        local i
        repeat i = rand(N) until g[i] == -1
        g[i], unlit = -3, unlit - 1
        for d = 1, 4 do
            local step = d == 1 and -n or d == 2 and n or d == 3 and -1 or 1
            local j = i + step
            while j >= 1 and j <= N and (d < 3 or (j - 1) // n == (i - 1) // n) and g[j] < 0 do
                if g[j] == -1 then g[j], unlit = -2, unlit - 1 end
                j = j + step
            end
        end
    end
    for i = 1, N do
        if g[i] == -2 then g[i] = -1 end
    end
    return g
end

-- A solver for the puzzle in g (n x n). solver.deduce() tells whether
-- reasoning alone solves it; solver.count(cap) counts the solutions: 0, 1
-- or 2 (two or more), or nil past `cap` search steps. The solver keeps its
-- white cells in g: -1 open, -4 no bulb, -5 a bulb. Each row or column
-- stretch of white cells (a segment) holds at most one bulb and keeps
-- counts of its bulbs (sb) and open cells (su). Reasoning: a bulb clears
-- its segments; a number with as many open neighbours as bulbs still
-- missing gets them all, one already met clears the rest; an unlit cell
-- with one place left to be lit from gets a bulb there. The search tries a
-- bulb, then none, in an open cell that can light the unlit cell with the
-- fewest places; the branches are a stack, changes a trail of cells.
function G.solver(g, n)
    local N = n * n
    -- Segments 1..S, the across ones first (1..A): SF their first cells, SL
    -- their lengths; HS each white cell's across segment, VS its down one
    -- less A. Strings, a byte each: the X3 has little room. t, sb and su
    -- lend their room meanwhile.
    local S, A, HS, VS, t, sb, su = 0, 0, nil, nil, {}, {}, {}
    for down = 0, 1 do
        local step = down == 1 and n or 1
        for i = 1, N do t[i] = 0 end
        for i = 1, N do
            local prev = i - step
            if g[i] < 0 and (prev < 1 or (down == 0 and (i - 1) % n == 0) or g[prev] >= 0) then
                S = S + 1
                local j, len = i, 0
                while j <= N and g[j] < 0 and (j == i or down == 1 or (j - 1) % n > 0) do
                    t[j], len, j = S - A, len + 1, j + step
                end
                sb[S], su[S] = i, len
            end
        end
        if down == 0 then HS, A = pack(t, N), S else VS = pack(t, N) end
    end
    t = nil
    local SF, SL = pack(sb, S), pack(su, S)

    local tr, tn, trail = nil, 0, false -- the trail: only while searching
    local function set(i, v)
        if trail then
            tn = tn + 1
            tr[tn] = i
        end
        g[i] = v
        local h, w = byte(HS, i), byte(VS, i) + A
        su[h], su[w] = su[h] - 1, su[w] - 1
        if v == -5 then sb[h], sb[w] = sb[h] + 1, sb[w] + 1 end
    end
    local function undo(mark)
        while tn > mark do
            local i = tr[tn]
            local h, w = byte(HS, i), byte(VS, i) + A
            su[h], su[w] = su[h] + 1, su[w] + 1
            if g[i] == -5 then sb[h], sb[w] = sb[h] - 1, sb[w] - 1 end
            g[i] = -1
            tn = tn - 1
        end
    end
    -- An open cell of segment s, or nil.
    local function openIn(s)
        local i, step = byte(SF, s), s > A and n or 1
        for k = i, i + step * (byte(SL, s) - 1), step do
            if g[k] == -1 then return k end
        end
        return nil
    end
    -- Neighbour d (1 up, 2 down, 3 left, 4 right) of cell i if white, else nil.
    local function white(i, d)
        local j = d == 1 and i - n or d == 2 and i + n or d == 3 and i - 1 or i + 1
        if j < 1 or j > N or (d > 2 and (j - 1) // n ~= (i - 1) // n) or g[j] >= 0 then return nil end
        return j
    end
    local function propagate()
        local changed = true
        while changed do
            changed = false
            for s = 1, S do
                if sb[s] > 1 then return false end
                if sb[s] == 1 and su[s] > 0 then
                    local k = openIn(s)
                    while k do
                        set(k, -4)
                        k = openIn(s)
                    end
                    changed = true
                end
            end
            for i = 1, N do
                local want = g[i]
                if want >= 0 and want <= 4 then
                    local have, open = 0, 0
                    for d = 1, 4 do
                        local j = white(i, d)
                        if j and g[j] == -5 then have = have + 1 elseif j and g[j] == -1 then open = open + 1 end
                    end
                    if have > want or have + open < want then return false end
                    if open > 0 and (have == want or have + open == want) then
                        for d = 1, 4 do
                            local j = white(i, d)
                            if j and g[j] == -1 then set(j, have == want and -4 or -5) end
                        end
                        changed = true
                    end
                end
            end
            for i = 1, N do
                local v = g[i]
                if v < 0 and v ~= -5 then
                    local h, w = byte(HS, i), byte(VS, i) + A
                    if sb[h] == 0 and sb[w] == 0 then
                        local places = su[h] + su[w] - (v == -1 and 1 or 0)
                        if places == 0 then return false end
                        if places == 1 then
                            set(v == -1 and i or openIn(su[h] > 0 and h or w), -5)
                            changed = true
                        end
                    end
                end
            end
        end
        return true
    end
    -- An open cell that can light the unlit cell with the fewest places.
    local function choose()
        local best, bestN = nil, 99
        for i = 1, N do
            local v = g[i]
            if v < 0 and v ~= -5 then
                local h, w = byte(HS, i), byte(VS, i) + A
                if sb[h] == 0 and sb[w] == 0 and su[h] + su[w] < bestN then
                    best, bestN = v == -1 and i or openIn(su[h] > 0 and h or w), su[h] + su[w]
                end
            end
        end
        return best
    end
    local function reset(search)
        for i = 1, N do
            if g[i] < 0 then g[i] = -1 end
        end
        for s = 1, S do sb[s], su[s] = 0, byte(SL, s) end
        tn, trail = 0, search
        return propagate()
    end

    local solver = {}
    function solver.deduce()
        return reset(false) and choose() == nil
    end
    -- Branches: (trail mark * 256 + cell) * 2 + 1 once the bulb was tried.
    local br = {}
    function solver.count(cap)
        tr = tr or {}
        local ok = reset(true)
        local d, nodes, count = 0, 0, 0
        while true do
            if ok then
                local i = choose()
                if not i then
                    count = count + 1
                    if count >= 2 then return 2 end
                    ok = false
                else
                    d = d + 1
                    br[d] = (tn * 256 + i) * 2
                    set(i, -5)
                    ok = propagate()
                end
            end
            if not ok then
                while d > 0 and br[d] % 2 == 1 do
                    undo(br[d] // 512)
                    d = d - 1
                end
                if d == 0 then return count end
                undo(br[d] // 512)
                br[d] = br[d] + 1
                set(br[d] // 2 % 256, -4)
                ok = propagate()
            end
            nodes = nodes + 1
            if nodes > cap then return nil end
        end
    end
    return solver
end

-- A puzzle with one solution: grid, solution, or nil when none was found
-- in capMs milliseconds (millis: a clock in ms). An easy one (`easy`) is
-- solved by reasoning alone. Past the time, the numbers left stay.
function G.generate(n, rand, millis, capMs, easy)
    local start, N = millis(), n * n
    repeat
        local g, sol = G.layout(n, rand, 20), ""
        for i = 1, N do
            if g[i] >= 0 then
                local k = 0
                for d = 1, 4 do
                    local j = d == 1 and i - n or d == 2 and i + n or d == 3 and i - 1 or i + 1
                    if j >= 1 and j <= N and (d < 3 or (j - 1) // n == (i - 1) // n) and g[j] == -3 then k = k + 1 end
                end
                g[i] = k
            end
        end
        -- The solution as a string, a row at a time.
        for a = 1, N, n do
            local t = {}
            for i = a, a + n - 1 do t[#t + 1] = g[i] == -3 and 49 or 48 end
            sol = sol .. char(unpack(t))
        end
        collectgarbage("collect")
        local solver = G.solver(g, n)
        collectgarbage("collect")
        if easy and solver.deduce() or not easy and solver.count(400) == 1 then
            -- The numbers in a random order: every `step`-th cell from a
            -- random one, step sharing no factor with N.
            local step, at, a
            repeat
                step, a = rand(N - 1), N
                local b = step
                while b > 0 do a, b = b, a % b end
            until a == 1
            at = rand(N)
            for _ = 1, N do
                if millis() - start > capMs then break end
                at = (at + step - 1) % N + 1
                local keep = g[at]
                if keep >= 0 and keep <= 4 then
                    g[at] = 5
                    if not (easy and solver.deduce() or not easy and solver.count(400) == 1) then g[at] = keep end
                end
            end
            solver = nil
            for i = 1, N do g[i] = g[i] < 0 and "." or g[i] == 5 and "#" or char(48 + g[i]) end
            return table.concat(g), sol
        end
        solver, g = nil, nil
        collectgarbage("collect")
    until millis() - start > capMs
    return nil
end

return G
end
