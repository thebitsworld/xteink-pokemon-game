-- The Caro computer player. Usage:
--   local AI = smudge.dofile("ai.lua")()
--   local cell = AI.choose(game, level, rand, millis, budgetMs)
-- for a game of logic.lua. It needs nothing of logic.lua, so main.lua drops
-- the rules while it loads this only for the device's thinking.
--
-- Every empty cell near the stones (two cells away at most; one for Easy) is
-- scored by the shapes a stone there would make along its four lines, for
-- the side to move (attack) and for the other side (defence): fives, open
-- and closed fours, open and closed threes, twos. The shapes come from the
-- five-cell windows through the cell that hold no enemy stone: a window
-- with all five is a five, a cell that completes a window of four is a
-- winning point (two of them make an open four), two or more windows of
-- three make an open three.
--   Easy:   the nearest cells only, defence counted lightly, a random pick
--           among the better moves - but it takes a five and blocks one.
--   Normal: the best score (a random one of those within 3%).
--   Hard:   a threat search first: its own win by continuous fours (VCF)
--           or by fours and open threes (VCT); then, if the opponent would
--           have such a win were it to move, the best move that takes it
--           away; else the best score. Bounded by a count of shape
--           evaluations per step and a time budget.

return function()
local A = {}
local N = 15
local NN = N * N
local DR, DC = { 0, 1, 1, 1 }, { 1, 0, 1, -1 }

local FIVE, OPEN4, FOUR, OPEN3, THREE = 100000, 15000, 1500, 1200, 200
A.FIVE, A.OPEN4 = FIVE, OPEN4

local line = {}            -- the nine cells of a line, reused
local gapA, gapB = 0, 0    -- the cells that would complete a four (lineShape)
local nodes, cap = 0, 0
local clock, deadline, timeUp = nil, nil, false

local function outOfTime()
    if timeUp or nodes >= cap then return true end
    if clock and clock() > deadline then
        timeUp = true
        return true
    end
    return false
end

-- The next search step may evaluate n more line shapes.
local function allow(n) cap = nodes + n end

-- p's shape along direction k through cell (r, c), as if p stood there:
-- returns a score and a kind (5 five, 4 open four, 3 four, 2 open three,
-- 1 three, 0 less). For a four, gapA (and gapB) are the winning cells.
local function lineShape(b, r, c, p, k)
    nodes = nodes + 1
    local dr, dc = DR[k], DC[k]
    for t = -4, 4 do
        local v = 1
        if t ~= 0 then
            local rr, cc = r + t * dr, c + t * dc
            if rr < 0 or rr >= N or cc < 0 or cc >= N then
                v = -1
            else
                local s = b[rr * N + cc + 1]
                v = s == 0 and 0 or (s == p and 1 or -1)
            end
        end
        line[t + 5] = v
    end
    local n3, n2, n1, w1, w2 = 0, 0, 0, 0, 0
    for s = 1, 5 do
        local cnt, gap = 0, 0
        for t = s, s + 4 do
            local v = line[t]
            if v < 0 then
                cnt = -1
                break
            end
            if v == 1 then cnt = cnt + 1 else gap = t end
        end
        if cnt == 5 then
            return FIVE, 5
        elseif cnt == 4 then
            if w1 == 0 then w1 = gap elseif gap ~= w1 then w2 = gap end
        elseif cnt == 3 then
            n3 = n3 + 1
        elseif cnt == 2 then
            n2 = n2 + 1
        elseif cnt == 1 then
            n1 = n1 + 1
        end
    end
    if w1 > 0 then
        gapA = (r + (w1 - 5) * dr) * N + c + (w1 - 5) * dc + 1
        gapB = w2 > 0 and (r + (w2 - 5) * dr) * N + c + (w2 - 5) * dc + 1 or 0
        if w2 > 0 then return OPEN4, 4 end
        return FOUR, 3
    end
    if n3 >= 2 then return OPEN3, 2 end
    if n3 == 1 then return THREE, 1 end
    return n2 * 12 + n1 * 2, 0
end

-- What a stone of p on cell i is worth to p: its four lines plus the
-- double threats (two fours, a four and an open three, two open threes).
local function cellValue(b, i, p)
    local r, c = (i - 1) // N, (i - 1) % N
    local total, fours, threes = 0, 0, 0
    for k = 1, 4 do
        local s, kind = lineShape(b, r, c, p, k)
        total = total + s
        if kind >= 3 then fours = fours + 1 elseif kind == 2 then threes = threes + 1 end
    end
    if total < FIVE then
        if fours >= 2 then
            total = total + OPEN4
        elseif fours == 1 and threes >= 1 then
            total = total + 12000
        elseif threes >= 2 then
            total = total + 6000
        end
    end
    return total
end
A.cellValue = cellValue

function A.nodes() return nodes end

-- The searches allocate nothing. occ[r + 3] has a bit for each stone of
-- row r (rows -2..16 kept, the outer ones empty), kept up to date by put and
-- take, and nextCell walks the empty cells within two of a stone with it.
-- Move lists live on one stack of two parallel arrays (cell, value).
local occ = {}
local ST, SV, sp = {}, {}, 0
local ROOT = 12 -- the root keeps its best moves in ST/SV[1..ROOT]

local function setOcc(b)
    for k = 1, N + 6 do occ[k] = 0 end
    for i = 1, NN do
        if b[i] ~= 0 then
            local r = (i - 1) // N
            occ[r + 3] = occ[r + 3] | (1 << ((i - 1) % N))
        end
    end
end

local function put(b, i, p)
    b[i] = p
    local r = (i - 1) // N
    occ[r + 3] = occ[r + 3] | (1 << ((i - 1) % N))
end

local function take(b, i)
    b[i] = 0
    local r = (i - 1) // N
    occ[r + 3] = occ[r + 3] & ~(1 << ((i - 1) % N))
end

-- The next empty cell after cell i (0 to start) within two cells of a
-- stone, or 0.
local function nextCell(b, i)
    local r, c = i // N, i % N
    while r < N do
        local m = occ[r + 1] | occ[r + 2] | occ[r + 3] | occ[r + 4] | occ[r + 5]
        m = (m | (m << 1) | (m << 2) | (m >> 1) | (m >> 2)) >> c
        while m ~= 0 and c < N do
            if m & 1 ~= 0 then
                local j = r * N + c + 1
                if b[j] == 0 then return j end
            end
            m, c = m >> 1, c + 1
        end
        r, c = r + 1, 0
    end
    return 0
end

local function push(i, v)
    sp = sp + 1
    ST[sp], SV[sp] = i, v
end

-- Keeps the n best (cell, value) pairs in ST/SV[base + 1 .. base + n], best
-- first; `count` of them filled so far. Returns the new count.
local function keepBest(base, n, count, i, v)
    if count == n and v <= SV[base + n] then return count end
    if count < n then count = count + 1 end
    local k = base + count
    while k > base + 1 and SV[k - 1] < v do
        ST[k], SV[k] = ST[k - 1], SV[k - 1]
        k = k - 1
    end
    ST[k], SV[k] = i, v
    return count
end

-- A cell where p makes five, or nil.
local function fiveCell(b, p)
    local i = nextCell(b, 0)
    while i > 0 do
        local r, c = (i - 1) // N, (i - 1) % N
        for d = 1, 4 do
            local _, kind = lineShape(b, r, c, p, d)
            if kind == 5 then return i end
        end
        i = nextCell(b, i)
    end
    return nil
end

-- Whether p's stone on i (already placed) makes a four or a five.
local function makesFour(b, i, p)
    local r, c = (i - 1) // N, (i - 1) % N
    for d = 1, 4 do
        local _, kind = lineShape(b, r, c, p, d)
        if kind >= 3 then return true end
    end
    return false
end

-- p's fours through empty cell i: returns the winning cells w1, w2 (w2 > 0:
-- an open four or two fours), or nil if p makes five there.
local function foursAt(b, i, p)
    local r, c = (i - 1) // N, (i - 1) % N
    local w1, w2 = 0, 0
    for d = 1, 4 do
        local _, kind = lineShape(b, r, c, p, d)
        if kind == 5 then return nil, nil end
        if kind >= 3 then
            if w1 == 0 then w1 = gapA elseif gapA ~= w1 then w2 = gapA end
            if gapB > 0 and gapB ~= w1 then w2 = gapB end
        end
    end
    return w1, w2
end

-- Victory by continuous fours: p (to move, the other side having no five
-- to make) plays a four, the other side must block it, again, until a five
-- or an open four. Returns p's first move, or nil (none found in time).
local function vcf(b, p, depth)
    if depth <= 0 or outOfTime() then return nil end
    local q = 3 - p
    -- First a five or an open four (or two fours) at once; the single fours,
    -- each with the cell that blocks it, are kept for the deeper search.
    local base = sp
    local i = nextCell(b, 0)
    while i > 0 do
        local w1, w2 = foursAt(b, i, p)
        if not w1 or w2 > 0 then
            sp = base
            return i
        end
        if w1 > 0 then push(i, w1) end
        i = nextCell(b, i)
    end
    local top = sp
    for k = base + 1, top do
        local f, w1 = ST[k], SV[k]
        put(b, f, p)
        put(b, w1, q)
        local win = not makesFour(b, w1, q) and vcf(b, p, depth - 1)
        take(b, w1)
        take(b, f)
        if win or outOfTime() then
            sp = base
            return win and f or nil
        end
    end
    sp = base
    return nil
end
A.vcf = function(b, p, depth, maxNodes)
    nodes, cap, clock, timeUp, sp = 0, maxNodes or 20000, nil, false, 0
    setOcc(b)
    return vcf(b, p, depth)
end

-- Victory by continuous threats: like vcf, but p may also play an open
-- three, after which the other side tries every cell that stops it from
-- becoming a four, and its own fours (once). The six most promising threats
-- a turn.
local vct

-- p has just made an open three with the stone on i: whether p still wins
-- whatever the other side does.
local function threeWins(b, p, i, depth, counters)
    local q = 3 - p
    local r, c = (i - 1) // N, (i - 1) % N
    -- The cells on the threes' lines where p would make a four.
    local base = sp
    for d = 1, 4 do
        local _, kind = lineShape(b, r, c, p, d)
        if kind == 2 then
            local dr, dc = DR[d], DC[d]
            for t = -4, 4 do
                local rr, cc = r + t * dr, c + t * dc
                if rr >= 0 and rr < N and cc >= 0 and cc < N then
                    local j = rr * N + cc + 1
                    if b[j] == 0 then
                        local _, k2 = lineShape(b, rr, cc, p, d)
                        if k2 >= 3 then push(j, 0) end
                    end
                end
            end
        end
    end
    local top = sp
    local ok = top > base
    for k = base + 1, top do
        if not ok then break end
        local j = ST[k]
        put(b, j, q)
        ok = not makesFour(b, j, q) and vct(b, p, depth - 1) ~= nil
        take(b, j)
    end
    sp = base
    if not ok then return false end
    -- The other side's fours: p blocks each, and the three must still win.
    local f = nextCell(b, 0)
    while f > 0 do
        local w1, w2 = foursAt(b, f, q)
        if not w1 or w2 > 0 then return false end
        if w1 > 0 then
            if counters <= 0 or outOfTime() then return false end
            put(b, f, q)
            put(b, w1, p)
            ok = threeWins(b, p, i, depth, counters - 1)
            take(b, w1)
            take(b, f)
            if not ok then return false end
        end
        f = nextCell(b, f)
    end
    return true
end

vct = function(b, p, depth)
    if depth <= 0 or outOfTime() then return nil end
    local win = vcf(b, p, 4)
    if win or outOfTime() then return win end
    local q = 3 - p
    -- The six best fours and open threes p can make; the value carries the
    -- cell that blocks a four (value * 256 + cell).
    local base, count = sp, 0
    local i = nextCell(b, 0)
    while i > 0 do
        local r, c = (i - 1) // N, (i - 1) % N
        local best, w = 0, 0
        for d = 1, 4 do
            local _, kind = lineShape(b, r, c, p, d)
            if kind == 3 then
                best, w = 3, gapA
            elseif kind == 2 and best < 2 then
                best = 2
            end
        end
        if best > 0 then
            count = keepBest(base, 6, count, i, (cellValue(b, i, p) + cellValue(b, i, q)) * 256 + w)
        end
        i = nextCell(b, i)
    end
    sp = base + count
    for n = base + 1, base + count do
        local t, w = ST[n], SV[n] % 256
        local ok
        put(b, t, p)
        if w > 0 then
            put(b, w, q)
            ok = not makesFour(b, w, q) and vct(b, p, depth - 1) ~= nil
            take(b, w)
        else
            ok = threeWins(b, p, t, depth, 1)
        end
        take(b, t)
        if ok then
            sp = base
            return t
        end
        if outOfTime() then break end
    end
    sp = base
    return nil
end
A.vct = function(b, p, depth, maxNodes)
    nodes, cap, clock, timeUp, sp = 0, maxNodes or 30000, nil, false, 0
    setOcc(b)
    return vct(b, p, depth)
end

-- Hard: its own forced win; else, if the other side has one, the best move
-- (of the root's list, ST/SV[1..count]) that stops it; else the best score.
local function hardPick(b, me, count)
    local opp = 3 - me
    sp = count
    allow(5000)
    local own = vcf(b, me, 10)
    if own then return own end
    allow(10000)
    own = vct(b, me, 3)
    if own then return own end
    -- Would the other side win by threats if it could move now? Another
    -- stone of mine never helps it, so without such a threat the best
    -- score is safe.
    allow(6000)
    local threat = vcf(b, opp, 10)
    if not threat then
        allow(8000)
        threat = vct(b, opp, 3)
    end
    if not threat or timeUp then return ST[1] end
    -- Else the first move that leaves it no such win: the threat's own
    -- first cell, then the best by score.
    for n = 0, count do
        local m = n == 0 and threat or ST[n]
        if n == 0 or m ~= threat then
            put(b, m, me)
            -- A five of mine to block keeps the other side busy.
            local safe = fiveCell(b, me) ~= nil
            if not safe then
                allow(3000)
                safe = not vcf(b, opp, 10)
                if safe then
                    allow(4000)
                    safe = not vct(b, opp, 3)
                end
            end
            take(b, m)
            if safe and not timeUp then return m end
            if timeUp then break end
        end
    end
    return threat
end

-- Whether cell i has a stone next to it.
local function touching(b, i)
    local r, c = (i - 1) // N, (i - 1) % N
    for rr = math.max(0, r - 1), math.min(N - 1, r + 1) do
        for cc = math.max(0, c - 1), math.min(N - 1, c + 1) do
            if b[rr * N + cc + 1] ~= 0 then return true end
        end
    end
    return false
end

-- The device's move for the side to move in `g`. level 1..3; rand(n) gives
-- 1..n; millis and budgetMs (optional) bound Hard's search.
function A.choose(g, level, rand, millis, budgetMs)
    local b, me = g.b, g.turn
    local opp = 3 - me
    if g.n == 0 then return (N // 2) * N + N // 2 + 1 end
    nodes, cap, timeUp, sp = 0, 1000000, false, 0
    clock = millis
    deadline = millis and (millis() + (budgetMs or 2000)) or nil
    setOcc(b)
    -- Every cell scored; the best ROOT kept. A five to make, then a five
    -- to stop, come first.
    local wdef = level == 1 and 3 or 7
    local count, win, forced, fv = 0, nil, nil, -1
    local i = nextCell(b, 0)
    while i > 0 do
        if level > 1 or touching(b, i) then
            local a, d = cellValue(b, i, me), cellValue(b, i, opp)
            local s = a * 10 + d * wdef
            count = keepBest(0, ROOT, count, i, s)
            if a >= FIVE then win = win or i end
            if d >= FIVE and s > fv then forced, fv = i, s end
        end
        i = nextCell(b, i)
    end
    if win or forced then return win or forced end
    local pick
    if level >= 3 then
        pick = hardPick(b, me, count)
    else
        -- Easy: one of the eight best that score at least 30% of the best;
        -- Normal: one of those within 3% of the best.
        local floor = SV[1] * (level == 1 and 0.3 or 0.97)
        local n = 1
        while n < count and n < 8 and SV[n + 1] >= floor do n = n + 1 end
        pick = ST[rand(n)]
    end
    clock = nil
    return pick
end

return A
end
