-- The device's backgammon player and the full moves of a roll. Usage:
--   local AI = smudge.dofile("ai.lua")(C)
--   local steps = AI.choose(me, opp, d1, d2, level, rand, now, budgetMs)
-- with C the rules (logic.lua). Loaded only while the device moves.
--
-- Easy plays a random legal move, hitting when it can most of the time.
-- Normal plays the move whose position scores best (AI.eval). Hard keeps
-- Normal's best few moves and scores each by the opponent's best answer to
-- every one of the 21 rolls (one ply of expectation), under a time and
-- position cap; in a pure race it plays like Normal.

return function(C)
local A = {}
local target, apply, undo = C.target, C.apply, C.undo

-- Full moves --------------------------------------------------------------

local G = {}   -- the running generation: callback, need, ...
local buf = {} -- the steps of the move being built: f1, d1, f2, d2, ...
local H = {}   -- a hash of each position found (two different dice)

-- Both sides count: a checker passing a blot on the way may have hit it.
-- (Two positions of one roll sharing a 30-bit hash would only hide one of
-- them from the device.)
local function hash(me, opp)
    local h = 0
    for i = 1, 26 do h = (h * 131 + me[i] * 16 + opp[i] + 1) & 0x3fffffff end
    return h
end

-- With two different dice, whether the second step (f, d: the higher die)
-- after the first (buf[1], buf[2]: the lower one, played, G.hit1 whether
-- it hit) gives a position the higher-die-first order also reaches: then
-- it is left to that order.
local function higherFirst(me, opp, f, d)
    local g1, lo = buf[1], buf[2]
    local m = g1 - lo
    if f == m and not G.hit1 and f - d > 0 and g1 - d > 0 and opp[25 - (g1 - d)] == 0 then
        return true -- one checker moving on: the same path the other way round
    end
    if f == m and me[m] < 2 then return false end
    -- Another checker: is the other order legal?
    undo(me, opp, g1, lo, G.hit1)
    local ok = false
    if target(me, opp, f, d) then
        local h = apply(me, opp, f, d)
        ok = target(me, opp, g1, lo) ~= nil
        undo(me, opp, f, d, h)
    end
    G.hit1 = apply(me, opp, g1, lo)
    return ok
end

local function dfs(me, opp, R, n, depth, maxf)
    if depth > G.need then
        if not G.doubles then
            local h = hash(me, opp)
            for i = 1, G.count do
                if H[i] == h then return end
            end
            H[G.count + 1] = h
        end
        G.count = G.count + 1
        G.fn(me, opp, buf, G.need)
        return
    end
    for k = 1, n do
        local d = R[k]
        local dup = false
        for j = 1, k - 1 do
            if R[j] == d then dup = true end
        end
        if not dup and (depth > 1 or not G.only or d == G.only) then
            local top = me[25] > 0 and 25 or 24
            if top > maxf then top = maxf end
            local late = depth == 2 and not G.doubles and buf[2] < d
            for f = top, 1, -1 do
                if me[f] > 0 and target(me, opp, f, d) and not (late and higherFirst(me, opp, f, d)) then
                    local hit = apply(me, opp, f, d)
                    if depth == 1 then G.hit1 = hit end
                    R[k] = R[n]
                    R[n] = d
                    -- A branch that cannot play G.need dice just ends.
                    buf[2 * depth - 1], buf[2 * depth] = f, d
                    dfs(me, opp, R, n - 1, depth + 1, G.doubles and f or 25)
                    R[n] = R[k]
                    R[k] = d
                    undo(me, opp, f, d, hit)
                end
            end
        end
    end
end

local GR = {}

-- Calls fn(me, opp, buf, n) once for each distinct position a legal move
-- of the roll d1-d2 leads to, with that position on the board (restored
-- after) and the move's n steps in buf (f1, d1, f2, d2, ...); n is 0 when
-- no die can be played. Returns how many positions it found. Each
-- position is reported once: doubles take their steps from the highest
-- point down (any order gives the same position, and each set of steps a
-- different one); two different dice leave to the higher-die-first order
-- what it also reaches, and check the rest against the positions found.
function A.gen(me, opp, d1, d2, fn)
    local n = C.dice(d1, d2, GR)
    local need, only = C.turnInfo(me, opp, d1, d2)
    if need == 0 then
        fn(me, opp, buf, 0)
        return 1
    end
    G.need, G.only, G.fn, G.count, G.doubles = need, only, fn, 0, d1 == d2
    dfs(me, opp, GR, n, 1, 25)
    G.fn = nil
    return G.count
end

-- The steps in buf as a number (no garbage while searching): base-32
-- digits, each step's die then its point, the die only once for doubles
-- (at most 25 bits).
local function codeOf(b, n)
    local c = 0
    for i = 1, n do
        if i == 1 or not G.doubles then c = c * 32 + b[2 * i] end
        c = c * 32 + b[2 * i - 1]
    end
    return c
end

-- A move number back to its step string; `doubles` whether the roll was.
local function stepsOf(c, doubles)
    local t, d = {}, 0
    while c > 0 do
        t[#t + 1] = c % 32
        c = c // 32
    end
    local out = {}
    local i = #t
    while i >= 1 do
        if d == 0 or not doubles then
            d = t[i]
            i = i - 1
        end
        out[#out + 1] = string.char(64 + t[i], 48 + d)
        i = i - 1
    end
    return table.concat(out)
end

-- The steps in buf as a move string.
function A.moveOf(b, n)
    local t = {}
    for i = 1, n do t[i] = string.char(64 + b[2 * i - 1], 48 + b[2 * i]) end
    return table.concat(t)
end

-- Evaluation ----------------------------------------------------------------

-- What a made point (two or more checkers) is worth on each point, in pips.
local PT = { 2, 3, 4, 6, 8, 8, 7, 5, 3, 2, 1, 1, 1, 1, 1, 1, 2, 4, 4, 5, 5, 3, 3, 2 }
local D = {} -- D[d]: an opponent's checker d pips behind the blot

-- Rolls out of 36 that hit a blot D marks (blocking ignored).
local function shots()
    local n = 0
    for a = 1, 6 do
        if D[a] or D[2 * a] or D[3 * a] or D[4 * a] then n = n + 1 end
        for b = a + 1, 6 do
            if D[a] or D[b] or D[a + b] then n = n + 2 end
        end
    end
    return n
end

-- Made points, prime and stacking of side s; the second result is how many
-- points of its home board it holds.
local function structure(s)
    local v, run, best, home = 0, 0, 0, 0
    for i = 1, 24 do
        local c = s[i]
        if c >= 2 then
            v = v + PT[i]
            run = run + 1
            if run > best then best = run end
            if i <= 6 then home = home + 1 end
            if c > 3 then v = v - (c - 3) * 0.7 end
        else
            run = 0
        end
    end
    return v + best * best * 0.8, home
end

-- Whether the two sides can still meet (no pure race yet).
local function contact(me, opp)
    local a, b = 0, 0
    for i = 25, 1, -1 do
        if me[i] > 0 then a = i break end
    end
    for i = 25, 1, -1 do
        if opp[i] > 0 then b = i break end
    end
    return a + b > 25
end
A.contact = contact

-- How good the position is for m, which has just moved (o rolls next).
function A.eval(m, o)
    local pm, po = C.pips(m), C.pips(o)
    if not contact(m, o) then
        -- A race: fewer pips, more off, no tall stacks at home.
        local v = (po - pm) + 2 * (m[26] - o[26])
        for i = 1, 6 do
            if m[i] > 3 then v = v - (m[i] - 3) * 0.5 end
        end
        return v
    end
    local sm, hm = structure(m)
    local so, ho = structure(o)
    local v = po - pm + sm - so
    if o[25] > 0 then v = v + o[25] * hm * 2.5 end
    if m[25] > 0 then v = v - m[25] * ho * 2.5 end
    -- m's blots, against o's checkers behind them.
    for i = 1, 24 do
        if m[i] == 1 then
            local any = false
            for d = 1, 24 do D[d] = false end
            for j = 25, 1, -1 do
                if o[j] > 0 then
                    local d = i + j - 25
                    if d >= 1 then
                        D[d] = true
                        any = true
                    end
                end
            end
            if any then v = v - shots() / 36 * (25 - i + 6 + ho * 1.5) end
        end
    end
    -- o's blots in front of m: a small bonus for chances next time.
    for i = 7, 24 do
        if o[i] == 1 then v = v + 0.3 end
    end
    return v
end
local eval = A.eval

-- Choosing ------------------------------------------------------------------

-- The running search (the callbacks are file-level: no closures per move).
local R = {}

local function easyLeaf(me, opp, b, n)
    R.count = R.count + 1
    if R.rand(R.count) == 1 then R.any = codeOf(b, n) end
    if opp[25] > R.bar then
        R.hits = R.hits + 1
        if R.rand(R.hits) == 1 then R.hit = codeOf(b, n) end
    end
end

-- Keeps the best R.keep moves by eval, best first.
local function bestLeaf(me, opp, b, n)
    local v = eval(me, opp)
    local top, vals = R.top, R.vals
    local k = #top
    if k >= R.keep and v <= vals[k] then return end
    if k >= R.keep then
        top[k], vals[k] = nil, nil
        k = k - 1
    end
    local i = k + 1
    while i > 1 and vals[i - 1] < v do
        top[i], vals[i] = top[i - 1], vals[i - 1]
        i = i - 1
    end
    top[i], vals[i] = codeOf(b, n), v
end

local function replyLeaf(me, opp)
    R.nodes = R.nodes + 1
    local v = eval(me, opp)
    if v > R.reply then R.reply = v end
end

A.NODE_CAP = 6000
A.KEEP = 4

-- The device's move as a step string ("" when nothing can be played).
function A.choose(me, opp, d1, d2, level, rand, now, budget)
    R.rand = rand
    if level == 1 then
        R.count, R.hits, R.bar, R.any, R.hit = 0, 0, opp[25], 0, nil
        A.gen(me, opp, d1, d2, easyLeaf)
        local mv = R.any
        if R.hit and rand(10) <= 7 then mv = R.hit end
        return stepsOf(mv, d1 == d2)
    end
    local hard = level >= 3 and contact(me, opp)
    R.top, R.vals, R.keep = {}, {}, hard and A.KEEP or 1
    A.gen(me, opp, d1, d2, bestLeaf)
    local top = R.top
    R.top, R.vals = nil, nil
    if not hard or #top <= 1 then return stepsOf(top[1] or 0, d1 == d2) end
    -- Hard: the opponent's best answer to each roll, averaged.
    local t0 = now()
    R.nodes = 0
    local m2, o2 = {}, {}
    local best, bestV = top[1], nil
    for c = 1, #top do
        C.copy(me, m2)
        C.copy(opp, o2)
        C.play(m2, o2, stepsOf(top[c], d1 == d2))
        local sum, done = 0, true
        for a = 1, 6 do
            for b = a, 6 do
                R.reply = -1e9
                A.gen(o2, m2, a, b, replyLeaf)
                sum = sum - R.reply * (a == b and 1 or 2)
                if R.nodes > A.NODE_CAP or now() - t0 > budget then
                    done = false
                    break
                end
            end
            if not done then break end
        end
        if not done then break end
        if not bestV or sum > bestV then best, bestV = top[c], sum end
    end
    return stepsOf(best, d1 == d2)
end

return A
end
