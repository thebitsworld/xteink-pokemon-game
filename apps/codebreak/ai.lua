-- The device breaking your code. Usage:
--   local A = smudge.dofile("ai.lua")(L)
--   local code, left = A.choose(g, rand, clock, ms)
-- with L the rules (logic.lua) and g a game whose guesses are all scored.
-- Returns the next guess and how many codes still fit the scores, or nil
-- when no code fits them (a score was given wrong). Loaded only while the
-- device thinks.
--
-- Every turn one pass over all codes (an odometer, no list of codes) counts
-- the codes that fit the scores so far and picks one of them at random
-- (reservoir sampling), keeping them in a flat array while few fit. When
-- the codes left times all possible guesses is small enough (BUDGET scores),
-- the guess is Knuth's minimax instead: the code whose worst score leaves
-- the fewest codes, preferring one that may itself be the answer, then the
-- lowest. On 4 x 6 with repeats it opens 1122 and takes its second guess
-- from a table (Knuth's: at most five guesses for any code); otherwise the
-- opening is fixed and the guesses are random fitting codes until few fit.

return function(L)
local A = {}
A.BUDGET = 1296 * 64
A.work = 0 -- scores computed by the last choose (for measuring)

-- The second guess after 1122 for each score (black, white): the minimax
-- below, worked out once (test/lua_apps/codebreak_test.lua checks it).
A.KNUTH2 = "00:3345 01:2344 02:2344 03:1213 04:2211 10:1344 11:1134 12:1213 20:1234 21:1223 " ..
               "22:1213 30:1223"

local H = 0
local hd, hb, ht, hc = {}, {}, {}, {} -- the guesses' digits (flat), black, black + white, symbol counts (8 each)
local cd, cc = {}, {}                 -- the odometer's code and its symbol counts
local cand, candc = {}, {}            -- the fitting codes while few: digits (4 bits each), symbol counts (3 bits each)
local part, gs = {}, {}

local function loadHistory(g)
    local n = g.n
    H = L.count(g)
    for j = 1, H do
        local code = L.guess(g, j)
        local b, w = L.scoreOf(g, j)
        hb[j], ht[j] = b, b + w
        local b8 = (j - 1) * 8
        for s = 1, 8 do hc[b8 + s] = 0 end
        for i = 1, n do
            local d = code:byte(i) - 48
            hd[(j - 1) * n + i] = d
            hc[b8 + d] = hc[b8 + d] + 1
        end
    end
end

local function reset(n)
    for s = 1, 8 do cc[s] = 0 end
    for i = 1, n do cd[i] = 1 end
    cc[1] = n
end

-- The next code in numerical order; false after the last.
local function advance(n, k)
    local i = n
    while i >= 1 do
        local d = cd[i]
        cc[d] = cc[d] - 1
        if d < k then
            cd[i] = d + 1
            cc[d + 1] = cc[d + 1] + 1
            return true
        end
        cd[i] = 1
        cc[1] = cc[1] + 1
        i = i - 1
    end
    return false
end

local function fits(n, k)
    for j = 1, H do
        local base, b = (j - 1) * n, 0
        for i = 1, n do
            if cd[i] == hd[base + i] then b = b + 1 end
        end
        if b ~= hb[j] then return false end
        local t, b8 = 0, (j - 1) * 8
        for s = 1, k do
            local x, y = cc[s], hc[b8 + s]
            if x < y then t = t + x else t = t + y end
        end
        if t ~= ht[j] then return false end
    end
    return true
end

local function current(n)
    local t = {}
    for i = 1, n do t[i] = string.char(48 + cd[i]) end
    return table.concat(t)
end

local function distinct(k)
    for s = 1, k do
        if cc[s] > 1 then return false end
    end
    return true
end

-- One pass over all codes: how many fit, one of them at random, and the
-- first `keep` of them in cand/candc.
local function scan(g, rand, keep)
    local n, k = g.n, g.k
    reset(n)
    local count, pick = 0, nil
    repeat
        if (g.rep or distinct(k)) and fits(n, k) then
            count = count + 1
            if count <= keep then
                local d, c = 0, 0
                for i = n, 1, -1 do d = d * 16 + cd[i] end
                for s = 8, 1, -1 do c = c * 8 + cc[s] end
                cand[count], candc[count] = d, c
            end
            if rand(count) == 1 then pick = current(n) end
        end
        A.work = A.work + 1
    until not advance(n, k)
    return count, pick
end

-- Knuth's minimax over every code as a guess against the m codes in cand.
local function minimax(g, m, clock, ms)
    local n, k = g.n, g.k
    local start = clock and clock()
    local best, bestCand, bestCode = m + 1, false, nil
    local win = n * 6 + n
    local guesses = 0
    reset(n)
    repeat
        guesses = guesses + 1
        if clock and bestCode and guesses % 32 == 0 and clock() - start > ms then break end
        local ns, gd = 0, 0
        for i = n, 1, -1 do gd = gd * 16 + cd[i] end
        for s = 1, k do
            if cc[s] > 0 then
                ns = ns + 1
                gs[ns] = s
            end
        end
        for f = 0, 35 do part[f] = 0 end
        local worst, j = 0, 1
        while j <= m do
            local x, b = cand[j] ~ gd, 0
            for _ = 1, n do
                if x & 15 == 0 then b = b + 1 end
                x = x >> 4
            end
            local t, c8 = 0, candc[j]
            for q = 1, ns do
                local s = gs[q]
                local x, y = cc[s], (c8 >> (3 * s - 3)) & 7
                if x < y then t = t + x else t = t + y end
            end
            local f = b * 6 + t
            local c = part[f] + 1
            part[f] = c
            if c > worst then
                worst = c
                if worst > best then break end
            end
            j = j + 1
        end
        A.work = A.work + (j > m and m or j)
        if j > m then
            local isCand = part[win] > 0
            if worst < best or (worst == best and isCand and not bestCand) then
                best, bestCand, bestCode = worst, isCand, current(n)
            end
        end
    until not advance(n, k)
    return bestCode
end

local function opening(g)
    if g.rep then return g.n == 4 and "1122" or "11223" end
    return g.n == 4 and "1234" or "12345"
end

-- The device's next guess for game g (nil if no code fits the scores) and
-- the number of codes that fit.
function A.choose(g, rand, clock, ms)
    A.work = 0
    loadHistory(g)
    local n, k = g.n, g.k
    if H == 0 then return opening(g), nil end
    local pool = 1
    for _ = 1, n do pool = pool * k end
    if n == 4 and k == 6 and g.rep and H == 1 and L.guess(g, 1) == "1122" and not A.noTable then
        local b, w = L.scoreOf(g, 1)
        local code = A.KNUTH2:match(b .. w .. ":(%d+)")
        if code then
            local count = scan(g, rand, 0)
            if count == 0 then return nil, 0 end
            return code, count
        end
    end
    local keep = A.BUDGET // pool
    local count, pick = scan(g, rand, keep)
    if count == 0 then return nil, 0 end
    if count > 2 and count <= keep then
        local code = minimax(g, count, clock, ms)
        if code then return code, count end
    end
    return pick, count
end

return A
end
