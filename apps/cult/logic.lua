-- Cult Ledger: the rules. No drawing, so it can be tested on a computer
-- (test/lua_apps/cult_test.lua).
--
-- You run a small cult. Each turn three event cards come up; pick one and one
-- of its two choices, paying what it costs. Every 4th turn the members must
-- be fed (one food for every two, or some walk out), and every few turns the
-- town grows more suspicious. Heat reaching 5 brings a police raid; no
-- members left ends the cult; and the bishop's inspector arrives on the
-- level's last turn. From turn 6, with enough relics, members and prisoners
-- to offer, summon your god and win.

local C = {}

C.KEYS = { "c", "f", "m", "p", "r", "s" }
C.NAMES = { c = "Coin", f = "Food", m = "Cultists", p = "Prisoners", r = "Relics", s = "Heat" }
C.HEAT_MAX = 5
C.FEAST_EVERY = 4
C.SUMMON_FROM = 6
-- gossip: every how many turns the town's suspicion rises by one; deadline:
-- the turn the inspector arrives. Tuned with a careful simulated player, who
-- wins about 95% of Easy games (in 12 turns) and half of Hard ones (in 21).
C.LEVELS = {
    { name = "Easy", start = { c = 3, f = 4, m = 3, p = 0, r = 0, s = 0 }, need = { r = 3, m = 4, p = 1 }, gossip = 6,
      deadline = 20 },
    { name = "Hard", start = { c = 2, f = 3, m = 2, p = 0, r = 0, s = 1 }, need = { r = 4, m = 4, p = 1 }, gossip = 4,
      deadline = 25 },
}

-- An effect string as a list of { key, amount }.
function C.parse(effect)
    local out = {}
    for key, sign, n in effect:gmatch("(%a)([%+%-])(%d+)") do
        out[#out + 1] = { key, (sign == "-" and -1 or 1) * tonumber(n) }
    end
    return out
end

-- "-1 Coin, +1 Cultists" for an effect.
function C.describe(effect)
    local parts = {}
    for _, e in ipairs(C.parse(effect)) do
        parts[#parts + 1] = string.format("%+d %s", e[2], C.NAMES[e[1]])
    end
    return #parts > 0 and table.concat(parts, ", ") or "Nothing happens"
end

-- The card lines of cards.lua's text: { "title|...", ... }.
function C.lines(text)
    local out = {}
    for line in text:gmatch("[^\n]+") do out[#out + 1] = line end
    return out
end

-- cards: C.lines(cards.lua's text).
function C.new(level, cards, rand)
    local g = { level = level, turn = 1, res = {}, deck = {}, cards = cards, over = nil, log = nil }
    for _, k in ipairs(C.KEYS) do g.res[k] = C.LEVELS[level].start[k] end
    for i = 1, #cards do g.deck[i] = i end
    for i = #g.deck, 2, -1 do
        local j = rand(i)
        g.deck[i], g.deck[j] = g.deck[j], g.deck[i]
    end
    C.deal(g)
    return g
end

-- Card id as { title, { label, effect }, { label, effect } }.
function C.card(g, id)
    local t, l1, e1, l2, e2 = g.cards[id]:match("^(.-)|(.-)|(.-)|(.-)|(.-)$")
    return { t, { l1, e1 }, { l2, e2 } }
end

-- Three cards from the top of the deck.
function C.deal(g)
    g.hand = {}
    for i = 1, 3 do g.hand[i] = table.remove(g.deck, 1) end
end

-- Whether the god can be summoned now: from turn 6, with enough of everything.
function C.canSummon(g)
    local need = C.LEVELS[g.level].need
    return not g.over and g.turn >= C.SUMMON_FROM and g.res.r >= need.r and g.res.m >= need.m and g.res.p >= need.p
end

function C.summon(g)
    if not C.canSummon(g) then return false end
    g.over, g.log = "won", "The god answers your call!"
    return true
end

-- Whether the player can afford choice (1 or 2) of card id.
function C.affordable(g, id, choice)
    local effect = C.card(g, id)[choice + 1][2]
    for _, e in ipairs(C.parse(effect)) do
        if e[1] ~= "s" and e[2] < 0 and g.res[e[1]] + e[2] < 0 then return false end
    end
    return true
end

local function checkOver(g)
    if g.res.s >= C.HEAT_MAX then
        g.over = "raid"
    elseif g.res.m <= 0 then
        g.over = "empty"
    elseif g.turn >= C.LEVELS[g.level].deadline then
        g.over = "late"
    end
end

-- Whether any choice on the table can be paid for.
function C.canChoose(g)
    for k = 1, 3 do
        for choice = 1, 2 do
            if C.affordable(g, g.hand[k], choice) then return true end
        end
    end
    return false
end

-- Picks choice (1 or 2) of hand card k - or, with k == 0 and nothing
-- affordable, lies low for a turn (+1 heat); then the turn's end (feast,
-- gossip) and the next deal. Returns false if it is not allowed.
function C.choose(g, k, choice)
    if g.over then return false end
    local notes, effect
    if k == 0 then
        if C.canChoose(g) then return false end
        notes, effect = { "You lie low." }, "s+1"
    else
        local id = g.hand[k]
        if id == nil or not C.affordable(g, id, choice) then return false end
        local card = C.card(g, id)
        effect = card[choice + 1][2]
        notes = { card[1] .. ": " .. card[choice + 1][1] .. "." }
    end
    for _, e in ipairs(C.parse(effect)) do g.res[e[1]] = math.max(0, g.res[e[1]] + e[2]) end
    -- All three go under the deck, the chosen one last, so every card comes
    -- round before any comes again.
    for i = 1, 3 do
        if i ~= k then g.deck[#g.deck + 1] = g.hand[i] end
    end
    if k > 0 then g.deck[#g.deck + 1] = g.hand[k] end
    -- Feast and gossip.
    if g.turn % C.FEAST_EVERY == 0 then
        local need = g.res.m // 2
        if g.res.f >= need then
            g.res.f = g.res.f - need
            notes[#notes + 1] = string.format("Feast: %d food eaten.", need)
        else
            local short = need - g.res.f
            g.res.f, g.res.m = 0, math.max(0, g.res.m - short)
            notes[#notes + 1] = string.format("Feast: not enough food - %d left the cult.", short)
        end
    end
    if g.turn % C.LEVELS[g.level].gossip == 0 then
        g.res.s = g.res.s + 1
        notes[#notes + 1] = "Gossip in town: +1 heat."
    end
    g.log = table.concat(notes, " ")
    checkOver(g)
    if not g.over then
        g.turn = g.turn + 1
        C.deal(g)
    end
    return true
end

-- Turns until the next feast and gossip (counting this one).
function C.turnsUntil(g, every) return every - (g.turn - 1) % every end

-- Saving: level, turn, the resources, the deck and the hand.
function C.serialize(g)
    local parts = { g.level, g.turn }
    for _, k in ipairs(C.KEYS) do parts[#parts + 1] = g.res[k] end
    return table.concat(parts, ",") .. "|" .. table.concat(g.deck, ",") .. "|" .. table.concat(g.hand, ",")
end

function C.deserialize(s, cards)
    local head, deck, hand = s:match("^([^|]*)|([^|]*)|([^|]*)$")
    if not head then return nil end
    local v = {}
    for x in head:gmatch("%d+") do v[#v + 1] = tonumber(x) end
    if #v ~= 2 + #C.KEYS or not C.LEVELS[v[1]] then return nil end
    local g = { level = v[1], turn = v[2], res = {}, deck = {}, hand = {}, cards = cards }
    for i, k in ipairs(C.KEYS) do g.res[k] = v[2 + i] end
    for x in deck:gmatch("%d+") do g.deck[#g.deck + 1] = tonumber(x) end
    for x in hand:gmatch("%d+") do g.hand[#g.hand + 1] = tonumber(x) end
    if #g.hand ~= 3 or #g.deck + 3 ~= #cards then return nil end
    return g
end

return C
