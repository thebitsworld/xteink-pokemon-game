-- O An Quan (Vietnamese mandarin-square mancala) rules. No drawing, so it can
-- be tested on a computer (test/lua_apps/oanquan_test.lua).
--
-- A game is one flat array of numbers (S.SIZE entries), copied cheaply by the
-- computer's search:
--   [1..12]  stones (dan) in the squares, round the board: 1 the left
--            mandarin square, 2..6 the bottom row left to right (player 1),
--            7 the right mandarin square, 8..12 the top row right to left
--            (player 2). Index +1 goes counter-clockwise on the screen.
--   [13]     1 while the left mandarin piece (quan) is still in square 1
--   [14]     the same for the right one in square 7
--   [15,16]  stones captured by player 1, 2 (borrowed ones already taken off)
--   [17,18]  mandarins captured by player 1, 2
--   [19,20]  stones player 1, 2 borrowed from the other (paid back at the end)
--   [21]     the player to move (1 or 2)
--   [22]     1 once the game is over
--   [23]     moves played
--   [24]     moves since the last capture
--
-- A move is square * 2 + 1 to sow towards higher indices, square * 2 to sow
-- towards lower ones.
--
-- Rules (the common Vietnamese game):
-- - Sow the stones of one of your non-empty squares one by one, either way,
--   mandarin squares included.
-- - After the last stone, the next square decides: a mandarin square that is
--   not empty ends the turn; a stone square with stones is picked up and sown
--   on; an empty square followed by a non-empty one captures that one, and so
--   on along the chain (empty, full, empty, full...); two empties end it.
-- - A mandarin square still holding its mandarin with fewer than 5 stones is
--   a young mandarin (quan non): it cannot be captured, the turn ends.
-- - A player whose five squares are all empty at the start of the turn puts
--   back one captured stone in each, borrowing from the other player when
--   short; the debt is paid back at the end. A player in debt who comes up
--   short again cannot borrow twice: the game ends.
-- - The game ends when both mandarin squares are empty; each player then
--   takes the stones left on their row. A mandarin is worth 10, a stone 1.
-- - Endless games: the game also ends after QUIET moves in a row without a
--   capture, after MAX_PLY moves, or when a player's row is empty and nobody
--   has a stone to put back; each player takes their row and what is left in
--   the mandarin squares counts for nobody. A turn stops after MAX_LAPS
--   pick-ups (never seen in play).

local C = {}

C.SIZE = 24
C.QUAN_VALUE = 10
C.NON = 5          -- a mandarin square with its mandarin and fewer stones is young
C.MAX_LAPS = 60
C.MAX_PLY = 300
C.QUIET = 40
C.LEFT, C.RIGHT = 1, 7
C.QUAN = { [1] = 13, [7] = 14 }
local CAPD, CAPQ, DEBT, SIDE, OVER, PLY, QUIET = 15, 17, 19, 21, 22, 23, 24
C.CAPD, C.CAPQ, C.DEBT, C.SIDE, C.OVER, C.PLY, C.SINCE = CAPD, CAPQ, DEBT, SIDE, OVER, PLY, QUIET

local function step(p, d) return (p - 1 + d) % 12 + 1 end
C.step = step

-- The first of a player's five squares.
function C.first(side) return side == 1 and 2 or 8 end
function C.owner(p)
    if p >= 2 and p <= 6 then return 1 end
    if p >= 8 then return 2 end
    return 0
end

local function quanAt(s, p)
    if p == 1 then return s[13] elseif p == 7 then return s[14] end
    return 0
end

local function empty(s, p) return s[p] == 0 and quanAt(s, p) == 0 end
C.empty = empty

function C.new(starter)
    local s = {}
    for i = 1, C.SIZE do s[i] = 0 end
    for p = 2, 6 do s[p] = 5 end
    for p = 8, 12 do s[p] = 5 end
    s[13], s[14] = 1, 1
    s[SIDE] = starter or 1
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
        for p = f, f + 4 do
            if s[p] > 0 then
                out[n + 1], out[n + 2] = p * 2, p * 2 + 1
                n = n + 2
            end
        end
    end
    for i = #out, n + 1, -1 do out[i] = nil end
    return n
end

function C.isLegal(s, m)
    if s[OVER] ~= 0 then return false end
    local p = m // 2
    local f = C.first(s[SIDE])
    return p >= f and p <= f + 4 and s[p] > 0
end

-- Points so far (a mandarin 10, a stone 1, debts settled).
function C.score(s, side)
    local o = 3 - side
    return s[CAPD + side - 1] + C.QUAN_VALUE * s[CAPQ + side - 1] - s[DEBT + side - 1] + s[DEBT + o - 1]
end

-- The end: each player takes the stones on their row.
local function finish(s)
    for side = 1, 2 do
        local f = C.first(side)
        for p = f, f + 4 do
            s[CAPD + side - 1] = s[CAPD + side - 1] + s[p]
            s[p] = 0
        end
    end
    s[OVER] = 1
end

-- 1 or 2 for the winner, 0 for a draw (only once the game is over).
function C.winner(s)
    local a, b = C.score(s, 1), C.score(s, 2)
    if a > b then return 1 elseif b > a then return 2 end
    return 0
end

-- The player to move with an empty row puts stones back. Returns how many
-- were put back and how many of them were borrowed.
local function refill(s)
    local side = s[SIDE]
    local f = C.first(side)
    for p = f, f + 4 do
        if s[p] > 0 then return 0, 0 end
    end
    local o = 3 - side
    local own = s[CAPD + side - 1]
    if own < 5 and s[DEBT + side - 1] > 0 then
        -- Already in debt and short again: the game ends.
        finish(s)
        return 0, 0
    end
    local take = own < 5 and own or 5
    s[CAPD + side - 1] = own - take
    local lend = s[CAPD + o - 1]
    local borrow = 5 - take
    if borrow > lend then borrow = lend end
    s[CAPD + o - 1] = lend - borrow
    s[DEBT + side - 1] = s[DEBT + side - 1] + borrow
    for i = 0, take + borrow - 1 do s[f + i] = 1 end
    if take + borrow == 0 then finish(s) end
    return take + borrow, borrow
end

-- Plays move m (legal) and gets the next turn ready. When `info` is given it
-- is filled with what happened:
--   changed, captured: bit masks of the squares (bit p - 1)
--   capD, capQ: stones and mandarins captured; laps: pick-ups after the first
--   young: a young mandarin stopped a capture; stopped: MAX_LAPS reached
--   refill, borrowed: stones the next player put back (and borrowed)
function C.play(s, m, info)
    local side = s[SIDE]
    local p, d = m // 2, (m % 2 == 1) and 1 or -1
    local changed, capMask, capD, capQ, laps = 1 << (p - 1), 0, 0, 0, 0
    local young, stopped = false, false
    local n = s[p]
    s[p] = 0
    while true do
        while n > 0 do
            p = step(p, d)
            s[p] = s[p] + 1
            n = n - 1
            changed = changed | (1 << (p - 1))
        end
        local nx = step(p, d)
        if (nx == 1 or nx == 7) and not empty(s, nx) then break end
        if s[nx] > 0 then
            if laps >= C.MAX_LAPS then
                stopped = true
                break
            end
            laps = laps + 1
            n, s[nx], p = s[nx], 0, nx
            changed = changed | (1 << (nx - 1))
        else
            -- nx is empty: capture the square after it, and on down the chain.
            local e = nx
            while true do
                local t = step(e, d)
                local q = quanAt(s, t)
                if s[t] == 0 and q == 0 then break end
                if q == 1 and s[t] < C.NON then
                    young = true
                    break
                end
                capD = capD + s[t]
                s[CAPD + side - 1] = s[CAPD + side - 1] + s[t]
                s[t] = 0
                if q == 1 then
                    s[C.QUAN[t]] = 0
                    s[CAPQ + side - 1] = s[CAPQ + side - 1] + 1
                    capQ = capQ + 1
                end
                capMask = capMask | (1 << (t - 1))
                changed = changed | (1 << (t - 1))
                e = step(t, d)
                if not empty(s, e) then break end
            end
            break
        end
    end
    s[PLY] = s[PLY] + 1
    s[QUIET] = capMask == 0 and s[QUIET] + 1 or 0
    local put, borrowed = 0, 0
    if empty(s, 1) and empty(s, 7) or s[PLY] >= C.MAX_PLY or s[QUIET] >= C.QUIET then
        finish(s)
    else
        s[SIDE] = 3 - side
        put, borrowed = refill(s)
    end
    if info then
        info.changed, info.captured, info.capD, info.capQ, info.laps = changed, capMask, capD, capQ, laps
        info.young, info.stopped, info.refill, info.borrowed = young, stopped, put, borrowed
    end
end

-- Stones on the board and in the captures (always 50), and mandarins still
-- on the board or captured (always 2).
function C.totals(s)
    local stones = s[CAPD] + s[CAPD + 1]
    for p = 1, 12 do stones = stones + s[p] end
    return stones, s[13] + s[14] + s[CAPQ] + s[CAPQ + 1]
end

-- Saved games: the numbers joined with commas.
function C.serialize(s) return table.concat(s, ",") end

function C.deserialize(str)
    local s = {}
    for v in str:gmatch("%-?%d+") do s[#s + 1] = tonumber(v) end
    if #s ~= C.SIZE then return nil end
    local stones, quans = C.totals(s)
    if stones ~= 50 or quans ~= 2 or (s[SIDE] ~= 1 and s[SIDE] ~= 2) then return nil end
    return s
end

return C
