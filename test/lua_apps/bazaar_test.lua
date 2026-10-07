-- Logic tests for apps/bazaar/logic.lua, ai.lua and save.lua (run by ctest: LuaAppLogic_bazaar).
local B = dofile(APPS .. "/bazaar/logic.lua")
local AI = dofile(APPS .. "/bazaar/ai.lua")(B)
local Save = dofile(APPS .. "/bazaar/save.lua")(B)

-- math.random, seeded: Lua here has 32-bit integers, so a hand-rolled LCG overflows.
math.randomseed(21)
local function rand(n) return math.random(1, n) end

local function cardsInPlay(g)
    local n = #g.deck
    for _, c in ipairs(g.market) do if c ~= 0 then n = n + 1 end end
    for p = 1, 2 do n = n + g.players[p].camels + B.handSize(g.players[p]) end
    return n
end

-- Set-up: 3 camels + 2 cards in the market, 5 cards each, 40 left.
do
    local g = B.new(rand)
    assert(B.marketCount(g, B.CAMEL) >= 3 and #g.market == 5)
    assert(#g.deck == 40 and cardsInPlay(g) == 55)
    for p = 1, 2 do
        local pl = g.players[p]
        assert(pl.camels + B.handSize(pl) == 5, "five cards each, camels in the herd")
    end
end

-- A hand-made position.
local function position(market, hand1, camels1)
    local g = B.new(rand)
    g.market = market
    g.players[1].hand = hand1
    g.players[1].camels = camels1 or 0
    g.turn = 1
    return g
end

-- Taking one good, the hand limit, taking all the camels.
do
    local g = position({ B.DIAMOND, B.CAMEL, B.CAMEL, B.CLOTH, B.LEATHER }, { 0, 0, 0, 0, 0, 0 })
    local deck = #g.deck
    assert(B.play(g, { kind = "one", slot = 1 }, rand))
    assert(g.players[1].hand[B.DIAMOND] == 1 and #g.deck == deck - 1 and g.market[1] ~= 0 and g.turn == 2)
    assert(B.refusal(g, { kind = "one", slot = 2 }) ~= nil or g.market[2] ~= B.CAMEL)

    g = position({ B.DIAMOND, B.CAMEL, B.CAMEL, B.CLOTH, B.LEATHER }, { 2, 2, 1, 1, 1, 0 })
    assert(B.refusal(g, { kind = "one", slot = 1 }):find("full"), "seven cards: no more")
    assert(B.play(g, { kind = "camels" }, rand))
    assert(g.players[1].camels == 2, "both camels")
end

-- Exchanges: at least two, same number back, not the same kind, camels may
-- be given but not taken, no deck draw.
do
    local g = position({ B.DIAMOND, B.GOLD, B.CAMEL, B.CLOTH, B.LEATHER }, { 0, 0, 0, 2, 1, 0 }, 3)
    assert(B.refusal(g, { kind = "trade", slots = { 1 }, give = {}, camels = 1 }):find("two"))
    assert(B.refusal(g, { kind = "trade", slots = { 1, 3 }, give = {}, camels = 2 }), "no camels in a trade")
    assert(B.refusal(g, { kind = "trade", slots = { 1, 4 }, give = { [B.CLOTH] = 1, [B.SPICE] = 1 }, camels = 0 })
               :find("kind"), "giving cloth while taking cloth")
    assert(B.refusal(g, { kind = "trade", slots = { 1, 2 }, give = { [B.SPICE] = 1 }, camels = 0 }):find("as many"))
    local deck = #g.deck
    assert(B.play(g, { kind = "trade", slots = { 1, 2 }, give = { [B.SPICE] = 1 }, camels = 1 }, rand))
    local pl = g.players[1]
    assert(pl.hand[B.DIAMOND] == 1 and pl.hand[B.GOLD] == 1 and pl.hand[B.SPICE] == 0 and pl.camels == 2)
    assert(#g.deck == deck, "an exchange does not draw")
    assert(B.marketCount(g, B.CAMEL) == 2 and B.marketCount(g, B.SPICE) == 1)
end

-- Selling: tokens top first, the precious two-card minimum, bonuses,
-- a short pile still earns the bonus.
do
    local g = position({ B.CAMEL, B.CAMEL, B.CAMEL, B.CLOTH, B.LEATHER }, { 1, 0, 0, 4, 0, 0 })
    assert(B.refusal(g, { kind = "sell", good = B.DIAMOND, count = 1 }):find("two"))
    assert(B.play(g, { kind = "sell", good = B.CLOTH, count = 4 }, rand))
    local pl = g.players[1]
    assert(pl.rupees >= 5 + 3 + 3 + 2 + 4 and pl.rupees <= 5 + 3 + 3 + 2 + 6, "tokens plus a 4-card bonus")
    assert(pl.bonuses == 1 and pl.goodsTokens == 4 and #g.piles[B.CLOTH] == 3)
    g = position({ B.CAMEL, B.CAMEL, B.CAMEL, B.CLOTH, B.LEATHER }, { 0, 0, 0, 0, 3, 0 })
    g.piles[B.SPICE] = { 1 }
    assert(B.play(g, { kind = "sell", good = B.SPICE, count = 3 }, rand))
    assert(g.players[1].goodsTokens == 1 and g.players[1].bonuses == 1, "short pile, bonus anyway")
end

-- The round ends with three empty piles; the camel token and the seal.
do
    local g = position({ B.CAMEL, B.CAMEL, B.CAMEL, B.CLOTH, B.LEATHER }, { 0, 0, 2, 0, 0, 0 }, 4)
    g.players[2].camels = 1
    g.piles[B.DIAMOND], g.piles[B.GOLD] = {}, {}
    g.piles[B.SILVER] = { 5, 5 }
    assert(B.play(g, { kind = "sell", good = B.SILVER, count = 2 }, rand))
    assert(g.phase == "scored" and g.camelToken == 1)
    assert(g.players[1].rupees >= 15 and g.roundWinner == 1 and g.seals[1] == 1)
    assert(B.nextRound(g, rand) and g.turn == 2, "the loser starts the next round")
    assert(g.round == 2 and cardsInPlay(g) == 55)
end

-- Ties are broken by bonus tokens, then goods tokens, then camels.
do
    local g = B.new(rand)
    local a, b = g.players[1], g.players[2]
    a.rupees, b.rupees, a.bonuses, b.bonuses, a.goodsTokens, b.goodsTokens = 30, 30, 1, 2, 5, 5
    a.camels, b.camels = 0, 0
    B.endRound(g)
    assert(g.roundWinner == 2)
    g = B.new(rand)
    a, b = g.players[1], g.players[2]
    a.rupees, b.rupees, a.camels, b.camels = 10, 10, 2, 2
    B.endRound(g)
    assert(g.roundWinner == 0 and g.seals[1] == 0 and g.seals[2] == 0, "a full tie gives no seal")
end

-- The deck running out ends the round.
do
    local g = position({ B.DIAMOND, B.CAMEL, B.CAMEL, B.CLOTH, B.LEATHER }, { 0, 0, 0, 0, 0, 0 })
    g.deck = {}
    assert(B.play(g, { kind = "one", slot = 1 }, rand))
    assert(g.phase == "scored")
end

-- Whole matches: the computer only plays legal moves, every match ends, and
-- the better level wins more.
local function match(levelA, levelB)
    local g = B.new(rand)
    local guard = 0
    while g.phase ~= "over" do
        if g.phase == "scored" then
            B.nextRound(g, rand)
        else
            local level = (g.turn == 1) and levelA or levelB
            local mv = AI.choose(g, level, rand)
            assert(mv and B.refusal(g, mv) == nil, "a legal move")
            assert(B.play(g, mv, rand))
            assert(cardsInPlay(g) <= 55)
        end
        guard = guard + 1
        assert(guard < 2000)
    end
    return g.winner
end

do
    local wins = 0
    for _ = 1, 30 do
        if match(2, 1) == 1 then wins = wins + 1 end
    end
    print(string.format("normal beat easy %d/30", wins))
    assert(wins >= 20)
end

-- Save and restore.
do
    local g = B.new(rand)
    for _ = 1, 6 do B.play(g, AI.choose(g, 2, rand), rand) end
    if g.phase == "play" then
        local s = Save.serialize(g)
        local back = Save.deserialize(s)
        assert(back and Save.serialize(back) == s)
    end
    assert(Save.deserialize("1|1|1|0|0|1|1.2|7.7.7.1.2") == nil)
end

print("bazaar logic ok")
