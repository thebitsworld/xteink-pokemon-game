-- Logic tests for apps/tienlen/logic.lua, game.lua, ai.lua and save.lua (run by ctest: LuaAppLogic_tienlen).
local T = dofile(APPS .. "/tienlen/logic.lua")
-- game.lua's dealing and moves, reached through T here for brevity.
for k, v in pairs(dofile(APPS .. "/tienlen/game.lua")(T)) do T[k] = v end
local AI = dofile(APPS .. "/tienlen/ai.lua")(T)
local Save = dofile(APPS .. "/tienlen/save.lua")(T)

math.randomseed(17)
local function rand(n) return math.random(1, n) end

-- rank: "3".."10","J","Q","K","A","2"; suit: 0 spades, 1 clubs, 2 diamonds, 3 hearts.
local RANKS = { ["3"] = 1, ["4"] = 2, ["5"] = 3, ["6"] = 4, ["7"] = 5, ["8"] = 6, ["9"] = 7, ["10"] = 8, J = 9, Q = 10,
                K = 11, A = 12, ["2"] = 13 }
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
local function kind(text)
    return T.classify(cards(text))
end
local function beats(a, b)
    local k, l, t = kind(a)
    local ok, ol, ot = kind(b)
    assert(k and ok, a .. " / " .. b)
    return T.beats(k, l, t, ok, ol, ot)
end

-- Cards: ranks 3..A then 2, suits spades < clubs < diamonds < hearts.
assert(card("3s") == T.THREE_SPADES and card("2h") == 51)
assert(card("2s") > card("Ah") and card("3h") > card("3d") and card("3d") > card("3c") and card("3c") > card("3s"))
assert(T.rankName(card("10d")) == "10" and T.rank(card("2c")) == T.TWO)

-- Classification.
assert(kind("7h") == T.SINGLE)
assert(kind("7h 7s") == T.PAIR)
assert(kind("7h 7s 7d") == T.TRIPLE)
assert(kind("7h 7s 7d 7c") == T.QUAD)
assert(kind("7h 8s") == nil, "two ranks are not a pair")
do
    local k, l, t = kind("5d 3s 4h")
    assert(k == T.STRAIGHT and l == 3 and t == card("5d"), "a straight in any order")
    k, l = kind("10s Jc Qd Kh As")
    assert(k == T.STRAIGHT and l == 5)
    k, l = kind("3s 4s 5s 6s 7s 8s 9s 10s Js Qs Ks As")
    assert(k == T.STRAIGHT and l == 12)
end
assert(kind("Ks As 2s") == nil, "no 2 in a straight")
assert(kind("As 2s 3s") == nil, "no wrapping")
assert(kind("3s 4s 6s") == nil, "a gap")
assert(kind("3s 4s") == nil, "two cards are no straight")
do
    local k, l, t = kind("3s 3h 4c 4d 5s 5h")
    assert(k == T.PAIRS and l == 3 and t == card("5h"))
    k, l = kind("6s 6h 7c 7d 8s 8h 9c 9d")
    assert(k == T.PAIRS and l == 4)
end
assert(kind("3s 3h 4c 4d") == nil, "two pairs are not a run")
assert(kind("Ks Kh As Ad 2c 2d") == nil, "no 2s in a run of pairs")
assert(kind("3s 3h 4c 4d 6s 6h") == nil)
assert(kind("3s 3h 3d 4c 4d 5s") == nil)

-- Beating: same kind and size, a higher top card, suits between equal ranks.
assert(beats("8s", "7h"))
assert(not beats("7h", "8s"))
assert(beats("7h", "7d") and not beats("7d", "7h"))
assert(beats("9s 9h", "9c 9d"), "the heart decides")
assert(not beats("9s 9d", "9c 9h"))
assert(beats("5s 6s 7h", "5c 6d 7d"))
assert(not beats("5s 6s 7s 8s", "5c 6d 7d"), "a straight of another length")
assert(not beats("Js Jh", "7h"), "another kind")
assert(beats("2s", "Ah") and beats("2h", "2d"))
assert(beats("4s 4h 5c 5d 6s 6h", "3s 3h 4c 4d 5s 5d"))
-- Chopping.
assert(beats("3s 3h 4c 4d 5s 5h", "2s"), "three pairs in a row chop a 2")
assert(not beats("3s 3h 4c 4d 5s 5h", "2s 2h"), "but not a pair of 2s")
assert(beats("7s 7h 7d 7c", "2h"), "four of a kind chops a 2")
assert(beats("7s 7h 7d 7c", "2s 2h"), "and a pair of 2s")
assert(beats("7s 7h 7d 7c", "Js Jh Qc Qd Ks Kh"), "and three pairs in a row")
assert(not beats("7s 7h 7d 7c", "Ah"), "but not a plain single")
assert(not beats("7s 7h 7d 7c", "2s 2h 2d"), "nor three 2s")
assert(beats("3s 3h 4c 4d 5s 5h 6c 6d", "2s 2h"), "four pairs in a row chop a pair of 2s")
assert(beats("3s 3h 4c 4d 5s 5h 6c 6d", "Ks Kh Kd Kc"), "and four of a kind")
assert(beats("3s 3h 4c 4d 5s 5h 6c 6d", "Js Jh Qc Qd Ks Kh"), "and three pairs in a row")
assert(beats("3s 3h 4c 4d 5s 5h 6c 6d", "2h"))
assert(beats("4s 4h 5c 5d 6s 6h 7c 7d", "3s 3h 4c 4d 5s 5h 6c 6d"))
assert(not beats("Ks Kh Kd Kc", "3s 3h 4c 4d 5s 5h 6c 6d"), "four of a kind does not chop four pairs")
assert(beats("Ks Kh Kd Kc", "7s 7h 7d 7c"), "a higher four of a kind")
assert(not beats("3s 3h 4c 4d 5s 5h", "Ah"), "a run of pairs only chops 2s")

-- Instant wins.
assert(T.instant(cards("2s 2c 2d 2h 3s 4s 5d 7d 8h 9h 10s Jc Qc")) == "four 2s")
assert(T.instant(cards("3s 4s 5d 6h 7d 8h 9h 10s Jc Qc Kd Ah 2c")) == "a dragon straight")
assert(T.instant(cards("3s 3c 5d 5h 7d 7h 9h 9s Jc Jd Kd Kh 2c")) == "six pairs")
assert(T.instant(cards("3s 3c 5d 5h 7d 7h 9h 9s Jc Jd Kd Ah 2c")) == nil)

-- The deal: 13 each, every card once; the 3 of spades leads the first hand.
local function checkCards(g, extra)
    local seen, n = {}, 0
    for s = 1, 4 do
        for _, c in ipairs(g.hands[s]) do
            assert(not seen[c], "a card twice")
            seen[c] = true
            n = n + 1
        end
    end
    for _, c in ipairs(extra or {}) do
        assert(not seen[c], "a played card still in a hand")
        seen[c] = true
        n = n + 1
    end
    return n
end
for _ = 1, 30 do
    local g = T.new(rand)
    assert(checkCards(g) == 52)
    for s = 1, 4 do assert(#g.hands[s] == 13) end
    if not g.instant then
        assert(g.hands[g.turn][1] == T.THREE_SPADES and g.need3)
        local hand = g.hands[g.turn]
        assert(T.refusal(g, g.turn, { hand[2] }) == "Play the 3 of spades in it")
        assert(T.passRefusal(g, g.turn) == "You lead: play something")
        assert(T.play(g, g.turn, { T.THREE_SPADES }) == nil and not g.need3)
    end
    local g2 = T.new(rand, 3)
    assert(g2.instant or (g2.turn == 3 and not g2.need3), "the last winner leads")
end

-- A hand-made deal.
local function fixed(hands, leader)
    local g = { hands = {}, passed = { false, false, false, false }, out = {}, order = {}, curBy = 0, over = false }
    for s = 1, 4 do
        g.hands[s] = cards(hands[s])
        table.sort(g.hands[s])
    end
    T.start(g, leader)
    return g
end

-- Turns, passing locks you out, and the round restarts once all others pass.
do
    local g = fixed({ "3s 5s 9h Kd", "4c 6d 10s Ac", "4d 7c Js 2h", "5c 8d Qs 2s" }, 1)
    assert(g.turn == 1)
    assert(T.refusal(g, 2, cards("4c")) == "Not your turn")
    assert(T.refusal(g, 1, cards("3s 5s")) == "Not a valid combination")
    assert(T.refusal(g, 1, cards("4c")) == "Not your cards")
    assert(T.play(g, 1, cards("3s")) == nil and g.turn == 2)
    assert(T.refusal(g, 2, cards("4c 6d")) == "Not a valid combination")
    assert(T.play(g, 2, cards("4c")) == nil and g.turn == 3)
    assert(T.pass(g, 3) == nil and g.passed[3] and g.turn == 4)
    assert(T.play(g, 4, cards("5c")) == nil and g.turn == 1)
    assert(T.refusal(g, 1, cards("5s")) == "That does not beat the table")
    assert(T.play(g, 1, cards("9h")) == nil)
    assert(g.turn == 2, "North passed: skipped until the round restarts")
    assert(T.pass(g, 2) == nil and g.turn == 4)
    assert(T.pass(g, 4) == nil)
    assert(g.turn == 1 and g.cur == nil and g.newRound and not g.passed[3], "you lead the new round")
    assert(T.play(g, 1, cards("5s")) == nil and g.turn == 2)
end

-- A player who goes out on the last play: the next player still in leads.
do
    local g = fixed({ "9h", "4c 6d", "4d 7c", "5c 8d" }, 1)
    assert(T.play(g, 1, cards("9h")) == nil and g.out[1] and g.order[1] == 1)
    assert(g.turn == 2)
    T.pass(g, 2)
    T.pass(g, 3)
    T.pass(g, 4)
    assert(g.turn == 2 and g.cur == nil, "East inherits the lead")
end

-- The hand ends with three out; points 3/2/1/0.
do
    local g = fixed({ "9h", "10c", "4d 7c", "5c 8d" }, 1)
    T.play(g, 1, cards("9h"))
    T.play(g, 2, cards("10c"))
    assert(g.turn == 3 and not g.over)
    T.pass(g, 3)
    assert(g.turn == 4)
    T.pass(g, 4)
    assert(g.turn == 3, "the new round goes past the two players out")
    T.play(g, 3, cards("4d"))
    T.play(g, 4, cards("5c"))
    T.play(g, 3, cards("7c"))
    assert(g.over and g.order[3] == 3 and g.order[4] == 4)
    local p = T.handPoints(g)
    assert(p[1] == 3 and p[2] == 2 and p[3] == 1 and p[4] == 0)
end

-- Instant win at the deal.
do
    local g = fixed({ "2s 2c 2d 2h 3s 4s 5d 7d 8h 9h 10s Jc Qc", "4c", "4d", "5c" })
    assert(g.over and g.instant == 1 and g.instantWhy == "four 2s")
    local p = T.handPoints(g)
    assert(p[1] == 3 and p[2] == 0)
end

-- The computer: legal plays only.
do
    local plays = AI.beatingPlays(cards("3s 3h 4c 4d 5s 5h 7c 7d 7s 7h 9s 2d"), T.SINGLE, 1, card("2s"))
    local chops, two = 0, false
    for _, p in ipairs(plays) do
        local k, l, t = T.classify(p)
        assert(k and T.beats(k, l, t, T.SINGLE, 1, card("2s")))
        if k ~= T.SINGLE then chops = chops + 1 end
        if k == T.SINGLE and p[1] == card("2d") then two = true end
    end
    assert(two and chops >= 2, "the 2 of diamonds, the four 7s and the run of pairs")
    -- Straights: the lowest cards, the top beating by suit.
    plays = AI.beatingPlays(cards("5s 5h 6c 7d 7h 8s"), T.STRAIGHT, 3, card("7c"))
    for _, p in ipairs(plays) do
        local k, l, t = T.classify(p)
        assert(k == T.STRAIGHT and l == 3 and t > card("7c"))
    end
    assert(#plays == 2, "5-6-7d and 6-7-8")
    plays = AI.beatingPlays(cards("9s 9h 9d"), T.PAIR, 2, card("9c"))
    assert(#plays == 1 and plays[1][1] == card("9s") and plays[1][2] == card("9d"))
end
do
    -- The 3 of spades first; chop a 2 with a bomb (Hard).
    local g = fixed({ "3s 3h 4c 4d 5s 5h Kd", "2h 9c", "Jd", "Qd" })
    assert(g.turn == 1)
    local c = AI.choose(g, 1, 2, rand)
    local has = false
    for _, x in ipairs(c) do if x == T.THREE_SPADES then has = true end end
    assert(has and T.refusal(g, 1, c) == nil)
    g = fixed({ "3s 3h 4c 4d 5s 5h Kd", "2h 9c", "Jd", "Qd" }, 2)
    T.play(g, 2, cards("2h"))
    T.pass(g, 3)
    T.pass(g, 4)
    c = AI.choose(g, 1, 2, rand)
    assert(c and T.classify(c) == T.PAIRS, "chops the 2")
    -- Leading keeps groups: the lowest group, a straight not broken up.
    g = fixed({ "4s 5d 6h 9c 9d Kh 2s", "3c", "Jd", "Qd" }, 1)
    c = AI.choose(g, 1, 2, rand)
    assert(#c == 3 and T.classify(c) == T.STRAIGHT)
    -- A hand that is one combination goes out at once.
    g = fixed({ "9c 9d", "3c", "Jd", "Qd" }, 1)
    assert(#AI.choose(g, 1, 1, rand) == 2)
end

-- Saving and restoring.
do
    local g = T.new(rand, 2)
    if not g.over then
        for _ = 1, 9 do
            local c = AI.choose(g, g.turn, 2, rand)
            if c then T.play(g, g.turn, c) else T.pass(g, g.turn) end
        end
        local s = Save.serialize(g)
        local back = Save.deserialize(s)
        assert(back and Save.serialize(back) == s and back.turn == g.turn and back.ck == g.ck)
        assert(Save.deserialize(s .. "|") == nil and Save.deserialize("garbage") == nil)
        local f = {}
        for part in (s .. "|"):gmatch("([^|]*)|") do f[#f + 1] = part end
        local from = #f[7] > 0 and 7 or 8
        f[9] = f[9] .. f[from]:sub(1, 1) -- a card twice
        assert(Save.deserialize(table.concat(f, "|")) == nil, "a card twice")
    end
end

-- Whole hands, computer against computer: they finish legally, every card
-- played once, a full finishing order.
local function playHand(levels, leader)
    local g = T.new(rand, leader)
    local played = {}
    local guard = 0
    while not g.over do
        local seat = g.turn
        local c = AI.choose(g, seat, levels[seat], rand)
        if c then
            assert(T.refusal(g, seat, c) == nil, "the computer chose an illegal play")
            for _, x in ipairs(c) do played[#played + 1] = x end
            assert(T.play(g, seat, c) == nil)
        else
            assert(g.cur, "passed on its own lead")
            assert(T.pass(g, seat) == nil)
        end
        assert(checkCards(g, played) == 52, "cards conserved")
        guard = guard + 1
        assert(guard < 500, "the hand does not end")
    end
    if not g.instant then
        assert(#g.order == 4)
        local seen = {}
        for _, s in ipairs(g.order) do assert(not seen[s]) seen[s] = true end
    end
    return g
end

do
    local games, hardPts, normalPts, instants, leader = 300, 0, 0, 0, nil
    local t0 = os.clock()
    for n = 1, games do
        local seat = (n - 1) % 4 + 1
        local levels = { 1, 1, 1, 1 }
        levels[seat] = 2
        local g = playHand(levels, leader)
        if g.instant then instants = instants + 1 end
        leader = g.order[1]
        local p = T.handPoints(g)
        hardPts = hardPts + p[seat]
        for s = 1, 4 do
            if s ~= seat then normalPts = normalPts + p[s] / 3 end
        end
    end
    print(string.format("%d hands in %.2f s, %d instant wins; points a hand: Hard %.2f, Normal %.2f", games,
                        os.clock() - t0, instants, hardPts / games, normalPts / games))
    assert(hardPts > normalPts, "Hard scores more than Normal")
end

print("tienlen logic ok")
