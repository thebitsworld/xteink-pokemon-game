-- Kakuro layouts, loaded only while a puzzle is made, before kakurogen.lua
-- (the X3 cannot hold both). make.lua's steps (as for Slitherlink):
--   local L = smudge.dofile("kakurolay.lua")()
--   local _, d = L.make(n, rand)   -- then kakurogen.lua's G.reduce
--
-- A layout of black cells is drawn at random (symmetric, the first row and
-- column black, runs of 2 to 5 cells, every white cell joined) and its white
-- cells filled with digits, no repeat in a run. The result is one flat
-- table d, cell by cell, row by row: 0 for a black cell, else its digit.

return function()
local G = {}
local byte = string.byte

-- The number of digits in a 9-bit set of digits (bit d - 1 is digit d).
local NIB = "\0\1\1\2\1\2\2\3\1\2\2\3\2\3\3\4"
local function pop(m)
    return byte(NIB, (m & 15) + 1) + byte(NIB, (m >> 4 & 15) + 1) + byte(NIB, (m >> 8) + 1)
end

-- A random digit of set `free` (not empty): the lowest or highest with
-- `edge` 1 or 2, else any.
local function pickDigit(free, rand, edge)
    local cnt = pop(free)
    local pick = edge == 1 and 1 or edge == 2 and cnt or rand(cnt)
    local digit = 0
    repeat
        digit = digit + 1
        if free & (1 << (digit - 1)) ~= 0 then pick = pick - 1 end
    until pick == 0
    return digit
end

-- Turns white cells with a run of one (across or down) black, with their
-- mirror cells, until none is left.
local function fixSingles(b, n)
    local changed = true
    while changed do
        changed = false
        for i = n + 2, n * n do
            if b[i] == 0 then
                local c = (i - 1) % n
                local h = b[i - 1] == 0 or (c < n - 1 and b[i + 1] == 0)
                local v = b[i - n] == 0 or (i + n <= n * n and b[i + n] == 0)
                if not (h and v) then
                    b[i], b[n * n + n + 2 - i] = 1, 1
                    changed = true
                end
            end
        end
    end
end

-- Whether the white cells are many enough (at least `least`) and all
-- joined: cells reached from the first are marked 2, then made white again.
local function joined(b, n, least)
    local N, first, whites = n * n, nil, 0
    for i = 1, N do
        if b[i] == 0 then first, whites = first or i, whites + 1 end
    end
    if whites < least then return false end
    local count, grew = 1, true
    b[first] = 2
    while grew do
        grew = false
        for i = 1, N do
            if b[i] == 0 then
                local c = (i - 1) % n
                if (c > 0 and b[i - 1] == 2) or (c < n - 1 and b[i + 1] == 2) or (i > n and b[i - n] == 2)
                    or (i <= N - n and b[i + n] == 2) then
                    b[i], count, grew = 2, count + 1, true
                end
            end
        end
    end
    for i = 1, N do
        if b[i] == 2 then b[i] = 0 end
    end
    return count == whites
end

-- A random layout (b), or nil. `density`: % of black inside the border.
function G.layout(n, rand, density)
    local b, N = {}, n * n
    for i = 1, N do
        b[i] = (i <= n or (i - 1) % n == 0) and 1 or 0
    end
    -- Cell (r, c) inside mirrors to (n - r, n - c): i to N + n + 2 - i.
    for i = n + 2, N do
        local j = N + n + 2 - i
        if b[i] == 0 and i <= j and rand(100) <= density then b[i], b[j] = 1, 1 end
    end
    -- Runs longer than 5 get a black cell (and its mirror) inside: long runs
    -- leave too many digit sets for reasoning to start.
    for step = 1, n, n - 1 do
        for i = n + 2, N do
            if b[i] == 0 and b[i - step] == 1 then
                local len, j = 0, i
                while j <= N and b[j] == 0 and (step == n or (j - 1) % n > 0) do
                    len, j = len + 1, j + step
                end
                if len > 5 then
                    local k = i + step * (rand(len - 4) + 1)
                    b[k], b[N + n + 2 - k] = 1, 1
                end
            end
        end
    end
    fixSingles(b, n)
    if not joined(b, n, (n - 1) * (n - 1) * 3 // 5) then return nil end
    return b
end

-- Digits for the white cells of layout b (no repeat in a run): d, or nil.
-- Cells are filled row by row, backing up when one has no digit left; each
-- cell keeps the digits it tried in d[i] // 16 (its digit: d[i] % 16).
-- The runs after each black cell lean low or high (bits of j * seed):
-- where both runs of a cell agree, it gets the lowest or highest digit
-- free, so many sums have few digit sets, where reasoning can start.
function G.fill(b, n, rand, cap)
    local N, d, seed = n * n, {}, rand(32000) * 2 + 1
    for i = 1, N do d[i] = 0 end
    local i, nodes = n + 2, 0
    while i <= N do
        if b[i] == 1 then
            i = i + 1
        else
            nodes = nodes + 1
            if nodes > cap then return nil end
            local tried = d[i] // 16
            local used, j = tried, i - 1
            while b[j] == 0 do
                used, j = used | (1 << (d[j] % 16 - 1)), j - 1
            end
            local pa = (j * seed >> 8) % 2 + 1
            j = i - n
            while b[j] == 0 do
                used, j = used | (1 << (d[j] % 16 - 1)), j - n
            end
            local pd = (j * seed >> 9) % 2 + 1
            local free = 511 & ~used
            if free == 0 then
                d[i] = 0
                repeat i = i - 1 until i <= n or b[i] == 0
                if i <= n then return nil end
            else
                local digit = pickDigit(free, rand, pa == pd and (rand(4) == 1 and 3 or pa) or 3)
                d[i] = digit + (tried | (1 << (digit - 1))) * 16
                i = i + 1
            end
        end
    end
    for k = 1, N do d[k] = d[k] % 16 end
    return d
end

-- Topology, for make.lua's steps: Kakuro has none to keep.
function G.topology() return true end

-- A filled layout: nil and the digits d (0: black).
function G.make(n, rand)
    while true do
        local b = G.layout(n, rand, 26)
        local d = b and G.fill(b, n, rand, 4000)
        if d then return nil, d end
    end
end

return G
end
