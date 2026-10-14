-- Co Up computer opponent. Usage: local AI = smudge.dofile("ai.lua")(C),
-- with C the rules from logic.lua.
--
-- The xiangqi app's search: negamax with alpha-beta pruning and a
-- capture-only quiescence search, deepening one ply at a time until the
-- level's depth or its time is used up; the last fully searched depth
-- decides.
--
-- It plays fair: it never reads what a face-down piece is (g.secret, which
-- main.lua even takes away while it thinks), only what everyone can see -
-- the face-up pieces and the captured ones. From those game.lua works out
-- each side's pool of pieces still face down (G.pool), and the search values
-- every face-down piece at that pool's average (g.avg). In the search a face-down piece that moves
-- stays face down and keeps moving as its role (what it turns out to be is
-- a matter of luck, worth the same average); the real move turns it up.
--
-- Memory is tight on the X3, so the search allocates nothing per node:
-- every ply's moves go into one shared array, each tagged with its ordering
-- score in the bits above the move (sorting the integers sorts the moves),
-- and the recursion depth is capped.

return function(C)
local AI = {}

local EMPTY, OFF = C.EMPTY, C.OFF
local VALUE = C.VALUE
local MATE = 30000
local MAX_PLY = 8          -- search plus quiescence: each level deepens the Lua stack
local MOVE_BITS = 65536    -- moves fit below, ordering scores go above
local PV = {}              -- value of each piece code 0..31 (face-down: the pool's average)

-- Sets PV for the position: face-up pieces at their value, face-down ones
-- at the average of their side's face-down pool (g.avg, kept by game.lua
-- from public information only).
local function setValues(g)
    for s = 0, 1 do
        local avg = g.avg and g.avg[s + 1] or 300
        for t = 0, 7 do
            PV[s * 8 + t] = VALUE[t] or 0
            PV[16 + s * 8 + t] = avg
        end
    end
    PV[0], PV[16] = 0, 0
end

-- Score from the side to move's point of view. Positional terms go by the
-- way a piece moves (a face-down one: its role).
local function evaluate(g)
    local b = g.board
    local score = 0
    for sq = 11, 109 do
        local p = b[sq]
        if p ~= EMPTY and p ~= OFF then
            local black = p & 8 ~= 0
            local t = p & 7
            local v = PV[p]
            if t >= 4 then
                local f = sq % 10
                local row = sq // 10
                if black then row = 11 - row end -- as if Red: 10 home rank .. 1 far rank
                local centre = f - 5
                if centre < 0 then centre = -centre end
                if t == 7 then
                    if row <= 5 then
                        v = v + 90 + (f >= 4 and f <= 6 and 20 or 0)
                        if row >= 2 and row <= 4 then v = v + 20 end
                    end
                elseif t == 4 then
                    v = v + (4 - centre) * 8 + (row <= 5 and 20 or 0) - (row == 10 and 20 or 0)
                elseif t == 5 then
                    v = v + (4 - centre) * 3 + (row <= 5 and 15 or 0)
                elseif f == 5 then
                    v = v + 15
                end
            end
            if black then score = score - v else score = score + v end
        end
    end
    return g.side == C.RED and score or -score
end
AI.evaluate = function(g)
    setValues(g)
    return evaluate(g)
end

local stack = {}       -- moves of every ply in the search, one segment each
local killers = {}     -- per ply, the last quiet move that caused a cut-off
local nodes, deadline, clock, stopped = 0, 0, nil, false

local function timeUp()
    nodes = nodes + 1
    if nodes % 128 == 0 and clock and clock() > deadline then stopped = true end
    return stopped
end

-- Generates moves at stack[base+1..], tags them with an ordering score and
-- sorts them best first. Returns the new top.
local function generate(g, base, noisy, first, killer)
    local n = C.pseudoMoves(g, stack, noisy, base)
    local b = g.board
    for i = base + 1, base + n do
        local mv = stack[i]
        local victim = b[mv // 256]
        local score = 0
        -- Most valuable victim, least valuable attacker first.
        if victim ~= EMPTY then
            local a = b[mv % 256]
            score = PV[victim] // 10 + 20 - PV[a] // 50
        end
        if mv == first then score = 1000 elseif score == 0 and mv == killer then score = 15 end
        stack[i] = mv + score * MOVE_BITS
    end
    -- Insertion sort, descending (the tag dominates the comparison).
    for i = base + 2, base + n do
        local v = stack[i]
        local j = i - 1
        while j > base and stack[j] < v do
            stack[j + 1] = stack[j]
            j = j - 1
        end
        stack[j + 1] = v
    end
    return base + n
end

local function quiesce(g, alpha, beta, ply, base)
    if timeUp() then return 0 end
    local stand = evaluate(g)
    if stand >= beta then return beta end
    if stand > alpha then alpha = stand end
    if ply >= MAX_PLY then return alpha end
    local top = generate(g, base, true)
    local side = g.side
    local b = g.board
    for i = base + 1, top do
        local mv = stack[i] % MOVE_BITS
        local victim = b[mv // 256]
        -- Delta pruning: a capture that cannot lift the score to alpha is skipped.
        if stand + PV[victim] + 200 > alpha then
            C.make(g, mv)
            if not C.inCheck(g, side) then
                local v = -quiesce(g, -beta, -alpha, ply + 1, top)
                C.unmake(g, mv)
                if stopped then return 0 end
                if v >= beta then return beta end
                if v > alpha then alpha = v end
            else
                C.unmake(g, mv)
            end
        end
    end
    return alpha
end

local function search(g, depth, alpha, beta, ply, base)
    if depth <= 0 or ply >= MAX_PLY then return quiesce(g, alpha, beta, ply, base) end
    if timeUp() then return 0 end
    local top = generate(g, base, false, nil, killers[ply])
    local side = g.side
    local legal = 0
    for i = base + 1, top do
        local mv = stack[i] % MOVE_BITS
        C.make(g, mv)
        if not C.inCheck(g, side) then
            legal = legal + 1
            local v = -search(g, depth - 1, -beta, -alpha, ply + 1, top)
            C.unmake(g, mv)
            if stopped then return 0 end
            if v >= beta then
                if g.board[mv // 256] == EMPTY then killers[ply] = mv end -- a quiet move that refuted
                return beta
            end
            if v > alpha then alpha = v end
        else
            C.unmake(g, mv)
        end
    end
    -- No legal move loses in xiangqi, stalemate included.
    if legal == 0 then return -MATE + ply end
    return alpha
end

-- Whether the side to move has a legal move (moves go at stack[base+1..]).
local function hasMove(g, base)
    local n = C.pseudoMoves(g, stack, false, base)
    local side = g.side
    for i = base + 1, base + n do
        local mv = stack[i]
        C.make(g, mv)
        local ok = not C.inCheck(g, side)
        C.unmake(g, mv)
        if ok then return true end
    end
    return false
end

-- The move for the side to move, and the nodes searched. `randomPercent`
-- plays a random move that often instead (never when a mate is in reach).
-- `now`/`timeMs` (optional) bound the time.
function AI.choose(g, depth, randomPercent, rand, now, timeMs)
    setValues(g)
    -- The legal moves, in the search's own array (no list of their own).
    local n, k, side = C.pseudoMoves(g, stack, false, 0), 0, g.side
    for i = 1, n do
        local mv = stack[i]
        C.make(g, mv)
        if not C.inCheck(g, side) then
            k = k + 1
            stack[k] = mv
        end
        C.unmake(g, mv)
    end
    if k <= 1 then
        local only = stack[1]
        stack = {}
        return k == 1 and only or nil, 0
    end
    local pick = (randomPercent > 0 and rand(100) <= randomPercent) and stack[rand(k)] or nil
    nodes, stopped, clock = 0, false, now
    for i = 1, MAX_PLY do killers[i] = 0 end
    deadline = now and (now() + (timeMs or 3000)) or 0
    local best, bestScore = stack[1], 0
    for d = 1, depth do
        local top = generate(g, 0, false, best)
        local alpha, choice = -MATE - 1, nil
        for i = 1, top do
            local mv = stack[i] % MOVE_BITS
            C.make(g, mv)
            if not C.inCheck(g, side) then
                local v
                if d == 1 and not hasMove(g, top) then
                    v = MATE - 1 -- quiescence alone would not see the win
                else
                    v = -search(g, d - 1, -MATE - 1, -alpha, 1, top)
                end
                C.unmake(g, mv)
                if stopped then break end
                if v > alpha or not choice then alpha, choice = v, mv end
            else
                C.unmake(g, mv)
            end
        end
        if stopped and d > 1 then break end
        stopped = false -- even a cut-short first depth leaves a move
        best, bestScore = choice or best, alpha
        if alpha >= MATE - 100 then break end -- a forced mate is found
    end
    -- Let the search's move array go: it is the biggest thing in memory.
    stack = {}
    if pick and bestScore < MATE - 100 then best = pick end
    return best, nodes
end

return AI
end
