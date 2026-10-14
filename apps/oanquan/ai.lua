-- O An Quan computer opponent. Usage: local AI = smudge.dofile("ai.lua")(C),
-- with C the rules from logic.lua. Loaded only while the device thinks.
--
-- Easy plays a random move, now and then the greedy one. Normal tries every
-- move and keeps the one that gains the most points right away. Hard looks
-- ahead with minimax and alpha-beta pruning, one ply deeper at a time up to
-- its depth, within a node budget; the last depth searched in full decides.
-- The search allocates nothing per node: one state and one move list per ply,
-- made when the module loads.

return function(C)
local AI = {}

local SIDE, OVER = C.SIDE, C.OVER
local MAX_DEPTH = 4
local WIN = 100000
local states, lists = {}, {}
for i = 1, MAX_DEPTH + 1 do
    states[i] = {}
    lists[i] = {}
end
local nodes, budget = 0, 0

-- From `me`'s point of view: points ahead (times 4) plus the stones on the
-- rows (they are ammunition, and each player's own at the end).
local function evaluate(s, me)
    local o = 3 - me
    local diff = C.score(s, me) - C.score(s, o)
    if s[OVER] == 1 then
        if diff > 0 then return WIN + diff elseif diff < 0 then return -WIN + diff end
        return 0
    end
    local row = 0
    local f, g = C.first(me), C.first(o)
    for i = 0, 4 do row = row + s[f + i] - s[g + i] end
    return diff * 4 + row
end
AI.evaluate = evaluate

local function search(s, depth, ply, me, alpha, beta)
    nodes = nodes + 1
    if depth == 0 or s[OVER] == 1 or nodes >= budget then return evaluate(s, me) end
    local list = lists[ply]
    local n = C.legal(s, list)
    local child = states[ply + 1]
    local maximize = s[SIDE] == me
    local best = maximize and -WIN * 2 or WIN * 2
    for i = 1, n do
        C.copy(child, s)
        C.play(child, list[i])
        local v = search(child, depth - 1, ply + 1, me, alpha, beta)
        if maximize then
            if v > best then best = v end
            if best > alpha then alpha = best end
        else
            if v < best then best = v end
            if best < beta then beta = best end
        end
        if alpha >= beta then break end
    end
    return best
end

-- The best move at `depth` (nil when the budget ran out before the end).
local function root(s, depth, rand)
    local me = s[SIDE]
    local list = lists[1]
    local n = C.legal(s, list)
    local child = states[2]
    local bestV, bestM, ties = nil, nil, 0
    for i = 1, n do
        C.copy(child, s)
        C.play(child, list[i])
        local v = search(child, depth - 1, 2, me, -WIN * 2, WIN * 2)
        if nodes >= budget and depth > 1 then return nil end
        if bestV == nil or v > bestV then
            bestV, bestM, ties = v, list[i], 1
        elseif v == bestV then
            ties = ties + 1
            if rand(ties) == 1 then bestM = list[i] end
        end
    end
    return bestM
end

-- A move for the player to move at `level` (1 Easy, 2 Normal, 3 Hard).
-- `rand(n)` returns 1..n. `limit` caps Hard's nodes (default 6000).
function AI.choose(s, level, rand, limit)
    local list = lists[1]
    local n = C.legal(s, list)
    if n == 0 then return nil end
    if level == 1 and rand(3) > 1 then return list[rand(n)] end
    nodes, budget = 0, limit or 6000
    if level <= 2 then return root(s, 1, rand) end
    local move = root(s, 1, rand)
    for depth = 2, MAX_DEPTH do
        nodes = 0
        local m = root(s, depth, rand)
        if not m then break end
        move = m
    end
    return move
end

return AI
end
