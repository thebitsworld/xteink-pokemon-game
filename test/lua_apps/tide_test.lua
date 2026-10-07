-- Logic tests for apps/tide (run by ctest: LuaAppLogic_tide).
local T = dofile(APPS .. "/tide/logic.lua")
local AI = dofile(APPS .. "/tide/ai.lua")(T)
local Save = dofile(APPS .. "/tide/save.lua")(T)
local Setup = dofile(APPS .. "/tide/setup.lua")(T)

math.randomseed(77)
local function rand(n) return math.random(1, n) end

-- Card ids of a kind, in order.
local function ids(kind)
    local out = {}
    for id = 1, T.DECK_SIZE do
        if T.KIND[id] == kind then out[#out + 1] = id end
    end
    return out
end

-- A game with player 1 holding `hand` (ids) and nothing else anywhere.
local function with(hand, played)
    local g = Setup.new(rand)
    g.hands = { hand, {} }
    g.played = { played or {}, {} }
    return g
end

-- The deck.
assert(T.DECK_SIZE == 58)
assert(#ids(T.CRAB) == 9 and #ids(T.MERMAID) == 4 and #ids(T.CAPTAIN) == 1)

-- Scoring.
do
    local crab, boat, fish, sw, sh = ids(T.CRAB), ids(T.BOAT), ids(T.FISH), ids(T.SWIMMER), ids(T.SHARK)
    assert(T.cardPoints(with({ crab[1], crab[2], crab[3] }), 1) == 1, "three crabs make one pair")
    assert(T.cardPoints(with({ sw[1], sh[1], sh[2] }), 1) == 1, "a swimmer and a shark")
    assert(T.cardPoints(with({ boat[1] }, { boat[2], boat[3] }), 1) == 1, "played pairs count")
    local shell = ids(T.SHELL)
    assert(T.cardPoints(with({ shell[1] }), 1) == 0)
    assert(T.cardPoints(with({ shell[1], shell[2], shell[3] }), 1) == 4)
    local oct = ids(T.OCTOPUS)
    assert(T.cardPoints(with({ oct[1], oct[2], oct[3], oct[4], oct[5] }), 1) == 12)
    local pen, sail = ids(T.PENGUIN), ids(T.SAILOR)
    assert(T.cardPoints(with({ pen[1] }), 1) == 1)
    assert(T.cardPoints(with({ pen[1], pen[2], ids(T.COLONY)[1] }), 1) == 3 + 4, "colony: 2 per penguin")
    assert(T.cardPoints(with({ sail[1], sail[2], ids(T.CAPTAIN)[1] }), 1) == 5 + 6, "captain: 3 per sailor")
    assert(T.cardPoints(with({ boat[1], ids(T.BEACON)[1] }, { boat[2], boat[3] }), 1) == 1 + 3, "beacon: 1 per boat")
    assert(T.cardPoints(with({ fish[1], ids(T.SHOAL)[1] }), 1) == 1)
    -- Mermaids: the first scores the most common mark, the next the next one.
    local m = ids(T.MERMAID)
    local hand = { m[1], m[2] }
    local marksSeen = {}
    for id = 1, T.DECK_SIZE do
        local mk = T.MARK[id]
        if mk == 1 and #marksSeen < 3 and T.KIND[id] ~= T.MERMAID then marksSeen[#marksSeen + 1] = id end
    end
    for _, id in ipairs(marksSeen) do hand[#hand + 1] = id end -- three cards of mark 1
    local g = with(hand)
    local base = 0
    do
        local h2 = {}
        for i = 3, #hand do h2[#h2 + 1] = hand[i] end
        base = T.cardPoints(with(h2), 1)
    end
    assert(T.cardPoints(g, 1) == base + 3, "the first mermaid scores the 3 cards of mark 1")
    assert(T.markBonus(g, 1) == 3)
end

-- Pairs and their effects.
do
    local crab, boat, fish, sw, sh = ids(T.CRAB), ids(T.BOAT), ids(T.FISH), ids(T.SWIMMER), ids(T.SHARK)
    assert(T.isPair(crab[1], crab[2]) and T.isPair(sh[1], sw[2]) and not T.isPair(sw[1], sw[2]))
    assert(not T.isPair(crab[1], boat[1]))

    local g = with({ boat[1], boat[2] })
    g.phase, g.turn = "play", 1
    assert(T.playPair(g, boat[1], boat[2], rand) == "boat")
    T.endTurn(g)
    assert(g.turn == 1 and g.phase == "draw", "a boat pair plays again")

    g = with({ fish[1], fish[2] })
    g.phase, g.turn = "play", 1
    local deck = #g.deck
    assert(T.playPair(g, fish[1], fish[2], rand) == "fish")
    assert(#g.hands[1] == 1 and #g.deck == deck - 1, "a fish pair draws a card")

    g = with({ sw[1], sh[1] })
    g.hands[2] = { crab[5] }
    g.phase, g.turn = "play", 1
    assert(T.playPair(g, sh[1], sw[1], rand) == "steal")
    assert(g.hands[1][1] == crab[5] and #g.hands[2] == 0, "a shark and swimmer steal")

    g = with({ crab[1], crab[2] })
    g.piles = { { fish[3], boat[4] }, {} }
    g.phase, g.turn = "play", 1
    assert(T.playPair(g, crab[1], crab[2], rand) == "crab" and g.phase == "crab")
    assert(T.crabTake(g, fish[3]) and g.hands[1][1] == fish[3] and #g.piles[1] == 1, "a crab pair takes any discarded card")

    assert(T.playPair(g, crab[3], crab[4], rand) == nil, "only cards in hand")
end

-- Drawing and discarding.
do
    local g = Setup.new(rand)
    g.turn = 1
    assert(#g.piles[1] == 1 and #g.piles[2] == 1 and #g.deck == 56)
    assert(T.drawTwo(g) and g.phase == "keep")
    local a, b = g.drawn[1], g.drawn[2]
    assert(T.keep(g, 2, 1))
    assert(g.hands[1][1] == b and g.piles[1][2] == a)
    g.piles[2] = {}
    g.phase = "draw"
    T.drawTwo(g)
    assert(not T.keep(g, 1, 1), "an empty pile must take the discard")
    assert(T.keep(g, 1, 2))
end

-- Ending a round.
do
    local shell, crab = ids(T.SHELL), ids(T.CRAB)
    local g = Setup.new(rand)
    g.turn, g.phase = 1, "play"
    g.hands = { { shell[1], shell[2], shell[3], shell[4], shell[5] }, { crab[1], crab[2] } }
    assert(T.canEnd(g), "8 points")
    assert(T.stop(g) and g.phase == "round")
    assert(g.scores[1] == 8 and g.scores[2] == 1, "Stop: both score their cards")

    -- Last chance that comes off: the caller adds the mark bonus, the other only its bonus.
    g = Setup.new(rand)
    g.turn, g.phase = 1, "play"
    g.hands = { { shell[1], shell[2], shell[3], shell[4], shell[5] }, { crab[1], crab[2] } }
    local bonus1, bonus2 = T.markBonus(g, 1), T.markBonus(g, 2)
    assert(T.lastChance(g) and g.turn == 2 and g.phase == "draw")
    assert(not T.canEnd(g))
    g.phase = "play"
    assert(T.endTurn(g) and g.phase == "round")
    assert(g.scores[1] == 8 + bonus1 and g.scores[2] == bonus2)

    -- And one that fails.
    g = Setup.new(rand)
    g.turn, g.phase = 1, "play"
    local oct = ids(T.OCTOPUS)
    g.hands = { { shell[1], shell[2], shell[3], shell[4], shell[5] }, { crab[1], crab[2], crab[3], crab[4] } }
    g.played = { {}, { oct[1], oct[2], oct[3], oct[4] } }
    assert(T.cardPoints(g, 2) == 11)
    bonus1 = T.markBonus(g, 1)
    local theirs = T.cardPoints(g, 2)
    T.lastChance(g)
    g.phase = "play"
    T.endTurn(g)
    assert(g.scores[1] == bonus1 and g.scores[2] == theirs)

    -- Four mermaids win at once.
    g = Setup.new(rand)
    g.turn, g.phase = 1, "draw"
    local m = ids(T.MERMAID)
    g.hands[1] = { m[1], m[2], m[3] }
    g.piles[1] = { m[4] }
    T.takePile(g, 1)
    assert(g.phase == "over" and g.winner == 1)

    -- A deck run dry ends the round with no score.
    g = Setup.new(rand)
    g.turn, g.phase, g.deck = 1, "play", {}
    T.endTurn(g)
    assert(g.phase == "round" and g.result.kind == "empty" and g.scores[1] == 0)
end

-- Saving.
do
    local g = Setup.new(rand)
    g.turn = 1
    T.drawTwo(g)
    g.scores = { 12, 31 }
    local s = Save.serialize(g)
    local h = Save.deserialize(s)
    assert(h and h.phase == "keep" and h.drawn[1] == g.drawn[1] and h.scores[2] == 31)
    assert(Save.serialize(h) == s)
    assert(Save.deserialize("1|2|3") == nil)
end

-- Device against device: every game ends, and level 2 beats level 1.
do
    local wins = { 0, 0 }
    for _ = 1, 150 do
        local g = Setup.new(rand)
        local guard = 0
        while g.phase ~= "over" do
            guard = guard + 1
            assert(guard < 3000, "the game ends")
            if g.phase == "round" then Setup.nextRound(g, rand) else AI.turn(g, g.turn == 1 and 2 or 1, rand) end
        end
        wins[g.winner] = wins[g.winner] + 1
    end
    assert(AI.describe({ { "take", 1, 1 }, { "pair", 1, 2, "crab" }, { "stop" } }) ==
           "Device took Crab from pile 1; played a Crab pair; said Stop.")
    print(string.format("level 2 won %d of 150 against level 1", wins[1]))
    assert(wins[1] > 90)
end

print("tide logic ok")
