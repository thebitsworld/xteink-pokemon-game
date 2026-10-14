-- Phom hands: the deal, eating, drawing, discarding, ù and the result.
-- Usage:
--   local G = smudge.dofile("game.lua")(T)
-- with T the rules (logic.lua). Loaded only to deal or to make a move, and
-- dropped after, so it is never in memory with the drawing.
--
-- A hand (g): hands[s] (eaten cards included), eaten[s], disc[s] (the
-- cards s threw, in order), nd[s] (how many), stock (drawn from the end),
-- first (the seat that got 10 cards and threw first), turn, phase ("take":
-- eat or draw; "discard"), top (the card just thrown, while it may be
-- eaten) and topBy, mlen[s] (the sizes of the melds the hand is arranged
-- in), thrown (the last card thrown, shown on the table; thrownEaten once
-- eaten), and for you: eat1 (whether you may eat now) and okd (okd[card]:
-- the cards you may throw now), so the table screen needs no rules. Once
-- over: u (the seat that went ù, or nil), order (seats by
-- place), left[s] (points left over), mom[s], pts[s] (table points).

return function(T)
local G = {}

G.POINTS = { 3, 2, 1, 0 } -- for 1st, 2nd, 3rd, 4th; móm scores 0
G.U_POINTS = 5            -- ù: the winner alone
G.ROUNDS = 4              -- discards each

local function arrange(g, s, byCount)
    g.mlen[s] = T.arrange(g.hands[s], g.eaten[s], byCount)
end

-- The result: everyone's melds and points left over, places and points.
local function finish(g)
    g.over, g.phase, g.top = true, nil, nil
    g.left, g.mom, g.pts, g.order = {}, {}, { 0, 0, 0, 0 }, {}
    for s = 1, 4 do
        arrange(g, s, s == g.u)
        local left, melds = T.split(g.hands[s], g.eaten[s], s == g.u)
        g.left[s] = left or 0
        g.mom[s] = #(melds or {}) == 0
    end
    -- By places: no móm before móm, fewer points, then who threw first.
    for k = 0, 3 do g.order[k + 1] = (g.first + k - 1) % 4 + 1 end
    if g.u then
        for i, s in ipairs(g.order) do
            if s == g.u then table.remove(g.order, i) break end
        end
        table.insert(g.order, 1, g.u)
        g.pts[g.u] = G.U_POINTS
        return
    end
    local key = {}
    for i, s in ipairs(g.order) do key[s] = (g.mom[s] and 100000 or 0) + g.left[s] * 10 + i end
    table.sort(g.order, function(a, b) return key[a] < key[b] end)
    for place, s in ipairs(g.order) do
        if not g.mom[s] then g.pts[s] = G.POINTS[place] end
    end
end

-- What the table screen shows you may do now.
function G.prepare(g)
    g.eat1, g.okd = false, nil
    if g.over or g.turn ~= 1 then return end
    local hand, eaten = g.hands[1], g.eaten[1]
    if g.phase == "take" then
        g.eat1 = g.top ~= nil and T.canEat(hand, eaten, g.top)
    else
        g.okd = {}
        for _, c in ipairs(hand) do
            if T.canDiscard(hand, eaten, c) then g.okd[c] = true end
        end
    end
end

local function checkU(g, s)
    if T.isU(g.hands[s], g.eaten[s]) then
        g.u = s
        finish(g)
        return true
    end
    return false
end

-- A new hand. `rand(n)` returns 1..n. `first` gets 10 cards and throws
-- first (the last winner; a random seat for the first hand).
function G.new(rand, first)
    first = first or rand(4)
    local deck = {}
    for i = 1, 52 do deck[i] = i - 1 end
    for i = 52, 2, -1 do
        local j = rand(i)
        deck[i], deck[j] = deck[j], deck[i]
    end
    local g = { hands = { {}, {}, {}, {} }, eaten = { {}, {}, {}, {} }, disc = { {}, {}, {}, {} },
                nd = { 0, 0, 0, 0 }, stock = {}, mlen = {}, first = first, turn = first, phase = "discard",
                topBy = 0, over = false }
    for i = 1, 37 do
        local hand = g.hands[(first + i - 2) % 4 + 1]
        hand[#hand + 1] = deck[i]
    end
    for i = 38, 52 do g.stock[#g.stock + 1] = deck[i] end
    for s = 1, 4 do arrange(g, s) end
    checkU(g, first)
    G.prepare(g)
    return g
end

function G.canEat(g, seat)
    return not g.over and g.phase == "take" and seat == g.turn and g.top ~= nil
        and T.canEat(g.hands[seat], g.eaten[seat], g.top)
end

-- Eats the card just thrown (`eat`) or draws from the stock. Returns nil,
-- or why not.
function G.take(g, seat, eat)
    if g.over then return "The hand is over" end
    if seat ~= g.turn then return "Not your turn" end
    if g.phase ~= "take" then return "Throw a card first" end
    local c
    if eat then
        if not G.canEat(g, seat) then return "That card makes no meld with your hand" end
        c = g.top
        g.thrownEaten = true
        local d = g.disc[g.topBy]
        d[#d] = nil
        local e = g.eaten[seat]
        e[#e + 1] = c
    else
        c = table.remove(g.stock)
        if not c then return "The stock is empty" end
    end
    local hand = g.hands[seat]
    hand[#hand + 1] = c
    g.top, g.phase, g.got, g.ate = nil, "discard", c, eat and true or false
    arrange(g, seat)
    checkU(g, seat)
    G.prepare(g)
    return nil
end

function G.discardRefusal(g, seat, c)
    if g.over then return "The hand is over" end
    if seat ~= g.turn then return "Not your turn" end
    if g.phase ~= "discard" then return "Eat or draw first" end
    if not c then return "Pick a card to discard" end
    if T.has(g.eaten[seat], c) then return "An eaten card stays in its meld" end
    if not T.canDiscard(g.hands[seat], g.eaten[seat], c) then return "That breaks the meld of an eaten card" end
    return nil
end

-- Throws `c`. Returns nil, or why not.
function G.discard(g, seat, c)
    local why = G.discardRefusal(g, seat, c)
    if why then return why end
    local hand = g.hands[seat]
    for i = #hand, 1, -1 do
        if hand[i] == c then table.remove(hand, i) end
    end
    local d = g.disc[seat]
    d[#d + 1] = c
    g.nd[seat] = g.nd[seat] + 1
    g.top, g.topBy, g.lastSeat, g.lastAte = c, seat, seat, g.ate and g.got or nil
    g.thrown, g.thrownEaten = c, false
    g.got, g.ate = nil, nil
    arrange(g, seat)
    local done = true
    for s = 1, 4 do
        if g.nd[s] < G.ROUNDS then done = false end
    end
    if done then
        finish(g)
    else
        g.turn, g.phase = seat % 4 + 1, "take"
    end
    G.prepare(g)
    return nil
end

return G
end
