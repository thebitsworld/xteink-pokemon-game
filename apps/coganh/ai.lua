-- Co Ganh computer opponent. Usage: local AI = smudge.dofile("ai.lua")(C),
-- with C the rules from logic.lua.
--
-- Negamax with alpha-beta pruning, deepening one ply at a time until the
-- level's depth, node budget or time is used up; the last fully searched
-- depth decides. At the horizon a short capture search plays out the ganh
-- moves (up to QDEPTH plies) so a conversion is not left half seen. Moves
-- that ganh are tried first, then killer moves.
--
-- The evaluation, from the side to move's view: pieces (each conversion
-- moves two: one less for them, one more for us), mobility (empty points next
-- to a piece), pieces with a single way out (one move from being
-- surrounded), strong points held, and ganh threats - the biggest ganh each
-- side could play next.
--
-- The search allocates nothing per node: every ply's moves go into one shared
-- array, each tagged with its ordering score above the move bits. make/unmake
-- keep the converted pieces on the game's own undo list.

return function(C)
local AI = {}

local WIN = 10000
local MAX_PLY = 24
local QDEPTH = 4
local DRAW_COST = 20
local TAG = 1024 -- moves fit below, ordering scores go above
local NB = C.NB
local ganhCount = C.ganhCount

local STRONG = {}
for p = 1, 25 do STRONG[p] = C.strong(p) and 1 or 0 end

local stack = {}
local killers = {}
local nodes, maxNodes, deadline, clock, stopped = 0, 0, 0, nil, false

-- Score from the side to move's point of view.
local function evaluate(g)
    local s = g.side
    local o = 3 - s
    local b, on = g.b, g.on
    if on[s] == 0 then return -WIN end
    if on[o] == 0 then return WIN end
    local score = (on[s] - on[o]) * 100
    local mobS, mobO, oneS, oneO, strS, strO, thS, thO = 0, 0, 0, 0, 0, 0, 0, 0
    for p = 1, 25 do
        local v, x = b[p], (p - 1) * 8
        if v == 0 then
            -- Could a piece next to p step in and ganh?
            local nearS, nearO = false, false
            for d = 1, 8 do
                local q = NB[x + d]
                if q > 0 then
                    if b[q] == s then nearS = true elseif b[q] == o then nearO = true end
                end
            end
            if nearS then
                local k = ganhCount(b, p, s)
                if k > thS then thS = k end
            end
            if nearO then
                local k = ganhCount(b, p, o)
                if k > thO then thO = k end
            end
        else
            local free = 0
            for d = 1, 8 do
                local q = NB[x + d]
                if q > 0 and b[q] == 0 then free = free + 1 end
            end
            if v == s then
                mobS, strS = mobS + free, strS + STRONG[p]
                if free <= 1 then oneS = oneS + 1 end
            else
                mobO, strO = mobO + free, strO + STRONG[p]
                if free <= 1 then oneO = oneO + 1 end
            end
        end
    end
    -- Against a few pieces, taking away their room is how they are won.
    local wS, wO = on[o] <= 4 and 10 or 3, on[s] <= 4 and 10 or 3
    return score + mobS * wO - mobO * wS + (strS - strO) * 4 - (oneS - oneO) * 6 + thS * 40 - thO * 20
end
AI.evaluate = evaluate

-- How many times the position after a root move was already seen in the
-- real game (g.reps: game.lua's keys, 15 characters each).
local function seen(g)
    local reps = g.reps
    if not reps or reps == "" then return 0 end
    local w, k, b = 0, 0, g.b
    for p = 25, 1, -1 do
        w = w * 2 + (b[p] == 1 and 1 or 0)
        k = k * 2 + (b[p] == 2 and 1 or 0)
    end
    local key, count = string.format("%08x%07x", w * 2 + g.side - 1, k), 0
    for i = 1, #reps, 15 do
        if reps:sub(i, i + 14) == key then count = count + 1 end
    end
    return count
end

local function timeUp()
    nodes = nodes + 1
    if nodes % 64 == 0 and ((clock and clock() > deadline) or nodes >= maxNodes) then stopped = true end
    return stopped
end

-- Generates moves at stack[base+1..], tags them with an ordering score and
-- sorts them best first; with `captures` only the ganh moves are kept.
-- Returns the new top.
local function generate(g, base, first, killer, captures)
    local n = C.gen(g, stack, base)
    local b, s, top = g.b, g.side, base
    for i = base + 1, base + n do
        local mv = stack[i]
        local k = ganhCount(b, mv // 32, s)
        if k > 0 or not captures then
            local score = k * 10
            if mv == first then score = 200 elseif mv == killer then score = score + 5 end
            top = top + 1
            stack[top] = mv + score * TAG
        end
    end
    for i = base + 2, top do
        local v = stack[i]
        local j = i - 1
        while j > base and stack[j] < v do
            stack[j + 1] = stack[j]
            j = j - 1
        end
        stack[j + 1] = v
    end
    return top
end

-- The capture search: stand pat, or play a ganh.
local function quiesce(g, alpha, beta, qd, base)
    if timeUp() then return 0 end
    local v = evaluate(g)
    if v >= beta or qd <= 0 or v >= WIN or v <= -WIN then return v end
    if v > alpha then alpha = v end
    local top = generate(g, base, nil, nil, true)
    for i = base + 1, top do
        local mv = stack[i] % TAG
        C.make(g, mv)
        local w = -quiesce(g, -beta, -alpha, qd - 1, top)
        C.unmake(g, mv)
        if stopped then return 0 end
        if w >= beta then return w end
        if w > alpha then alpha = w end
    end
    return alpha
end

local function search(g, depth, alpha, beta, ply, base)
    if timeUp() then return 0 end
    if g.on[g.side] == 0 then return -WIN + ply end
    if depth <= 0 or ply >= MAX_PLY then return quiesce(g, alpha, beta, QDEPTH, base) end
    local top = generate(g, base, nil, killers[ply])
    if top == base then return -WIN + ply end -- no legal move
    for i = base + 1, top do
        local mv = stack[i] % TAG
        C.make(g, mv)
        local v = -search(g, depth - 1, -beta, -alpha, ply + 1, top)
        C.unmake(g, mv)
        if stopped then return 0 end
        if v >= beta then
            killers[ply] = mv
            return beta
        end
        if v > alpha then alpha = v end
    end
    return alpha
end

-- The move for the side to move, and the nodes searched. L: { depth = ,
-- random = percent of moves played at random, nodes = budget }. `now` and
-- `timeMs` (optional) bound the time.
function AI.choose(g, L, rand, now, timeMs)
    local k = C.gen(g, stack, 0)
    if k <= 1 then
        local only = stack[1]
        stack = {}
        return k == 1 and only or nil, 0
    end
    if (L.random or 0) > 0 and rand(100) <= L.random then
        local mv = stack[rand(k)]
        stack = {}
        return mv, 0
    end
    nodes, stopped, clock, maxNodes = 0, false, now, L.nodes or 100000
    for i = 1, MAX_PLY do killers[i] = 0 end
    deadline = now and (now() + (timeMs or 1500)) or 0
    local best, bestScore = stack[1], 0
    for d = 1, math.min(L.depth, MAX_PLY) do
        local top = generate(g, 0, best)
        if d == 1 then
            -- Shuffled, so equal moves vary from game to game (later depths
            -- try the best so far first, and keep it on a tie).
            for i = top, 2, -1 do
                local j = rand(i)
                stack[i], stack[j] = stack[j], stack[i]
            end
        end
        local alpha, choice = -WIN - 1, nil
        for i = 1, top do
            local mv = stack[i] % TAG
            C.make(g, mv)
            -- A repeated position: a third time is a draw (taken only when
            -- the rest is worse than a little behind), and going round again
            -- gives up part of a lead.
            local rep = seen(g)
            local v
            if rep >= 2 then
                v = -DRAW_COST
            else
                v = -search(g, d - 1, -WIN - 1, -alpha, 1, top)
                if rep == 1 then v = (v > 0 and v // 2 or v) - DRAW_COST end
            end
            C.unmake(g, mv)
            if stopped then break end
            if v > alpha or not choice then alpha, choice = v, mv end
        end
        if stopped and d > 1 then break end
        best, bestScore = choice or best, alpha
        if stopped then break end
        if alpha >= WIN - 100 or alpha <= -WIN + 100 then break end -- decided
    end
    stack = {}
    return best, nodes, bestScore
end

return AI
end
