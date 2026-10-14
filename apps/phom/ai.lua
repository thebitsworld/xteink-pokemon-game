-- Phom computer players. Usage:
--   local AI = smudge.dofile("ai.lua")(T)
--   local eat = AI.take(g, seat, level)           -- eat the card just thrown?
--   local card = AI.discard(g, seat, level, rand) -- the card to throw
-- with T the rules (logic.lua) and rand(n) 1..n. Loaded for one decision
-- and dropped after.
--
-- Rule based. A player eats whenever the card makes a meld. To throw, it
-- tries each card it may throw and scores what is left: the points outside
-- the best melds, less half the points of the cards left over that still
-- have a partner (two of a rank, two of a suit one or two apart) while it
-- has cards to take, and a fine for no meld at all. Level 1 (Normal) counts
-- every partner, even one whose missing cards are all gone, and is a few
-- points careless in its choice. Level 2 (Hard) tracks the cards it has
-- seen (its hand, every card thrown, what the others ate), so it drops
-- pairs and near runs that can no longer be completed, and on its last
-- throw it avoids feeding the next player: a card that many unseen pairs
-- could turn into a meld is risky, less so when that player has thrown
-- cards like it. (An eaten card shows nothing the next player could still
-- eat: it must stay in a meld of its own.) Tried and dropped: weighing the
-- unseen cards and the takes left, a one-card lookahead, and avoiding
-- feeding in every round - none beat these rules in AI-vs-AI hands.

return function(T)
local A = {}

local ROUNDS = 4

-- The cards `seat` knows are out of reach: its hand, everything thrown,
-- the cards others ate.
local function seenSet(g, seat)
    local seen, n = {}, 0
    local function mark(c)
        if not seen[c] then seen[c], n = true, n + 1 end
    end
    for _, c in ipairs(g.hands[seat]) do mark(c) end
    for s = 1, 4 do
        for _, c in ipairs(g.disc[s]) do mark(c) end
        if s ~= seat then
            for _, c in ipairs(g.eaten[s]) do mark(c) end
        end
    end
    return seen, 52 - n
end

local function live(seen, c)
    return c >= 0 and c < 52 and not seen[c]
end

-- The unseen cards that would make a meld of a and b (same rank, or one
-- suit one or two apart), or -1 when they are no pair or near run.
local function outs(seen, a, b)
    if a > b then a, b = b, a end
    local ra, rb = a // 4, b // 4
    if ra == rb then
        local k = 0
        for s = 0, 3 do
            local c = ra * 4 + s
            if c ~= a and c ~= b and not seen[c] then k = k + 1 end
        end
        return k
    end
    if a % 4 ~= b % 4 then return -1 end
    local k = 0
    if rb - ra == 1 then
        if ra > 0 and live(seen, a - 4) then k = k + 1 end
        if rb < 12 and live(seen, b + 4) then k = k + 1 end
        return k
    end
    if rb - ra == 2 then return live(seen, a + 4) and 1 or 0 end
    return -1
end

-- What the cards left over may still save.
local function bonus(rest, seen, takes, level)
    if takes <= 0 then return 0 end
    local total = 0
    for i, a in ipairs(rest) do
        local p = 0
        for j, b in ipairs(rest) do
            if i ~= j then
                local o = outs(seen, a, b)
                if o > 0 or (o == 0 and level < 2) then p = 0.5 end
            end
        end
        total = total + p * (a // 4 + 1)
    end
    return total
end

-- How likely the next player can eat `c` (Hard), as a cost.
local function danger(g, seat, c, seen, unseen)
    local nxt = seat % 4 + 1
    if g.nd[nxt] >= ROUNDS then return 0 end
    local r, k = c // 4, 0
    for s = 0, 3 do
        local x = r * 4 + s
        if x ~= c and not seen[x] then k = k + 1 end
    end
    local pairs = k * (k - 1) // 2
    if live(seen, c - 8) and live(seen, c - 4) and r >= 2 then pairs = pairs + 1 end
    if r >= 1 and r <= 11 and live(seen, c - 4) and live(seen, c + 4) then pairs = pairs + 1 end
    if r <= 10 and live(seen, c + 4) and live(seen, c + 8) then pairs = pairs + 1 end
    local f = #g.hands[nxt] / math.max(1, unseen)
    local p = math.min(1, pairs * f * f)
    for _, d in ipairs(g.disc[nxt]) do
        if d // 4 == r or (d % 4 == c % 4 and math.abs(d // 4 - r) <= 2) then
            p = p / 2
            break
        end
    end
    return p * (10 + 2 * (r + 1))
end

-- Whether to eat the card just thrown: always, when it makes a meld.
function A.take(g, seat, level)
    return g.top ~= nil and T.canEat(g.hands[seat], g.eaten[seat], g.top)
end

function A.discard(g, seat, level, rand)
    local hand, eaten = g.hands[seat], g.eaten[seat]
    local seen, unseen = seenSet(g, seat)
    local takes = ROUNDS - g.nd[seat] - 1
    local bestC, bestV = nil, nil
    for _, c in ipairs(hand) do
        if not T.has(eaten, c) then
            local left, melds, rest = T.split(T.minus(hand, c), eaten)
            if left then
                local v = left - bonus(rest, seen, takes, level) - c / 1000
                if #melds == 0 then v = v + 15 end
                if level >= 2 then
                    if takes == 0 then v = v + danger(g, seat, c, seen, unseen) end
                else
                    v = v + rand(7) - 1
                end
                if not bestV or v < bestV then bestC, bestV = c, v end
            end
        end
    end
    return bestC
end

return A
end
