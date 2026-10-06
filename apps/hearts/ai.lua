-- Hearts computer players. Usage: local AI = smudge.dofile("ai.lua")(H),
-- with H the rules from logic.lua.
--
-- Each decision sees only what that seat could see at a real table: its own
-- hand, the trick on the table, the cards already played and the points each
-- seat has taken. Two levels: 1 follows suit with its lowest card and dumps
-- its highest when it cannot; 2 ducks under the trick, sheds dangerous cards,
-- passes with a plan and stops a player who is shooting the moon.

return function(H)
local AI = {}

local function countSuit(hand, suit)
    local n = 0
    for _, c in ipairs(hand) do
        if H.suit(c) == suit then n = n + 1 end
    end
    return n
end

local function queenOut(g) return H.wasPlayed(g, H.QUEEN_OF_SPADES) end

-- How much a card is worth getting rid of.
local function danger(g, hand, card)
    local suit, rank = H.suit(card), H.rank(card)
    local spades = countSuit(hand, H.SPADES)
    if card == H.QUEEN_OF_SPADES then return 100 end
    -- The ace and king of spades catch the queen, unless guarded by plenty
    -- of low spades.
    if suit == H.SPADES and rank > 12 and not queenOut(g) and spades < 5 then return 60 + rank end
    local d = rank
    if suit == H.HEARTS then d = d + 6 end
    -- Short suits are worth emptying: a void lets you shed later.
    local n = countSuit(hand, suit)
    if suit ~= H.SPADES and n <= 3 then d = d + (4 - n) * 3 end
    return d
end

-- The three cards to pass.
function AI.choosePass(g, seat, level, rand)
    local hand = g.hands[seat]
    local order = {}
    for k, c in ipairs(hand) do order[k] = c end
    if level <= 1 then
        table.sort(order, function(a, b) return H.rank(a) > H.rank(b) end)
    else
        table.sort(order, function(a, b) return danger(g, hand, a) > danger(g, hand, b) end)
    end
    return { order[1], order[2], order[3] }
end

-- A seat that has taken every point so far and enough of them to be going
-- for the moon.
local function shooter(g)
    local total, who = 0, nil
    for s = 1, 4 do
        if g.taken[s] > 0 then
            total = total + g.taken[s]
            who = who and -1 or s
        end
    end
    if who and who > 0 and total >= 8 then return who end
    return nil
end

local function trickPoints(trick)
    local p = 0
    for _, t in ipairs(trick) do p = p + H.points(t.card) end
    return p
end

local function highest(cards)
    local best = cards[1]
    for _, c in ipairs(cards) do
        if H.rank(c) > H.rank(best) then best = c end
    end
    return best
end

local function lowest(cards)
    local best = cards[1]
    for _, c in ipairs(cards) do
        if H.rank(c) < H.rank(best) then best = c end
    end
    return best
end

-- The card for the seat to move.
function AI.choosePlay(g, level, rand)
    local seat = g.turn
    local hand = g.hands[seat]
    local legal = H.legal(g, seat)
    if #legal == 1 then return legal[1] end
    local trick = g.trick

    if #trick == 0 then
        -- Leading.
        if level <= 1 then return lowest(legal) end
        local holdsQueen = false
        for _, c in ipairs(hand) do
            if c == H.QUEEN_OF_SPADES then holdsQueen = true end
        end
        local best, bestScore = nil, nil
        for _, c in ipairs(legal) do
            local suit, rank = H.suit(c), H.rank(c)
            local s = rank
            if suit == H.HEARTS then s = s + 8 end
            -- Never lead a spade that could catch the queen; low spades are
            -- good leads to flush it out when you do not hold it.
            if suit == H.SPADES and not queenOut(g) then
                if c == H.QUEEN_OF_SPADES or rank > 12 then s = s + 40
                elseif not holdsQueen and rank < 12 then s = s - 3 end
            end
            -- Lead from a short suit to make a void.
            s = s - (4 - math.min(4, countSuit(hand, suit)))
            if not bestScore or s < bestScore then best, bestScore = c, s end
        end
        return best
    end

    local led = H.suit(trick[1].card)
    local following = H.suit(legal[1]) == led
    if not following then
        -- Void in the suit led: shed the most dangerous card.
        if level <= 1 then return highest(legal) end
        local best, bestDanger = nil, nil
        for _, c in ipairs(legal) do
            local d = danger(g, hand, c)
            if not bestDanger or d > bestDanger then best, bestDanger = c, d end
        end
        -- Do not hand the shooter the points they need.
        local moon = shooter(g)
        if moon and H.trickWinner(trick) == moon then
            local safe = {}
            for _, c in ipairs(legal) do
                if H.points(c) == 0 then safe[#safe + 1] = c end
            end
            if #safe > 0 then return highest(safe) end
        end
        return best
    end

    if level <= 1 then return lowest(legal) end
    -- Following suit: the card currently winning.
    local top = 0
    for _, t in ipairs(trick) do
        if H.suit(t.card) == led and t.card > top then top = t.card end
    end
    local under, over = {}, {}
    for _, c in ipairs(legal) do
        if c < top then under[#under + 1] = c else over[#over + 1] = c end
    end
    local points = trickPoints(trick)
    local moon = shooter(g)
    -- Stop a moon: take a point trick from the shooter with the lowest card
    -- that wins it.
    if moon and moon ~= seat and H.trickWinner(trick) == moon and points > 0 and #over > 0 then
        return lowest(over)
    end
    -- Last to play on a trick with no points: win it with the highest card
    -- (getting rid of it), but never with the queen.
    if #trick == 3 and points == 0 then
        local safe = {}
        for _, c in ipairs(legal) do
            if c ~= H.QUEEN_OF_SPADES then safe[#safe + 1] = c end
        end
        if #safe > 0 then return highest(safe) end
    end
    -- Duck with the highest card that still loses the trick.
    if #under > 0 then return highest(under) end
    -- Must win it: if last, win with the highest; otherwise the lowest, and
    -- never the queen of spades if another spade will do.
    if #trick == 3 then return highest(over) end
    local noQueen = {}
    for _, c in ipairs(over) do
        if c ~= H.QUEEN_OF_SPADES then noQueen[#noQueen + 1] = c end
    end
    return lowest(#noQueen > 0 and noQueen or over)
end

return AI
end
