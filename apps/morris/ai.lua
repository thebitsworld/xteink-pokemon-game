-- Nine Men's Morris computer opponent. Usage: local AI = smudge.dofile("ai.lua")(C),
-- with C the rules from logic.lua.
--
-- Negamax with alpha-beta pruning over whole moves (a move that closes a mill
-- comes once for each piece it may remove), deepening one ply at a time until
-- the level's depth, node budget or time is used up; the last fully searched
-- depth decides. Mills and killer moves are tried first.
--
-- The evaluation, from the side to move's view: pieces (on the board and in
-- hand), mills, open twos (two of a colour and an empty point on a line),
-- double mills (two mills sharing a piece), running mills (a piece of a mill
-- that can step out and close another, then step back), mobility and blocked
-- pieces - a player that cannot move loses.
--
-- The search allocates nothing per node: every ply's moves go into one shared
-- array, each tagged with its ordering score above the move bits.

return function(C)
local AI = {}

local WIN = 10000
local MAX_PLY = 14
local TAG = 32768 -- moves fit below, ordering scores go above
local PL1, PL2, ADJ = C.PL1, C.PL2, C.ADJ

local stack = {}
local killers = {}
local nodes, maxNodes, deadline, clock, stopped = 0, 0, 0, nil, false

-- Score from the side to move's point of view.
local function evaluate(g)
    local s = g.side
    local o = 3 - s
    local lc, b, on, hand = g.lc, g.b, g.on, g.hand
    local placing = hand[1] + hand[2] > 0
    local score = (on[s] + hand[s] - on[o] - hand[o]) * 100
    -- Lines: mills and open twos.
    local mills, twos = 0, 0
    for l = 1, 16 do
        local cs, co = lc[l * 2 - 2 + s], lc[l * 2 - 2 + o]
        if cs == 3 then
            mills = mills + 1
        elseif co == 3 then
            mills = mills - 1
        elseif co == 0 and cs == 2 then
            twos = twos + 1
        elseif cs == 0 and co == 2 then
            twos = twos - 1
        end
    end
    score = score + mills * (placing and 14 or 20) + twos * (placing and 12 or 8)
    -- Points: mobility, blocked pieces, double and running mills.
    local mobS, mobO, blkS, blkO, dbl = 0, 0, 0, 0, 0
    for p = 1, 24 do
        local c = b[p]
        if c ~= 0 then
            local a, free = ADJ[p], 0
            while a > 0 do
                if b[a % 32] == 0 then free = free + 1 end
                a = a // 32
            end
            local l1, l2 = PL1[p] * 2 - 2 + c, PL2[p] * 2 - 2 + c
            local m1, m2 = lc[l1] == 3, lc[l2] == 3
            local d = 0
            if m1 and m2 then d = 3 end
            if (m1 or m2) and free > 0 then
                -- Stepping out along the other line onto a point whose
                -- other line already has two of ours and none of theirs.
                local other = m1 and PL2[p] or PL1[p]
                a = ADJ[p]
                while a > 0 do
                    local e = a % 32
                    a = a // 32
                    if b[e] == 0 then
                        local le = PL1[e] == other and PL2[e] or PL1[e]
                        if lc[le * 2 - 2 + c] == 2 and lc[le * 2 - 2 + (3 - c)] == 0 then d = d + 4 end
                    end
                end
            end
            if c == s then
                mobS, dbl = mobS + free, dbl + d
                if free == 0 then blkS = blkS + 1 end
            else
                mobO, dbl = mobO + free, dbl - d
                if free == 0 then blkO = blkO + 1 end
            end
        end
    end
    score = score + dbl * 8
    local fs, fo = C.phase(g, s), C.phase(g, o)
    if fs ~= 3 then
        score = score + mobS * 2 - blkS * 4
        if fs == 2 and mobS == 0 then score = score - 300 end
    end
    if fo ~= 3 then
        score = score - mobO * 2 + blkO * 4
        if fo == 2 and mobO == 0 then score = score + 300 end
    end
    return score
end
AI.evaluate = evaluate

local function timeUp()
    nodes = nodes + 1
    if nodes % 64 == 0 and ((clock and clock() > deadline) or nodes >= maxNodes) then stopped = true end
    return stopped
end

-- Generates moves at stack[base+1..], tags them with an ordering score and
-- sorts them best first. Returns the new top.
local function generate(g, base, first, killer)
    local n = C.gen(g, stack, base)
    for i = base + 1, base + n do
        local mv = stack[i]
        local score = mv >= 1024 and 20 or 0
        if mv == first then score = 100 elseif mv == killer then score = score + 10 end
        stack[i] = mv + score * TAG
    end
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

local function search(g, depth, alpha, beta, ply, base)
    if timeUp() then return 0 end
    if C.lost(g) then return -WIN + ply end
    if depth <= 0 or ply >= MAX_PLY then return evaluate(g) end
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
        local alpha, choice = -WIN - 1, nil
        for i = 1, top do
            local mv = stack[i] % TAG
            -- A mill now is worth a little more than the same mill later:
            -- the search would otherwise put it off past its horizon.
            local bonus = mv >= 1024 and 20 or 0
            C.make(g, mv)
            local v = -search(g, d - 1, -WIN - 1, bonus - alpha, 1, top) + bonus
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
