-- Whodunit: logic-grid murder mysteries, generated on the reader. No
-- drawing, so it can be tested on a computer (test/lua_apps/whodunit_test.lua).
--
-- Every suspect had one weapon, was in one place (and at the hard level had
-- one motive); one of them is the murderer. From the clues you work out who
-- had what and was where, then the last clue - where the body was found -
-- names the murderer, whom you accuse with their weapon and place.
--
-- Cases are built by gen.lua, loaded only for that; this file is what play
-- needs: the levels, the grid of marks, the accusation and saving the marks.

local W = {}

W.SUSPECT, W.WEAPON, W.PLACE, W.MOTIVE = 1, 2, 3, 4
W.CATEGORY_NAMES = { "Suspects", "Weapons", "Places", "Motives" }
W.LEVELS = {
    { name = "Easy", categories = 3, items = 3, forms = { "yes", "no", "no" } },
    { name = "Medium", categories = 3, items = 4, forms = { "no", "no", "trait", "trait", "yes" } },
    { name = "Hard", categories = 4, items = 4, forms = { "no", "no", "trait", "either" } },
}

-- The grid ------------------------------------------------------------------
-- A mark for every pair of items from two different categories:
-- 0 unknown, 1 yes, 2 no. Stored flat, keyed by both items.

W.UNKNOWN, W.YES, W.NO = 0, 1, 2

-- Keys 1..96: the pair of categories (6 pairs at most), then the cell, so a
-- grid is a small array rather than a hash.
local PAIR = { [6] = 1, [7] = 2, [8] = 3, [11] = 4, [12] = 5, [16] = 6 } -- a * 4 + b for a < b

local function key(a, i, b, j)
    if a > b then a, i, b, j = b, j, a, i end
    return (PAIR[a * 4 + b] - 1) * 16 + (i - 1) * 4 + j
end
W.key = key

function W.newGrid() return {} end
function W.mark(grid, a, i, b, j) return grid[key(a, i, b, j)] or 0 end

-- Whether an accusation (suspect, weapon, place, motive) is right.
function W.check(case, accused)
    local s = case.murderer
    for c = 1, case.categories do
        if accused[c] ~= case.solution[c][s] then return false end
    end
    return true
end

-- The player's marks, saved as one digit per grid cell in a fixed order.
function W.packMarks(case, grid)
    local out = {}
    local n, k = case.categories, case.items
    for a = 1, n do
        for b = a + 1, n do
            for i = 1, k do
                for j = 1, k do out[#out + 1] = tostring(grid[key(a, i, b, j)] or 0) end
            end
        end
    end
    return table.concat(out)
end

function W.unpackMarks(case, s)
    local grid, p = {}, 1
    local n, k = case.categories, case.items
    for a = 1, n do
        for b = a + 1, n do
            for i = 1, k do
                for j = 1, k do
                    local v = tonumber(s:sub(p, p)) or 0
                    if v ~= 0 then grid[key(a, i, b, j)] = v end
                    p = p + 1
                end
            end
        end
    end
    return grid
end

return W
