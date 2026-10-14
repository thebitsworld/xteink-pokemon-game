-- Tien Len (Tiến lên miền Nam) rules: the cards and the combinations.
-- Dealing, what may be played now and making a move are in game.lua, the computer
-- players in ai.lua. No drawing, so it can be tested on a computer
-- (test/lua_apps/tienlen_test.lua).
--
-- Four players, 13 cards each. Ranks run 3 4 5 6 7 8 9 10 J Q K A 2 (the 2
-- is the highest) and suits spades < clubs < diamonds < hearts. A play is a
-- single, a pair, a triple, four of a kind, a straight of three or more
-- ranks (no 2 in it) or three or more consecutive pairs. It beats the table
-- with the same kind and size and a higher top card (suit decides between
-- equal ranks). Chopping: three consecutive pairs beat a single 2; four of a
-- kind beats a single 2, a pair of 2s and three consecutive pairs; four (or
-- more) consecutive pairs beat a single 2, a pair of 2s, four of a kind and
-- shorter runs of pairs. Passing locks a player out until everyone else has
-- passed; the last player to play then leads anything (if they have just
-- gone out, the next player still in leads). Play goes to the right
-- (counter-clockwise): You, East, North, West.
--
-- The first hand of a session is led by the holder of the 3 of spades, who
-- must play it in that first combination; later hands are led by the last
-- winner. Instant wins (tới trắng) are checked at the deal: four 2s, a
-- dragon (one card of each rank 3 to A) or six pairs.

local T = {}

T.SINGLE, T.PAIR, T.TRIPLE, T.QUAD, T.STRAIGHT, T.PAIRS = 1, 2, 3, 4, 5, 6
T.TWO = 13           -- the rank of the 2s
T.THREE_SPADES = 0

-- A card is 0..51: rank = card // 4 + 1 (1 = the 3 .. 12 = the ace, 13 =
-- the 2), suit = card % 4 (0 spades, 1 clubs, 2 diamonds, 3 hearts), so a
-- higher number is always a higher card.
function T.rank(c) return c // 4 + 1 end
function T.suit(c) return c % 4 end
T.RANK_NAMES = { "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K", "A", "2" }
function T.rankName(c) return T.RANK_NAMES[c // 4 + 1] end

local function sorted(cards)
    local s = {}
    for i, c in ipairs(cards) do s[i] = c end
    table.sort(s)
    return s
end
T.sorted = sorted

-- The kind of a set of cards: kind, size (cards, or pairs for T.PAIRS) and
-- top card; nil if it is not a combination.
function T.classify(cards)
    local n = #cards
    if n == 0 then return nil end
    local s = sorted(cards)
    local top = s[n]
    local r1, rn = s[1] // 4 + 1, top // 4 + 1
    if r1 == rn then
        if n <= 4 then return n, n, top end
        return nil
    end
    if rn == T.TWO then return nil end
    if n >= 3 and rn - r1 == n - 1 then
        for i = 2, n do
            if s[i] // 4 + 1 ~= r1 + i - 1 then return nil end
        end
        return T.STRAIGHT, n, top
    end
    if n >= 6 and n % 2 == 0 and rn - r1 == n // 2 - 1 then
        for i = 1, n // 2 do
            local r = r1 + i - 1
            if s[2 * i - 1] // 4 + 1 ~= r or s[2 * i] // 4 + 1 ~= r then return nil end
        end
        return T.PAIRS, n // 2, top
    end
    return nil
end

-- Whether a play (kind k, size l, top t) beats the table (ok, ol, ot; ok
-- nil when the player leads).
function T.beats(k, l, t, ok, ol, ot)
    if not ok then return true end
    if k == ok and l == ol then return t > ot end
    local two = ot // 4 + 1 == T.TWO
    if ok == T.SINGLE and two then return k == T.QUAD or (k == T.PAIRS and l >= 3) end
    if ok == T.PAIR and two then return k == T.QUAD or (k == T.PAIRS and l >= 4) end
    if ok == T.PAIRS then return (k == T.QUAD and ol == 3) or (k == T.PAIRS and l >= 4 and l > ol) end
    if ok == T.QUAD then return k == T.PAIRS and l >= 4 end
    return false
end

-- Whether a play that beats the table is a chop (a different kind).
function T.isChop(k, l, ok, ol) return ok ~= nil and (k ~= ok or l ~= ol) end

return T
