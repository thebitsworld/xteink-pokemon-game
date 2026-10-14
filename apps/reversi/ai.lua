-- Reversi computer opponent. Usage: local AI = smudge.dofile("ai.lua")(L),
-- with L the rules from logic.lua; main.lua loads it for one move and drops
-- it after.
--
--   Easy:   greedy - the move that flips the most discs, with some noise.
--   Normal: alpha-beta 3 plies deep (2 if time runs short); positional
--           weights (corners good, the squares next to an empty corner bad)
--           plus mobility (how many moves each side has).
--   Hard:   the same search deepening up to 6 plies, and with 12 or fewer
--           empty squares up to the end of the game, which is then perfect
--           play (most discs). A node cap and a clock bound it; the last
--           depth searched in full decides.
--
-- The search allocates nothing per node: every ply's moves go into one
-- shared array (tagged with an ordering key above the square) and the
-- flipped discs onto one shared stack.

return function(L)
local AI = {}

local EMPTY = L.EMPTY
local WIN = 10000
local INF = 1000000

AI.LEVELS = {
    { depth = 0 },                                     -- Easy: greedy
    { depth = 3, nodes = 6000, ms = 1000 },            -- Normal
    { depth = 6, nodes = 40000, ms = 2000, exact = 12 }, -- Hard
}

-- Positional weights, row by row from a1.
local ROWS = {
    { 100, -20, 10, 5, 5, 10, -20, 100 },
    { -20, -50, -2, -2, -2, -2, -50, -20 },
    { 10, -2, 1, 1, 1, 1, -2, 10 },
    { 5, -2, 1, 0, 0, 1, -2, 5 },
    { 5, -2, 1, 0, 0, 1, -2, 5 },
    { 10, -2, 1, 1, 1, 1, -2, 10 },
    { -20, -50, -2, -2, -2, -2, -50, -20 },
    { 100, -20, 10, 5, 5, 10, -20, 100 },
}
local WT = {}
for i = 0, 99 do WT[i] = 0 end
for r = 1, 8 do
    for c = 1, 8 do WT[r * 10 + c] = ROWS[r][c] end
end
ROWS = nil
-- Each corner with the three squares next to it, whose penalty only holds
-- while the corner is empty.
local CORNERS = { 11, 12, 21, 22, 18, 17, 28, 27, 81, 82, 71, 72, 88, 87, 78, 77 }
AI.WT = WT

local mstack, fstack = {}, {}
local ftop = 0
local nodes, cap, deadline, clock, stopped = 0, 0, 0, nil, false

local function timeUp()
    nodes = nodes + 1
    if nodes >= cap then stopped = true
    elseif clock and nodes % 64 == 0 and clock() > deadline then stopped = true end
    return stopped
end

local function mobility(b, side)
    local n, canPlay = 0, L.canPlay
    for sq = 11, 88 do
        if b[sq] == EMPTY and canPlay(b, sq, side) then n = n + 1 end
    end
    return n
end

-- Final score for `side`: a win is worth more than any evaluation.
local function final(b, side)
    local nb, nw = L.counts(b)
    local d = side == L.BLACK and nb - nw or nw - nb
    if d > 0 then return WIN + d elseif d < 0 then return -WIN + d end
    return 0
end

-- Score from `side`'s point of view.
local function evaluate(b, side)
    local opp = 3 - side
    local s, empties = 0, 0
    for sq = 11, 88 do
        local v = b[sq]
        if v == side then s = s + WT[sq] elseif v == opp then s = s - WT[sq] elseif v == EMPTY then empties = empties + 1 end
    end
    if empties == 0 then return final(b, side) end
    for k = 1, 16, 4 do
        if b[CORNERS[k]] ~= EMPTY then
            for j = k + 1, k + 3 do
                local sq = CORNERS[j]
                local v = b[sq]
                if v == side then s = s - WT[sq] + 5 elseif v == opp then s = s + WT[sq] - 5 end
            end
        end
    end
    return s + 8 * (mobility(b, side) - mobility(b, opp))
end
AI.evaluate = evaluate

-- Moves of `side` at mstack[top+1..], best-looking first. Returns how many.
local function generate(b, side, top)
    local n = L.moves(b, side, mstack, top)
    for i = top + 1, top + n do
        local sq = mstack[i]
        local key = (WT[sq] + 64) * 128 + sq
        local j = i - 1
        while j > top and mstack[j] < key do
            mstack[j + 1] = mstack[j]
            j = j - 1
        end
        mstack[j + 1] = key
    end
    return n
end

local function search(b, side, depth, alpha, beta, top)
    if timeUp() then return 0 end
    if depth <= 0 then return evaluate(b, side) end
    local opp = 3 - side
    local n = generate(b, side, top)
    if n == 0 then
        if not L.hasMove(b, opp) then return final(b, side) end
        return -search(b, opp, depth, -beta, -alpha, top) -- a pass costs no depth
    end
    local best = -INF
    for i = top + 1, top + n do
        local sq = mstack[i] % 128
        local old = ftop
        ftop = L.make(b, sq, side, fstack, old)
        local v = -search(b, opp, depth - 1, -beta, -alpha, top + n)
        L.unmake(b, sq, side, fstack, old, ftop)
        ftop = old
        if stopped then return 0 end
        if v > best then
            best = v
            if v > alpha then
                alpha = v
                if alpha >= beta then break end
            end
        end
    end
    return best
end

-- One full-width search of the root moves (in `root`, best guess first).
-- Returns the best move and its score; the move is nil when stopped
-- before the first move was searched.
local function rootSearch(b, side, depth, root, n)
    local alpha, bestSq, bestV = -INF, nil, -INF
    for i = 1, n do
        local sq = root[i]
        ftop = L.make(b, sq, side, fstack, 0)
        local v = -search(b, 3 - side, depth - 1, -INF, -alpha, 0)
        L.unmake(b, sq, side, fstack, 0, ftop)
        ftop = 0
        if stopped then break end
        if v > bestV then bestSq, bestV = sq, v end
        if v > alpha then alpha = v end
    end
    return bestSq, bestV
end

local function greedy(g, rand)
    local b, side = g.b, g.side
    local best, bestV = nil, -INF
    for sq = 11, 88 do
        local f = L.flipCount(b, sq, side)
        if f > 0 then
            local v = f * 3 + rand(8)
            if v > bestV then best, bestV = sq, v end
        end
    end
    return best
end

-- The device's move for game `g` at `level` (1..3). `rand(n)` gives 1..n,
-- `clockFn` the time in ms (nil: only the node cap bounds the search),
-- `nodeCap` overrides the level's cap (the tests use it).
function AI.choose(g, level, rand, clockFn, nodeCap)
    local L3 = AI.LEVELS[level] or AI.LEVELS[2]
    if L3.depth == 0 then return greedy(g, rand) end
    local b, side = g.b, g.side
    AI.depthDone, AI.nodes = 0, 0
    -- The root moves, shuffled (equal scores then vary from game to game)
    -- and stably sorted by weight.
    local root = {}
    local n = L.moves(b, side, root, 0)
    if n == 0 then return nil end
    if n == 1 then return root[1] end
    for i = n, 2, -1 do
        local j = rand(i)
        root[i], root[j] = root[j], root[i]
    end
    for i = 2, n do
        local sq = root[i]
        local j = i - 1
        while j >= 1 and WT[root[j]] < WT[sq] do
            root[j + 1] = root[j]
            j = j - 1
        end
        root[j + 1] = sq
    end
    local _, _, empties = L.counts(b)
    local maxDepth = L3.depth
    if L3.exact and empties <= L3.exact then maxDepth = empties end
    nodes, cap, stopped, clock = 0, nodeCap or L3.nodes, false, clockFn
    deadline = clock and clock() + L3.ms or 0
    ftop = 0
    local best = root[1]
    for depth = 1, maxDepth do
        local sq = rootSearch(b, side, depth, root, n)
        if sq then
            best = sq
            if not stopped then AI.depthDone = depth end
        end
        if stopped then break end
        -- The best move first in the next depth.
        for i = 1, n do
            if root[i] == best then
                table.remove(root, i)
                table.insert(root, 1, best)
                break
            end
        end
    end
    AI.nodes = nodes
    return best
end

return AI
end
