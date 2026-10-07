-- Tide & Paper: the rules. No drawing, so it can be tested on a computer
-- (test/lua_apps/tide_test.lua).
--
-- A set-collecting card game for two (you and the device), first to 40.
-- Each turn: draw two cards from the deck and keep one (the other goes face
-- up on a discard pile), or take the top card of a discard pile. Then play
-- any pairs you like - each pair scores a point and does something - and,
-- with 7 or more points in your cards, you may end the round:
--   Stop        - the round ends and both score their cards;
--   Last chance - the other player has one more turn; then if you have more
--                 points than them you score your cards and your mark
--                 bonus and they score only their mark bonus, otherwise you
--                 score only your mark bonus and they score their cards.
-- Every card but a mermaid carries one of six marks; the mark bonus is how
-- many cards you have of your most common mark. Four mermaids win at once.

local T = {}

T.TARGET = 40
T.END_POINTS = 7

-- Kinds of card.
T.CRAB, T.BOAT, T.FISH, T.SWIMMER, T.SHARK = 1, 2, 3, 4, 5        -- pairs
T.SHELL, T.OCTOPUS, T.PENGUIN, T.SAILOR = 6, 7, 8, 9              -- collections
T.BEACON, T.SHOAL, T.COLONY, T.CAPTAIN = 10, 11, 12, 13           -- bonuses
T.MERMAID = 14
T.NAMES = { "Crab", "Boat", "Fish", "Swimmer", "Shark", "Shell", "Octopus", "Penguin", "Sailor",
            "Beacon", "Shoal", "Colony", "Captain", "Mermaid" }
local COUNTS = { 9, 8, 7, 5, 5, 6, 5, 3, 2, 1, 1, 1, 1, 4 }
T.MARKS = 6

-- Card ids 1..58; KIND[id] and MARK[id] (0 for mermaids).
local KIND, MARK = {}, {}
do
    local id = 0
    for kind, count in ipairs(COUNTS) do
        for j = 1, count do
            id = id + 1
            KIND[id] = kind
            MARK[id] = kind == T.MERMAID and 0 or (j + kind) % T.MARKS + 1
        end
    end
    T.DECK_SIZE = id
end
T.KIND, T.MARK = KIND, MARK

function T.isDuo(kind) return kind <= T.SHARK end

-- Whether cards a and b (ids) form a pair that can be played.
function T.isPair(a, b)
    local ka, kb = KIND[a], KIND[b]
    if ka > kb then ka, kb = kb, ka end
    return (ka == kb and ka <= T.FISH) or (ka == T.SWIMMER and kb == T.SHARK)
end

-- Scoring ---------------------------------------------------------------------

local SHELLS = { [0] = 0, 0, 2, 4, 6, 8, 10 }
local OCTOPI = { [0] = 0, 0, 3, 6, 9, 12 }
local PENGUINS = { [0] = 0, 1, 3, 5 }
local SAILORS = { [0] = 0, 0, 5 }

local counts, marks = {}, {}

-- Fills `counts` (per kind) and `marks` (per mark) for player p's cards.
local function tally(g, p)
    for k = 1, #COUNTS do counts[k] = 0 end
    for m = 1, T.MARKS do marks[m] = 0 end
    for _, list in ipairs({ g.hands[p], g.played[p] }) do
        for _, id in ipairs(list) do
            counts[KIND[id]] = counts[KIND[id]] + 1
            if MARK[id] > 0 then marks[MARK[id]] = marks[MARK[id]] + 1 end
        end
    end
end

-- Points from player p's cards in hand and played (no mark bonus).
function T.cardPoints(g, p)
    tally(g, p)
    local pts = counts[T.CRAB] // 2 + counts[T.BOAT] // 2 + counts[T.FISH] // 2 +
                math.min(counts[T.SWIMMER], counts[T.SHARK])
    pts = pts + SHELLS[math.min(counts[T.SHELL], 6)] + OCTOPI[math.min(counts[T.OCTOPUS], 5)] +
          PENGUINS[math.min(counts[T.PENGUIN], 3)] + SAILORS[math.min(counts[T.SAILOR], 2)]
    if counts[T.BEACON] > 0 then pts = pts + counts[T.BOAT] end
    if counts[T.SHOAL] > 0 then pts = pts + counts[T.FISH] end
    if counts[T.COLONY] > 0 then pts = pts + 2 * counts[T.PENGUIN] end
    if counts[T.CAPTAIN] > 0 then pts = pts + 3 * counts[T.SAILOR] end
    -- Each mermaid scores one of your marks: the most common, then the next...
    local mermaids = counts[T.MERMAID]
    if mermaids > 0 then
        local sorted = {}
        for m = 1, T.MARKS do sorted[m] = marks[m] end
        table.sort(sorted, function(x, y) return x > y end)
        for i = 1, math.min(mermaids, T.MARKS) do pts = pts + sorted[i] end
    end
    return pts
end

-- The mark bonus: how many cards of player p's most common mark.
function T.markBonus(g, p)
    tally(g, p)
    local best = 0
    for m = 1, T.MARKS do
        if marks[m] > best then best = marks[m] end
    end
    return best
end

function T.mermaids(g, p)
    tally(g, p)
    return counts[T.MERMAID]
end

-- The game ----------------------------------------------------------------------
-- g.phase: "draw" (take cards), "keep" (choose one of g.drawn), "play" (pairs,
-- end turn or end the round), "crab" (after a crab pair), "round" (round over,
-- g.result says how), "over". New games and rounds are dealt by setup.lua.

local function removeCard(list, id)
    for i, x in ipairs(list) do
        if x == id then
            table.remove(list, i)
            return true
        end
    end
    return false
end

local function gameOverIfWon(g)
    for p = 1, 2 do
        if T.mermaids(g, p) >= 4 then
            g.winner, g.phase = p, "over"
            g.result = { kind = "mermaids", player = p }
            return true
        end
    end
    return false
end

-- Draw two from the deck (phase draw -> keep).
function T.drawTwo(g)
    if g.phase ~= "draw" or #g.deck == 0 then return false end
    g.drawn = { table.remove(g.deck) }
    if #g.deck > 0 then g.drawn[2] = table.remove(g.deck) end
    g.phase = "keep"
    return true
end

-- Which piles the unkept card may go on: an empty one if there is one.
function T.discardPiles(g)
    local a, b = #g.piles[1] == 0, #g.piles[2] == 0
    if a and not b then return { 1 } end
    if b and not a then return { 2 } end
    return { 1, 2 }
end

-- Keep drawn card i; the other goes on `pile` (phase keep -> play).
function T.keep(g, i, pile)
    if g.phase ~= "keep" or not g.drawn[i] then return false end
    local other = g.drawn[3 - i]
    if other then
        local ok = false
        for _, q in ipairs(T.discardPiles(g)) do
            if q == pile then ok = true end
        end
        if not ok then return false end
        table.insert(g.piles[pile], other)
    end
    table.insert(g.hands[g.turn], g.drawn[i])
    g.drawn = nil
    g.phase = "play"
    gameOverIfWon(g)
    return true
end

-- Take the top card of a discard pile (phase draw -> play).
function T.takePile(g, pile)
    if g.phase ~= "draw" or #g.piles[pile] == 0 then return false end
    table.insert(g.hands[g.turn], table.remove(g.piles[pile]))
    g.phase = "play"
    gameOverIfWon(g)
    return true
end

-- Play cards a and b (ids in hand) as a pair. Returns what it did:
-- "crab" (the player now picks a discarded card with T.crabTake), "boat",
-- "fish", "steal" - or nil if not allowed.
function T.playPair(g, a, b, rand)
    local hand = g.hands[g.turn]
    if g.phase ~= "play" or a == b or not T.isPair(a, b) then return nil end
    local ha, hb = false, false
    for _, x in ipairs(hand) do
        if x == a then ha = true end
        if x == b then hb = true end
    end
    if not ha or not hb then return nil end
    removeCard(hand, a)
    removeCard(hand, b)
    table.insert(g.played[g.turn], a)
    table.insert(g.played[g.turn], b)
    local kind = math.min(KIND[a], KIND[b])
    if kind == T.CRAB then
        if #g.piles[1] + #g.piles[2] > 0 then g.phase = "crab" end
        return "crab"
    elseif kind == T.BOAT then
        g.extraTurn = true
        return "boat"
    elseif kind == T.FISH then
        if #g.deck > 0 then table.insert(hand, table.remove(g.deck)) end
        gameOverIfWon(g)
        return "fish"
    else
        local other = g.hands[3 - g.turn]
        if #other > 0 then table.insert(hand, table.remove(other, rand(#other))) end
        gameOverIfWon(g)
        return "steal"
    end
end

-- After a crab pair: take card `id` from either discard pile (phase crab -> play).
function T.crabTake(g, id)
    if g.phase ~= "crab" then return false end
    for p = 1, 2 do
        if removeCard(g.piles[p], id) then
            table.insert(g.hands[g.turn], id)
            g.phase = "play"
            gameOverIfWon(g)
            return true
        end
    end
    return false
end

-- Whether the player whose turn it is may end the round now.
function T.canEnd(g)
    return g.phase == "play" and not g.caller and T.cardPoints(g, g.turn) >= T.END_POINTS
end

local function finishRound(g, kind)
    local pts = { T.cardPoints(g, 1), T.cardPoints(g, 2) }
    local bonus = { T.markBonus(g, 1), T.markBonus(g, 2) }
    local gain = { 0, 0 }
    if kind == "stop" then
        gain = { pts[1], pts[2] }
    elseif kind == "last" then
        local c = g.caller
        if pts[c] > pts[3 - c] then
            gain[c], gain[3 - c] = pts[c] + bonus[c], bonus[3 - c]
        else
            gain[c], gain[3 - c] = bonus[c], pts[3 - c]
        end
    end
    for p = 1, 2 do g.scores[p] = g.scores[p] + gain[p] end
    g.result = { kind = kind, player = g.caller or g.turn, points = pts, bonus = bonus, gain = gain }
    g.phase = "round"
    if g.scores[1] >= T.TARGET or g.scores[2] >= T.TARGET then
        if g.scores[1] ~= g.scores[2] then
            g.winner = g.scores[1] > g.scores[2] and 1 or 2
        else
            g.winner = g.result.player -- a tie goes to whoever ended the round
        end
        g.phase = "over"
    end
end

-- Ends the turn: the next player's (or the same, after a boat pair), or the
-- end of the round after a last chance; a player with no deck to draw from ends
-- the round with no score.
function T.endTurn(g)
    if g.phase ~= "play" then return false end
    if g.caller and g.turn ~= g.caller then
        finishRound(g, "last")
        return true
    end
    if g.extraTurn then
        g.extraTurn = false
    else
        g.turn = 3 - g.turn
    end
    g.phase = "draw"
    if #g.deck == 0 then finishRound(g, "empty") end
    return true
end

function T.stop(g)
    if not T.canEnd(g) then return false end
    g.caller = g.turn
    finishRound(g, "stop")
    return true
end

function T.lastChance(g)
    if not T.canEnd(g) then return false end
    g.caller = g.turn
    g.extraTurn = false
    g.turn = 3 - g.turn
    g.phase = "draw"
    if #g.deck == 0 then finishRound(g, "last") end
    return true
end

return T
