-- Logic tests for apps/yacht/logic.lua and ai.lua (run by ctest: LuaAppLogic_yacht).
local Y = dofile(APPS .. "/yacht/logic.lua")
local AI = dofile(APPS .. "/yacht/ai.lua")(Y)

local function stream(list)
    local i = 0
    return function(n)
        i = i + 1
        local v = list[(i - 1) % #list + 1]
        assert(v >= 1 and v <= n)
        return v
    end
end

local function card(filled)
    local c = Y.newCard()
    for b, v in pairs(filled or {}) do c[b] = v end
    return c
end

-- Plain box scores.
do
    local d = { 3, 3, 3, 5, 5 }
    assert(Y.rawScore(d, 3) == 9 and Y.rawScore(d, 5) == 10 and Y.rawScore(d, 1) == 0)
    assert(Y.rawScore(d, Y.THREE_KIND) == 19 and Y.rawScore(d, Y.FOUR_KIND) == 0)
    assert(Y.rawScore(d, Y.FULL_HOUSE) == 25 and Y.rawScore(d, Y.CHANCE) == 19)
    assert(Y.rawScore({ 1, 2, 3, 4, 6 }, Y.SMALL_STRAIGHT) == 30)
    assert(Y.rawScore({ 3, 4, 5, 6, 3 }, Y.SMALL_STRAIGHT) == 30)
    assert(Y.rawScore({ 1, 2, 3, 5, 6 }, Y.SMALL_STRAIGHT) == 0)
    assert(Y.rawScore({ 2, 3, 4, 5, 6 }, Y.LARGE_STRAIGHT) == 40)
    assert(Y.rawScore({ 1, 2, 3, 4, 6 }, Y.LARGE_STRAIGHT) == 0)
    assert(Y.rawScore({ 4, 4, 4, 4, 2 }, Y.FOUR_KIND) == 18 and Y.rawScore({ 4, 4, 4, 4, 2 }, Y.FULL_HOUSE) == 0)
    assert(Y.rawScore({ 6, 6, 6, 6, 6 }, Y.YACHT) == 50)
    assert(Y.rawScore({ 6, 6, 6, 6, 6 }, Y.FULL_HOUSE) == 25, "five of a kind is a full house")
    assert(Y.rawScore({ 2, 2, 3, 3, 4 }, Y.FULL_HOUSE) == 0, "two pairs is not")
end

-- Totals and the upper bonus at 63.
do
    local c = card({ [1] = 3, [2] = 6, [3] = 9, [4] = 12, [5] = 15, [6] = 18 })
    assert(Y.upperTotal(c) == 63 and Y.total(c) == 63 + 35)
    c[6] = 12
    assert(Y.total(c) == 57, "62 or less earns nothing")
    c[Y.CHANCE] = 0
    assert(Y.total(c) == 57, "a zero is a score, not a minus one")
end

-- Joker rules: matching upper box first, then any lower box (paid in full),
-- an upper zero only once the lower section is full. Bonus only after a 50.
do
    local d = { 3, 3, 3, 3, 3 }
    local c = card({ [Y.YACHT] = 50 })
    assert(Y.jokerApplies(c, d) and Y.yachtBonusDue(c, d))
    assert(Y.canTake(c, d, 3) and not Y.canTake(c, d, Y.CHANCE) and not Y.canTake(c, d, 4))
    c[3] = 6
    assert(Y.canTake(c, d, Y.LARGE_STRAIGHT) and Y.boxScore(c, d, Y.LARGE_STRAIGHT) == 40)
    assert(Y.boxScore(c, d, Y.SMALL_STRAIGHT) == 30 and Y.boxScore(c, d, Y.FULL_HOUSE) == 25)
    assert(not Y.canTake(c, d, 1), "upper boxes are closed while a lower box is free")
    for b = 7, 13 do if c[b] == Y.UNSCORED then c[b] = 0 end end
    assert(Y.canTake(c, d, 1) and Y.boxScore(c, d, 1) == 0)

    local z = card({ [Y.YACHT] = 0 })
    assert(Y.jokerApplies(z, d) and not Y.yachtBonusDue(z, d), "a zeroed Yacht box forces the joker, no bonus")
    assert(not Y.jokerApplies(card(), d), "no joker while the Yacht box is free")
end

-- A turn: three rolls at most, holds kept, then exactly one box.
do
    local g = Y.new(1)
    assert(not Y.canHold(g) and Y.take(g, Y.CHANCE) == nil, "must roll first")
    assert(Y.roll(g, stream({ 1, 2, 3, 4, 5 })))
    assert(Y.toggleHold(g, 1) and Y.toggleHold(g, 2))
    assert(Y.roll(g, stream({ 6 })))
    assert(g.dice[1] == 1 and g.dice[2] == 2 and g.dice[3] == 6 and g.dice[5] == 6, "held dice stay")
    assert(Y.roll(g, stream({ 6 })) and not Y.canRoll(g) and not Y.canHold(g))
    assert(Y.take(g, Y.CHANCE) == 21 and g.rolls == 0)
    assert(Y.take(g, 1) == nil, "next turn must roll first")
    assert(Y.roll(g, stream({ 2 })) and Y.take(g, Y.CHANCE) == nil, "box already used")

    -- A Yacht bonus is added when the second five of a kind is taken.
    g = Y.new(1)
    Y.roll(g, stream({ 5 }))
    assert(Y.take(g, Y.YACHT) == 50)
    Y.roll(g, stream({ 5 }))
    assert(Y.take(g, 5) == 25 and g.cards[1].yachtBonus == 100)
    assert(Y.total(g.cards[1]) == 175)
end

-- Two players alternate; the game ends when both cards are full.
do
    local g = Y.new(2)
    for _ = 1, Y.BOXES * 2 do
        local p = g.turn
        Y.roll(g, stream({ 1, 2, 3, 4, 6 }))
        local box = AI.chooseBox(g.cards[p], g.dice)
        assert(box and Y.take(g, box))
    end
    assert(g.over and Y.cardFull(g.cards[1]) and Y.cardFull(g.cards[2]))
    assert(Y.winner(g) == -1, "same dice every turn is a tie")
    assert(not Y.roll(g, stream({ 1 })))
end

-- The opponent: keeps four of a kind, keeps a four-run, takes the Yacht.
do
    local held = AI.chooseHold(card(), { 6, 6, 2, 6, 6 }, 2)
    assert(held[1] and held[2] and not held[3] and held[4] and held[5], "keeps the four sixes")
    assert(AI.chooseBox(card(), { 4, 4, 4, 4, 4 }) == Y.YACHT)
    assert(AI.chooseBox(card(), { 2, 3, 4, 5, 6 }) == Y.LARGE_STRAIGHT)
    -- Under the joker it obeys the forced box.
    assert(AI.chooseBox(card({ [Y.YACHT] = 50 }), { 2, 2, 2, 2, 2 }) == 2)
end

-- Whole solo games end with a full card and a plausible score.
do
    math.randomseed(5)
    local function rand(n) return math.random(1, n) end
    local total = 0
    for _ = 1, 10 do
        local g = Y.new(1)
        while not g.over do
            Y.roll(g, rand)
            while Y.canRoll(g) do
                g.held = AI.chooseHold(g.cards[1], g.dice, Y.ROLLS - g.rolls)
                Y.roll(g, rand)
            end
            assert(Y.take(g, AI.chooseBox(g.cards[1], g.dice)))
        end
        total = total + Y.total(g.cards[1])
    end
    assert(total / 10 > 150, "the opponent should average well over 150")
end

-- Save and restore round-trip; a corrupt save is refused.
do
    local g = Y.new(2)
    Y.roll(g, stream({ 3, 1, 4, 1, 5 }))
    Y.toggleHold(g, 3)
    Y.take(g, Y.CHANCE)
    Y.roll(g, stream({ 6 }))
    local s = Y.serialize(g)
    local back = Y.deserialize(s)
    assert(back and Y.serialize(back) == s and back.turn == 2 and back.cards[1][Y.CHANCE] == 14)
    assert(Y.deserialize("3|1|0|0|1.1.1.1.1|00000|0:" .. string.rep("-1.", 12) .. "-1") == nil)
    assert(Y.deserialize("1|1|0|0|1.1.9.1.1|00000|0:" .. string.rep("-1.", 12) .. "-1") == nil)
    assert(Y.deserialize("1|1|0|0|1.1.1.1.1|00000|0:" .. string.rep("-1.", 12) .. "-1") ~= nil)
end

print("yacht logic ok")
