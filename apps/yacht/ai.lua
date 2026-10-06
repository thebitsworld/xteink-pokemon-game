-- Yacht computer opponent. Usage: local AI = smudge.dofile("ai.lua")(Y), with
-- Y the rules from logic.lua.
--
-- An exact one-roll expectation: for every set of dice it could keep, it
-- averages the best box over every outcome of re-rolling the rest. Dice are
-- unordered, so the outcomes are enumerated as multisets with their weights
-- (252 instead of 7776 for five dice), one at a time with nothing stored.
-- It is only handed the card and the dice, never the game, so it cannot see
-- the dice it is about to roll.

return function(Y)
local AI = {}

-- About what each box is worth when filled later in a game; taking a box
-- costs that much, so a 9 in Chance on turn one does not look like 9 points.
local BOX_WORTH = { 2.1, 5.3, 8.6, 12.2, 15.7, 19.2, 21.7, 13.1, 22.6, 29.5, 32.7, 16.9, 22.0 }

local FACT = { [0] = 1, 1, 2, 6, 24, 120 }

-- Worth of scoring dice with counts `c` and total `total` in the best free box
-- now: score minus the box's usual worth, plus credit for progress towards the
-- upper bonus. Works on counts so the search allocates nothing per outcome.
local function bestBox(card, c, total)
    local face = nil
    for v = 1, 6 do
        if c[v] == 5 then face = v end
    end
    local yachtFilled = card[Y.YACHT] ~= Y.UNSCORED
    local joker = face ~= nil and yachtFilled
    local bonus = (face ~= nil and card[Y.YACHT] == 50) and Y.YACHT_BONUS or 0
    local lowerOpen = false
    for b = 7, Y.BOXES do
        if card[b] == Y.UNSCORED then lowerOpen = true end
    end
    local upper = Y.upperTotal(card)
    local best, bestBoxId = nil, nil
    for b = 1, Y.BOXES do
        local allowed = card[b] == Y.UNSCORED
        if allowed and joker then
            -- Same order as Y.canTake: the matching upper box, then any lower box.
            if card[face] == Y.UNSCORED then
                allowed = b == face
            elseif b <= 6 then
                allowed = not lowerOpen
            end
        end
        if allowed then
            local s
            if joker and b == Y.FULL_HOUSE then s = 25
            elseif joker and b == Y.SMALL_STRAIGHT then s = 30
            elseif joker and b == Y.LARGE_STRAIGHT then s = 40
            else s = Y.scoreCounts(c, total, b) end
            local v = s - BOX_WORTH[b] + bonus
            if b <= 6 and upper < Y.UPPER_BONUS_AT then
                -- Three of a face is par for the bonus; above par helps, below hurts.
                v = v + (s - 3 * b) * 0.6
                if upper + s >= Y.UPPER_BONUS_AT then v = v + Y.UPPER_BONUS * 0.8 end
            end
            if not best or v > best then best, bestBoxId = v, b end
        end
    end
    return best, bestBoxId
end

-- Expected best-box worth of keeping the dice counted in `c` (with pip total
-- `total`) and re-rolling `n` dice once. Each multiset of re-rolled values is
-- visited once with its probability, adding to and then restoring `c`.
local function keepValue(card, c, total, n)
    local added = { 0, 0, 0, 0, 0, 0 }
    local denom = 6 ^ n
    local value = 0
    local function rec(start, left, sumAdded)
        if left == 0 then
            local ways = FACT[n]
            for v = 1, 6 do ways = ways / FACT[added[v]] end
            value = value + (ways / denom) * (bestBox(card, c, total + sumAdded))
            return
        end
        for v = start, 6 do
            c[v] = c[v] + 1
            added[v] = added[v] + 1
            rec(v, left - 1, sumAdded + v)
            added[v] = added[v] - 1
            c[v] = c[v] - 1
        end
    end
    rec(1, n, 0)
    return value
end

-- Which dice to hold before the next roll: a list of five booleans.
-- `rollsLeft` is 1 or 2; with two rolls left it looks one roll ahead only.
function AI.chooseHold(card, dice, rollsLeft)
    local bestValue, bestMask, seen = nil, 0, {}
    local c = { 0, 0, 0, 0, 0, 0 }
    for mask = 0, 31 do
        for v = 1, 6 do c[v] = 0 end
        local kept, total, key = 0, 0, 0
        for i = 1, Y.DICE do
            if mask & (1 << (i - 1)) ~= 0 then
                local v = dice[i]
                c[v] = c[v] + 1
                kept = kept + 1
                total = total + v
            end
        end
        for v = 1, 6 do key = key * 6 + c[v] end
        if not seen[key] then
            seen[key] = true
            local value
            if kept == Y.DICE then
                value = (bestBox(card, c, total))
            else
                value = keepValue(card, c, total, Y.DICE - kept)
            end
            -- With a roll still to come after this one, keeping fewer dice
            -- is worth a little more than the one-roll value suggests.
            if rollsLeft > 1 then value = value + (Y.DICE - kept) * 0.15 end
            if not bestValue or value > bestValue then bestValue, bestMask = value, mask end
        end
    end
    local held = {}
    for i = 1, Y.DICE do held[i] = bestMask & (1 << (i - 1)) ~= 0 end
    return held
end

-- Which box to fill with these dice.
function AI.chooseBox(card, dice)
    local _, box = bestBox(card, Y.counts(dice), Y.sum(dice))
    return box
end

return AI
end
