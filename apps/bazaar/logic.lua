-- Bazaar rules: a two-player trading card game. No drawing, so it can be
-- tested on a computer (test/lua_apps/bazaar_test.lua). The computer trader
-- is in ai.lua and saving a game in save.lua.
--
-- 55 cards: 6 diamond, 6 gold, 6 silver, 8 cloth, 8 spice, 10 leather and
-- 11 camels. The market holds 5 cards. On a turn you either take or sell:
--   * take one good from the market (the deck refills its place);
--   * take all the camels from the market (the deck refills their places);
--   * exchange: take two or more goods and put back as many cards from your
--     hand or your herd of camels - never the same kind you take, and the
--     deck is not touched;
--   * sell any number of one kind of good: take that many tokens from its
--     pile, highest first, plus a bonus token for selling 3, 4 or 5+.
--     Diamonds, gold and silver are sold two at a time at least.
-- A hand holds 7 goods at most; camels sit apart. A round ends when three
-- token piles are empty or the deck cannot refill the market. The trader
-- with more camels takes 5 more; the richer one wins a seal (ties: more bonus
-- tokens, then more goods tokens, then more camels). Two seals win.

local B = {}

B.DIAMOND, B.GOLD, B.SILVER, B.CLOTH, B.SPICE, B.LEATHER, B.CAMEL = 1, 2, 3, 4, 5, 6, 7
B.NAMES = { "Diamond", "Gold", "Silver", "Cloth", "Spice", "Leather", "Camel" }
B.HAND_LIMIT = 7
local COUNTS = { 6, 6, 6, 8, 8, 10, 11 }
B.COUNTS = COUNTS
-- Each good's tokens, top of the pile first.
local TOKENS = {
    { 7, 7, 5, 5, 5 },
    { 6, 6, 5, 5, 5 },
    { 5, 5, 5, 5, 5 },
    { 5, 3, 3, 2, 2, 1, 1 },
    { 5, 3, 3, 2, 2, 1, 1 },
    { 4, 3, 2, 1, 1, 1, 1, 1, 1 },
}
local BONUS = { { 1, 1, 2, 2, 2, 3, 3 }, { 4, 4, 5, 5, 6, 6 }, { 8, 8, 9, 10, 10 } }
B.CAMEL_TOKEN = 5

function B.precious(good) return good <= B.SILVER end
function B.minSale(good) return B.precious(good) and 2 or 1 end

local function shuffle(t, rand)
    for i = #t, 2, -1 do
        local j = rand(i)
        t[i], t[j] = t[j], t[i]
    end
end

local function newPlayer()
    return { hand = { 0, 0, 0, 0, 0, 0 }, camels = 0, rupees = 0, bonuses = 0, goodsTokens = 0 }
end

-- Sets up a round. `starter` (1 or 2) moves first.
function B.startRound(g, starter, rand)
    local deck = {}
    for kind = 1, 7 do
        local n = COUNTS[kind] - (kind == B.CAMEL and 3 or 0)
        for _ = 1, n do deck[#deck + 1] = kind end
    end
    shuffle(deck, rand)
    g.deck = deck
    g.market = { B.CAMEL, B.CAMEL, B.CAMEL, table.remove(deck), table.remove(deck) }
    g.players = { newPlayer(), newPlayer() }
    for p = 1, 2 do
        for _ = 1, 5 do
            local c = table.remove(deck)
            local pl = g.players[p]
            if c == B.CAMEL then pl.camels = pl.camels + 1 else pl.hand[c] = pl.hand[c] + 1 end
        end
    end
    g.piles = {}
    for good = 1, 6 do
        local pile = {}
        for k, v in ipairs(TOKENS[good]) do pile[k] = v end
        g.piles[good] = pile -- pile[1] is the top
    end
    g.bonus = {}
    for k = 1, 3 do
        local stack = {}
        for i, v in ipairs(BONUS[k]) do stack[i] = v end
        shuffle(stack, rand)
        g.bonus[k] = stack
    end
    g.turn, g.starter = starter, starter
    g.phase = "play"
    g.last = nil
end

function B.new(rand)
    local g = { seals = { 0, 0 }, round = 1, winner = 0 }
    B.startRound(g, 1, rand)
    return g
end

function B.handSize(pl)
    local n = 0
    for good = 1, 6 do n = n + pl.hand[good] end
    return n
end

function B.marketCount(g, kind)
    local n = 0
    for _, c in ipairs(g.market) do
        if c == kind then n = n + 1 end
    end
    return n
end

local function emptyPiles(g)
    local n = 0
    for good = 1, 6 do
        if #g.piles[good] == 0 then n = n + 1 end
    end
    return n
end

-- Refills market slot i from the deck; false when the deck is empty.
local function refill(g, i)
    if #g.deck == 0 then
        g.market[i] = 0
        return false
    end
    g.market[i] = table.remove(g.deck)
    return true
end

-- Why a move cannot be played, or nil if it can. A move is
--   { kind = "one", slot = i }                       take one good
--   { kind = "camels" }                              take all the camels
--   { kind = "trade", slots = { i, ... }, give = { [good] = n, ... }, camels = n }
--   { kind = "sell", good = g, count = n }
function B.refusal(g, mv)
    if g.phase ~= "play" then return "The round is over" end
    local pl = g.players[g.turn]
    if mv.kind == "one" then
        local c = g.market[mv.slot]
        if not c or c == 0 or c == B.CAMEL then return "Pick a good from the market" end
        if B.handSize(pl) >= B.HAND_LIMIT then return "Your hand is full (7 cards)" end
    elseif mv.kind == "camels" then
        if B.marketCount(g, B.CAMEL) == 0 then return "There are no camels in the market" end
    elseif mv.kind == "trade" then
        local taking, takenKinds = #mv.slots, {}
        if taking < 2 then return "An exchange is at least two cards for two" end
        local seen = {}
        for _, i in ipairs(mv.slots) do
            local c = g.market[i]
            if seen[i] or not c or c == 0 or c == B.CAMEL then return "Only goods can be taken in an exchange" end
            seen[i] = true
            takenKinds[c] = true
        end
        local giving = mv.camels or 0
        if giving > pl.camels then return "Not that many camels" end
        for good, n in pairs(mv.give or {}) do
            if n > 0 then
                if n > pl.hand[good] then return "Not that many " .. B.NAMES[good] end
                if takenKinds[good] then return "You cannot give back a kind you take" end
                giving = giving + n
            end
        end
        if giving ~= taking then return "Give back as many cards as you take" end
        if B.handSize(pl) + taking - (giving - (mv.camels or 0)) > B.HAND_LIMIT then
            return "That would leave more than 7 cards in your hand"
        end
    elseif mv.kind == "sell" then
        local good, n = mv.good, mv.count
        if not good or good < 1 or good > 6 or not n or n < 1 then return "Pick goods to sell" end
        if n > pl.hand[good] then return "Not that many " .. B.NAMES[good] end
        if n < B.minSale(good) then return B.NAMES[good] .. " sells two at a time" end
    else
        return "Unknown move"
    end
    return nil
end

-- Plays a move for the trader to move. Returns false if it is not allowed.
function B.play(g, mv, rand)
    if B.refusal(g, mv) then return false end
    local p = g.turn
    local pl = g.players[p]
    local deckRanOut = false
    local last = { player = p, kind = mv.kind }
    if mv.kind == "one" then
        local c = g.market[mv.slot]
        pl.hand[c] = pl.hand[c] + 1
        last.good = c
        if not refill(g, mv.slot) then deckRanOut = true end
    elseif mv.kind == "camels" then
        local n = 0
        for i, c in ipairs(g.market) do
            if c == B.CAMEL then
                n = n + 1
                pl.camels = pl.camels + 1
                if not refill(g, i) then deckRanOut = true end
            end
        end
        last.count = n
    elseif mv.kind == "trade" then
        local taken = {}
        for _, i in ipairs(mv.slots) do
            local c = g.market[i]
            pl.hand[c] = pl.hand[c] + 1
            taken[#taken + 1] = c
        end
        -- The cards given back take the freed market places.
        local back = {}
        for _ = 1, mv.camels or 0 do back[#back + 1] = B.CAMEL end
        for good = 1, 6 do
            for _ = 1, (mv.give or {})[good] or 0 do back[#back + 1] = good end
        end
        pl.camels = pl.camels - (mv.camels or 0)
        for good, n in pairs(mv.give or {}) do pl.hand[good] = pl.hand[good] - n end
        for k, i in ipairs(mv.slots) do g.market[i] = back[k] end
        last.count, last.taken = #mv.slots, taken
    else
        local good, n = mv.good, mv.count
        pl.hand[good] = pl.hand[good] - n
        local pile, value = g.piles[good], 0
        for _ = 1, n do
            if #pile == 0 then break end
            value = value + table.remove(pile, 1)
            pl.goodsTokens = pl.goodsTokens + 1
        end
        local stack = (n >= 5) and 3 or ((n == 4) and 2 or ((n == 3) and 1 or nil))
        local bonus = 0
        if stack and #g.bonus[stack] > 0 then
            bonus = table.remove(g.bonus[stack])
            pl.bonuses = pl.bonuses + 1
        end
        pl.rupees = pl.rupees + value + bonus
        last.good, last.count, last.value, last.bonus = good, n, value, bonus
    end
    g.last = last
    if deckRanOut or emptyPiles(g) >= 3 then
        B.endRound(g)
    else
        g.turn = 3 - p
    end
    return true
end

-- Scores the round: the camel token, then a seal for the richer trader.
function B.endRound(g)
    local a, b = g.players[1], g.players[2]
    g.camelToken = 0
    if a.camels > b.camels then
        g.camelToken = 1
    elseif b.camels > a.camels then
        g.camelToken = 2
    end
    if g.camelToken > 0 then
        local pl = g.players[g.camelToken]
        pl.rupees = pl.rupees + B.CAMEL_TOKEN
    end
    local order = { { a.rupees, b.rupees }, { a.bonuses, b.bonuses }, { a.goodsTokens, b.goodsTokens },
                    { a.camels, b.camels } }
    g.roundWinner = 0
    for _, pair in ipairs(order) do
        if pair[1] ~= pair[2] then
            g.roundWinner = pair[1] > pair[2] and 1 or 2
            break
        end
    end
    if g.roundWinner > 0 then g.seals[g.roundWinner] = g.seals[g.roundWinner] + 1 end
    g.phase = "scored"
    for p = 1, 2 do
        if g.seals[p] >= 2 then g.winner = p end
    end
    if g.winner > 0 then g.phase = "over" end
end

-- After a scored round: the next one, started by the round's loser (the
-- same starter again after a draw).
function B.nextRound(g, rand)
    if g.phase ~= "scored" then return false end
    g.round = g.round + 1
    local starter = (g.roundWinner > 0) and (3 - g.roundWinner) or g.starter
    B.startRound(g, starter, rand)
    return true
end

-- Every distinct move for the trader to move (exchanges give back the given
-- `give` choice only; ai.lua builds its own). Used by tests and the AI.
function B.simpleMoves(g)
    local out = {}
    local pl = g.players[g.turn]
    for i = 1, 5 do
        local c = g.market[i]
        if c ~= 0 and c ~= B.CAMEL and not B.refusal(g, { kind = "one", slot = i }) then
            out[#out + 1] = { kind = "one", slot = i }
        end
    end
    if B.marketCount(g, B.CAMEL) > 0 then out[#out + 1] = { kind = "camels" } end
    for good = 1, 6 do
        if pl.hand[good] >= B.minSale(good) then out[#out + 1] = { kind = "sell", good = good, count = pl.hand[good] } end
    end
    return out
end

return B
