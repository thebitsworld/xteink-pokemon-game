-- Checkers (English draughts) rules. No drawing, so it can be tested on a
-- computer (test/lua_apps/checkers_test.lua). The computer opponent is in
-- ai.lua.
--
-- 8x8 board, played on the dark squares; Light starts on the bottom three
-- rows and moves first, upwards. Men step and jump diagonally forward; a man
-- reaching the far row is crowned and a king steps and jumps one square in
-- any diagonal direction. Capturing is compulsory, and a piece that can jump
-- again must: the whole chain is one move. A man crowned during a chain stops
-- there. You lose when you have no legal move. Eighty plies in a row with no
-- capture and no crowning is a draw.

local C = {}

C.EMPTY = 0
-- Pieces: 1 light man, 2 light king, 3 dark man, 4 dark king.
C.LIGHT, C.DARK = 1, 2
C.IDLE_LIMIT = 80

function C.index(c, r) return (r - 1) * 8 + c end
function C.colOf(i) return (i - 1) % 8 + 1 end
function C.rowOf(i) return (i - 1) // 8 + 1 end
function C.isDark(c, r) return (c + r) % 2 == 1 end
function C.owner(v) if v == 0 then return 0 end return v <= 2 and C.LIGHT or C.DARK end
function C.isKing(v) return v == 2 or v == 4 end

function C.new()
    local g = { board = {}, turn = C.LIGHT, idle = 0, winner = 0, last = nil }
    for r = 1, 8 do
        for c = 1, 8 do
            local v = 0
            if C.isDark(c, r) then
                if r <= 3 then v = 3 elseif r >= 6 then v = 1 end
            end
            g.board[C.index(c, r)] = v
        end
    end
    return g
end

-- Directions a piece may move in: { dc, dr }.
local UP = { { -1, -1 }, { 1, -1 } }
local DOWN = { { -1, 1 }, { 1, 1 } }
local ALL = { { -1, -1 }, { 1, -1 }, { -1, 1 }, { 1, 1 } }

local function dirsFor(v)
    if C.isKing(v) then return ALL end
    return v == 1 and UP or DOWN
end

local function crownRow(v) return v == 1 and 1 or 8 end

-- A move is its path: the square it starts from and each square it lands
-- on. Each capture is the square between two landings, so it needs no
-- storing. Paths of up to four squares (three jumps), nearly every move, are
-- one integer - 3 bits of length and 6 bits per square - so a search makes no
-- strings; longer chains are a string of bytes (length, then the squares).
function C.pathLen(mv)
    if type(mv) == "number" then return mv >> 24 end
    return mv:byte(1)
end

function C.pathAt(mv, k)
    if type(mv) == "number" then return ((mv >> (24 - 6 * k)) & 63) + 1 end
    return mv:byte(1 + k)
end

function C.from(mv) return C.pathAt(mv, 1) end
function C.to(mv) return C.pathAt(mv, C.pathLen(mv)) end

function C.capCount(mv)
    local a, b = C.pathAt(mv, 1), C.pathAt(mv, 2)
    if math.abs(C.rowOf(a) - C.rowOf(b)) == 2 then return C.pathLen(mv) - 1 end
    return 0
end

-- The k-th captured square: halfway between the k-th and next path squares.
function C.capAt(mv, k) return (C.pathAt(mv, k) + C.pathAt(mv, k + 1)) // 2 end

local function encode(path)
    local n = #path
    if n > 4 then return string.char(n, table.unpack(path)) end
    local v = n
    for k = 1, 4 do v = (v << 6) | ((path[k] or 1) - 1) end
    return v
end

-- Jump chains from square `i` for piece `v`. `path` and `caps` are the chain
-- so far; complete chains are added to `out`.
local function jumps(board, v, i, path, caps, out)
    local c, r = C.colOf(i), C.rowOf(i)
    local me = C.owner(v)
    local extended = false
    for _, d in ipairs(dirsFor(v)) do
        local mc, mr, lc, lr = c + d[1], r + d[2], c + 2 * d[1], r + 2 * d[2]
        if lc >= 1 and lc <= 8 and lr >= 1 and lr <= 8 then
            local mid, land = C.index(mc, mr), C.index(lc, lr)
            local taken = false
            for n = 1, #caps do
                if caps[n] == mid then taken = true end
            end
            local victim = board[mid]
            if not taken and victim ~= 0 and C.owner(victim) ~= me and board[land] == 0 then
                extended = true
                path[#path + 1] = land
                caps[#caps + 1] = mid
                if not C.isKing(v) and lr == crownRow(v) then
                    out[#out + 1] = encode(path) -- crowned: the chain ends here
                else
                    jumps(board, v, land, path, caps, out)
                end
                path[#path] = nil
                caps[#caps] = nil
            end
        end
    end
    if not extended and #caps > 0 then out[#out + 1] = encode(path) end
end

-- Every legal move for `side` on `board` (see the move encoding above).
-- Captures only, when any exists.
local scratchPath, scratchCaps = {}, {} -- reused: a search calls this a lot

function C.movesFor(board, side)
    local captures, steps = {}, {}
    for i = 1, 64 do
        local v = board[i]
        if v ~= 0 and C.owner(v) == side then
            -- Lift the piece so a chain may pass back over its own square.
            board[i] = 0
            scratchPath[1] = i
            jumps(board, v, i, scratchPath, scratchCaps, captures)
            board[i] = v
            if #captures == 0 then
                local c, r = C.colOf(i), C.rowOf(i)
                for _, d in ipairs(dirsFor(v)) do
                    local tc, tr = c + d[1], r + d[2]
                    if tc >= 1 and tc <= 8 and tr >= 1 and tr <= 8 and board[C.index(tc, tr)] == 0 then
                        steps[#steps + 1] = (2 << 24) | ((i - 1) << 18) | ((C.index(tc, tr) - 1) << 12)
                    end
                end
            end
        end
    end
    if #captures > 0 then return captures end
    return steps
end

function C.moves(g) return C.movesFor(g.board, g.turn) end

-- Applies `move` to a board; returns whether a man was crowned.
function C.apply(board, move)
    local from, to = C.from(move), C.to(move)
    local v = board[from]
    board[from] = 0
    for k = 1, C.capCount(move) do board[C.capAt(move, k)] = 0 end
    local crowned = false
    if not C.isKing(v) and C.rowOf(to) == crownRow(v) then
        v = v + 1
        crowned = true
    end
    board[to] = v
    return crowned
end

-- Plays `move` for the side to move and passes the turn.
function C.play(g, move)
    if g.winner ~= 0 then return false end
    local crowned = C.apply(g.board, move)
    if C.capCount(move) > 0 or crowned then g.idle = 0 else g.idle = g.idle + 1 end
    g.last = move
    g.turn = 3 - g.turn
    if #C.moves(g) == 0 then
        g.winner = 3 - g.turn
    elseif g.idle >= C.IDLE_LIMIT then
        g.winner = -1
    end
    return true
end

-- The moves whose path starts with `prefix` (the squares picked so far).
function C.matching(moves, prefix)
    local out = {}
    for _, mv in ipairs(moves) do
        local ok = C.pathLen(mv) >= #prefix
        for n = 1, #prefix do
            if ok and C.pathAt(mv, n) ~= prefix[n] then ok = false end
        end
        if ok then out[#out + 1] = mv end
    end
    return out
end

function C.count(board, side)
    local men, kings = 0, 0
    for i = 1, 64 do
        local v = board[i]
        if v ~= 0 and C.owner(v) == side then
            if C.isKing(v) then kings = kings + 1 else men = men + 1 end
        end
    end
    return men, kings
end

-- Save/restore: turn | idle | 64 board digits.
function C.serialize(g)
    return g.turn .. "|" .. g.idle .. "|" .. table.concat(g.board)
end

function C.deserialize(s)
    local turn, idle, cells = s:match("^([12])|(%d+)|([0-4]+)$")
    if not turn or #cells ~= 64 then return nil end
    local g = C.new()
    g.turn, g.idle = tonumber(turn), tonumber(idle)
    local light, dark = 0, 0
    for i = 1, 64 do
        local v = cells:byte(i) - 48
        if v ~= 0 and not C.isDark(C.colOf(i), C.rowOf(i)) then return nil end
        if v ~= 0 then
            if C.owner(v) == C.LIGHT then light = light + 1 else dark = dark + 1 end
        end
        g.board[i] = v
    end
    if light > 12 or dark > 12 then return nil end
    if #C.moves(g) == 0 then g.winner = 3 - g.turn end
    return g
end

return C
