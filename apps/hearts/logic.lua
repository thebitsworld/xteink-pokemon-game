-- Hearts rules. No drawing, so it can be tested on a computer
-- (test/lua_apps/hearts_test.lua). The computer players are in ai.lua and
-- saving a game in save.lua (loaded only to save or restore, to keep the X3's
-- memory for playing).
--
-- Standard American Hearts for four: you (South) and three computer players.
-- Thirteen cards each; play goes clockwise (South, West, North, East). Before
-- each hand three cards are passed - left, right, across, then no pass. The
-- two of clubs leads the first trick; follow suit if you can; the highest
-- card of the suit led takes the trick and leads the next. Each heart costs a
-- point and the queen of spades thirteen. Hearts cannot be led until one has
-- been played on another suit (unless you hold nothing else), and the first
-- trick takes no point cards (unless you hold nothing else). Taking all 26 -
-- shooting the moon - gives the other three 26 each instead. The game ends
-- when someone reaches 100; the lowest score wins.

local H = {}

H.SEATS = 4
H.CLUBS, H.DIAMONDS, H.SPADES, H.HEARTS = 0, 1, 2, 3
H.SUIT_NAMES = { [0] = "clubs", "diamonds", "spades", "hearts" }
H.TWO_OF_CLUBS = 1
H.QUEEN_OF_SPADES = 2 * 13 + 11 -- suit 2, rank 12
H.END_SCORE = 100

-- A card is 1..52: suit = (card - 1) // 13, rank = (card - 1) % 13 + 2
-- (2..14, the ace high), so a higher number in a suit is a higher card.
function H.suit(card) return (card - 1) // 13 end
function H.rank(card) return (card - 1) % 13 + 2 end
H.RANK_NAMES = { "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K", "A" }
function H.rankName(card) return H.RANK_NAMES[H.rank(card) - 1] end

function H.points(card)
    if H.suit(card) == H.HEARTS then return 1 end
    if card == H.QUEEN_OF_SPADES then return 13 end
    return 0
end

-- Pass direction for hand number n: seats to the left (+1), right (+3),
-- across (+2), or none (0).
function H.passOffset(hand) return ({ 1, 3, 2, 0 })[(hand - 1) % 4 + 1] end

local function sortHand(hand) table.sort(hand) end

-- Deals a new hand. `rand(n)` returns 1..n.
function H.deal(g, rand)
    local deck = {}
    for i = 1, 52 do deck[i] = i end
    for i = 52, 2, -1 do
        local j = rand(i)
        deck[i], deck[j] = deck[j], deck[i]
    end
    g.hands = { {}, {}, {}, {} }
    for i = 1, 52 do
        local hand = g.hands[(i - 1) % 4 + 1]
        hand[#hand + 1] = deck[i]
    end
    for s = 1, 4 do sortHand(g.hands[s]) end
    g.taken = { 0, 0, 0, 0 }
    g.trick, g.lastTrick = {}, nil
    g.tricks = 0
    g.broken = false
    g.played = string.rep("0", 52) -- "1" for every card played this hand
    g.phase = H.passOffset(g.hand) == 0 and "play" or "pass"
    if g.phase == "play" then H.startPlay(g) end
end

function H.new(rand)
    local g = { scores = { 0, 0, 0, 0 }, hand = 1, over = false }
    H.deal(g, rand)
    return g
end

local function holds(hand, card)
    for k, c in ipairs(hand) do
        if c == card then return k end
    end
    return nil
end

-- After passing: whoever holds the two of clubs leads.
function H.startPlay(g)
    g.phase = "play"
    for s = 1, 4 do
        if holds(g.hands[s], H.TWO_OF_CLUBS) then g.turn, g.leader = s, s end
    end
end

-- `picks[s]` is the list of three cards seat s passes. All four are taken out
-- before any is handed over.
function H.pass(g, picks)
    local offset = H.passOffset(g.hand)
    for s = 1, 4 do
        if #picks[s] ~= 3 then return false end
        for _, c in ipairs(picks[s]) do
            if not holds(g.hands[s], c) then return false end
        end
    end
    for s = 1, 4 do
        for _, c in ipairs(picks[s]) do table.remove(g.hands[s], holds(g.hands[s], c)) end
    end
    for s = 1, 4 do
        local to = (s - 1 + offset) % 4 + 1
        for _, c in ipairs(picks[s]) do g.hands[to][#g.hands[to] + 1] = c end
    end
    for s = 1, 4 do sortHand(g.hands[s]) end
    H.startPlay(g)
    return true
end

function H.wasPlayed(g, card) return g.played:byte(card) == 49 end

local function onlyPoints(hand)
    for _, c in ipairs(hand) do
        if H.points(c) == 0 then return false end
    end
    return true
end

-- Why `card` may not be played by `seat` now, or nil if it may.
function H.refusal(g, seat, card)
    if g.over or g.phase ~= "play" then return "not now" end
    local hand = g.hands[seat]
    if not holds(hand, card) then return "not in hand" end
    if #g.trick == 0 then
        if g.tricks == 0 and card ~= H.TWO_OF_CLUBS then return "The two of clubs leads the first trick" end
        if H.suit(card) == H.HEARTS and not g.broken then
            for _, c in ipairs(hand) do
                if H.suit(c) ~= H.HEARTS then return "Hearts are not broken yet" end
            end
        end
        return nil
    end
    local led = H.suit(g.trick[1].card)
    if H.suit(card) ~= led then
        for _, c in ipairs(hand) do
            if H.suit(c) == led then return "Follow " .. H.SUIT_NAMES[led] end
        end
    end
    if g.tricks == 0 and H.points(card) > 0 and not onlyPoints(hand) then
        return "No points on the first trick"
    end
    return nil
end

function H.legal(g, seat)
    local out = {}
    for _, c in ipairs(g.hands[seat]) do
        if not H.refusal(g, seat, c) then out[#out + 1] = c end
    end
    return out
end

-- The seat winning the trick so far.
function H.trickWinner(trick)
    local led = H.suit(trick[1].card)
    local best = trick[1]
    for k = 2, #trick do
        local t = trick[k]
        if H.suit(t.card) == led and t.card > best.card then best = t end
    end
    return best.seat
end

-- Plays `card` for the seat to move. Completing a trick gives its points to
-- its winner, who leads next; completing the hand scores it. Returns false
-- if the card is not allowed.
function H.play(g, card)
    local seat = g.turn
    if H.refusal(g, seat, card) then return false end
    table.remove(g.hands[seat], holds(g.hands[seat], card))
    g.trick[#g.trick + 1] = { seat = seat, card = card }
    g.played = g.played:sub(1, card - 1) .. "1" .. g.played:sub(card + 1)
    if H.suit(card) == H.HEARTS then g.broken = true end
    if #g.trick < 4 then
        g.turn = seat % 4 + 1
        return true
    end
    local winner = H.trickWinner(g.trick)
    for _, t in ipairs(g.trick) do g.taken[winner] = g.taken[winner] + H.points(t.card) end
    g.lastTrick = { cards = g.trick, winner = winner }
    g.trick = {}
    g.tricks = g.tricks + 1
    g.turn, g.leader = winner, winner
    if g.tricks == 13 then H.scoreHand(g) end
    return true
end

function H.scoreHand(g)
    local moon = nil
    for s = 1, 4 do
        if g.taken[s] == 26 then moon = s end
    end
    g.handScores = {}
    for s = 1, 4 do
        local add = g.taken[s]
        if moon then add = (s == moon) and 0 or 26 end
        g.handScores[s] = add
        g.scores[s] = g.scores[s] + add
    end
    g.moon = moon
    g.phase = "scored"
    for s = 1, 4 do
        if g.scores[s] >= H.END_SCORE then g.over = true end
    end
end

-- The seats with the lowest score (more than one on a tie).
function H.leaders(g)
    local best, out = nil, {}
    for s = 1, 4 do
        if not best or g.scores[s] < best then
            best, out = g.scores[s], { s }
        elseif g.scores[s] == best then
            out[#out + 1] = s
        end
    end
    return out
end

function H.nextHand(g, rand)
    if g.over or g.phase ~= "scored" then return false end
    g.hand = g.hand + 1
    H.deal(g, rand)
    return true
end

return H
