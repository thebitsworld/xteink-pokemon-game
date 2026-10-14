-- Kakuro puzzle generator, loaded only while a puzzle is made, after
-- kakurolay.lua has made a filled layout (the X3 cannot hold both). Usage:
--   local grid = smudge.dofile("kakurogen.lua")().reduce(n, T, d, rand, millis, deadline)
--
-- The clues are the sums of the layout's digits d (0: black). Random digits
-- leave many solutions, so the digits are then changed one at a time,
-- keeping a change unless the solver's reasoning (no guessing) leaves more
-- cells open than before, until it leaves none: the puzzle then has one
-- solution, found by reasoning alone. When that gets stuck the layout is
-- given up (make.lua asks kakurolay.lua for another). Grids are
-- kakuro.lua's: "#" black, "1".."9" a white cell.

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

-- The number of digits in a 9-bit set of digits (bit d - 1 is digit d).
local NIB = "\0\1\1\2\1\2\2\3\1\2\2\3\2\3\3\4"
local function pop(m)
    return byte(NIB, (m & 15) + 1) + byte(NIB, (m >> 4 & 15) + 1) + byte(NIB, (m >> 8) + 1)
end

-- The sets of `len` digits from d..9 adding up to `sum`, put into acc as
-- two bytes each (m: the digits taken so far). Depth 9 at most.
local acc, na = {}, 0
local function sets(d, len, sum, m)
    if len == 0 then
        if sum == 0 then
            acc[na + 1], acc[na + 2], na = m >> 8, m & 255, na + 2
        end
        return
    end
    if sum < len * d + len * (len - 1) // 2 or sum > len * 9 - len * (len - 1) // 2 then return end
    sets(d + 1, len - 1, sum - d, m | (1 << (d - 1)))
    sets(d + 1, len, sum, m)
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

-- A solver for the layout of digits d (n x n), by reasoning only. solver.clues(d) takes
-- the clues from digits d (solver.update(d, i) after cell i changed);
-- solver.deduce() then narrows each white cell's set of digits
-- (solver.mask[i]) as far as reasoning goes and returns how many cells stay
-- open (or false on a contradiction). Reasoning: for each run only the digit
-- sets of its length and sum that fit its cells stay, which narrows the
-- cells, and a digit every such set has that only one cell can take goes
-- there. It never rules out a digit that a solution has, so when no cell
-- stays open the solution is the only one.
function G.solver(d, n)
    local N = n * n
    local mask, dirty, rk = {}, {}, {}
    -- Runs: RB the black cell before (+ 128 for a down run), RL the length.
    -- Strings, a byte each (the X3 has little room); dirty and rk lend
    -- their room meanwhile, and mask for each white cell's runs, kept after
    -- the R bytes of RL (across) and RB (down).
    local R = 0
    for i = 1, N do
        mask[i] = 0
        if d[i] == 0 then
            for step = 1, n, n - 1 do
                local len, j = 0, i + step
                while j <= N and (step == n or (j - 1) % n > 0) and d[j] > 0 do
                    len, j = len + 1, j + step
                end
                if len > 0 then
                    R = R + 1
                    dirty[R], rk[R] = step == 1 and i or i + 128, len
                end
            end
        end
    end
    local RB, RL = pack(dirty, R), pack(rk, R)
    for down = 0, 1 do
        for j = 1, R do
            local x = byte(RB, j)
            if (x >= 128) == (down == 1) then
                local step = down == 1 and n or 1
                for k = x % 128 + step, x % 128 + step * byte(RL, j), step do mask[k] = j end
            end
        end
        if down == 0 then RL = RL .. pack(mask, N) else RB = RB .. pack(mask, N) end
    end
    local solver = { mask = mask }

    -- The digit sets of run j, two bytes each (short strings: Lua keeps
    -- one copy of each).
    local function sumOf(j, d)
        local x, len = byte(RB, j), byte(RL, j)
        local step = x >= 128 and n or 1
        local s = 0
        for k = x % 128 + step, x % 128 + step * len, step do s = s + d[k] end
        na = 0
        sets(1, len, s, 0)
        rk[j] = char(unpack(acc, 1, na))
    end
    function solver.clues(d)
        for j = 1, R do sumOf(j, d) end
    end
    function solver.update(d, i)
        sumOf(byte(RL, R + i), d)
        sumOf(byte(RB, R + i), d)
    end

    local function run(j)
        local x, len = byte(RB, j), byte(RL, j)
        local step = x >= 128 and n or 1
        local s = x % 128 + step
        local e = s + step * (len - 1)
        local fixed = 0
        for k = s, e, step do
            local m = mask[k]
            if m & (m - 1) == 0 then
                if fixed & m ~= 0 then return false end
                fixed = fixed | m
            end
        end
        local U, I, cs = 0, 511, rk[j]
        for t = 1, #cs, 2 do
            local S = byte(cs, t) * 256 + byte(cs, t + 1)
            if S & fixed == fixed then
                local ok = true
                for k = s, e, step do
                    if mask[k] & S == 0 then
                        ok = false
                        break
                    end
                end
                if ok then U, I = U | S, I & S end
            end
        end
        if U == 0 then return false end
        local free = U & ~fixed
        for k = s, e, step do
            local m = mask[k]
            local nm = (m & (m - 1) == 0) and m or (m & free)
            if nm == 0 then return false end
            if nm ~= m then
                mask[k] = nm
                dirty[byte(RL, R + k)], dirty[byte(RB, R + k)] = true, true
            end
        end
        -- A digit every set has, that one cell only can take.
        I = I & ~fixed
        while I ~= 0 do
            local bit = I & -I
            I = I & ~bit
            local at, count = nil, 0
            for k = s, e, step do
                if mask[k] & bit ~= 0 then at, count = k, count + 1 end
            end
            if count == 0 then return false end
            if count == 1 and mask[at] ~= bit then
                mask[at] = bit
                dirty[byte(RL, R + at)], dirty[byte(RB, R + at)] = true, true
            end
        end
        return true
    end

    function solver.deduce()
        for i = 1, N do mask[i] = d[i] > 0 and 511 or 0 end
        for j = 1, R do dirty[j] = true end
        local again = true
        while again do
            again = false
            for j = 1, R do
                if dirty[j] then
                    dirty[j] = false
                    if not run(j) then return false end
                    again = true
                end
            end
        end
        local open = 0
        for i = 1, N do
            local m = mask[i]
            if m & (m - 1) ~= 0 then open = open + 1 end
        end
        return open
    end
    return solver
end

-- The digits white cell i could take: none used elsewhere in its runs.
local function freeAt(d, n, i)
    local used = 0
    for step = 1, n, n - 1 do
        local j = i - step
        while d[j] > 0 do
            used, j = used | (1 << (d[j] - 1)), j - step
        end
        j = i + step
        while j <= n * n and d[j] > 0 and (step == n or (j - 1) % n > 0) do
            used, j = used | (1 << (d[j] - 1)), j + step
        end
    end
    return 511 & ~used
end

-- The puzzle (its grid) made from layout d, or nil when it gets stuck or
-- the time (millis() past deadline) is up.
function G.reduce(n, T, d, rand, millis, deadline)
    if not d then return nil end
    local solver = G.solver(d, n)
    solver.clues(d)
    local open, mask, stuck = solver.deduce(), solver.mask, 0
    while open ~= 0 do
        if stuck >= 6 * n or millis() > deadline then return nil end
        -- Another digit for a white cell (mostly one left open by the last
        -- deduction), kept unless more cells stay open.
        local i, m
        repeat
            i = rand(n * n)
            m = mask[i]
        until d[i] > 0 and (m & (m - 1) ~= 0 or rand(4) == 1)
        local free = freeAt(d, n, i) & ~(1 << (d[i] - 1))
        stuck = stuck + 1
        if free ~= 0 then
            local old = d[i]
            d[i] = pickDigit(free, rand, rand(3))
            solver.update(d, i)
            local now = solver.deduce() or n * n
            if now < open then
                open, stuck = now, 0
            elseif now > open then
                d[i] = old
                solver.update(d, i)
            end
        end
    end
    solver = nil
    for i = 1, n * n do d[i] = d[i] == 0 and "#" or char(48 + d[i]) end
    return table.concat(d)
end

return G
end
