-- Logic tests for apps/nonogram/logic.lua and generator.lua (run by ctest: LuaAppLogic_nonogram).
local N = dofile(APPS .. "/nonogram/logic.lua")
local Gen = dofile(APPS .. "/nonogram/generator.lua")(N)

-- math.random, seeded: Lua here has 32-bit integers, so a hand-rolled LCG overflows.
math.randomseed(12345)
local function rand(n) return math.random(1, n) end

local function same(a, b)
    if #a ~= #b then return false end
    for i = 1, #a do if a[i] ~= b[i] then return false end end
    return true
end

-- Runs.
assert(same(N.runs({ true, true, false, true }), { 2, 1 }))
assert(same(N.runs({ false, false }), {}))
assert(same(N.runs({ true, true, true }), { 3 }))

-- The line solver against brute force: every line of length 1..8, with random
-- known cells, must give exactly the cells all matching lines agree on.
do
    for _ = 1, 3000 do
        local len = rand(8)
        local picture = {}
        for i = 1, len do picture[i] = rand(2) == 1 end
        local clue = N.runs(picture)
        local known = {}
        for i = 1, len do
            local k = rand(4)
            known[i] = (k == 1) and (picture[i] and 1 or 2) or 0
        end
        -- Brute force over all 2^len lines.
        local fill, empty, count = {}, {}, 0
        for mask = 0, (1 << len) - 1 do
            local line = {}
            for i = 1, len do line[i] = mask & (1 << (i - 1)) ~= 0 end
            local ok = same(N.runs(line), clue)
            for i = 1, len do
                if known[i] == 1 and not line[i] then ok = false end
                if known[i] == 2 and line[i] then ok = false end
            end
            if ok then
                count = count + 1
                for i = 1, len do
                    if line[i] then fill[i] = true else empty[i] = true end
                end
            end
        end
        local out = {}
        for i = 1, len do out[i] = known[i] end
        assert(count > 0 and Gen.solveLine(len, clue, out) ~= nil, "the picture itself always fits")
        for i = 1, len do
            local want = (fill[i] and not empty[i]) and 1 or ((empty[i] and not fill[i]) and 2 or 0)
            assert(out[i] == want, "line solver disagrees with brute force")
        end
    end
    -- A contradiction is reported.
    assert(Gen.solveLine(3, { 3 }, { 0, 2, 0 }) == nil)
    assert(Gen.solveLine(3, {}, { 1, 0, 0 }) == nil)
    local line = { 0, 0, 0 }
    assert(Gen.solveLine(3, {}, line) == true and line[1] == 2 and line[3] == 2, "no runs: all empty")
    assert(Gen.solveLine(3, {}, line) == false, "nothing left to change")
end

-- Counts the solutions of a puzzle (up to 2) by trying every row in turn.
local function countSolutions(size, rows, cols)
    local lines = {}
    for r = 1, size do
        lines[r] = {}
        for mask = 0, (1 << size) - 1 do
            local line = {}
            for c = 1, size do line[c] = mask & (1 << (c - 1)) ~= 0 end
            if same(N.runs(line), rows[r]) then lines[r][#lines[r] + 1] = line end
        end
    end
    local grid, found = {}, 0
    local function rec(r)
        if found >= 2 then return end
        if r > size then
            for c = 1, size do
                local col = {}
                for rr = 1, size do col[rr] = grid[rr][c] end
                if not same(N.runs(col), cols[c]) then return end
            end
            found = found + 1
            return
        end
        for _, line in ipairs(lines[r]) do
            grid[r] = line
            rec(r + 1)
        end
    end
    rec(1)
    return found
end

-- Generated puzzles have exactly one solution, no blank lines, and are
-- mirrored left to right.
do
    for _ = 1, 15 do
        local picture = Gen.generate(5, rand, 400)
        assert(picture and #picture == 25, "a 5x5 puzzle is always found")
        local p = N.newPuzzle(5, picture)
        assert(countSolutions(5, p.rowClues, p.colClues) == 1, "exactly one solution")
        for r = 1, 5 do
            assert(#p.rowClues[r] > 0 and #p.colClues[r] > 0)
            for c = 1, 5 do
                assert(N.solid(p, c, r) == N.solid(p, 6 - c, r))
            end
        end
    end
    for _, size in ipairs({ 8, 10, 12 }) do
        local p = N.newPuzzle(size, Gen.generate(size, rand, 400))
        assert(Gen.lineSolvable(size, p.rowClues, p.colClues) == true)
    end
    -- An ambiguous puzzle (two diagonals) is not line-solvable.
    assert(Gen.lineSolvable(2, { { 1 }, { 1 } }, { { 1 }, { 1 } }) == false)
end

-- Playing: wrong fills are locked mistakes, marks are free, the last correct
-- fill solves it, and done lines are reported.
do
    local p = N.newPuzzle(2, "1011")
    local rows = N.cluesFor(2, { true, false, true, true })
    assert(same(rows[1], p.rowClues[1]) and same(rows[2], p.rowClues[2]), "booleans and strings agree")
    assert(same(p.rowClues[1], { 1 }) and same(p.rowClues[2], { 2 }) and same(p.colClues[1], { 2 }))
    assert(N.fill(p, 2, 1) == "mistake" and p.mistakes == 1)
    assert(N.fill(p, 2, 1) == nil and not N.toggleMark(p, 2, 1), "a mistake is locked")
    assert(N.toggleMark(p, 1, 1) and N.cell(p, 1, 1) == N.MARKED)
    assert(N.fill(p, 1, 1) == nil, "a marked cell is not filled")
    assert(N.toggleMark(p, 1, 1) and N.cell(p, 1, 1) == N.UNKNOWN)
    assert(N.fill(p, 1, 1) == "filled" and N.rowDone(p, 1) and not N.colDone(p, 1))
    N.fill(p, 1, 2)
    assert(not p.solved)
    N.fill(p, 2, 2)
    assert(p.solved and N.rowDone(p, 2) and N.colDone(p, 1))
    assert(N.fill(p, 1, 1) == nil, "nothing changes once solved")
end

-- Save and restore round-trip; a save that contradicts its picture is refused.
do
    local p = N.newPuzzle(8, Gen.generate(8, rand, 400))
    for r = 1, 8 do N.fill(p, r, r) end
    N.toggleMark(p, 1, 8)
    p.elapsedMs = 61000
    local s = N.serialize(p)
    local back = N.deserialize(s)
    assert(back and N.serialize(back) == s and back.mistakes == p.mistakes and back.filled == p.filled)
    assert(N.deserialize("2|0|0|1001|1100") == nil, "filled cell outside the picture")
    assert(N.deserialize("2|0|0|1001|1000") ~= nil)
    assert(N.deserialize("2|0|0|101|1000") == nil)
end

print("nonogram logic ok")
