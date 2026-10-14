-- Sam Loc hands: the deal, instant wins, declaring Sam, what may be played
-- now, real plays and passes, and the points. Usage:
--   local G = smudge.dofile("game.lua")(T)
-- with T the rules (logic.lua). Loaded only to deal or to make a move, and
-- dropped after, so it is never in memory with the drawing.
--
-- A hand: g.hands[1..4] (sorted), g.turn, the table (g.cur, its kind g.ck,
-- size g.cl, top rank g.ct, played by g.curBy), g.passed, g.played (who has
-- played a card yet), g.lead (who leads). While g.ask is set the players
-- are asked in turn, from the leader, whether they declare Sam (g.ask = how
-- many have said no). g.sam is the declarer; g.over, g.winner, g.instant
-- (why) and g.samFail tell how it ended.

return function(T)
local G = {}

G.SAM = 20      -- each other player pays this for Sam or an instant win
G.TWO_COST = 5  -- a 2 left in the hand (thối 2)

-- An instant win at the deal (tới trắng): a reason, or nil.
function G.instant(hand)
    local cnt = {}
    for r = 1, 13 do cnt[r] = 0 end
    local red = 0
    for _, c in ipairs(hand) do
        local r = c // 4 + 1
        cnt[r] = cnt[r] + 1
        if c % 4 >= 2 then red = red + 1 end
    end
    if cnt[T.TWO] == 4 then return "four 2s" end
    if #hand == 10 and T.classify(hand) == T.STRAIGHT then return "a 10-card straight" end
    local pairs, triples = 0, 0
    for r = 1, 13 do
        pairs = pairs + cnt[r] // 2
        if cnt[r] >= 3 then triples = triples + 1 end
    end
    if triples >= 3 then return "three triples" end
    if pairs >= 5 then return "five pairs" end
    if #hand == 10 and (red == 0 or red == 10) then return "ten cards of one colour" end
    return nil
end

local function finish(g, winner)
    g.over, g.winner, g.turn, g.ask = true, winner, 0, nil
end

-- A new hand. `rand(n)` returns 1..n. `leader` is the seat that leads (the
-- last winner), or nil for the first hand: a seat drawn at random.
function G.new(rand, leader)
    local deck = {}
    for i = 1, 52 do deck[i] = i - 1 end
    for i = 52, 2, -1 do
        local j = rand(i)
        deck[i], deck[j] = deck[j], deck[i]
    end
    local g = { hands = { {}, {}, {}, {} }, passed = { false, false, false, false },
                played = { false, false, false, false }, curBy = 0, over = false }
    for i = 1, 40 do
        local hand = g.hands[(i - 1) % 4 + 1]
        hand[#hand + 1] = deck[i]
    end
    for s = 1, 4 do table.sort(g.hands[s]) end
    G.start(g, leader and leader >= 1 and leader <= 4 and leader or rand(4))
    return g
end

-- Checks for instant wins once the hands are dealt, then asks for Sam.
function G.start(g, leader)
    g.lead, g.turn = leader, leader
    for k = 0, 3 do
        local s = (leader + k - 1) % 4 + 1
        local why = G.instant(g.hands[s])
        if why then
            g.instant = why
            finish(g, s)
            return
        end
    end
    g.ask = 0
end

-- `seat`, asked, declares Sam (yes) or not. Returns nil, or why not.
function G.declare(g, seat, yes)
    if g.over or not g.ask then return "Not now" end
    if seat ~= g.turn then return "Not your turn" end
    if yes then
        g.sam, g.ask, g.turn = seat, nil, seat
    elseif g.ask == 3 then
        g.ask, g.turn = nil, g.lead
    else
        g.ask, g.turn = g.ask + 1, seat % 4 + 1
    end
    return nil
end

-- Points for each seat when a hand is over (they add up to 0).
function G.cost(g, s)
    local hand = g.hands[s]
    local p = #hand
    for _, c in ipairs(hand) do
        if c // 4 + 1 == T.TWO then p = p + G.TWO_COST end
    end
    if not g.played[s] then p = p * 2 end -- cóng: never played a card
    return p
end

function G.handPoints(g)
    local pts = { 0, 0, 0, 0 }
    -- A beaten Sam: the declarer pays everyone else; otherwise everyone
    -- pays the winner.
    local w = g.samFail and g.sam or g.winner
    for s = 1, 4 do
        if s ~= w then
            local p = (g.instant or g.sam) and G.SAM or G.cost(g, s)
            if g.samFail then p = -p end
            pts[s], pts[w] = pts[s] - p, pts[w] + p
        end
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
    if g.ask then return "Declare Sam or not first" end
    if #cards == 0 then return "Pick cards to play" end
    local hand = g.hands[seat]
    if not owns(hand, cards) then return "Not your cards" end
    local k, l, t = T.classify(cards)
    if not k then return "Not a valid combination" end
    if g.cur and not T.beats(k, l, t, g.ck, g.cl, g.ct) then return "That does not beat the table" end
    if k == T.SINGLE and T.mustTop(g, seat) and t < hand[#hand] // 4 + 1 then
        return "Next player is on one: play your highest"
    end
    return nil
end

function G.passRefusal(g, seat)
    if g.over then return "The hand is over" end
    if seat ~= g.turn then return "Not your turn" end
    if g.ask then return "Declare Sam or not first" end
    if not g.cur then return "You lead: play something" end
    return nil
end

-- The next player, or a new round when everyone else has passed.
local function advance(g)
    local s = T.nextSeat(g, g.turn)
    if not s or s == g.curBy then
        g.cur, g.ck, g.cl, g.ct = nil, nil, nil, nil
        g.passed = { false, false, false, false }
        g.turn, g.newRound = g.curBy, true
    else
        g.turn = s
    end
end

-- Plays `cards` for `seat`. Returns nil, or why not.
function G.play(g, seat, cards)
    local why = G.refusal(g, seat, cards)
    if why then return why end
    local k, l, t = T.classify(cards)
    cards = k == T.STRAIGHT and T.inOrder(cards) or T.sorted(cards) -- the caller's list may be the hand itself
    g.chop = g.ck ~= nil and k ~= g.ck
    local hand = g.hands[seat]
    for _, c in ipairs(cards) do
        for i = #hand, 1, -1 do
            if hand[i] == c then table.remove(hand, i) end
        end
    end
    g.cur, g.ck, g.cl, g.ct, g.curBy = cards, k, l, t, seat
    g.played[seat], g.newRound = true, false
    g.lastSeat, g.lastPass = seat, false
    if g.sam and seat ~= g.sam then
        g.samFail = true -- the declarer was beaten
        finish(g, seat)
    elseif #hand == 0 then
        finish(g, seat)
    else
        advance(g)
    end
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
