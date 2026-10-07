-- Dealing Tide & Paper games and rounds, loaded only for that:
--   local Setup = smudge.dofile("setup.lua")(T)
--   local g = Setup.new(rand)      -- a new game
--   Setup.nextRound(g, rand)       -- the next round, after a "round" result

return function(T)
local Setup = {}

local function shuffle(list, rand)
    for i = #list, 2, -1 do
        local j = rand(i)
        list[i], list[j] = list[j], list[i]
    end
end

local function newRound(g, rand)
    g.deck = {}
    for id = 1, T.DECK_SIZE do g.deck[id] = id end
    shuffle(g.deck, rand)
    g.piles = { { table.remove(g.deck) }, { table.remove(g.deck) } }
    g.hands = { {}, {} }
    g.played = { {}, {} }
    g.drawn = nil
    g.caller = nil
    g.extraTurn = false
    g.result = nil
    g.turn = g.starter
    g.phase = "draw"
    g.starter = 3 - g.starter
end

function Setup.new(rand)
    local g = { scores = { 0, 0 }, starter = 1, round = 0, winner = nil }
    newRound(g, rand)
    g.round = 1
    return g
end

-- The next round, after a "round" result.
function Setup.nextRound(g, rand)
    if g.phase ~= "round" then return false end
    g.round = g.round + 1
    newRound(g, rand)
    return true
end

return Setup
end
