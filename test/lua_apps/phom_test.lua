-- Logic tests for apps/phom/logic.lua, game.lua, ai.lua and save.lua (run by ctest: LuaAppLogic_phom).
local T = dofile(APPS .. "/phom/logic.lua")
local G = dofile(APPS .. "/phom/game.lua")(T)
local AI = dofile(APPS .. "/phom/ai.lua")(T)
local Save = dofile(APPS .. "/phom/save.lua")(T)

math.randomseed(23)
local function rand(n) return math.random(1, n) end

-- rank: "A","2".."10","J","Q","K"; suit: 0 spades, 1 clubs, 2 diamonds, 3 hearts.
local RANKS = { A = 1, J = 11, Q = 12, K = 13 }
for r = 2, 10 do RANKS[tostring(r)] = r end
local SUITS = { s = 0, c = 1, d = 2, h = 3 }
local function card(text)
    local r, s = text:match("^(%w+)(%a)$")
    return (RANKS[r] - 1) * 4 + SUITS[s]
end
local function cards(text)
    local out = {}
    for t in text:gmatch("%S+") do out[#out + 1] = card(t) end
    return out
end
local function left(hand, eaten, byCount)
    return T.split(cards(hand), eaten and cards(eaten) or {}, byCount)
end

-- Cards: A low.
assert(card("As") == 0 and card("Kh") == 51 and card("Kd") // 4 + 1 == 13)

-- Melds: cards that the best split puts in one meld with nothing left.
function T.isMeld(list)
    local l, melds = T.split(list, {})
    return l == 0 and #melds == 1
end
assert(T.isMeld(cards("7s 7h 7d")))
assert(T.isMeld(cards("7s 7h 7d 7c")))
assert(T.isMeld(cards("5c 3c 4c")), "a run in any order")
assert(T.isMeld(cards("As 2s 3s")), "the ace is low")
assert(T.isMeld(cards("10d Jd Qd Kd")))
assert(not T.isMeld(cards("Qs Ks As")), "no wrap")
assert(not T.isMeld(cards("Ks As 2s")), "no wrap")
assert(not T.isMeld(cards("3s 4s 5c")), "one suit")
assert(not T.isMeld(cards("3s 4s 6s")), "a gap")
assert(not T.isMeld(cards("7s 7h")), "two cards")

-- Best split: fewest points left over.
do
    local l, melds, rest, cnt = left("3s 4s 5s 9c 9d 9h Kc Qd 2h")
    assert(l == 13 + 12 + 2 and #melds == 2 and cnt == 6 and #rest == 3)
    -- No wrap: Q K A of one suit is no meld.
    l, melds = left("Qs Ks As 2c 7d")
    assert(l == 12 + 13 + 1 + 2 + 7 and #melds == 0)
    -- Overlap: 5s in the set or in the run: 5s 5c 5d leaves 3s 4s (7), the
    -- run 3s 4s 5s leaves 5c 5d (10).
    l, melds = left("3s 4s 5s 5c 5d")
    assert(l == 7 and #melds == 1 and #melds[1] == 3 and melds[1][1] // 4 + 1 == 5)
    -- Four of a kind can lend a card to a run: 7s 7c 7d 7h 8h 9h -> 7s 7c 7d + 7h 8h 9h.
    l, melds = left("7s 7c 7d 7h 8h 9h")
    assert(l == 0 and #melds == 2)
    -- A long run split for a set: 4h 5h 6h 7h 8h + 8s 8c -> 4h-7h and 8h 8s 8c.
    l, melds = left("4h 5h 6h 7h 8h 8s 8c")
    assert(l == 0 and #melds == 2)
    -- Four kings (Qh Jh 2c left) or three and K-Q-J of hearts (2c left).
    l = left("Ks Kc Kd Kh Qh Jh 2c")
    assert(l == 2, "K K K and K-Q-J of hearts leave the 2")
    -- byCount: most cards in melds even if more points stay.
    l, melds, rest, cnt = left("As 2s 3s 4s Kc Kd Kh", nil, true)
    assert(cnt == 7 and l == 0)
end

-- Eaten cards: each in a meld, never two in one meld.
do
    assert(left("7s 7h 7d 2c", "7s"))
    assert(not left("7s 2c 9d", "7s"), "an eaten card without a meld")
    assert(not left("7s 7h 7d", "7s 7h"), "two eaten cards in one meld")
    assert(left("7s 7h 7d 7c 8c 9c", "7s 9c"), "7s 7h 7d and 7c 8c 9c")
    -- Eat legality.
    local hand = cards("7h 7d 3c 4c Kd 2s 9s Jd 5h")
    assert(T.canEat(hand, {}, card("7s")), "makes a set")
    assert(T.canEat(hand, {}, card("5c")) and T.canEat(hand, {}, card("2c")), "makes a run")
    assert(not T.canEat(hand, {}, card("8s")), "no meld")
    assert(not T.canEat(hand, {}, card("6c")), "a gap")
    -- After eating 7s with 7h 7d, the 7c cannot be eaten into the same set.
    local h2 = cards("7s 7h 7d 3c 4c Kd 2s 9s Jd 5h")
    assert(not T.canEat(h2, cards("7s"), card("7c")), "two eaten cards in one set")
    assert(T.canEat(h2, cards("7s"), card("5c")))
    -- Discard legality.
    assert(not T.canDiscard(h2, cards("7s"), card("7s")), "an eaten card stays")
    assert(not T.canDiscard(h2, cards("7s"), card("7h")), "breaks the eaten meld")
    assert(T.canDiscard(h2, cards("7s"), card("Kd")))
    assert(not T.canDiscard(h2, cards("7s"), card("8h")), "not in the hand")
end

-- Ù: 10 cards with at least 9 in melds.
assert(T.isU(cards("As 2s 3s 5c 5d 5h 9d 10d Jd Kh"), {}))
assert(T.isU(cards("As 2s 3s 4s 5c 5d 5h 9d 10d Jd"), {}), "ù tròn")
assert(not T.isU(cards("As 2s 3s 5c 5d 5h 9d 10d Qh Kh"), {}))
assert(not T.isU(cards("As 2s 3s 5c 5d 5h 9d 10d Jd"), {}), "9 cards")
assert(not T.isU(cards("As 2s 3s 5c 5d 5h 9d 10d Jd Kh"), cards("As 2s")), "two eaten cards in one run")

-- Arranging: melds first.
do
    local h = cards("Kc 3s 9d 4s 9h 5s 9c 2h")
    local sizes = T.arrange(h, {})
    assert(#sizes == 2 and sizes[1] + sizes[2] == 6)
    assert(T.isMeld({ h[1], h[2], h[3] }) and T.isMeld({ h[4], h[5], h[6] }))
    assert(h[7] == card("2h") and h[8] == card("Kc"))
end

-- Dealing.
local function count(g)
    local seen, n = {}, 0
    local function add(list)
        for _, c in ipairs(list) do
            assert(not seen[c], "a card twice")
            seen[c], n = true, n + 1
        end
    end
    add(g.stock)
    for s = 1, 4 do
        add(g.hands[s])
        add(g.disc[s])
        for _, c in ipairs(g.eaten[s]) do assert(T.has(g.hands[s], c), "an eaten card is in the hand") end
    end
    return n
end

do
    for first = 1, 4 do
        local g = G.new(rand, first)
        assert(count(g) == 52)
        if not g.over then
            assert(g.turn == first and g.phase == "discard" and #g.stock == 15)
            for s = 1, 4 do assert(#g.hands[s] == (s == first and 10 or 9)) end
        end
    end
end

-- A fixed hand: hands[s] given, the rest in the stock.
local function fixed(handsText, first, stockText)
    local g = { hands = {}, eaten = { {}, {}, {}, {} }, disc = { {}, {}, {}, {} }, nd = { 0, 0, 0, 0 }, stock = {},
                mlen = {}, first = first, turn = first, phase = "discard", topBy = 0, over = false }
    local used = {}
    for s = 1, 4 do
        g.hands[s] = cards(handsText[s])
        for _, c in ipairs(g.hands[s]) do used[c] = true end
        g.mlen[s] = T.arrange(g.hands[s], {})
    end
    local st = stockText and cards(stockText) or {}
    for _, c in ipairs(st) do used[c] = true end
    for c = 0, 51 do
        if not used[c] then g.stock[#g.stock + 1] = c end
    end
    for i = #st, 1, -1 do g.stock[#g.stock + 1] = st[i] end -- the first given is drawn first
    return g
end

-- Moves, eating, refusals.
do
    local g = fixed({ "As 2s 3s 5c 5d 9h 10h Jd Qd Kc", "5h 5s 4d 6d 8c 9c Qc Kh Js", "2c 3c 4c 6s 7s 8s 9s 10s Jc",
                      "Ah Ac 2d 3d 7h 8h 2h 3h Kd" }, 1, "7d")
    G.prepare(g)
    assert(g.okd and g.okd[card("Kc")] and not g.eat1, "what the table screen may offer")
    assert(G.take(g, 1, false) == "Throw a card first")
    assert(G.discard(g, 2, card("5h")) == "Not your turn")
    assert(G.discard(g, 1, card("5c")) == nil)
    assert(g.turn == 2 and g.phase == "take" and g.top == card("5c") and g.topBy == 1)
    assert(G.discard(g, 2, card("5h")) == "Eat or draw first")
    assert(not g.eat1 and not g.okd, "not your turn")
    assert(AI.take(g, 2, 2), "5c makes 5h 5s 5c")
    assert(G.take(g, 2, true) == nil)
    assert(#g.disc[1] == 0 and T.has(g.eaten[2], card("5c")) and #g.hands[2] == 10 and g.top == nil)
    assert(G.discard(g, 2, card("5c")) == "An eaten card stays in its meld")
    assert(G.discard(g, 2, card("5h")) == "That breaks the meld of an eaten card")
    local c = AI.discard(g, 2, 2, rand)
    assert(c and G.discardRefusal(g, 2, c) == nil)
    assert(G.discard(g, 2, c) == nil and g.turn == 3)
    assert(not G.canEat(g, 2))
    assert(count(g) == 52)
end

-- Ù on a draw ends the hand at once, 5 points to the winner.
do
    local g = fixed({ "As 2s 3s 5c 5d 9d 10d Jd Kh Qc", "4h 5s 4d 6d 8c 9c Qh Kc Js", "2c 3c 4c 6s 7s 8s 9s 10s Jc",
                      "Ah Ac 2d 3d 7h 8h 2h 3h Kd" }, 4, "5h")
    g.hands[1] = cards("As 2s 3s 5c 5d 9d 10d Jd Kh")
    g.hands[4][10] = card("Qc")
    g.turn = 4
    assert(G.discard(g, 4, card("Qc")) == nil and g.turn == 1)
    assert(G.take(g, 1, false) == nil)
    assert(g.over and g.u == 1 and g.order[1] == 1 and g.pts[1] == G.U_POINTS)
    assert(g.pts[2] + g.pts[3] + g.pts[4] == 0)
end

-- Scoring and móm at the end of a hand.
do
    local g = fixed({ "As 2s 3s 5c 9h 10h Jd Qd Kc", "4h 5s 4d 6d 8c 9c Qh Kh Js", "2c 3c 4c 6s 7s 8s 9s 10s Jc",
                      "Ah Ac 2d 3d 7h 8h 2h 3h Kd" }, 1)
    g.nd = { 4, 4, 4, 3 }
    g.turn, g.phase = 4, "discard"
    local d = card("5h")
    g.hands[4][#g.hands[4] + 1] = d
    for i = #g.stock, 1, -1 do
        if g.stock[i] == d then table.remove(g.stock, i) end
    end
    assert(G.discard(g, 4, d) == nil and g.over and not g.u)
    -- You: As 2s 3s, 5+9+10+11+12+13 = 60 left. East: no meld (móm). North:
    -- 2c 3c 4c and 6s-10s, Jc (11) left. West: Ah 2h 3h, Ac 2d 3d 7h 8h Kd (34) left.
    assert(g.left[1] == 60 and g.mom[2] and g.left[3] == 11 and g.left[4] == 34)
    assert(g.order[1] == 3 and g.order[2] == 4 and g.order[3] == 1 and g.order[4] == 2)
    assert(g.pts[3] == 3 and g.pts[4] == 2 and g.pts[1] == 1 and g.pts[2] == 0)
end

-- Ties: who threw first in the hand wins.
do
    local g = fixed({ "As 2s 3s 5c", "Ah 2h 3h 5d", "Ac 2c 3c 4h", "Ad 2d 3d 4s" }, 3)
    g.nd = { 4, 3, 4, 4 }
    g.turn, g.phase = 2, "discard"
    g.hands[2][#g.hands[2] + 1] = card("Kd")
    assert(G.discard(g, 2, card("Kd")) == nil and g.over)
    -- Left: You 5, East 5, North 4, West 4. North threw first (seat 3), then West.
    assert(g.order[1] == 3 and g.order[2] == 4 and g.order[3] == 1 and g.order[4] == 2)
end

-- Saving and restoring.
do
    local g = G.new(rand, 2)
    local guard = 0
    while not g.over and guard < 7 do
        guard = guard + 1
        local seat = g.turn
        if g.phase == "take" then G.take(g, seat, AI.take(g, seat, 2)) end
        if not g.over then G.discard(g, seat, AI.discard(g, seat, 2, rand)) end
    end
    if not g.over then
        local s = Save.serialize(g)
        local back = Save.deserialize(s)
        assert(back and Save.serialize(back) == s and back.turn == g.turn and back.top == g.top)
        assert(Save.deserialize(s .. "|") == nil and Save.deserialize("garbage") == nil)
        local f = {}
        for part in (s .. "|"):gmatch("([^|]*)|") do f[#f + 1] = part end
        f[7] = f[7] .. f[8]:sub(1, 1)
        assert(Save.deserialize(table.concat(f, "|")) == nil, "a card twice")
        -- In the middle of a turn (after taking) too.
        local seat = g.turn
        G.take(g, seat, false)
        if not g.over then
            s = Save.serialize(g)
            back = Save.deserialize(s)
            assert(back and Save.serialize(back) == s and back.phase == "discard")
        end
    end
end

-- Whole hands, computer against computer: legal moves, every card kept,
-- the hand ends after four rounds (or an ù).
local function playHand(levels, first)
    local g = G.new(rand, first)
    local guard = 0
    while not g.over do
        local seat = g.turn
        if g.phase == "take" then
            local eat = AI.take(g, seat, levels[seat])
            assert(not eat or G.canEat(g, seat))
            assert(G.take(g, seat, eat) == nil)
        end
        if not g.over then
            local c = AI.discard(g, seat, levels[seat], rand)
            assert(c and G.discardRefusal(g, seat, c) == nil, "the computer chose an illegal card")
            assert(G.discard(g, seat, c) == nil)
        end
        assert(count(g) == 52, "cards conserved")
        guard = guard + 1
        assert(guard < 20, "the hand does not end")
    end
    if not g.u then
        for s = 1, 4 do assert(g.nd[s] == 4 and #g.hands[s] == 9) end
    end
    local seen = {}
    for _, s in ipairs(g.order) do assert(not seen[s]) seen[s] = true end
    assert(#g.order == 4)
    return g
end

do
    local games, hardPts, normalPts, us, moms, eats, first = 1200, 0, 0, 0, 0, 0, nil
    local t0 = os.clock()
    for n = 1, games do
        local seat = (n - 1) % 4 + 1
        local levels = { 1, 1, 1, 1 }
        levels[seat] = 2
        local g = playHand(levels, first)
        if g.u then us = us + 1 end
        for s = 1, 4 do
            if g.mom[s] then moms = moms + 1 end
            eats = eats + #g.eaten[s]
        end
        first = g.order[1]
        hardPts = hardPts + g.pts[seat]
        for s = 1, 4 do
            if s ~= seat then normalPts = normalPts + g.pts[s] / 3 end
        end
    end
    print(string.format("%d hands in %.2f s, %d ù, %.2f móm and %.2f eats a hand; points a hand: Hard %.2f, Normal %.2f",
                        games, os.clock() - t0, us, moms / games, eats / games, hardPts / games, normalPts / games))
    assert(hardPts > normalPts, "Hard scores more than Normal")
end

print("phom logic ok")
