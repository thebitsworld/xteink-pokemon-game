-- Logic tests for apps/samloc/logic.lua, game.lua, ai.lua and save.lua (run by ctest: LuaAppLogic_samloc).
local T = dofile(APPS .. "/samloc/logic.lua")
-- game.lua's dealing and moves, reached through T here for brevity.
for k, v in pairs(dofile(APPS .. "/samloc/game.lua")(T)) do T[k] = v end
local AI = dofile(APPS .. "/samloc/ai.lua")(T)
local Save = dofile(APPS .. "/samloc/save.lua")(T)

math.randomseed(23)
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

-- Classification.
assert(kind("7h") == T.SINGLE and kind("7h 7s") == T.PAIR and kind("7h 7s 7d") == T.TRIPLE)
assert(kind("7h 7s 7d 7c") == T.QUAD)
assert(kind("7h 8s") == nil, "two ranks are not a pair")
do
    local k, l, t = kind("5d 3s 4h")
    assert(k == T.STRAIGHT and l == 3 and t == 3, "a straight in any order, top rank 5")
    k, l, t = kind("10s Jc Qd Kh As")
    assert(k == T.STRAIGHT and l == 5 and t == T.ACE)
    k, l, t = kind("As 2d 3c")
    assert(k == T.STRAIGHT and l == 3 and t == 1, "A-2-3 is the lowest straight")
    k, l, t = kind("4h As 3c 2d 5s")
    assert(k == T.STRAIGHT and l == 5 and t == 3, "A-2-3-4-5")
end
assert(kind("Ks As 2s") == nil, "K-A-2 is not a straight")
assert(kind("2s 3s 4s") == nil, "no 2 in another straight")
assert(kind("Qs Ks As 2s 3s") == nil, "no wrapping")
assert(kind("As 2s 4s") == nil, "a gap")
assert(kind("3s 4s") == nil and kind("As 2s") == nil, "two cards are no straight")
assert(kind("3s 3h 4c 4d 5s 5h") == nil, "no runs of pairs in Sam")
assert(kind("7s 7h 7d 7c 7s") == nil)
do
    local o = T.inOrder(cards("3c 2d As 4h"))
    assert(T.rank(o[1]) == T.ACE and T.rank(o[2]) == T.TWO and T.rank(o[4]) == 2, "low straight shown A 2 3 4")
end

-- Beating: suits do not count.
assert(beats("8s", "7h"))
assert(not beats("7h", "7d") and not beats("7d", "7h"), "equal ranks do not beat")
assert(not beats("9s 9h", "9c 9d"))
assert(beats("10s 10c", "9c 9h"))
assert(beats("5s 6s 7h", "4c 5d 6d") and not beats("5s 6s 7h", "5c 6d 7d"))
assert(beats("3s 4s 5h", "As 2c 3d"), "3-4-5 beats A-2-3")
assert(not beats("As 2c 3d", "3s 4s 5h"))
assert(beats("Qs Kh Ad", "Js Qh Kd"))
assert(not beats("5s 6s 7s 8s", "5c 6d 7d"), "a straight of another length")
assert(not beats("Js Jh", "7h"), "another kind")
assert(beats("2s", "Ah") and not beats("2h", "2d"))
-- Chặt heo.
assert(beats("7s 7h 7d 7c", "2h"), "four of a kind chops a 2")
assert(not beats("7s 7h 7d 7c", "Ah"), "but not another single")
assert(not beats("7s 7h 7d 7c", "2s 2h"), "nor a pair of 2s")
assert(beats("Ks Kh Kd Kc", "7s 7h 7d 7c") and not beats("7s 7h 7d 7c", "Ks Kh Kd Kc"))

-- Instant wins.
assert(T.instant(cards("2s 2c 2d 2h 3s 4s 5d 7d 8h 9h")) == "four 2s")
assert(T.instant(cards("3s 4d 5d 6h 7d 8h 9h 10s Jc Qc")) == "a 10-card straight")
assert(T.instant(cards("As 2d 3d 4h 5d 6h 7h 8s 9c 10c")) == "a 10-card straight", "a low one too")
assert(T.instant(cards("3s 3c 3d 5h 5d 5c 9h 9s 9c Jd")) == "three triples")
assert(T.instant(cards("3s 3c 5d 5h 7d 7h 9h 9s Jc Jd")) == "five pairs")
assert(T.instant(cards("3s 4c 6s 7c 9s 10c Qs Ks Ac 2s")) == "ten cards of one colour")
assert(T.instant(cards("3s 3c 5d 5h 7d 7h 9h 9s Jc Kd")) == nil)

-- The deal: 10 each, every card once; then everyone is asked about Sam.
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
    assert(checkCards(g) == 40)
    for s = 1, 4 do assert(#g.hands[s] == 10) end
    if not g.instant then
        assert(g.ask == 0 and g.turn == g.lead)
        assert(T.refusal(g, g.turn, { g.hands[g.turn][1] }) == "Declare Sam or not first")
        local lead = g.lead
        for k = 0, 3 do
            local s = (lead + k - 1) % 4 + 1
            assert(g.turn == s and T.declare(g, (s % 4) + 1, false) == "Not your turn")
            assert(T.declare(g, s, false) == nil)
        end
        assert(not g.ask and not g.sam and g.turn == lead, "nobody declares: the leader leads")
    end
    local g2 = T.new(rand, 3)
    assert(g2.instant or (g2.lead == 3 and g2.turn == 3), "the last winner leads")
end

-- A hand-made deal (asked about Sam, nobody declares).
local function fixed(hands, leader, sam)
    local g = { hands = {}, passed = { false, false, false, false }, played = { false, false, false, false },
                curBy = 0, over = false }
    for s = 1, 4 do
        g.hands[s] = cards(hands[s])
        table.sort(g.hands[s])
    end
    T.start(g, leader)
    while g.ask do T.declare(g, g.turn, g.turn == sam) end
    return g
end

-- Turns, passing locks you out, and the round restarts once all others pass.
do
    local g = fixed({ "3s 5s 9h Kd", "4c 6d 10s Ac", "4d 7c Js 2h", "5c 8d Qs 2s" }, 1)
    assert(g.turn == 1)
    assert(T.refusal(g, 2, cards("4c")) == "Not your turn")
    assert(T.refusal(g, 1, cards("3s 5s")) == "Not a valid combination")
    assert(T.refusal(g, 1, cards("4c")) == "Not your cards")
    assert(T.passRefusal(g, 1) == "You lead: play something")
    assert(T.play(g, 1, cards("3s")) == nil and g.turn == 2)
    assert(T.play(g, 2, cards("4c")) == nil and g.turn == 3)
    assert(T.refusal(g, 3, cards("4d")) == "That does not beat the table", "an equal rank does not beat")
    assert(T.pass(g, 3) == nil and g.passed[3] and g.turn == 4)
    assert(T.play(g, 4, cards("5c")) == nil and g.turn == 1)
    assert(T.play(g, 1, cards("9h")) == nil)
    assert(g.turn == 2, "North passed: skipped until the round restarts")
    assert(T.pass(g, 2) == nil and g.turn == 4)
    assert(T.pass(g, 4) == nil)
    assert(g.turn == 1 and g.cur == nil and g.newRound and not g.passed[3], "you lead the new round")
end

-- Báo 1: before a player on one card, a single must be the highest.
do
    local g = fixed({ "3s 5s 9h Kd", "4c", "4d 7c Js 2h", "5c 8d Qs 2s" }, 1)
    assert(T.mustTop(g, 1) and not T.mustTop(g, 3))
    assert(T.refusal(g, 1, cards("3s")) == "Next player is on one: play your highest")
    assert(T.refusal(g, 1, cards("Kd")) == nil)
    local c = AI.choose(g, 1, 1, rand)
    assert(#c ~= 1 or c[1] == card("Kd"), "the computer obeys it")
    T.play(g, 1, cards("Kd"))
    assert(T.pass(g, 2) == nil and T.play(g, 3, cards("2h")) == nil)
    -- East passed: the next to answer North is West, not East.
    assert(not T.mustTop(g, 3))
end

-- The first player out wins; losers pay cards left, +5 a 2, double when cóng.
do
    local g = fixed({ "9h", "10c Jd", "4d 7c 2s", "5c 8d Qs 2c 2d 3s 4s 6c 9c 10s" }, 1)
    g.played[2], g.played[3] = true, true
    assert(T.play(g, 1, cards("9h")) == nil)
    assert(g.over and g.winner == 1)
    local p = T.handPoints(g)
    assert(p[2] == -2 and p[3] == -8 and p[4] == -40, "2, 3 + 5 and (10 + 10) x 2")
    assert(p[1] == 50)
end

-- Sam: the declarer leads; unbeaten it collects 20 from each player.
do
    local g = fixed({ "4s 5s 6s", "2h 3c", "Ks Kh", "9c 10d" }, 3, 1)
    assert(g.sam == 1 and g.turn == 1 and not g.ask)
    T.play(g, 1, cards("4s 5s 6s"))
    assert(g.over and g.winner == 1)
    local p = T.handPoints(g)
    assert(p[1] == 60 and p[2] == -20 and p[3] == -20 and p[4] == -20)
end
do
    local g = fixed({ "4s 5s 6s 9h", "2h 3c", "5c 6d 7h Kh", "9c 10d" }, 1, 1)
    T.play(g, 1, cards("4s 5s 6s"))
    T.pass(g, 2)
    assert(T.play(g, 3, cards("5c 6d 7h")) == nil)
    assert(g.over and g.samFail and g.winner == 3, "North stops the Sam")
    local p = T.handPoints(g)
    assert(p[1] == -60 and p[2] == 20 and p[3] == 20 and p[4] == 20)
    -- The computer always tries to stop a Sam.
    g = fixed({ "4s 5s 6s 9h", "2h 3c", "5c 6d 7h Kh", "9c 10d" }, 1, 1)
    T.play(g, 1, cards("9h"))
    local c = AI.choose(g, 2, 1, rand)
    assert(c and c[1] == card("2h"), "East spends its 2 on the Sam")
end

-- Instant win at the deal: 20 from each.
do
    local g = fixed({ "4c", "2s 2c 2d 2h 3s 4s 5d 7d 8h 9h", "4d", "5c" }, 1)
    assert(g.over and g.winner == 2 and g.instant == "four 2s" and not g.ask)
    local p = T.handPoints(g)
    assert(p[2] == 60 and p[1] == -20)
end

-- The computer: legal plays only.
do
    local plays = AI.beatingPlays(cards("3s 3h 7c 7d 7s 7h 9s 2d"), T.SINGLE, 1, T.TWO)
    assert(#plays == 1 and #plays[1] == 4, "only the four 7s beat a 2")
    plays = AI.beatingPlays(cards("5s 5h 6c 7d 7h 8s"), T.STRAIGHT, 3, 5)
    assert(#plays == 1 and T.classify(plays[1]) == T.STRAIGHT, "6-7-8 only; 5-6-7 is equal")
    plays = AI.beatingPlays(cards("9s 9h 9d Js Jd"), T.PAIR, 2, 6)
    assert(#plays == 2)
    plays = AI.beatingPlays(cards("3s 4h 5d"), T.STRAIGHT, 3, 1)
    assert(#plays == 1, "3-4-5 over A-2-3")
    -- A hand that is one combination goes out at once.
    local g = fixed({ "As 2c 3d", "3c", "Jd", "Qd" }, 1)
    assert(#AI.choose(g, 1, 1, rand) == 3)
    -- Declaring Sam: a hand nobody can beat.
    g = fixed({ "Qs Kh Ad 2s 2h 7c 7d 7s 7h", "3c", "Jd", "Qd" }, 1)
    assert(AI.declare(g, 1, 1))
    g = fixed({ "4s 5s 6s 2s", "3c", "Jd", "Qd" }, 1)
    assert(not AI.declare(g, 1, 2))
end

-- Saving and restoring.
do
    local g = T.new(rand, 2)
    if not g.over then
        local s0 = Save.serialize(g)
        local b0 = Save.deserialize(s0)
        assert(b0 and b0.ask == g.ask and Save.serialize(b0) == s0, "saved while asking")
        while g.ask do T.declare(g, g.turn, false) end
        for _ = 1, 6 do
            if g.over then break end
            local c = AI.choose(g, g.turn, 2, rand)
            if c then T.play(g, g.turn, c) else T.pass(g, g.turn) end
        end
        if not g.over then
            local s = Save.serialize(g)
            local back = Save.deserialize(s)
            assert(back and Save.serialize(back) == s and back.turn == g.turn and back.ck == g.ck and back.ct == g.ct)
            for i = 1, 4 do assert(back.played[i] == g.played[i]) end
            assert(Save.deserialize(s .. "|") == nil and Save.deserialize("garbage") == nil)
            local f = {}
            for part in (s .. "|"):gmatch("([^|]*)|") do f[#f + 1] = part end
            f[12] = f[12] .. f[9]:sub(1, 1) -- a card twice
            assert(Save.deserialize(table.concat(f, "|")) == nil, "a card twice")
        end
    end
    local g2 = fixed({ "4s 5s 6s 9h", "2h 3c", "5c 6d 7h Kh", "9c 10d" }, 1, 1)
    T.play(g2, 1, cards("4s 5s 6s"))
    g2.played = { true, true, true, true } -- short hand-made hands
    local back = Save.deserialize(Save.serialize(g2))
    assert(back.sam == 1 and back.cl == 3, "the Sam is saved")
end

-- Whole hands, computer against computer: they finish legally, the 40 dealt
-- cards conserved, points adding up to zero.
local function playHand(levels, leader)
    local g = T.new(rand, leader)
    local played = {}
    local guard = 0
    while g.ask do
        T.declare(g, g.turn, AI.declare(g, g.turn, levels[g.turn]))
    end
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
        assert(checkCards(g, played) == 40, "cards conserved")
        guard = guard + 1
        assert(guard < 300, "the hand does not end")
    end
    assert(g.winner and #g.hands[g.winner] == 0 or g.instant or g.samFail)
    local p, sum = T.handPoints(g), 0
    for s = 1, 4 do sum = sum + p[s] end
    assert(sum == 0, "points add up to zero")
    return g, p
end

do
    local games, hardPts, normalPts, instants, sams, fails, leader = 400, 0, 0, 0, 0, 0, nil
    local t0 = os.clock()
    for n = 1, games do
        local seat = (n - 1) % 4 + 1
        local levels = { 1, 1, 1, 1 }
        levels[seat] = 2
        local g, p = playHand(levels, leader)
        if g.instant then instants = instants + 1 end
        if g.sam then sams = sams + 1 end
        if g.samFail then fails = fails + 1 end
        leader = g.winner
        hardPts = hardPts + p[seat]
        for s = 1, 4 do
            if s ~= seat then normalPts = normalPts + p[s] / 3 end
        end
    end
    print(string.format("%d hands in %.2f s, %d instant wins, %d Sams (%d beaten); points a hand: Hard %.2f, Normal %.2f",
                        games, os.clock() - t0, instants, sams, fails, hardPts / games, normalPts / games))
    assert(hardPts > normalPts, "Hard scores more than Normal")
end

print("samloc logic ok")
