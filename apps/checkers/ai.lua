-- Checkers computer opponent. Usage: local AI = smudge.dofile("ai.lua")(C),
-- with C the rules from logic.lua.
--
-- Negamax with alpha-beta pruning, deepening one ply at a time until the
-- level's depth, its node budget or its time is used up (the reader is far
-- slower than a computer, so time is what bounds the thinking there); the
-- last fully searched depth decides.

return function(C)
local AI = {}

local MAN, KING = 100, 160
local WIN = 100000
local MAX_PLY = 7

-- Score of `board` for `side`: material, men's advance, a guarded back row,
-- and - when ahead - kings closing in on what is left, so a won ending is
-- actually finished rather than shuffled until the draw rule.
local function evaluate(board, side)
    local score, mine, theirs = 0, 0, 0
    for i = 1, 64 do
        local v = board[i]
        if v ~= 0 then
            local r = C.rowOf(i)
            local s
            if C.isKing(v) then
                s = KING
            else
                -- Rows advanced (0..6) and the home row kept as a guard.
                local adv = (v == 1) and (8 - r) or (r - 1)
                s = MAN + adv * 3
                if adv == 0 then s = s + 6 end
            end
            local c = C.colOf(i)
            if c >= 3 and c <= 6 and r >= 3 and r <= 6 then s = s + 4 end
            if C.owner(v) == side then
                score, mine = score + s, mine + s
            else
                score, theirs = score - s, theirs + s
            end
        end
    end
    if mine > theirs + 50 then
        -- Kings: the closer to the nearest enemy piece, the better.
        for i = 1, 64 do
            local v = board[i]
            if v ~= 0 and C.isKing(v) and C.owner(v) == side then
                local best = 99
                for j = 1, 64 do
                    local u = board[j]
                    if u ~= 0 and C.owner(u) ~= side then
                        local d = math.max(math.abs(C.colOf(i) - C.colOf(j)), math.abs(C.rowOf(i) - C.rowOf(j)))
                        if d < best then best = d end
                    end
                end
                score = score - best * 2
            end
        end
    end
    return score
end
AI.evaluate = evaluate

local nodes, budget, aborted = 0, 0, false
local clock, deadline = nil, 0

-- Plays `move` on `board`, keeping what taking it back needs in `saved`
-- (one reused array per search depth, so the search allocates no tables).
local savedStack = {}

local function make(board, move, ply)
    local saved = savedStack[ply]
    if not saved then
        saved = {}
        savedStack[ply] = saved
    end
    saved[1], saved[2] = board[C.from(move)], board[C.to(move)]
    for k = 1, C.capCount(move) do saved[2 + k] = board[C.capAt(move, k)] end
    C.apply(board, move)
    return saved
end

local function unmake(board, move, saved)
    board[C.to(move)] = saved[2]
    for k = 1, C.capCount(move) do board[C.capAt(move, k)] = saved[2 + k] end
    board[C.from(move)] = saved[1]
end

local function negamax(board, side, depth, alpha, beta, ply)
    nodes = nodes + 1
    if nodes > budget or (nodes % 128 == 0 and clock and clock() > deadline) then
        aborted = true
        return 0
    end
    local moves = C.movesFor(board, side)
    if #moves == 0 then return -WIN + ply end
    -- Captures are forced and search on (no horizon in the middle of an
    -- exchange); quiet positions stop at depth 0. MAX_PLY bounds the
    -- recursion: each level deepens the Lua stack, which on the X3 is memory.
    if (depth <= 0 and C.capCount(moves[1]) == 0) or ply >= MAX_PLY then return evaluate(board, side) end
    for _, mv in ipairs(moves) do
        local saved = make(board, mv, ply)
        local v = -negamax(board, 3 - side, depth - 1, -beta, -alpha, ply + 1)
        unmake(board, mv, saved)
        if aborted then return 0 end
        if v > alpha then alpha = v end
        if alpha >= beta then break end
    end
    return alpha
end

-- The move for the side to move. `depth` is the deepest search, `maxNodes`
-- the most positions to look at; `randomPercent` of moves are random.
-- `now` (optional) returns milliseconds; the search stops after `timeMs`.
function AI.choose(g, depth, maxNodes, randomPercent, rand, now, timeMs)
    local moves = C.moves(g)
    if #moves == 0 then return nil end
    if #moves == 1 then return moves[1] end
    if randomPercent > 0 and rand(100) <= randomPercent then return moves[rand(#moves)] end
    local board = {}
    for i = 1, 64 do board[i] = g.board[i] end
    local best = moves[1]
    nodes, budget = 0, maxNodes
    clock, deadline = now, now and (now() + timeMs) or 0
    for d = 1, depth do
        aborted = false
        local alpha, choice = -WIN - 1, nil
        -- Search the previous best first: it makes the cut-offs sharper.
        local order = { best }
        for _, mv in ipairs(moves) do
            if mv ~= best then order[#order + 1] = mv end
        end
        for _, mv in ipairs(order) do
            local saved = make(board, mv, 0)
            local v = -negamax(board, 3 - g.turn, d - 1, -WIN - 1, -alpha, 1)
            unmake(board, mv, saved)
            if aborted then break end
            if v > alpha then alpha, choice = v, mv end
        end
        if aborted then break end
        best = choice or best
        if alpha >= WIN - 100 then break end -- a forced win is found
    end
    return best, nodes
end

return AI
end
