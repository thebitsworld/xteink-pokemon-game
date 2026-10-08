-- Tests for apps/cult (run by ctest: LuaAppLogic_cult).
local C = dofile(APPS .. "/cult/logic.lua")
local CARDS = C.lines(dofile(APPS .. "/cult/cards.lua"))

math.randomseed(31)
local function rand(n) return math.random(1, n) end

-- Every card has a title and two choices with readable effects.
for id in ipairs(CARDS) do
    local card = C.card({ cards = CARDS }, id)
    assert(type(card[1]) == "string" and card[1] ~= "" and #card == 3)
    for i = 2, 3 do
        local label, effect = card[i][1], card[i][2]
        assert(type(label) == "string" and label ~= "")
        assert(effect == "" or effect:match("^[cfmprs][%+%-]%d"), "effect of " .. card[1])
        for _, e in ipairs(C.parse(effect)) do assert(C.NAMES[e[1]]) end
    end
end
assert(C.describe("c-1 m+1") == "-1 Coin, +1 Cultists" and C.describe("") == "Nothing happens")

-- A game with a chosen hand: card ids of the given titles.
local function idOf(title)
    for i, line in ipairs(CARDS) do
        if line:match("^(.-)|") == title then return i end
    end
    error("no card " .. title)
end
local function game(level, res, hand)
    local g = C.new(level, CARDS, rand)
    for k, v in pairs(res) do g.res[k] = v end
    if hand then
        -- Put those cards in the hand, the rest in the deck.
        local inHand = {}
        g.hand = {}
        for i, t in ipairs(hand) do
            g.hand[i] = idOf(t)
            inHand[g.hand[i]] = true
        end
        g.deck = {}
        for i = 1, #CARDS do
            if not inHand[i] then g.deck[#g.deck + 1] = i end
        end
    end
    return g
end

-- Paying for a choice; what cannot be paid for.
do
    local g = game(1, { c = 2, f = 4, m = 3, p = 0, r = 0, s = 0 }, { "The antique shop", "A lost tourist", "Market day" })
    assert(C.affordable(g, g.hand[1], 1), "two coins buy a relic")
    assert(not C.affordable(g, g.hand[3], 2), "no relic to sell")
    assert(C.choose(g, 1, 1))
    assert(g.res.c == 0 and g.res.r == 1 and g.turn == 2 and #g.hand == 3)
    assert(#g.deck + 3 == #CARDS, "no card lost")
end

-- The feast on turn 4, gossip on the level's turns.
do
    local g = game(1, { c = 0, f = 1, m = 4, p = 0, r = 0, s = 0 }, { "A lost tourist", "A lost tourist", "A lost tourist" })
    g.turn = 4
    C.choose(g, 1, 2) -- point the way: nothing happens
    assert(g.res.f == 0 and g.res.m == 3, "two food needed for four members: one short, one leaves")
    local h = game(2, { c = 0, f = 9, m = 3, p = 0, r = 0, s = 1 }, { "A lost tourist", "A lost tourist", "A lost tourist" })
    h.turn = 4
    C.choose(h, 1, 2)
    assert(h.res.s == 2, "Hard gossips every 4th turn")
end

-- Losing: a raid at 5 heat, an empty cult, the inspector at the deadline.
do
    local g = game(1, { c = 0, f = 9, m = 3, p = 0, r = 0, s = 4 }, { "A police patrol", "A lost tourist", "A lost tourist" })
    C.choose(g, 1, 2) -- hope for the best: +1 heat
    assert(g.over == "raid")
    g = game(1, { c = 0, f = 9, m = 1, p = 0, r = 0, s = 0 }, { "Hungry members", "A lost tourist", "A lost tourist" })
    C.choose(g, 1, 2) -- declare a fast: -1 member
    assert(g.over == "empty")
    g = game(1, { c = 0, f = 9, m = 3, p = 0, r = 0, s = 0 }, { "A lost tourist", "A lost tourist", "A lost tourist" })
    g.turn = C.LEVELS[1].deadline
    C.choose(g, 1, 2)
    assert(g.over == "late")
end

-- Lying low only when nothing can be paid for.
do
    local g = game(1, { c = 0, f = 0, m = 1, p = 0, r = 0, s = 0 }, { "Market day", "Sacrifice night", "The antique shop" })
    assert(C.affordable(g, g.hand[3], 2), "browsing is free")
    assert(not C.choose(g, 0))
    g = game(1, { c = 0, f = 0, m = 1, p = 0, r = 0, s = 0 }, { "Market day", "Sacrifice night", "Market day" })
    assert(not C.canChoose(g) and C.choose(g, 0) and g.res.s == 1)
end

-- Summoning: from turn 6, with everything the level needs.
do
    local need = C.LEVELS[1].need
    local g = game(1, { c = 0, f = 9, m = need.m, p = need.p, r = need.r, s = 0 })
    assert(not C.canSummon(g), "not before turn 6")
    g.turn = C.SUMMON_FROM
    assert(C.summon(g) and g.over == "won")
    g = game(1, { c = 0, f = 9, m = need.m, p = 0, r = need.r, s = 0 })
    g.turn = C.SUMMON_FROM
    assert(not C.canSummon(g), "an offering is needed")
end

-- Saving.
do
    local g = game(2, { c = 1, f = 2, m = 3, p = 1, r = 2, s = 3 })
    g.turn = 9
    local h = C.deserialize(C.serialize(g), CARDS)
    assert(h and h.level == 2 and h.turn == 9 and h.res.r == 2 and h.res.s == 3)
    for i = 1, 3 do assert(h.hand[i] == g.hand[i]) end
    assert(C.serialize(h) == C.serialize(g))
    assert(C.deserialize("", CARDS) == nil and C.deserialize("1,2|3|4", CARDS) == nil)
end

-- Balance: a careful simulated player wins most Easy games and some Hard ones.
do
    local function value(g, id, ch)
        local need = C.LEVELS[g.level].need
        local v = 0
        for _, e in ipairs(C.parse(C.card(g, id)[ch + 1][2])) do
            local k, n = e[1], e[2]
            local wt = ({ c = 1, f = 0.8, m = 2, p = 3, r = 4.5, s = -(0.8 + g.res.s * 1.2) })[k]
            if need[k] and g.res[k] >= need[k] then wt = 0.4 end
            if k == "f" and g.res.f < g.res.m // 2 + 1 then wt = 2 end
            if n < 0 and need[k] and g.res[k] + n < need[k] then wt = 6 end
            v = v + wt * n
        end
        return v + rand(100) / 1000
    end
    for level, range in ipairs({ { 0.85, 1.0 }, { 0.3, 0.7 } }) do
        local wins = 0
        for _ = 1, 400 do
            local g = C.new(level, CARDS, rand)
            while not g.over do
                if C.canSummon(g) then
                    C.summon(g)
                else
                    local bk, bc, bv = nil, nil, -1e9
                    for k = 1, 3 do
                        for ch = 1, 2 do
                            if C.affordable(g, g.hand[k], ch) then
                                local v = value(g, g.hand[k], ch)
                                if v > bv then bk, bc, bv = k, ch, v end
                            end
                        end
                    end
                    assert(C.choose(g, bk or 0, bc))
                end
            end
            if g.over == "won" then wins = wins + 1 end
        end
        local rate = wins / 400
        print(string.format("level %d: a careful player wins %.0f%%", level, rate * 100))
        assert(rate >= range[1] and rate <= range[2], "level " .. level .. " balance")
    end
end

print("cult ok")
