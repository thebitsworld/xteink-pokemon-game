-- The Toy Front device player, loaded for its turn and dropped after:
--   local AI = smudge.dofile("ai.lua")(T)
--   local k, i = AI.choose(g, level, rand)   -- hand card k on base i (i nil: give up card k)
-- Level 1 takes what looks best now, with some chance; level 2 also weighs
-- which bases the other side could take next, and which it could.

return function(T)
local AI = {}
local N = T.SIZE * T.SIZE

-- How good the board is for player p: medals held, and how safe they are.
local function eval(g, p)
    local v = 0
    for i = 1, N do
        local o = g.owner[i]
        if o ~= 0 then
            local med = T.medals(g, i)
            local worth = med * 3 + math.min(g.strength[i], 6) * 0.15 * med
            v = v + (o == p and worth or -worth)
        end
    end
    return v
end

-- Plays v on base i for player p without the hand bookkeeping; returns the
-- old owner and strength to undo it.
local function apply(g, p, v, i)
    local o, s = g.owner[i], g.strength[i]
    if o == 0 then
        g.owner[i], g.strength[i] = p, v
    elseif o == p then
        g.strength[i] = math.min(T.MAX_STRENGTH, s + v)
    elseif v > s then
        g.owner[i], g.strength[i] = p, v - s
    elseif v < s then
        g.strength[i] = s - v
    else
        g.owner[i], g.strength[i] = 0, 0
    end
    return o, s
end

-- The strongest toy player q may still have: in hand or deck (the device
-- does not look at your hand, only at what you have not yet played).
local function strongest(g, q)
    local m = 0
    for _, list in ipairs({ g.hands[q], g.decks[q] }) do
        for _, v in ipairs(list) do
            if v > m then m = v end
        end
    end
    return m
end

-- Level 2's view of the board: the medals, less those the other side could
-- take next with its strongest toy, plus those p could take with its own.
local function eval2(g, p, mine, theirs)
    local v = eval(g, p)
    local q = 3 - p
    for i = 1, N do
        local o = g.owner[i]
        if o == p and T.canPlay(g, q, i) then
            if theirs > g.strength[i] then
                v = v - T.medals(g, i) * 0.5
            elseif theirs == g.strength[i] then
                v = v - T.medals(g, i) * 0.2
            end
        elseif o == q and T.canPlay(g, p, i) and mine > g.strength[i] then
            v = v + T.medals(g, i) * 0.3
        end
    end
    return v
end

function AI.choose(g, level, rand)
    local p, q = g.turn, 3 - g.turn
    local hand = g.hands[p]
    local theirs = strongest(g, q)
    local bestK, bestI, bestV = nil, nil, -1e9
    for k, v in ipairs(hand) do
        for i = 1, N do
            if T.canPlay(g, p, i) then
                local o, s = apply(g, p, v, i)
                local e
                if level >= 2 then
                    -- The strongest toy left after this one.
                    local mine = 0
                    for j, x in ipairs(hand) do
                        if j ~= k and x > mine then mine = x end
                    end
                    e = eval2(g, p, mine, theirs)
                else
                    e = eval(g, p) + rand(100) / 40
                end
                -- Keep the strong toys for later when a weak one does as well (level 2
                -- weighs this much more: spending them early loses games).
                e = e - v * (level >= 2 and 0.6 or 0.05)
                g.owner[i], g.strength[i] = o, s
                if e > bestV then bestK, bestI, bestV = k, i, e end
            end
        end
    end
    if not bestK then
        -- Nowhere to play: give up the weakest toy.
        local k = 1
        for j, v in ipairs(hand) do
            if v < hand[k] then k = j end
        end
        return k, nil
    end
    return bestK, bestI
end

return AI
end
