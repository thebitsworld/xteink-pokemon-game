-- Whodunit case wording: turns a case built by gen.lua (all numbers) into
-- clue sentences and a case file. Usage:
--   smudge.dofile("words.lua")(W).finish(case)
-- Loaded only after gen.lua has been dropped.

return function(W)
local Words = {}

-- The words for each trait's values (gen.lua picks the values).
local TRAITS = {
    [1] = { { "left-handed", "right-handed" }, { "tall", "short" }, { "blue-eyed", "brown-eyed", "green-eyed" } },
    [2] = { { "heavy", "light" }, { "made of metal", "not made of metal" } },
    [3] = { { "indoors", "outdoors" } },
}
local DETAILS = { "next to a stopped clock", "beside a puddle of wax", "under a crooked portrait",
                  "near a pair of muddy boots", "by a cracked mirror", "beside spilled ink", "in a smell of smoke",
                  "next to a torn letter", "under a scatter of feathers", "beside a broken teacup" }

local function subject(case, a, x)
    local name = case.names[a][x]
    if a == W.SUSPECT then return name end
    if a == W.WEAPON then return "Whoever had the " .. name end
    if a == W.PLACE then return "Whoever was in the " .. name end
    return "Whoever acted out of " .. name
end

local function predicate(case, b, values, negative)
    local names = {}
    for _, v in ipairs(values) do
        local name = case.names[b][v]
        if b == W.WEAPON or b == W.PLACE then name = "the " .. name end
        names[#names + 1] = name
    end
    local list = table.concat(names, " or ")
    if b == W.SUSPECT then return (negative and "was not " or "was ") .. list end
    if b == W.WEAPON then return (negative and "did not have " or "had ") .. list end
    if b == W.PLACE then return (negative and "was not in " or "was in ") .. list end
    return (negative and "did not act out of " or "acted out of ") .. list
end

local function sentence(case, c)
    local s = subject(case, c.a, c.x) .. " "
    if c.trait then return s .. c.trait .. "." end
    local inside, outside = {}, {}
    for v = 1, case.items do
        if c.set & (1 << (v - 1)) ~= 0 then inside[#inside + 1] = v else outside[#outside + 1] = v end
    end
    if #inside <= #outside or #outside == 0 then return s .. predicate(case, c.b, inside, false) .. "." end
    return s .. predicate(case, c.b, outside, true) .. "."
end

-- The full clue for a candidate that is kept, with its sentence.
local function unpack(case, c)
    local clue = { a = c & 7, x = (c >> 3) & 7, b = (c >> 6) & 7, set = (c >> 9) & 15 }
    local t, value = (c >> 13) & 3, (c >> 15) & 3
    if t > 0 then
        local text = TRAITS[clue.b][t][value]
        clue.trait = (clue.b == W.WEAPON) and ("had a weapon that was " .. text) or ("was " .. text)
    end
    clue.text = sentence(case, clue)
    return clue
end

-- Writes the case's clues, the murder clue and the case file.
function Words.finish(case)
    local n, k = case.categories, case.items
    for i, c in ipairs(case.clues) do case.clues[i] = unpack(case, c) end
    for i = 1, k do case.details[i] = DETAILS[case.details[i]] end
    local place = case.solution[W.PLACE][case.murderer]
    case.murderClue = "The body was found " .. case.details[place] .. "."
    -- The case file: each item's traits (and each place's detail), as text.
    case.dossier = {}
    for c = 1, n do
        case.dossier[c] = {}
        for i = 1, k do
            local parts = {}
            for q, values in ipairs(TRAITS[c] or {}) do parts[#parts + 1] = values[case.traits[c][i][q]] end
            if c == W.PLACE then parts[#parts + 1] = case.details[i] end
            case.dossier[c][i] = table.concat(parts, ", ")
        end
    end
    return case
end

return Words
end
