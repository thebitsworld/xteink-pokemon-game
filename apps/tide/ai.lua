-- The Tide & Paper device player, loaded for its turn and dropped after:
--   local AI = smudge.dofile("ai.lua")(T)
--   local log = AI.turn(g, level, rand)   -- plays the whole turn
--   AI.describe(log)                      -- what it did, in a sentence
-- Level 1 takes what helps now and ends a round as soon as it can; level 2
-- also weighs cards that build towards points and judges when to bet.

return function(T)
local AI = {}
local KIND = T.KIND

-- How much card `id` is worth to player p: the points it adds now, plus
-- something for a start on a pair or a set (level 2).
local function value(g, p, id, level)
    local hand = g.hands[p]
    local before = T.cardPoints(g, p)
    hand[#hand + 1] = id
    local gain = T.cardPoints(g, p) - before
    hand[#hand] = nil
    if level < 2 then return gain end
    local kind, same = KIND[id], 0
    for _, x in ipairs(hand) do
        if KIND[x] == kind then same = same + 1 end
    end
    if T.isDuo(kind) and same % 2 == 0 then gain = gain + 0.3 end
    if kind == T.SHELL or kind == T.OCTOPUS or kind == T.SAILOR then gain = gain + 0.4 end
    if kind == T.MERMAID then gain = gain + 1 + T.mermaids(g, p) end
    return gain
end

-- Plays every pair in hand; returns false if the turn has to stop for good.
local function playPairs(g, level, rand, log)
    local again = true
    while again and g.phase == "play" do
        again = false
        local hand = g.hands[g.turn]
        for i = 1, #hand do
            for j = i + 1, #hand do
                if not again and T.isPair(hand[i], hand[j]) then
                    local a, b = hand[i], hand[j]
                    local did = T.playPair(g, a, b, rand)
                    log[#log + 1] = { "pair", a, b, did }
                    if g.phase == "crab" then
                        -- The best card in either discard pile.
                        local best, bestV = nil, -1
                        for q = 1, 2 do
                            for _, id in ipairs(g.piles[q]) do
                                local v = value(g, g.turn, id, level)
                                if v > bestV then best, bestV = id, v end
                            end
                        end
                        T.crabTake(g, best)
                        log[#log + 1] = { "crab", best }
                    end
                    again = true
                end
            end
        end
    end
end

-- A guess at the other player's points: what has been played, and some for
-- each card in hand.
local function guessOther(g, p)
    local q = 3 - p
    local hand = g.hands[q]
    g.hands[q] = {}
    local seen = T.cardPoints(g, q)
    g.hands[q] = hand
    return seen + #hand * 0.7
end

function AI.turn(g, level, rand)
    local log = {}
    local p = g.turn
    while g.phase ~= "round" and g.phase ~= "over" and g.turn == p do
        if g.phase == "draw" then
            local bestPile, bestV = nil, -1
            for q = 1, 2 do
                local pile = g.piles[q]
                if #pile > 0 then
                    local v = value(g, p, pile[#pile], level)
                    if v > bestV then bestPile, bestV = q, v end
                end
            end
            -- Level 1 now and then just takes a pile or draws, without thinking.
            local careless = level < 2 and rand(100) <= 30
            if careless and bestPile and #g.deck > 0 then bestV = rand(2) == 1 and 9 or -1 end
            if bestPile and (bestV >= 1 or #g.deck == 0) then
                log[#log + 1] = { "take", bestPile, g.piles[bestPile][#g.piles[bestPile]] }
                T.takePile(g, bestPile)
            elseif not T.drawTwo(g) then
                break
            else
                local keep = 1
                if careless then
                    keep = g.drawn[2] and rand(2) or 1
                elseif g.drawn[2] and value(g, p, g.drawn[2], level) > value(g, p, g.drawn[1], level) then
                    keep = 2
                end
                local piles = T.discardPiles(g)
                local pile = piles[#piles == 1 and 1 or rand(2)]
                log[#log + 1] = { "draw", g.drawn[keep], g.drawn[3 - keep], pile }
                T.keep(g, keep, pile)
            end
        end
        if g.phase == "play" then playPairs(g, level, rand, log) end
        if g.phase == "play" then
            local mine = T.cardPoints(g, p)
            if T.canEnd(g) then
                local theirs = guessOther(g, p)
                if level < 2 then
                    log[#log + 1] = { "stop" }
                    T.stop(g)
                elseif mine >= theirs + 5 and mine >= 10 then
                    log[#log + 1] = { "last" }
                    T.lastChance(g)
                elseif mine >= theirs or #g.deck < 10 then
                    log[#log + 1] = { "stop" }
                    T.stop(g)
                else
                    T.endTurn(g)
                end
            else
                T.endTurn(g)
            end
        end
    end
    return log
end

local NAMES = { "Crab", "Boat", "Fish", "Swimmer", "Shark", "Shell", "Octopus", "Penguin", "Sailor",
                "Beacon", "Shoal", "Colony", "Captain", "Mermaid" }
local EFFECT = { crab = "", boat = " (plays again)", fish = " (drew a card)", steal = " (took a card from you)" }

function AI.describe(log)
    local parts = {}
    for _, e in ipairs(log) do
        local what = e[1]
        if what == "take" then
            parts[#parts + 1] = "took " .. NAMES[KIND[e[3]]] .. " from pile " .. e[2]
        elseif what == "draw" then
            parts[#parts + 1] = "drew two, put " .. NAMES[KIND[e[3]]] .. " on pile " .. e[4]
        elseif what == "pair" then
            parts[#parts + 1] = "played a " .. NAMES[KIND[e[2]]] .. " pair" .. (EFFECT[e[4]] or "")
        elseif what == "crab" then
            parts[#parts + 1] = "took " .. NAMES[KIND[e[2]]] .. " from a pile"
        elseif what == "stop" then
            parts[#parts + 1] = "said Stop"
        elseif what == "last" then
            parts[#parts + 1] = "bet Last chance"
        end
    end
    return "Device " .. table.concat(parts, "; ") .. "."
end

return AI
end
