-- Logic tests for apps/chess/logic.lua, ai.lua, history.lua and the notation in view.lua (run by ctest: LuaAppLogic_chess).
local C = dofile(APPS .. "/chess/logic.lua")
local AI = dofile(APPS .. "/chess/ai.lua")(C)
local History = dofile(APPS .. "/chess/history.lua")(C)
local V = dofile(APPS .. "/chess/view.lua")(C, {})

-- math.random, seeded: Lua here has 32-bit integers, so a hand-rolled LCG overflows.
math.randomseed(64)
local function rand(n) return math.random(1, n) end

-- A position from FEN (tests only).
local PIECES = { p = 1, n = 2, b = 3, r = 4, q = 5, k = 6 }
local function fromFen(fen)
    local g = C.new()
    local placement, side, castle, ep, half, full = fen:match("^(%S+) (%S+) (%S+) (%S+) ?(%d*) ?(%d*)")
    for sq = 21, 98 do
        if g.board[sq] ~= C.OFF then g.board[sq] = C.EMPTY end
    end
    local rank, file = 8, 1
    for ch in placement:gmatch(".") do
        if ch == "/" then
            rank, file = rank - 1, 1
        elseif ch:match("%d") then
            file = file + tonumber(ch)
        else
            local t = PIECES[ch:lower()]
            local p = (ch == ch:lower()) and t + 8 or t
            local sq = C.square(file, rank)
            g.board[sq] = p
            if t == C.KING then g.kings[(ch == ch:lower()) and 2 or 1] = sq end
            file = file + 1
        end
    end
    g.side = (side == "w") and C.WHITE or C.BLACK
    g.castle = 0
    if castle:find("K") then g.castle = g.castle | 1 end
    if castle:find("Q") then g.castle = g.castle | 2 end
    if castle:find("k") then g.castle = g.castle | 4 end
    if castle:find("q") then g.castle = g.castle | 8 end
    g.ep = 0
    if ep ~= "-" then g.ep = C.square(ep:byte(1) - 96, tonumber(ep:sub(2))) end
    g.half = tonumber(half) or 0
    g.reps = { C.hash(g) }
    return g
end

local function perft(g, depth)
    if depth == 0 then return 1 end
    local moves = {}
    local n = C.pseudoMoves(g, moves)
    local side, total = g.side, 0
    for i = 1, n do
        C.make(g, moves[i])
        if not C.inCheck(g, side) then total = total + perft(g, depth - 1) end
        C.unmake(g, moves[i])
    end
    return total
end

-- Move generation against the published perft counts.
do
    local start = C.new()
    assert(perft(start, 1) == 20 and perft(start, 2) == 400 and perft(start, 3) == 8902, "start position")
    local kiwipete = fromFen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq -")
    assert(perft(kiwipete, 1) == 48, "kiwipete 1")
    assert(perft(kiwipete, 2) == 2039, "kiwipete 2")
    assert(perft(kiwipete, 3) == 97862, "kiwipete 3")
    local pos3 = fromFen("8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - -")
    assert(perft(pos3, 4) == 43238, "position 3 (en passant, pins)")
    local pos4 = fromFen("r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1")
    assert(perft(pos4, 3) == 9467, "position 4 (promotions, castling)")
    -- make/unmake leave the position exactly as it was.
    assert(C.hash(kiwipete) == C.hash(fromFen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq -")))
end

local function sq(name) return C.square(name:byte(1) - 96, tonumber(name:sub(2))) end
local function find(g, from, to, promo)
    for _, mv in ipairs(C.legalMoves(g)) do
        if C.from(mv) == sq(from) and C.to(mv) == sq(to) and (C.promo(mv) == (promo or C.promo(mv))) then return mv end
    end
    return nil
end
local function playAll(g, list)
    for _, m in ipairs(list) do
        local mv = find(g, m:sub(1, 2), m:sub(3, 4), m:sub(5, 5) ~= "" and PIECES[m:sub(5, 5)] or nil)
        assert(mv and C.play(g, mv), "illegal " .. m)
    end
end

-- Results: checkmate, stalemate, the fifty-move rule, repetition, material.
do
    local g = C.new()
    playAll(g, { "f2f3", "e7e5", "g2g4", "d8h4" })
    assert(g.result == "black" and g.reason == "checkmate")
    assert(not C.play(g, C.legalMoves(g)[1] or 0), "no moves after the end")

    g = fromFen("k7/8/1Q6/8/8/8/8/7K w - -")
    C.play(g, find(g, "b6", "c7"))
    assert(g.result == "draw" and g.reason == "stalemate", tostring(g.reason))

    g = fromFen("k7/8/8/8/8/8/8/6RK w - - 99 80")
    C.play(g, find(g, "g1", "g2"))
    assert(g.result == "draw" and g.reason:find("fifty"))

    g = fromFen("k7/8/8/8/8/8/8/6RK w - -")
    playAll(g, { "g1g2", "a8b8", "g2g1", "b8a8", "g1g2", "a8b8", "g2g1", "b8a8" })
    assert(g.result == "draw" and g.reason == "threefold repetition")

    g = fromFen("k7/8/8/8/8/8/1p6/1R5K w - -")
    C.play(g, find(g, "b1", "b2"))
    -- K+R vs K is not a draw...
    assert(not g.result)
    g = fromFen("k7/8/8/8/8/8/1p6/1N5K w - -")
    C.play(g, find(g, "b1", "d2"))
    C.play(g, find(g, "b2", "b1", C.KNIGHT))
    C.play(g, find(g, "d2", "b1"))
    assert(g.result == "draw" and g.reason:find("material"), "K+N vs K")
end

-- Castling rights, en passant timing, promotion, notation, replay.
do
    local g = C.new()
    playAll(g, { "e2e4", "e7e5", "g1f3", "b8c6", "f1c4", "g8f6" })
    local castle = find(g, "e1", "g1")
    assert(castle and C.flag(castle) == C.CASTLE)
    C.play(g, castle)
    assert(g.board[sq("f1")] == C.ROOK and g.board[sq("g1")] == C.KING and g.castle & 3 == 0)
    assert(V.describe(g, g.last) == "O-O")

    g = fromFen("4k3/8/8/8/1p6/8/P7/4K3 w - -")
    playAll(g, { "a2a4" })
    assert(find(g, "b4", "a3") and C.flag(find(g, "b4", "a3")) == C.EN_PASSANT)
    playAll(g, { "e8d8", "e1d1" })
    assert(not find(g, "b4", "a3"), "en passant only straight away")

    g = fromFen("8/P6k/8/8/8/8/8/K7 w - -")
    local queen, knight = find(g, "a7", "a8", C.QUEEN), find(g, "a7", "a8", C.KNIGHT)
    assert(queen and knight)
    C.play(g, knight)
    assert(g.board[sq("a8")] == C.KNIGHT and V.describe(g, g.last) == "a7-a8=N")

    g = C.new()
    playAll(g, { "e2e4", "d7d5", "e4d5" })
    assert(V.describe(g, g.last) == "e4xd5")
    local back = History.takeBack(g, 2)
    assert(back and C.moveCount(back) == 1 and back.board[sq("e4")] == C.PAWN and back.side == C.BLACK)
    local again = History.replay(g.history)
    assert(again and C.hash(again) == C.hash(g))
    assert(History.replay("zzzz") == nil, "an illegal saved move")
end

-- The computer: mates in one, takes a hanging queen, and beats random play.
do
    local g = fromFen("6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - -")
    local mv = AI.choose(g, 2, 0, rand)
    assert(C.from(mv) == sq("d1") and C.to(mv) == sq("d8"), "back-rank mate")

    g = fromFen("4k3/8/8/3q4/8/8/8/3RK3 w - -")
    mv = AI.choose(g, 2, 0, rand)
    assert(C.to(mv) == sq("d5"), "takes the queen")

    local wins = 0
    for game = 1, 2 do
        g = C.new()
        local plies = 0
        while not g.result and plies < 200 do
            local m
            if g.side == C.WHITE then
                m = AI.choose(g, 2, 0, rand)
            else
                local legal = C.legalMoves(g)
                m = legal[rand(#legal)]
            end
            assert(C.play(g, m))
            plies = plies + 1
        end
        if g.result == "white" then wins = wins + 1 end
    end
    assert(wins >= 1, "beats random play")
end

print("chess logic ok")
