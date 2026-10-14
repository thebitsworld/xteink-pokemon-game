-- Sam Loc (Sâm lốc) rules: the cards and the combinations. Dealing, what
-- may be played now and making a move are in game.lua, the computer players
-- in ai.lua. No drawing, so it can be tested on a computer
-- (test/lua_apps/samloc_test.lua).
--
-- Four players, 10 cards each (12 stay undealt). Ranks run 3 4 5 6 7 8 9 10
-- J Q K A 2 (the 2 is the highest) and suits do not count: cards of one rank
-- are equal. A play is a single, a pair, a triple, four of a kind or a
-- straight of three or more ranks: 3 up to A, or a low straight A-2-3(-4..)
-- with the A and the 2 under the 3. No other straight holds a 2, and there is
-- no wrapping (K-A-2 and 2-3-4 are not straights). A play beats the table
-- with the same kind and size and a higher top rank; an equal rank does not
-- beat. Four of a kind also beats a single 2 (chặt heo). Passing locks a
-- player out until everyone else has passed; the last player to play then
-- leads anything. The first player to shed every card wins the hand.
-- Play goes to the right: You, East, North, West.
--
-- At the deal a player may declare Sam (báo Sâm): they lead and must shed
-- every card with nobody beating any of their plays. A player down to one
-- card is "on one" (báo 1): whoever plays just before them may play a single
-- only if it is their highest card. Instant wins (tới trắng) and points are
-- in game.lua.

local T = {}

T.SINGLE, T.PAIR, T.TRIPLE, T.QUAD, T.STRAIGHT = 1, 2, 3, 4, 5
T.ACE, T.TWO = 12, 13 -- the ranks of the aces and the 2s

-- A card is 0..51: rank = card // 4 + 1 (1 = the 3 .. 12 = the ace, 13 =
-- the 2), suit = card % 4 (0 spades, 1 clubs, 2 diamonds, 3 hearts; drawn
-- only, and the colour of an instant win).
function T.rank(c) return c // 4 + 1 end
T.RANK_NAMES = { "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K", "A", "2" }
function T.rankName(c) return T.RANK_NAMES[c // 4 + 1] end

local function sorted(cards)
    local s = {}
    for i, c in ipairs(cards) do s[i] = c end
    table.sort(s)
    return s
end
T.sorted = sorted

-- The kind of a set of cards: kind, size (cards) and top rank; nil if it is
-- not a combination. The top of a low straight A-2-3..x is the rank of x.
function T.classify(cards)
    local n = #cards
    if n == 0 then return nil end
    local s = sorted(cards)
    local r1, rn = s[1] // 4 + 1, s[n] // 4 + 1
    if r1 == rn then
        if n <= 4 then return n, n, rn end
        return nil
    end
    if n < 3 then return nil end
    if rn == T.TWO then
        -- Only a low straight: 3 4 .. then the A and the 2.
        if s[n - 1] // 4 + 1 ~= T.ACE then return nil end
        for i = 1, n - 2 do
            if s[i] // 4 + 1 ~= i then return nil end
        end
        return T.STRAIGHT, n, n - 2
    end
    for i = 2, n do
        if s[i] // 4 + 1 ~= r1 + i - 1 then return nil end
    end
    return T.STRAIGHT, n, rn
end

-- Whether a play (kind k, size l, top rank t) beats the table (ok, ol, ot;
-- ok nil when the player leads).
function T.beats(k, l, t, ok, ol, ot)
    if not ok then return true end
    if k == ok and l == ol then return t > ot end
    return k == T.QUAD and ok == T.SINGLE and ot == T.TWO
end

-- A low straight's cards in playing order (the A and the 2 first).
function T.inOrder(cards)
    local s = sorted(cards)
    local n = #s
    if n >= 3 and s[n] // 4 + 1 == T.TWO and s[1] // 4 + 1 ~= T.TWO then
        table.insert(s, 1, table.remove(s))
        table.insert(s, 1, table.remove(s))
    end
    return s
end

-- The player who answers `seat` next (still in the round), or nil.
function T.nextSeat(g, seat)
    for k = 1, 3 do
        local s = (seat + k - 1) % 4 + 1
        if not g.passed[s] then return s end
    end
    return nil
end

-- Báo 1: whether a single from `seat` must be its highest card now.
function T.mustTop(g, seat)
    local s = T.nextSeat(g, seat)
    return s ~= nil and #g.hands[s] == 1
end

return T
