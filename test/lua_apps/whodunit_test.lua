-- Logic tests for apps/whodunit/logic.lua and gen.lua (run by ctest: LuaAppLogic_whodunit).
local W = dofile(APPS .. "/whodunit/logic.lua")
local Gen = dofile(APPS .. "/whodunit/gen.lua")(W)
local Words = dofile(APPS .. "/whodunit/words.lua")(W)

-- A case as the app builds it: generated, then worded.
local function generate(level, seed)
    local case = Gen.generate(level, seed)
    return case and Words.finish(case)
end

-- Every assignment of the other categories to the suspects that satisfies the
-- clues, by brute force (independent of the solver). Stops at 2.
local function permutations(k)
    local out, perm, used = {}, {}, {}
    local function rec(d)
        if d > k then
            local p = {}
            for i = 1, k do p[i] = perm[i] end
            out[#out + 1] = p
            return
        end
        for v = 1, k do
            if not used[v] then
                used[v], perm[d] = true, v
                rec(d + 1)
                used[v] = false
            end
        end
    end
    rec(1)
    return out
end

local function countSolutions(case, clues)
    local n, k = case.categories, case.items
    local perms = permutations(k)
    local solution = { {} }
    for i = 1, k do solution[1][i] = i end
    local found = 0
    local function holds()
        for _, c in ipairs(clues) do
            local s = nil
            for t = 1, k do
                if solution[c.a][t] == c.x then s = t end
            end
            if c.set & (1 << (solution[c.b][s] - 1)) == 0 then return false end
        end
        return true
    end
    local function rec(c)
        if found >= 2 then return end
        if c > n then
            if holds() then found = found + 1 end
            return
        end
        for _, p in ipairs(perms) do
            solution[c] = p
            rec(c + 1)
        end
    end
    rec(2)
    return found
end

-- Cases at every level: exactly one solution, every clue true, the solver
-- fills the grid and agrees with it, no clue is spare, and a seed always
-- gives the same case.
for level = 1, 3 do
    local total, maxClues = 0, 0
    local runs = (level == 3) and 6 or 25
    for seed = 1, runs do
        local case = generate(level, seed * 7919)
        assert(case, "a case for level " .. level .. " seed " .. seed)
        assert(#case.clues >= 2)
        total = total + #case.clues
        if #case.clues > maxClues then maxClues = #case.clues end
        assert(countSolutions(case, case.clues) == 1, "exactly one solution")
        local grid = W.newGrid()
        assert(Gen.solve(case, grid, case.clues), "solvable by reasoning alone")
        for a = 1, case.categories do
            for b = a + 1, case.categories do
                for s = 1, case.items do
                    local i, j = case.solution[a][s], case.solution[b][s]
                    assert(W.mark(grid, a, i, b, j) == W.YES, "the solver agrees with the solution")
                end
            end
        end
        for drop = 1, #case.clues do
            local fewer = {}
            for i, c in ipairs(case.clues) do
                if i ~= drop then fewer[#fewer + 1] = c end
            end
            assert(not Gen.solve(case, W.newGrid(), fewer), "no spare clue")
        end
        for _, c in ipairs(case.clues) do assert(type(c.text) == "string" and #c.text > 10) end
        for c = 1, case.categories do
            for i = 1, case.items do assert(case.dossier[c][i]) end
        end
        assert(case.murderClue:find(case.details[case.solution[W.PLACE][case.murderer]], 1, true))
        local again = generate(level, seed * 7919)
        assert(again.murderer == case.murderer and #again.clues == #case.clues and again.clues[1].text == case.clues[1].text,
               "same seed, same case")
    end
    print(string.format("level %d: %.1f clues on average, at most %d", level, total / runs, maxClues))
end

-- Names in a category have different initials (the grid labels use them).
do
    local case = generate(3, 99)
    for c = 1, case.categories do
        local seen = {}
        for _, name in ipairs(case.names[c]) do
            local initial = (c == W.SUSPECT) and name:match("(%a)%a*$") or name:sub(1, 1)
            initial = initial:upper()
            assert(not seen[initial], "initials repeat in " .. W.CATEGORY_NAMES[c])
            seen[initial] = true
        end
    end
end

-- Accusations, and the saved marks.
do
    local case = generate(2, 4242)
    local s = case.murderer
    local right = {}
    for c = 1, case.categories do right[c] = case.solution[c][s] end
    assert(W.check(case, right))
    local wrong = { right[1], right[2] % case.items + 1, right[3] }
    assert(not W.check(case, wrong))
    local grid = W.newGrid()
    grid[W.key(1, 2, 3, 1)] = W.YES
    grid[W.key(2, 3, 3, 4)] = W.NO
    local packed = W.packMarks(case, grid)
    local back = W.unpackMarks(case, packed)
    assert(W.mark(back, 1, 2, 3, 1) == W.YES and W.mark(back, 3, 4, 2, 3) == W.NO and W.packMarks(case, back) == packed)
end

print("whodunit logic ok")
