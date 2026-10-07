-- Bazaar computer trader. Usage: local AI = smudge.dofile("ai.lua")(B), with
-- B the rules from logic.lua.
--
-- It sees what a player at the table sees: its own hand, both herds, the
-- market, the token piles and the deck's size - never the deck's order or
-- the other hand. Every move is judged one turn ahead: what it earns now,
-- what the cards in hand are likely to be worth when sold, the camels, and
-- what the move leaves in the market for the opponent. Cards a move draws
-- from the deck count as unknown.

return function(B)
local AI = {}

local BONUS_EXPECTED = { [3] = 2, [4] = 5, [5] = 9 }

local function topTokens(pile, n)
    local s = 0
    for k = 1, math.min(n, #pile) do s = s + pile[k] end
    return s
end

-- Likely worth of holding `c` cards of `good`.
local function holding(g, good, c)
    if c == 0 then return 0 end
    local pile = g.piles[good]
    if #pile == 0 then return 0 end
    local v = topTokens(pile, c) * 0.85
    if B.precious(good) and c == 1 then v = v * 0.6 end -- needs a second one to sell
    if c >= 3 then v = v + BONUS_EXPECTED[math.min(c, 5)] * 0.9 elseif c == 2 then v = v + 0.8 end
    return v
end

local function handWorth(g, hand)
    local v = 0
    for good = 1, 6 do v = v + holding(g, good, hand[good]) end
    return v
end

-- What the opponent could take from `market` next turn.
local function gift(g, market)
    local best, camels = 0, 0
    for _, c in ipairs(market) do
        if c == B.CAMEL then
            camels = camels + 1
        elseif c ~= 0 and #g.piles[c] > 0 then
            if g.piles[c][1] > best then best = g.piles[c][1] end
        end
    end
    return best * 0.6 + camels * 0.4
end

-- The score of a position for the trader to move after a move: `hand`,
-- `camels`, `rupees` are its own; `market` what is left showing.
local function score(g, hand, camels, rupees, market)
    local opp = g.players[3 - g.turn]
    local v = rupees + handWorth(g, hand) + camels * 0.5
    if camels > opp.camels then v = v + 2.5 end
    local size = 0
    for good = 1, 6 do size = size + hand[good] end
    if size >= B.HAND_LIMIT then v = v - 1.5 end
    return v - gift(g, market)
end

local function copy(t)
    local out = {}
    for k, v in ipairs(t) do out[k] = v end
    return out
end

-- Every move worth considering, each with its score.
local function candidates(g)
    local pl = g.players[g.turn]
    local out = {}
    local function add(mv, hand, camels, rupees, market)
        if not B.refusal(g, mv) then
            out[#out + 1] = { mv = mv, v = score(g, hand, camels, rupees, market) }
        end
    end
    -- One good.
    for i = 1, 5 do
        local c = g.market[i]
        if c ~= 0 and c ~= B.CAMEL then
            local hand, market = copy(pl.hand), copy(g.market)
            hand[c] = hand[c] + 1
            market[i] = 0 -- unknown card from the deck
            add({ kind = "one", slot = i }, hand, pl.camels, pl.rupees, market)
        end
    end
    -- All the camels.
    local n = B.marketCount(g, B.CAMEL)
    if n > 0 then
        local market = copy(g.market)
        for i = 1, 5 do
            if market[i] == B.CAMEL then market[i] = 0 end
        end
        add({ kind = "camels" }, pl.hand, pl.camels + n, pl.rupees, market)
    end
    -- Selling a whole kind.
    for good = 1, 6 do
        local count = pl.hand[good]
        if count >= B.minSale(good) then
            local pile = g.piles[good]
            local gain = topTokens(pile, count)
            if count >= 3 and #g.bonus[math.min(count, 5) - 2] > 0 then gain = gain + BONUS_EXPECTED[math.min(count, 5)] end
            local hand = copy(pl.hand)
            hand[good] = 0
            add({ kind = "sell", good = good, count = count }, hand, pl.camels, pl.rupees + gain, g.market)
        end
    end
    -- Exchanges: every set of two or more market goods, paid for with the
    -- cards worth least to keep (camels first, then the cheapest goods).
    local slots = {}
    for i = 1, 5 do
        if g.market[i] ~= 0 and g.market[i] ~= B.CAMEL then slots[#slots + 1] = i end
    end
    for mask = 1, (1 << #slots) - 1 do
        local chosen, taken = {}, {}
        for k = 1, #slots do
            if mask & (1 << (k - 1)) ~= 0 then
                chosen[#chosen + 1] = slots[k]
                taken[g.market[slots[k]]] = true
            end
        end
        if #chosen >= 2 then
            local hand = copy(pl.hand)
            for _, i in ipairs(chosen) do hand[g.market[i]] = hand[g.market[i]] + 1 end
            local need, camels, give = #chosen, pl.camels, {}
            local useCamels = math.min(camels, need)
            need = need - useCamels
            camels = camels - useCamels
            -- Then goods not being taken, least valuable to keep first.
            while need > 0 do
                local worst, worstLoss = nil, nil
                for good = 1, 6 do
                    if not taken[good] and pl.hand[good] - (give[good] or 0) > 0 then
                        local loss = holding(g, good, hand[good]) - holding(g, good, hand[good] - 1)
                        if not worstLoss or loss < worstLoss then worst, worstLoss = good, loss end
                    end
                end
                if not worst then break end
                give[worst] = (give[worst] or 0) + 1
                hand[worst] = hand[worst] - 1
                need = need - 1
            end
            if need == 0 then
                local market = copy(g.market)
                local back = {}
                for _ = 1, useCamels do back[#back + 1] = B.CAMEL end
                for good = 1, 6 do
                    for _ = 1, give[good] or 0 do back[#back + 1] = good end
                end
                for k, i in ipairs(chosen) do market[i] = back[k] end
                add({ kind = "trade", slots = chosen, give = give, camels = useCamels }, hand, camels, pl.rupees, market)
            end
        end
    end
    return out
end
AI.candidates = candidates

-- The move for the trader to move. level 1 often plays a random move.
function AI.choose(g, level, rand)
    local list = candidates(g)
    if #list == 0 then return nil end
    if level <= 1 and rand(100) <= 40 then return list[rand(#list)].mv end
    local best = list[1]
    for _, c in ipairs(list) do
        if c.v > best.v + 0.01 or (math.abs(c.v - best.v) <= 0.01 and rand(2) == 1) then best = c end
    end
    return best.mv
end

return AI
end
