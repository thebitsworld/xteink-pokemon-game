-- Logic tests for apps/hearts/logic.lua, ai.lua and save.lua (run by ctest: LuaAppLogic_hearts).
local H = dofile(APPS .. "/hearts/logic.lua")
local AI = dofile(APPS .. "/hearts/ai.lua")(H)
local Save = dofile(APPS .. "/hearts/save.lua")(H)

-- math.random, seeded: Lua here has 32-bit integers, so a hand-rolled LCG overflows.
math.randomseed(31)
local function rand(n) return math.random(1, n) end

local function card(rank, suit) return suit * 13 + rank - 1 end -- rank 2..14

-- Cards: the ace is high, the queen of spades and the hearts carry points.
assert(H.rank(card(14, H.CLUBS)) == 14 and H.suit(card(14, H.CLUBS)) == H.CLUBS)
assert(card(12, H.SPADES) == H.QUEEN_OF_SPADES and H.points(H.QUEEN_OF_SPADES) == 13)
assert(H.points(card(2, H.HEARTS)) == 1 and H.points(card(13, H.SPADES)) == 0)
assert(card(14, H.HEARTS) > card(13, H.HEARTS), "the ace beats the king")
assert(H.TWO_OF_CLUBS == card(2, H.CLUBS))

-- The deal: 13 each, every card once; passing rotates left, right, across, none.
do
    local g = H.new(rand)
    local seen = {}
    for s = 1, 4 do
        assert(#g.hands[s] == 13)
        for _, c in ipairs(g.hands[s]) do assert(not seen[c]) seen[c] = true end
    end
    assert(H.passOffset(1) == 1 and H.passOffset(2) == 3 and H.passOffset(3) == 2 and H.passOffset(4) == 0)
    assert(g.phase == "pass")
end

-- Passing is simultaneous; afterwards the two of clubs leads.
do
    local g = H.new(rand)
    local picks = {}
    for s = 1, 4 do picks[s] = { g.hands[s][1], g.hands[s][2], g.hands[s][3] } end
    local before = {}
    for s = 1, 4 do before[s] = picks[s] end
    assert(H.pass(g, picks))
    for s = 1, 4 do
        assert(#g.hands[s] == 13)
        local to = s % 4 + 1
        for _, c in ipairs(before[s]) do
            local found = false
            for _, x in ipairs(g.hands[to]) do if x == c then found = true end end
            assert(found, "passed to the left")
        end
    end
    assert(g.phase == "play")
    local lead = H.legal(g, g.turn)
    assert(#lead == 1 and lead[1] == H.TWO_OF_CLUBS)
end

-- A hand-made position for the playing rules.
local function fixed(hands)
    local g = { scores = { 0, 0, 0, 0 }, hand = 4, over = false }
    H.deal(g, rand) -- hand 4: no pass
    g.hands = hands
    g.played = string.rep("0", 52)
    for s = 1, 4 do table.sort(g.hands[s]) end
    H.startPlay(g)
    return g
end

do
    -- South: 2C, 5H, QS; West: 3C, 4D; North: KC, 9H; East: 4H, AS.
    local g = fixed({ { card(2, H.CLUBS), card(5, H.HEARTS), H.QUEEN_OF_SPADES },
                      { card(3, H.CLUBS), card(4, H.DIAMONDS) },
                      { card(13, H.CLUBS), card(9, H.HEARTS) },
                      { card(4, H.HEARTS), card(14, H.SPADES) } })
    assert(g.turn == 1)
    assert(H.play(g, H.TWO_OF_CLUBS))
    assert(H.refusal(g, 2, card(4, H.DIAMONDS)) == "Follow clubs")
    assert(H.play(g, card(3, H.CLUBS)))
    assert(H.play(g, card(13, H.CLUBS)))
    -- East has no clubs and only a heart and the ace of spades: no points on the
    -- first trick, so the ace of spades it is.
    assert(H.refusal(g, 4, card(4, H.HEARTS)) == "No points on the first trick")
    assert(H.play(g, card(14, H.SPADES)))
    assert(g.turn == 3 and g.tricks == 1 and g.taken[3] == 0, "North's king took it")
    -- North leads: hearts not broken while North has something else? North
    -- holds only a heart, so it may lead it.
    assert(H.refusal(g, 3, card(9, H.HEARTS)) == nil)
end

do
    -- Hearts cannot be led until broken.
    local g = fixed({ { card(2, H.CLUBS), card(5, H.HEARTS), card(6, H.DIAMONDS) },
                      { card(3, H.CLUBS), card(4, H.DIAMONDS), card(8, H.HEARTS) },
                      { card(13, H.CLUBS), card(9, H.HEARTS), card(10, H.DIAMONDS) },
                      { card(4, H.CLUBS), card(14, H.HEARTS), card(3, H.DIAMONDS) } })
    H.play(g, card(2, H.CLUBS)); H.play(g, card(3, H.CLUBS)); H.play(g, card(13, H.CLUBS)); H.play(g, card(4, H.CLUBS))
    assert(g.turn == 3)
    assert(H.refusal(g, 3, card(9, H.HEARTS)) == "Hearts are not broken yet")
    H.play(g, card(10, H.DIAMONDS))
    H.play(g, card(3, H.DIAMONDS))
    H.play(g, card(6, H.DIAMONDS))
    assert(not g.broken)
    H.play(g, card(4, H.DIAMONDS))
    assert(g.turn == 3 and g.taken[3] == 0)
end

-- Scoring a hand, and shooting the moon.
do
    local g = { scores = { 10, 20, 30, 40 }, taken = { 5, 0, 21, 0 }, hand = 1 }
    H.scoreHand(g)
    assert(g.scores[1] == 15 and g.scores[3] == 51 and not g.moon and g.phase == "scored")
    g = { scores = { 0, 0, 0, 0 }, taken = { 0, 26, 0, 0 }, hand = 1 }
    H.scoreHand(g)
    assert(g.moon == 2 and g.scores[2] == 0 and g.scores[1] == 26 and g.scores[4] == 26)
    g = { scores = { 99, 0, 0, 0 }, taken = { 1, 25, 0, 0 }, hand = 1 }
    H.scoreHand(g)
    assert(g.over and #H.leaders(g) == 2, "100 ends the game; a tie shares the lead")
end

-- The computer players: pass dangerous cards, duck, dump the queen.
do
    local g = fixed({ { H.QUEEN_OF_SPADES, card(14, H.SPADES), card(13, H.HEARTS), card(2, H.CLUBS), card(3, H.CLUBS),
                        card(4, H.CLUBS), card(5, H.DIAMONDS) }, {}, {}, {} })
    local pass = AI.choosePass(g, 1, 2, rand)
    local has = {}
    for _, c in ipairs(pass) do has[c] = true end
    assert(has[H.QUEEN_OF_SPADES] and has[card(14, H.SPADES)], "passes the queen and the ace of spades")

    -- Ducking: hearts led with the 9, holding 7 and Q of hearts -> play the 7... the highest under 9.
    g = fixed({ { card(7, H.HEARTS), card(12, H.HEARTS), card(8, H.HEARTS) }, { card(9, H.HEARTS), card(2, H.CLUBS) },
                {}, {} })
    g.tricks, g.broken = 3, true
    g.turn = 2
    H.play(g, card(9, H.HEARTS))
    g.turn = 1
    assert(AI.choosePlay(g, 2, rand) == card(8, H.HEARTS), "the highest heart under the 9")
    assert(AI.choosePlay(g, 1, rand) == card(7, H.HEARTS), "level 1: the lowest")

    -- Void in the suit led: shed the queen of spades.
    g = fixed({ { H.QUEEN_OF_SPADES, card(5, H.HEARTS), card(14, H.DIAMONDS) }, { card(9, H.CLUBS), card(2, H.CLUBS) },
                {}, {} })
    g.tricks, g.broken = 3, true
    g.turn = 2
    H.play(g, card(9, H.CLUBS))
    g.turn = 1
    assert(AI.choosePlay(g, 2, rand) == H.QUEEN_OF_SPADES)
end

-- Whole games: they always finish legally, and the better player scores less.
local function playGame(levels)
    local g = H.new(rand)
    local guard = 0
    while not g.over do
        if g.phase == "pass" then
            local picks = {}
            for s = 1, 4 do picks[s] = AI.choosePass(g, s, levels[s], rand) end
            assert(H.pass(g, picks))
        elseif g.phase == "play" then
            local c = AI.choosePlay(g, levels[g.turn], rand)
            assert(H.refusal(g, g.turn, c) == nil, "the computer chose an illegal card")
            assert(H.play(g, c))
        else
            local total = 0
            for s = 1, 4 do total = total + g.handScores[s] end
            assert(total == 26 or total == 78, "26 points a hand (78 after a moon)")
            assert(H.nextHand(g, rand))
        end
        guard = guard + 1
        assert(guard < 5000)
    end
    return g
end

do
    local sharp, rookie, games = 0, 0, 40
    for n = 1, games do
        -- The level-2 player sits in a different seat each game.
        local seat = (n - 1) % 4 + 1
        local levels = { 1, 1, 1, 1 }
        levels[seat] = 2
        local g = playGame(levels)
        sharp = sharp + g.scores[seat]
        for s = 1, 4 do
            if s ~= seat then rookie = rookie + g.scores[s] / 3 end
        end
    end
    print(string.format("average final score: level 2 %.1f, level 1 %.1f", sharp / games, rookie / games))
    assert(sharp < rookie, "level 2 scores less")
end

-- Save and restore mid-hand.
do
    local g = H.new(rand)
    local picks = {}
    for s = 1, 4 do picks[s] = AI.choosePass(g, s, 2, rand) end
    H.pass(g, picks)
    for _ = 1, 6 do H.play(g, AI.choosePlay(g, 2, rand)) end
    local s = Save.serialize(g)
    local back = Save.deserialize(s)
    assert(back and Save.serialize(back) == s and back.turn == g.turn and #back.trick == #g.trick)
    assert(Save.deserialize(s:gsub("|(%d+)%.", "|%1.%1.", 1)) == nil, "a card twice")
end

print("hearts logic ok")
