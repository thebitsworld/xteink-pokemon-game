-- Mancala (Kalah rules). No drawing, so it can be tested on a computer
-- (test/lua_apps/mancala_test.lua).
--
-- A game is one flat array of numbers (C.SIZE entries), copied cheaply by the
-- computer's search:
--   [1..6]   seeds in player 1's pits (the bottom row), left to right
--   [7]      player 1's store (the right end)
--   [8..13]  seeds in player 2's pits (the top row), right to left
--   [14]     player 2's store (the left end)
--            Index +1 goes counter-clockwise on the screen; pit p faces 14 - p.
--   [15]     the player to move (1 or 2)
--   [16]     1 once the game is over
--   [17]     moves played
--   [18]     seeds per pit at the start (3..6)
--   [19]     1 when the empty-capture option is on
--
-- A move is the index of the pit sown (1..6 or 8..13).
--
-- Rules:
-- - Take all the seeds of one of your non-empty pits and sow them one by one
--   counter-clockwise, into your own store too but never the other's.
-- - Last seed in your store: you move again.
-- - Last seed in an empty pit of yours: when the pit facing it has seeds,
--   both go to your store. With the empty-capture option, the lone seed is
--   captured even when the facing pit is empty.
-- - When a player's six pits are all empty after a move, the game ends and the
--   other player puts the seeds left on their side into their own store.
-- - The fuller store wins.

local C = {}

C.SIZE = 19
C.STORE = { 7, 14 }
local SIDE, OVER, PLY, SEEDS, EC = 15, 16, 17, 18, 19
C.SIDE, C.OVER, C.PLY, C.SEEDS, C.EC = SIDE, OVER, PLY, SEEDS, EC

-- The first of a player's six pits.
function C.first(side) return side == 1 and 1 or 8 end

function C.owner(p)
    if p <= 6 then return 1 end
    if p >= 8 and p <= 13 then return 2 end
    return 0
end

function C.new(starter, seeds, emptyCapture)
    local s = {}
    seeds = seeds or 4
    for i = 1, C.SIZE do s[i] = 0 end
    for p = 1, 6 do
        s[p] = seeds
        s[p + 7] = seeds
    end
    s[SIDE] = starter or 1
    s[SEEDS] = seeds
    s[EC] = emptyCapture and 1 or 0
    return s
end

function C.copy(dst, src)
    for i = 1, C.SIZE do dst[i] = src[i] end
    return dst
end

-- The legal moves of the player to move into `out`; returns their count.
function C.legal(s, out)
    local n = 0
    if s[OVER] == 0 then
        local f = C.first(s[SIDE])
        for p = f, f + 5 do
            if s[p] > 0 then
                n = n + 1
                out[n] = p
            end
        end
    end
    for i = #out, n + 1, -1 do out[i] = nil end
    return n
end

function C.isLegal(s, p)
    if s[OVER] ~= 0 then return false end
    local f = C.first(s[SIDE])
    return p >= f and p <= f + 5 and s[p] > 0
end

function C.score(s, side) return s[C.STORE[side]] end

-- Seeds on a player's six pits.
function C.onSide(s, side)
    local f, n = C.first(side), 0
    for p = f, f + 5 do n = n + s[p] end
    return n
end

-- 1 or 2 for the winner, 0 for a draw (only once the game is over).
function C.winner(s)
    local a, b = s[7], s[14]
    if a > b then return 1 elseif b > a then return 2 end
    return 0
end

-- Plays move p (legal) and gets the next turn ready. When `info` is given it
-- is filled with what happened:
--   from: the pit sown; last: where the last seed fell
--   changed: bit mask of the pits and stores that changed (bit p - 1)
--   extra: the same player moves again
--   cap: seeds captured (the lone seed included), capPit: the pit they left
--   sweep1, sweep2: seeds each player took from their side at the end
function C.play(s, p, info)
    local side = s[SIDE]
    local mine = side == 1 and 7 or 14
    local skip = side == 1 and 14 or 7
    local n = s[p]
    s[p] = 0
    local changed, q = 1 << (p - 1), p
    while n > 0 do
        q = q % 14 + 1
        if q ~= skip then
            s[q] = s[q] + 1
            n = n - 1
            changed = changed | (1 << (q - 1))
        end
    end
    local extra, cap, capPit = q == mine, 0, 0
    if not extra and s[q] == 1 and (side == 1) == (q <= 6) then
        local o = 14 - q
        if s[o] > 0 or s[EC] == 1 then
            cap = s[o] + 1
            s[mine] = s[mine] + cap
            s[q], s[o] = 0, 0
            capPit = q
            changed = changed | (1 << (o - 1)) | (1 << (mine - 1))
        end
    end
    s[PLY] = s[PLY] + 1
    local a, b = 0, 0
    for i = 1, 6 do
        a = a + s[i]
        b = b + s[i + 7]
    end
    local sweep1, sweep2 = 0, 0
    if a == 0 or b == 0 then
        sweep1, sweep2 = a, b
        for i = 1, 6 do
            s[i], s[i + 7] = 0, 0
        end
        s[7], s[14] = s[7] + a, s[14] + b
        s[OVER] = 1
        if a > 0 then changed = changed | 0x7F end
        if b > 0 then changed = changed | 0x3F80 end
    elseif not extra then
        s[SIDE] = 3 - side
    end
    if info then
        info.from, info.last, info.changed, info.extra = p, q, changed, extra
        info.cap, info.capPit, info.sweep1, info.sweep2 = cap, capPit, sweep1, sweep2
    end
end

-- All the seeds on the board and in the stores (always 12 x seeds per pit).
function C.total(s)
    local n = 0
    for p = 1, 14 do n = n + s[p] end
    return n
end

-- A move as a character for saved games and the take-back history.
function C.moveChar(p) return string.char(64 + p) end

-- The game after replaying `count` moves of `hist` (all when nil) from a new
-- game; nil when the history does not fit. `info` gets the last move's info,
-- `sides` (optional) the player who made each move.
function C.replay(starter, seeds, ec, hist, count, info, sides)
    local s = C.new(starter, seeds, ec)
    count = count or #hist
    for i = 1, count do
        local p = hist:byte(i) - 64
        if not C.isLegal(s, p) then return nil end
        if sides then sides[i] = s[SIDE] end
        C.play(s, p, info)
    end
    return s
end

return C
