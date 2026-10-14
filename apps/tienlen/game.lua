-- Tien Len hands: the deal, instant wins, what may be played now, real
-- plays and passes, and the points. Usage:
--   local G = smudge.dofile("game.lua")(T)
-- with T the rules (logic.lua). Loaded only to deal or to make a move, and
-- dropped after, so it is never in memory with the drawing.

return function(T)
local G = {}

G.POINTS = { 3, 2, 1, 0 } -- for finishing 1st, 2nd, 3rd, 4th

-- An instant win at the deal: a reason, or nil.
function G.instant(hand)
    local cnt = {}
    for r = 1, 13 do cnt[r] = 0 end
    for _, c in ipairs(hand) do
        local r = c // 4 + 1
        cnt[r] = cnt[r] + 1
    end
    if cnt[T.TWO] == 4 then return "four 2s" end
    local all, pairs = true, 0
    for r = 1, 13 do
        if r <= 12 and cnt[r] == 0 then all = false end
        pairs = pairs + cnt[r] // 2
    end
    if all then return "a dragon straight" end
    if pairs >= 6 then return "six pairs" end
    return nil
end

local function finish(g)
    g.over = true
    g.turn = 0
end

-- A new hand. `rand(n)` returns 1..n. `leader` is the seat that leads (the
-- last winner), or nil for the first hand: the 3 of spades leads.
function G.new(rand, leader)
    local deck = {}
    for i = 1, 52 do deck[i] = i - 1 end
    for i = 52, 2, -1 do
        local j = rand(i)
        deck[i], deck[j] = deck[j], deck[i]
    end
    local g = { hands = { {}, {}, {}, {} }, passed = { false, false, false, false }, out = {}, order = {},
                cur = nil, curBy = 0, need3 = false, over = false }
    for i = 1, 52 do
        local hand = g.hands[(i - 1) % 4 + 1]
        hand[#hand + 1] = deck[i]
    end
    for s = 1, 4 do table.sort(g.hands[s]) end
    G.start(g, leader)
    return g
end

-- Sets who leads (and checks for instant wins) once the hands are dealt.
function G.start(g, leader)
    if leader and leader >= 1 and leader <= 4 then
        g.turn, g.need3 = leader, false
    else
        for s = 1, 4 do
            if g.hands[s][1] == T.THREE_SPADES then g.turn = s end
        end
        g.need3 = true
    end
    for k = 0, 3 do
        local s = (g.turn + k - 1) % 4 + 1
        local why = G.instant(g.hands[s])
        if why then
            g.instant, g.instantWhy, g.order = s, why, { s }
            finish(g)
            return
        end
    end
end

-- Points for each seat when a hand is over: by finishing place, or the
-- winner alone after an instant win.
function G.handPoints(g)
    local pts = { 0, 0, 0, 0 }
    if g.instant then
        pts[g.instant] = G.POINTS[1]
    else
        for place, s in ipairs(g.order) do pts[s] = G.POINTS[place] end
    end
    return pts
end

local function owns(hand, cards)
    local seen = {}
    for _, c in ipairs(cards) do
        if seen[c] then return false end
        seen[c] = true
        local found = false
        for _, x in ipairs(hand) do
            if x == c then found = true break end
        end
        if not found then return false end
    end
    return true
end

-- Why `seat` may not play `cards` now, or nil.
function G.refusal(g, seat, cards)
    if g.over then return "The hand is over" end
    if seat ~= g.turn then return "Not your turn" end
    if #cards == 0 then return "Pick cards to play" end
    if not owns(g.hands[seat], cards) then return "Not your cards" end
    local k, l, t = T.classify(cards)
    if not k then return "Not a valid combination" end
    if g.need3 then
        local has = false
        for _, c in ipairs(cards) do
            if c == T.THREE_SPADES then has = true end
        end
        if not has then return "Play the 3 of spades in it" end
    end
    if g.cur and not T.beats(k, l, t, g.ck, g.cl, g.ct) then return "That does not beat the table" end
    return nil
end

function G.passRefusal(g, seat)
    if g.over then return "The hand is over" end
    if seat ~= g.turn then return "Not your turn" end
    if not g.cur then return "You lead: play something" end
    return nil
end

-- The next player, or a new round when everyone else has passed.
local function advance(g)
    local by = g.curBy
    local open = false
    for s = 1, 4 do
        if s ~= by and not g.out[s] and not g.passed[s] then open = true end
    end
    if not open then
        g.cur, g.ck, g.cl, g.ct = nil, nil, nil, nil
        g.passed = { false, false, false, false }
        local s = by
        while g.out[s] do s = s % 4 + 1 end
        g.turn, g.newRound = s, true
        return
    end
    local s = g.turn
    for _ = 1, 4 do
        s = s % 4 + 1
        if not g.out[s] and not g.passed[s] then break end
    end
    g.turn = s
end

-- Plays `cards` for `seat`. Returns nil, or why not.
function G.play(g, seat, cards)
    local why = G.refusal(g, seat, cards)
    if why then return why end
    cards = T.sorted(cards) -- the caller's list may be the hand itself
    local k, l, t = T.classify(cards)
    g.chop = T.isChop(k, l, g.ck, g.cl)
    local hand = g.hands[seat]
    for _, c in ipairs(cards) do
        for i = #hand, 1, -1 do
            if hand[i] == c then table.remove(hand, i) end
        end
    end
    g.cur, g.ck, g.cl, g.ct, g.curBy = cards, k, l, t, seat
    g.need3, g.newRound = false, false
    g.lastSeat, g.lastPass = seat, false
    if #hand == 0 then
        g.out[seat] = true
        g.order[#g.order + 1] = seat
        if #g.order == 3 then
            for s = 1, 4 do
                if not g.out[s] then
                    g.out[s] = true
                    g.order[4] = s
                end
            end
            finish(g)
            return nil
        end
    end
    advance(g)
    return nil
end

-- `seat` passes. Returns nil, or why not.
function G.pass(g, seat)
    local why = G.passRefusal(g, seat)
    if why then return why end
    g.passed[seat] = true
    g.lastSeat, g.lastPass, g.chop, g.newRound = seat, true, false, false
    advance(g)
    return nil
end

return G
end
