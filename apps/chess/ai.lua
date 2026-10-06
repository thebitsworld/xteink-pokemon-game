-- Chess computer opponent. Usage: local AI = smudge.dofile("ai.lua")(C),
-- with C the rules from logic.lua.
--
-- Negamax with alpha-beta pruning and a capture-only quiescence search,
-- deepening one ply at a time until the level's depth or its time is used
-- up; the last fully searched depth decides. The evaluation is material plus
-- the well-known "simplified evaluation function" piece-square tables.
--
-- Memory is tight on the X3, so the search allocates nothing per node:
-- every ply's moves go into one shared array, each tagged with its ordering
-- score in the bits above the move (sorting the integers sorts the moves),
-- and the recursion depth is capped.

return function(C)
local AI = {}

local VALUE = { 100, 320, 330, 500, 900, 0 }
-- Piece-square tables, white's view from a8 to h1, as (value / 2 + 60).
local PST = {
    "<<<<<<<<UUUUUUUUAAFKKFAA>>AHHA>><<<FF<<<>:7<<7:>>AA22AA><<<<<<<<",
    "#(----(#(2<<<<2(-<ADDA<-->DFFD>--<DFFD<-->ADDA>-(2<>><2(#(----(#",
    "277777727<<<<<<77<>AA><77>>AA>>77<AAAA<77AAAAAA77><<<<>727777772",
    "<<<<<<<<>AAAAAA>:<<<<<<::<<<<<<::<<<<<<::<<<<<<::<<<<<<:<<<>><<<",
    "277::7727<<<<<<77<>>>><7:<>>>><:<<>>>><:7>>>>><77<><<<<7277::772",
    "-((##((--((##((--((##((--((##((-2--((--272222227FF<<<<FFFKA<<AKF",
}
local KING_END = "#(-22-(#-27<<72--7FKKF7--7KPPK7--7KPPK7--7FKKF7---<<<<--#------#"
local MATE = 30000
local MAX_PLY = 7       -- search plus quiescence: each level deepens the Lua stack
local MOVE_BITS = 1048576 -- 2^20: moves fit below, ordering scores go above

-- Score from the side to move's point of view.
local function evaluate(g)
    local b = g.board
    local score, material = 0, 0
    for sq = 21, 98 do
        local p = b[sq]
        if p ~= C.EMPTY and p ~= C.OFF and p ~= C.KING and p ~= C.KING + 8 then
            material = material + VALUE[C.typeOf(p)]
        end
    end
    local endgame = material <= 2600
    for sq = 21, 98 do
        local p = b[sq]
        if p ~= C.EMPTY and p ~= C.OFF then
            local t, white = C.typeOf(p), p < 8
            local idx = (8 - C.rankOf(sq)) * 8 + C.fileOf(sq) -- 1..64 from a8, white's view
            if not white then idx = (C.rankOf(sq) - 1) * 8 + C.fileOf(sq) end
            local table_ = (t == C.KING and endgame) and KING_END or PST[t]
            local v = VALUE[t] + (table_:byte(idx) - 60) * 2
            if white then score = score + v else score = score - v end
        end
    end
    return g.side == C.WHITE and score or -score
end
AI.evaluate = evaluate

local stack = {}       -- moves of every ply in the search, one segment each
local nodes, deadline, clock, stopped = 0, 0, nil, false

local function timeUp()
    nodes = nodes + 1
    if nodes % 256 == 0 and clock and clock() > deadline then stopped = true end
    return stopped
end

-- Generates moves at stack[base+1..], tags them with an ordering score and
-- sorts them best first. Returns the new top.
local function generate(g, base, noisy, first)
    local n = C.pseudoMoves(g, stack, noisy, base)
    local b = g.board
    for i = base + 1, base + n do
        local mv = stack[i]
        local victim = b[C.to(mv)]
        local score = 0
        -- Most valuable victim, least valuable attacker first.
        if victim ~= C.EMPTY then score = VALUE[C.typeOf(victim)] // 10 + 64 - C.typeOf(b[C.from(mv)]) end
        if C.promo(mv) == C.QUEEN then score = score + 90 end
        if C.flag(mv) == C.EN_PASSANT then score = score + 64 end
        if mv == first then score = 1000 end
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
    for i = base + 1, top do
        local mv = stack[i] % MOVE_BITS
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
    return alpha
end

local function search(g, depth, alpha, beta, ply, base)
    if depth <= 0 or ply >= MAX_PLY then return quiesce(g, alpha, beta, ply, base) end
    if timeUp() then return 0 end
    local top = generate(g, base, false)
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
            if v >= beta then return beta end
            if v > alpha then alpha = v end
        else
            C.unmake(g, mv)
        end
    end
    if legal == 0 then
        if C.inCheck(g, side) then return -MATE + ply end
        return 0
    end
    return alpha
end

-- The move for the side to move. `now`/`timeMs` (optional) bound the time.
function AI.choose(g, depth, randomPercent, rand, now, timeMs)
    local legal = C.legalMoves(g)
    if #legal == 0 then return nil end
    if #legal == 1 then return legal[1] end
    if randomPercent > 0 and rand(100) <= randomPercent then return legal[rand(#legal)] end
    nodes, stopped, clock = 0, false, now
    deadline = now and (now() + (timeMs or 2000)) or 0
    local best = legal[1]
    for d = 1, depth do
        local top = generate(g, 0, false, best)
        local side = g.side
        local alpha, choice = -MATE - 1, nil
        for i = 1, top do
            local mv = stack[i] % MOVE_BITS
            C.make(g, mv)
            if not C.inCheck(g, side) then
                local v = -search(g, d - 1, -MATE - 1, -alpha, 1, top)
                C.unmake(g, mv)
                if stopped then break end
                if v > alpha or not choice then alpha, choice = v, mv end
            else
                C.unmake(g, mv)
            end
        end
        if stopped then break end
        best = choice or best
        if alpha >= MATE - 100 then break end -- a forced mate is found
    end
    -- Let the search's move array go: it is the biggest thing in memory.
    stack = {}
    return best, nodes
end

return AI
end
