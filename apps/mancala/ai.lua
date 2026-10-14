-- Mancala computer opponent. Usage: local AI = smudge.dofile("ai.lua")(C),
-- with C the rules from logic.lua. Loaded only while the device thinks.
--
-- Easy plays a random move one time in three and otherwise looks two moves
-- ahead. Normal looks four moves ahead, Hard deepens one move at a time
-- (iterative deepening) until its node or time budget runs out; the last
-- depth searched in full decides. An extra turn is just a move after which the
-- same player is to move again: the search is negamax and flips the score
-- only when the player changes. The search allocates nothing per node: one
-- state and one move list per ply, made when the module loads.

return function(C)
local AI = {}

local SIDE, OVER = C.SIDE, C.OVER
local MAX_DEPTH = 12
local WIN = 100000
local INF = WIN * 4
local states, lists = {}, {}
for i = 1, MAX_DEPTH + 1 do
    local t = {}
    for k = 1, C.SIZE do t[k] = 0 end
    states[i] = t
    lists[i] = { 0, 0, 0, 0, 0, 0 }
end
local nodes, budget, stopped = 0, 0, false
local now, deadline = nil, 0

-- From the view of the player to move: store difference (times 4) plus the
-- seeds on each side (most end up in that side's store).
local function evaluate(s)
    local me = s[SIDE]
    local a, b = s[7], s[14]
    local diff = me == 1 and a - b or b - a
    if s[OVER] == 1 then
        if diff > 0 then return WIN + diff elseif diff < 0 then return -WIN + diff end
        return 0
    end
    local side = 0
    for i = 1, 6 do side = side + s[i] - s[i + 7] end
    if me == 2 then side = -side end
    return diff * 4 + side
end
AI.evaluate = evaluate

-- The moves of the player to move into `list`, extra turns first, then the
-- pits nearest the store. Returns the count.
local function moves(s, list)
    local f = s[SIDE] == 1 and 1 or 8
    local n = 0
    for p = f + 5, f, -1 do
        if s[p] == f + 6 - p then
            n = n + 1
            list[n] = p
        end
    end
    for p = f + 5, f, -1 do
        local v = s[p]
        if v > 0 and v ~= f + 6 - p then
            n = n + 1
            list[n] = p
        end
    end
    return n
end

local function search(s, depth, ply, alpha, beta)
    nodes = nodes + 1
    if nodes >= budget then stopped = true end
    if now and nodes & 127 == 0 and now() >= deadline then stopped = true end
    if depth == 0 or s[OVER] == 1 or stopped then return evaluate(s) end
    local list = lists[ply]
    local n = moves(s, list)
    local child = states[ply + 1]
    local me = s[SIDE]
    local best = -INF
    for i = 1, n do
        C.copy(child, s)
        C.play(child, list[i])
        local v
        if child[SIDE] == me then
            v = search(child, depth - 1, ply + 1, alpha, beta)
        else
            v = -search(child, depth - 1, ply + 1, -beta, -alpha)
        end
        if v > best then
            best = v
            if v > alpha then alpha = v end
            if alpha >= beta then break end
        end
    end
    return best
end

-- The best move at `depth`, `first` tried first; nil when the search was
-- cut short (depth > 1). Also returns its value.
local function root(s, depth, rand, first)
    local list = lists[1]
    local n = moves(s, list)
    if first then
        for i = 2, n do
            if list[i] == first then
                list[1], list[i] = list[i], list[1]
                break
            end
        end
    end
    local child = states[2]
    local me = s[SIDE]
    local bestV, bestM, ties = -INF, nil, 0
    for i = 1, n do
        C.copy(child, s)
        C.play(child, list[i])
        local v
        -- A window one wider than the best so far keeps ties exact.
        if child[SIDE] == me then
            v = search(child, depth - 1, 2, bestV - 1, INF)
        else
            v = -search(child, depth - 1, 2, -INF, -(bestV - 1))
        end
        if stopped and depth > 1 then return nil end
        if v > bestV then
            bestV, bestM, ties = v, list[i], 1
        elseif v == bestV then
            ties = ties + 1
            if rand(ties) == 1 then bestM = list[i] end
        end
    end
    return bestM, bestV
end

-- A move for the player to move at `level` (1 Easy, 2 Normal, 3 Hard).
-- `rand(n)` returns 1..n. `clock` (optional, a millisecond clock) and `ms`
-- bound Hard's time; `limit` caps its nodes (default 40000).
function AI.choose(s, level, rand, clock, ms, limit)
    local list = lists[1]
    local n = C.legal(s, list)
    if n == 0 then return nil end
    if n == 1 then return list[1] end
    stopped, nodes, budget, now = false, 0, 1000000, nil
    if level == 1 then
        if rand(3) == 1 then return list[rand(n)] end
        return (root(s, 2, rand))
    end
    if level == 2 then return (root(s, 4, rand)) end
    budget = limit or 40000
    now = clock
    if clock then deadline = clock() + (ms or 1500) end
    local move, v = root(s, 1, rand)
    for depth = 2, MAX_DEPTH do
        local m, mv = root(s, depth, rand, move)
        if not m then break end
        move, v = m, mv
        if v >= WIN or v <= -WIN then break end
    end
    return move
end

return AI
end
