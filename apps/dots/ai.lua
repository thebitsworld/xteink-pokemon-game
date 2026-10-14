-- The device's player for Dots and Boxes, loaded only while it thinks:
--   local AI = smudge.dofile("ai.lua")(D)
--   local line = AI.choose(g, level, rand, clock, budgetMs)
-- with D the rules (logic.lua). It returns one line; the caller plays it and
-- asks again while the device keeps the turn.
--
-- Easy takes every free box, otherwise mostly lines that give nothing away.
-- Normal never gives a box away while a safe line is left, takes every free
-- box and, when it must, gives away the fewest. Hard also plays the chains:
-- once only chains and loops are left it counts which to open, declines the
-- last two boxes of a chain (four of a loop) to keep control when that pays,
-- and with few safe lines left searches them to the end (node and time cap).

return function(D)
local A = {}

local SEARCH_SAFE = 10   -- Hard searches the safe lines when this few remain
local NODE_CAP = 800

-- The position being thought about is the game's own lines and sides: every
-- line drawn while thinking is taken back. Nothing is allocated per node:
-- lists live in two stack buffers (each user takes the slots above the top
-- and gives them back), "seen" marks are stamps compared with a counter, and
-- chainValue remembers results in a small fixed cache.
local byte = string.byte
local lines, sides, b1, b2, bl, L, nb
local buf, top = {}, 0          -- lines: safe lines, captures, openings
local cbuf, ctop = {}, 0        -- numbers: components, walks, line values
local stamps, stamp = {}, 0     -- lines 1..L, then box b at L + b
local cache = {}                -- chainValue: key * 256 + value + 128

local function other(l, b)
    local a = byte(b1, l)
    if a == b then return byte(b2, l) end
    return a
end

local function draw(l)
    lines[l] = 1
    local done = 0
    local a = byte(b1, l)
    if a > 0 then
        local s = sides[a] + 1
        sides[a] = s
        if s == 4 then done = done + 1 end
    end
    a = byte(b2, l)
    if a > 0 then
        local s = sides[a] + 1
        sides[a] = s
        if s == 4 then done = done + 1 end
    end
    return done
end

local function undraw(l)
    lines[l] = 0
    local a = byte(b1, l)
    if a > 0 then sides[a] = sides[a] - 1 end
    a = byte(b2, l)
    if a > 0 then sides[a] = sides[a] - 1 end
end

local function isSafe(l) return lines[l] == 0 and sides[byte(b1, l)] < 2 and sides[byte(b2, l)] < 2 end

-- A free line of box b other than `but`.
local function freeLine(b, but)
    local k = (b - 1) * 4
    for i = 1, 4 do
        local l = byte(bl, k + i)
        if lines[l] == 0 and l ~= but then return l end
    end
    return nil
end

-- A line that completes a box, or nil.
local function captureLine()
    for b = 1, nb do
        if sides[b] == 3 then return freeLine(b) end
    end
    return nil
end

-- Pushes the safe lines on buf; returns how many.
local function pushSafe()
    local n = 0
    for l = 1, L do
        if isSafe(l) then
            n = n + 1
            buf[top + n] = l
        end
    end
    top = top + n
    return n
end

local function anySafe()
    for l = 1, L do
        if isSafe(l) then return true end
    end
    return false
end

-- Takes every box on offer, pushing the lines on buf; returns how many boxes.
-- restore(base) takes them back, base being top before.
local function greedy()
    local count = 0
    while true do
        local l = captureLine()
        if not l then return count end
        count = count + draw(l)
        top = top + 1
        buf[top] = l
    end
end

local function restore(base)
    for i = top, base + 1, -1 do undraw(buf[i]) end
    top = base
end

-- Boxes on offer run from a 3-sided box along a chain of 2-sided boxes. A
-- run that ends at the edge with 2 boxes or more can be declined at its last
-- two (t = 2); one with a 3-sided box at both ends (an opened loop) of 4 or
-- more at its last four (t = 4). Returns the cheapest t, its start box and
-- length, or nil.
local function declineRun()
    stamp = stamp + 1
    local best, bestA, bestK
    for a = 1, nb do
        if sides[a] == 3 and stamps[L + a] ~= stamp then
            stamps[L + a] = stamp
            local k, cur, e, kind = 1, a, freeLine(a), nil
            while true do
                local b = other(e, cur)
                if b == 0 then
                    kind = 1
                    break
                end
                local s = sides[b]
                if s == 3 then
                    stamps[L + b] = stamp
                    k, kind = k + 1, 2
                    break
                end
                if s ~= 2 then
                    kind = 0
                    break
                end
                k = k + 1
                cur, e = b, freeLine(b, e)
            end
            local t = (kind == 1 and k >= 2) and 2 or ((kind == 2 and k >= 4) and 4 or nil)
            if t and (not best or t < best) then best, bestA, bestK = t, a, k end
        end
    end
    return best, bestA, bestK
end

-- Value for the player to move of the components sorted in
-- cbuf[base + 1 .. base + n] - chains k, loops -k, joined shapes 100 + k
-- (counted as chains) - once no safe line is left: opening one hands it
-- over, and the other player either takes it all and moves on, or keeps
-- control by declining the last 2 (4 of a loop). A 2-chain is opened in the
-- middle, so it cannot be declined. Each component tried is taken out of the
-- list in place and put back.
local function chainValue(base, n)
    if n == 0 then return 0 end
    local key = n
    for i = base + 1, base + n do key = (key * 131 + cbuf[i] + 40) % 4194301 end
    local slot = key % 251 + 1
    local hit = cache[slot]
    if hit and hit // 256 == key then return hit % 256 - 128 end
    local best, prev, last = -999, nil, base + n
    for i = base + 1, last do
        local c = cbuf[i]
        if c ~= prev then
            prev = c
            for j = i, last - 1 do cbuf[j] = cbuf[j + 1] end
            local v = chainValue(base, n - 1)
            for j = last, i + 1, -1 do cbuf[j] = cbuf[j - 1] end
            cbuf[i] = c
            local opp
            if c < 0 then
                opp = math.max(-c + v, -c - 8 - v)
            else
                local k = c > 100 and c - 100 or c
                opp = k + v
                if k >= 3 and k - 4 - v > opp then opp = k - 4 - v end
            end
            if -opp > best then best = -opp end
        end
    end
    cache[slot] = key * 256 + best + 128
    return best
end

-- Net boxes for the player to move in a position with no safe line and no
-- box on offer. Plain chains and loops are counted by chainValue. Joined
-- shapes (a box with 3 or 4 free sides) are searched - each way to open a
-- joined shape, one per kind of plain chain or loop - while `budget`
-- openings last; past it they count as chains of their size.
local TOP_BUDGET = 300
local MAX_DEPTH = 4    -- joined shapes searched this many openings deep
local budget, leafBudget, depth = 0, 0, 0
local captureValue

local function loonyValue()
    local exact = budget > 0 and depth < MAX_DEPTH
    local cbase, kbase, joinedAny = ctop, top, false
    stamp = stamp + 1
    for b = 1, nb do
        if sides[b] < 4 and stamps[L + b] ~= stamp then
            stamps[L + b] = stamp
            -- Walk the component, its boxes stacked above ctop.
            cbuf[ctop + 1] = b
            local sp, k, ground, joined, edge, inner = 1, 0, 0, false, nil, nil
            local first = top
            while sp > 0 do
                local x = cbuf[ctop + sp]
                sp = sp - 1
                k = k + 1
                local free, kk = 0, (x - 1) * 4
                for i = 1, 4 do
                    local l = byte(bl, kk + i)
                    if lines[l] == 0 then
                        free = free + 1
                        local y = other(l, x)
                        if y == 0 then
                            ground = ground + 1
                            edge = edge or l
                        else
                            inner = inner or l
                            if stamps[L + y] ~= stamp then
                                stamps[L + y] = stamp
                                sp = sp + 1
                                cbuf[ctop + sp] = y
                            end
                        end
                        if exact and stamps[l] ~= stamp then
                            stamps[l] = stamp
                            top = top + 1
                            buf[top] = l
                        end
                    end
                end
                if free ~= 2 then joined = true end
            end
            local c = joined and 100 + k or (ground == 0 and -k or k)
            if joined then
                joinedAny = true
            elseif exact then
                -- A plain shape: one opening for each kind (an end of a
                -- chain, the middle of a 2-chain, any line of a loop).
                top = first
                local seenKind = false
                for i = cbase + 1, ctop do
                    if cbuf[i] == c then
                        seenKind = true
                        break
                    end
                end
                if not seenKind then
                    top = top + 1
                    buf[top] = edge or inner
                    if k == 2 and edge then
                        top = top + 1
                        buf[top] = inner
                    end
                end
            end
            ctop = ctop + 1
            cbuf[ctop] = c
        end
    end
    local n = ctop - cbase
    local best = -999
    if joinedAny and exact then
        for i = kbase + 1, top do
            if budget <= 0 then break end
            budget = budget - 1
            local l = buf[i]
            draw(l)
            depth = depth + 1
            local v = -captureValue()
            depth = depth - 1
            undraw(l)
            if v > best then best = v end
        end
        if budget > 0 then
            ctop, top = cbase, kbase
            return best
        end
    end
    -- Plain shapes, or out of budget: all counted by chainValue.
    for i = cbase + 2, cbase + n do
        local v, j = cbuf[i], i - 1
        while j > cbase and cbuf[j] > v do
            cbuf[j + 1] = cbuf[j]
            j = j - 1
        end
        cbuf[j + 1] = v
    end
    local v = chainValue(cbase, n)
    ctop, top = cbase, kbase
    return v > best and v or best
end

-- Net boxes from now on for the player to move when boxes are on offer and
-- no safe line will be left: take them all, or decline the last few.
function captureValue()
    local t = declineRun()
    local base = top
    local c = greedy()
    local v = loonyValue()
    restore(base)
    local take = c + v
    if t and c - 2 * t - v > take then return c - 2 * t - v end
    return take
end

-- Search over the safe lines to the end of them (no box changes hands until
-- then), scored by loonyValue. Aborts past the node cap or the deadline.
local nodes, abort, clock, deadline = 0, false, nil, 0

local function search(alpha, beta)
    nodes = nodes + 1
    if nodes > NODE_CAP or (clock and nodes % 32 == 0 and clock() > deadline) then
        abort = true
        return 0
    end
    local base = top
    local n = pushSafe()
    if n == 0 then
        budget = leafBudget
        return loonyValue()
    end
    local best = -999
    for i = base + 1, base + n do
        local l = buf[i]
        draw(l)
        local v = -search(-beta, -alpha)
        undraw(l)
        if abort then break end
        if v > best then
            best = v
            if v > alpha then
                alpha = v
                if alpha >= beta then break end
            end
        end
    end
    top = base
    if abort then return 0 end
    return best
end

-- Hard with boxes on offer.
local function hardCapture(cap)
    local t, start, k = declineRun()
    if not t then return cap end
    local base = top
    local c = greedy()
    local safeLeft = anySafe()
    budget = TOP_BUDGET
    local v = safeLeft and 0 or loonyValue()
    restore(base)
    if safeLeft or c + v >= c - 2 * t - v then return cap end
    if c > t then
        -- Take everything but the declined run's tail first.
        if k > t then return freeLine(start) end
        for b = 1, nb do
            if sides[b] == 3 then
                local l = freeLine(b)
                draw(l)
                local t2 = declineRun()
                undraw(l)
                if t2 == t then return l end
            end
        end
        return cap
    end
    -- Decline: the second line along the run leaves the boxes in pairs.
    local e1 = freeLine(start)
    return freeLine(other(e1, start), e1)
end

-- One of the first `count` lines on buf whose value (cbuf, same place)
-- equals best, at random.
local function pickBest(count, best, rand)
    local n, pick = 0, nil
    for i = 1, count do
        if cbuf[i] == best then
            n = n + 1
            if rand(n) == 1 then pick = buf[i] end
        end
    end
    return pick
end

-- Every line gives boxes away: the one that costs least. The lines sit on
-- buf[1 .. count], their values on cbuf[1 .. count].
local function bestOpening(level, rand)
    local count = 0
    for l = 1, L do
        if lines[l] == 0 then
            count = count + 1
            buf[count] = l
        end
    end
    top, ctop = count, count
    local best = nil
    for i = 1, count do
        local l = buf[i]
        draw(l)
        local value
        if level == 2 then
            local base = top
            value = -greedy()
            restore(base)
        else
            budget = TOP_BUDGET // count + 4
            value = -captureValue()
        end
        undraw(l)
        cbuf[i] = value
        if not best or value > best then best = value end
    end
    return pickBest(count, best, rand)
end

-- The device's next line in game g at level 1..3. `rand(n)` returns 1..n;
-- `clock` (optional) returns milliseconds, for the search's time budget.
function A.choose(g, level, rand, clockFn, budgetMs)
    if D.over(g) then return nil end
    lines, sides, b1, b2, bl, L, nb = g.lines, g.sides, g.b1, g.b2, g.bl, g.L, g.nb
    -- Small boards can afford searching joined shapes at the search's ends.
    leafBudget = nb <= 9 and 6 or 0
    top, ctop = 0, 0
    local l = nil
    local cap = captureLine()
    if cap then
        l = level < 3 and cap or hardCapture(cap)
    else
        local nsafe = pushSafe()
        if level == 1 then
            if nsafe == 0 or rand(10) > 7 then
                nsafe = 0
                for x = 1, L do
                    if lines[x] == 0 then
                        nsafe = nsafe + 1
                        buf[nsafe] = x
                    end
                end
            end
            l = buf[rand(nsafe)]
        elseif nsafe == 0 then
            l = bestOpening(level, rand)
        elseif level == 2 or nsafe > SEARCH_SAFE then
            l = buf[rand(nsafe)]
        else
            -- Hard, few safe lines left: search them (the root's lines stay
            -- on buf[1 .. nsafe], their values on cbuf); ties at random.
            ctop = nsafe
            nodes, abort, clock = 0, false, clockFn
            deadline = clockFn and (clockFn() + (budgetMs or 1500)) or 0
            local best, done = -999, 0
            local start = rand(nsafe)
            for i = 0, nsafe - 1 do
                local j = (start + i - 1) % nsafe + 1
                local x = buf[j]
                draw(x)
                local v = -search(-999, -best + 1)
                undraw(x)
                if abort then break end
                cbuf[j] = v
                done = done + 1
                if v > best then best = v end
            end
            clock = nil
            local n = 0
            for i = 0, done - 1 do
                local j = (start + i - 1) % nsafe + 1
                if cbuf[j] == best then
                    n = n + 1
                    if rand(n) == 1 then l = buf[j] end
                end
            end
            l = l or buf[rand(nsafe)]
        end
    end
    top, ctop = 0, 0
    return l
end

-- For tests: how many nodes the last search visited.
function A.nodes() return nodes end

return A
end
