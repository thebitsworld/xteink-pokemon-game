-- Saving a Tide & Paper game in progress, loaded only at start and exit:
--   local Save = smudge.dofile("save.lua")(T)
--   Save.serialize(g)  /  Save.deserialize(s) -> g or nil

return function(T)
local Save = {}
local PHASES = { "draw", "keep", "play", "crab" }

local function list(t) return table.concat(t, ",") end
local function parse(s)
    local t = {}
    for x in s:gmatch("%d+") do t[#t + 1] = tonumber(x) end
    return t
end

-- Fields separated by "|": numbers, then the card lists.
function Save.serialize(g)
    local phase = 0
    for i, ph in ipairs(PHASES) do
        if ph == g.phase then phase = i end
    end
    return table.concat({
        g.scores[1], g.scores[2], g.starter, g.round, g.turn, phase, g.caller or 0, g.extraTurn and 1 or 0,
        list(g.deck), list(g.piles[1]), list(g.piles[2]), list(g.hands[1]), list(g.hands[2]),
        list(g.played[1]), list(g.played[2]), list(g.drawn or {}),
    }, "|")
end

function Save.deserialize(s)
    local f = {}
    for part in (s .. "|"):gmatch("([^|]*)|") do f[#f + 1] = part end
    if #f ~= 16 then return nil end
    local phase = PHASES[tonumber(f[6])]
    if not phase then return nil end
    local g = {
        scores = { tonumber(f[1]), tonumber(f[2]) }, starter = tonumber(f[3]), round = tonumber(f[4]),
        turn = tonumber(f[5]), phase = phase, caller = tonumber(f[7]) ~= 0 and tonumber(f[7]) or nil,
        extraTurn = f[8] == "1",
        deck = parse(f[9]), piles = { parse(f[10]), parse(f[11]) }, hands = { parse(f[12]), parse(f[13]) },
        played = { parse(f[14]), parse(f[15]) },
    }
    if phase == "keep" then g.drawn = parse(f[16]) end
    -- Every card exactly once.
    local seen, count = {}, 0
    for _, t in ipairs({ g.deck, g.piles[1], g.piles[2], g.hands[1], g.hands[2], g.played[1], g.played[2],
                         g.drawn or {} }) do
        for _, id in ipairs(t) do
            if id < 1 or id > T.DECK_SIZE or seen[id] then return nil end
            seen[id], count = true, count + 1
        end
    end
    if count ~= T.DECK_SIZE or not g.scores[1] or not g.scores[2] then return nil end
    return g
end

return Save
end
