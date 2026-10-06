-- Logic tests for apps/solitaire/logic.lua (run by ctest: LuaAppLogic_solitaire).
local K = dofile(APPS .. "/solitaire/logic.lua")

-- math.random, seeded: Lua here has 32-bit integers, so a hand-rolled LCG overflows.
math.randomseed(7)
local function rand(n) return math.random(1, n) end

local function card(rankValue, suitValue) return suitValue * 13 + rankValue end -- suit 0..3

local function allCards(g)
    local seen, n = {}, 0
    local function add(list) for _, c in ipairs(list) do assert(not seen[c], "duplicate card") seen[c] = true n = n + 1 end end
    add(g.stock); add(g.waste)
    for i = 1, 4 do add(g.foundations[i]) end
    for i = 1, 7 do add(g.tableau[i].cards) end
    return n
end

-- The deal: 28 cards in columns of 1..7 with one face up each, 24 in stock.
do
    local g = K.new(rand, 1)
    for col = 1, 7 do
        assert(#g.tableau[col].cards == col)
        assert(g.tableau[col].down == col - 1)
    end
    assert(#g.stock == 24 and #g.waste == 0)
    assert(allCards(g) == 52)
end

-- Card helpers.
assert(K.rank(card(1, 0)) == 1 and K.suit(card(13, 3)) == 3)
assert(K.isRed(card(5, 1)) and K.isRed(card(5, 2)) and not K.isRed(card(5, 0)) and not K.isRed(card(5, 3)))

-- Drawing one or three, and recycling the waste.
do
    local g = K.new(rand, 3)
    assert(K.draw(g) and #g.waste == 3 and #g.stock == 21)
    for _ = 1, 7 do K.draw(g) end
    assert(#g.stock == 0 and #g.waste == 24)
    local topBefore = g.waste[24]
    assert(K.draw(g)) -- recycle
    assert(#g.stock == 24 and #g.waste == 0)
    assert(g.stock[1] == topBefore, "recycled stock must come back in order")
    assert(allCards(g) == 52)
end

-- Building rules on a hand-made position.
local function emptyGame()
    local g = K.new(rand, 1)
    g.stock, g.waste = {}, {}
    for i = 1, 4 do g.foundations[i] = {} end
    for i = 1, 7 do g.tableau[i] = { cards = {}, down = 0 } end
    return g
end

do
    local g = emptyGame()
    -- Column 1: hidden card, then black 8 (clubs) face up. Waste: red 7 (hearts).
    g.tableau[1] = { cards = { card(2, 3), card(8, 0) }, down = 1 }
    g.waste = { card(7, 2) }
    assert(K.move(g, { kind = "waste" }, { kind = "tableau", col = 1 }), "red 7 on black 8")
    -- Same colour is refused, so is a gap in rank.
    g.waste = { card(6, 1) }
    assert(not K.move(g, { kind = "waste" }, { kind = "tableau", col = 1 }), "red 6 on red 7 refused")
    g.waste = { card(6, 3) }
    assert(K.move(g, { kind = "waste" }, { kind = "tableau", col = 1 }), "black 6 on red 7")
    g.waste = { card(4, 0) }
    assert(not K.move(g, { kind = "waste" }, { kind = "tableau", col = 1 }), "rank gap refused")
    g.waste = { card(5, 3) }
    assert(not K.move(g, { kind = "waste" }, { kind = "tableau", col = 1 }), "same colour refused")
    -- Only a king goes to an empty column; moving a run turns over the hidden card.
    assert(not K.move(g, { kind = "tableau", col = 1, index = 2 }, { kind = "tableau", col = 2 }), "not a king")
    g.tableau[2] = { cards = { card(9, 1) }, down = 0 }
    assert(K.move(g, { kind = "tableau", col = 1, index = 2 }, { kind = "tableau", col = 2 }), "8-7-6 run onto red 9")
    assert(#g.tableau[2].cards == 4 and #g.tableau[1].cards == 1 and g.tableau[1].down == 0, "hidden card turned up")
    -- Face-down cards cannot be picked.
    g.tableau[3] = { cards = { card(13, 0), card(12, 1) }, down = 1 }
    assert(K.sourceCards(g, { kind = "tableau", col = 3, index = 1 }) == nil)
end

-- Foundations: ace first, then same suit upwards; only single cards.
do
    local g = emptyGame()
    g.waste = { card(2, 2) }
    assert(not K.moveToFoundation(g, { kind = "waste" }), "a 2 cannot start a foundation")
    g.waste = { card(2, 2), card(1, 2) }
    assert(K.moveToFoundation(g, { kind = "waste" }))
    assert(K.moveToFoundation(g, { kind = "waste" }))
    local f
    for i = 1, 4 do if #g.foundations[i] == 2 then f = i end end
    assert(f, "ace and two on one foundation")
    g.waste = { card(3, 1) }
    assert(not K.move(g, { kind = "waste" }, { kind = "foundation", index = f }), "wrong suit")
end

-- Undo restores the position; a won game is detected; auto-finish completes.
do
    local g = emptyGame()
    for s = 0, 3 do
        for r = 13, 1, -1 do
            local col = s + 1
            table.insert(g.tableau[col].cards, card(r, s))
        end
    end
    assert(K.canAutoFinish(g))
    local before = K.serialize(g)
    assert(K.autoFinishStep(g))
    assert(K.canUndo(g) and K.undo(g) and K.serialize(g) == before, "undo restores the position")
    local steps = 0
    while K.canAutoFinish(g) and steps < 100 do
        assert(K.autoFinishStep(g))
        steps = steps + 1
    end
    assert(K.isWon(g) and steps == 52, "auto-finish should win in 52 moves")
end

-- Save and restore round-trip; a corrupt save is refused.
do
    local g = K.new(rand, 3)
    K.draw(g)
    local s = K.serialize(g)
    local back = K.deserialize(s)
    assert(back and K.serialize(back) == s and back.drawCount == 3)
    assert(K.deserialize("1|0|1.2.3") == nil)
    local dup = s:gsub("^(%d+|%d+|)(%d+)%.(%d+)", "%1%2.%2")
    assert(K.deserialize(dup) == nil, "duplicate card refused")
end

print("solitaire logic ok")
