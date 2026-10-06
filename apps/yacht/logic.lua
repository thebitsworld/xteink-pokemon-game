-- Yacht dice rules (the scorecard game of five dice, three rolls and thirteen
-- boxes). No drawing, so it can be tested on a computer
-- (test/lua_apps/yacht_test.lua). The computer opponent is in ai.lua, loaded
-- only for games against the device to keep the others within the X3's
-- memory.

local Y = {}

Y.DICE = 5
Y.ROLLS = 3
Y.UNSCORED = -1

-- Box ids 1..13: the upper section (1..6 = Ones..Sixes), then the lower one.
Y.THREE_KIND, Y.FOUR_KIND, Y.FULL_HOUSE, Y.SMALL_STRAIGHT, Y.LARGE_STRAIGHT, Y.YACHT, Y.CHANCE =
    7, 8, 9, 10, 11, 12, 13
Y.BOXES = 13
Y.BOX_NAMES = { "Ones", "Twos", "Threes", "Fours", "Fives", "Sixes", "Three of a kind", "Four of a kind",
                "Full house", "Small straight", "Large straight", "Yacht", "Chance" }
Y.UPPER_BONUS_AT, Y.UPPER_BONUS = 63, 35
Y.YACHT_BONUS = 100

local function counts(dice)
    local c = { 0, 0, 0, 0, 0, 0 }
    for _, v in ipairs(dice) do c[v] = c[v] + 1 end
    return c
end

local function sum(dice)
    local s = 0
    for _, v in ipairs(dice) do s = s + v end
    return s
end

local function longestRun(c)
    local best, run = 0, 0
    for v = 1, 6 do
        if c[v] > 0 then run = run + 1 else run = 0 end
        if run > best then best = run end
    end
    return best
end

function Y.isYacht(dice)
    local c = counts(dice)
    for v = 1, 6 do
        if c[v] == 5 then return true end
    end
    return false
end

-- What dice with face counts `c` (c[v] = how many show v) and pip total
-- `total` score in `box` by the plain rules (no joker).
local function scoreCounts(c, total, box)
    if box <= 6 then return c[box] * box end
    local most, pair, three = 0, false, false
    for v = 1, 6 do
        if c[v] > most then most = c[v] end
        if c[v] == 2 then pair = true end
        if c[v] == 3 then three = true end
    end
    if box == Y.THREE_KIND then return most >= 3 and total or 0 end
    if box == Y.FOUR_KIND then return most >= 4 and total or 0 end
    -- Five of a kind is three of one and two of another, so it is a full house.
    if box == Y.FULL_HOUSE then return ((three and pair) or most == 5) and 25 or 0 end
    if box == Y.SMALL_STRAIGHT then return longestRun(c) >= 4 and 30 or 0 end
    if box == Y.LARGE_STRAIGHT then return longestRun(c) >= 5 and 40 or 0 end
    if box == Y.YACHT then return most == 5 and 50 or 0 end
    return total -- chance
end

function Y.rawScore(dice, box) return scoreCounts(counts(dice), sum(dice), box) end

-- For ai.lua, which scores from face counts.
Y.scoreCounts, Y.counts, Y.sum = scoreCounts, counts, sum

-- Joker rules: a five of a kind rolled when the Yacht box is already filled
-- (with 50 or with 0).
function Y.jokerApplies(card, dice)
    return Y.isYacht(dice) and card[Y.YACHT] ~= Y.UNSCORED
end

-- A 100 bonus for every further five of a kind, only if the Yacht box holds 50.
function Y.yachtBonusDue(card, dice)
    return Y.isYacht(dice) and card[Y.YACHT] == 50
end

-- Whether `box` may be taken with these dice. Under the joker the matching
-- upper box must be used if free; otherwise any free lower box; only when the
-- whole lower section is full may another upper box take a zero.
function Y.canTake(card, dice, box)
    if box < 1 or box > Y.BOXES or card[box] ~= Y.UNSCORED then return false end
    if not Y.jokerApplies(card, dice) then return true end
    local face = dice[1]
    if card[face] == Y.UNSCORED then return box == face end
    if box > 6 then return true end
    for b = 7, Y.BOXES do
        if card[b] == Y.UNSCORED then return false end
    end
    return true
end

-- What taking `box` scores (the joker pays straights and full house in full).
function Y.boxScore(card, dice, box)
    if Y.jokerApplies(card, dice) and box > 6 then
        if box == Y.FULL_HOUSE then return 25 end
        if box == Y.SMALL_STRAIGHT then return 30 end
        if box == Y.LARGE_STRAIGHT then return 40 end
    end
    return Y.rawScore(dice, box)
end

function Y.newCard()
    local card = { yachtBonus = 0 }
    for b = 1, Y.BOXES do card[b] = Y.UNSCORED end
    return card
end

function Y.upperTotal(card)
    local t = 0
    for b = 1, 6 do
        if card[b] > 0 then t = t + card[b] end
    end
    return t
end

function Y.total(card)
    local t = card.yachtBonus
    for b = 1, Y.BOXES do
        if card[b] > 0 then t = t + card[b] end
    end
    if Y.upperTotal(card) >= Y.UPPER_BONUS_AT then t = t + Y.UPPER_BONUS end
    return t
end

function Y.cardFull(card)
    for b = 1, Y.BOXES do
        if card[b] == Y.UNSCORED then return false end
    end
    return true
end

-- Game ------------------------------------------------------------------------

-- `players` is 1 or 2.
function Y.new(players)
    local g = { players = players, cards = {}, turn = 1, dice = { 1, 1, 1, 1, 1 },
                held = { false, false, false, false, false }, rolls = 0, over = false }
    for p = 1, players do g.cards[p] = Y.newCard() end
    return g
end

function Y.canRoll(g) return not g.over and g.rolls < Y.ROLLS end

-- Rolls every die not held (all of them on the first roll of a turn).
function Y.roll(g, rand)
    if not Y.canRoll(g) then return false end
    for i = 1, Y.DICE do
        if g.rolls == 0 or not g.held[i] then g.dice[i] = rand(6) end
    end
    g.rolls = g.rolls + 1
    return true
end

function Y.canHold(g) return not g.over and g.rolls > 0 and g.rolls < Y.ROLLS end

function Y.toggleHold(g, i)
    if not Y.canHold(g) then return false end
    g.held[i] = not g.held[i]
    return true
end

-- Scores the dice in `box` for the player to move and passes the turn.
-- Returns the points written, or nil if the box cannot be taken now.
function Y.take(g, box)
    if g.over or g.rolls == 0 then return nil end
    local card = g.cards[g.turn]
    if not Y.canTake(card, g.dice, box) then return nil end
    if Y.yachtBonusDue(card, g.dice) then card.yachtBonus = card.yachtBonus + Y.YACHT_BONUS end
    local points = Y.boxScore(card, g.dice, box)
    card[box] = points
    g.rolls = 0
    g.held = { false, false, false, false, false }
    if g.turn < g.players then
        g.turn = g.turn + 1
    else
        g.turn = 1
        if Y.cardFull(g.cards[1]) then g.over = true end
    end
    return points
end

-- 1 or 2 for a two-player winner, -1 for a tie, 0 while playing or solo.
function Y.winner(g)
    if not g.over or g.players < 2 then return 0 end
    local a, b = Y.total(g.cards[1]), Y.total(g.cards[2])
    if a > b then return 1 elseif b > a then return 2 end
    return -1
end

-- Save/restore ---------------------------------------------------------------

function Y.serialize(g)
    local parts = { tostring(g.players), tostring(g.turn), tostring(g.rolls), g.over and "1" or "0",
                    table.concat(g.dice, ".") }
    local held = {}
    for i = 1, Y.DICE do held[i] = g.held[i] and "1" or "0" end
    parts[#parts + 1] = table.concat(held)
    for p = 1, g.players do
        local card = g.cards[p]
        local boxes = {}
        for b = 1, Y.BOXES do boxes[b] = tostring(card[b]) end
        parts[#parts + 1] = card.yachtBonus .. ":" .. table.concat(boxes, ".")
    end
    return table.concat(parts, "|")
end

function Y.deserialize(s)
    local fields = {}
    for f in (s .. "|"):gmatch("([^|]*)|") do fields[#fields + 1] = f end
    local players = tonumber(fields[1])
    if (players ~= 1 and players ~= 2) or #fields ~= 6 + players then return nil end
    local g = Y.new(players)
    g.turn, g.rolls, g.over = tonumber(fields[2]), tonumber(fields[3]), fields[4] == "1"
    if not g.turn or g.turn < 1 or g.turn > players or not g.rolls or g.rolls < 0 or g.rolls > Y.ROLLS then
        return nil
    end
    local dice = {}
    for n in fields[5]:gmatch("%d+") do dice[#dice + 1] = tonumber(n) end
    if #dice ~= Y.DICE then return nil end
    for i = 1, Y.DICE do
        if dice[i] < 1 or dice[i] > 6 then return nil end
    end
    g.dice = dice
    if #fields[6] ~= Y.DICE then return nil end
    for i = 1, Y.DICE do g.held[i] = fields[6]:sub(i, i) == "1" end
    for p = 1, players do
        local bonus, boxes = fields[6 + p]:match("^(%d+):(.*)$")
        if not bonus then return nil end
        local card = Y.newCard()
        card.yachtBonus = tonumber(bonus)
        local b = 0
        for n in (boxes .. "."):gmatch("(-?%d+)%.") do
            b = b + 1
            if b > Y.BOXES then return nil end
            card[b] = tonumber(n)
        end
        if b ~= Y.BOXES then return nil end
        g.cards[p] = card
    end
    return g
end

return Y
