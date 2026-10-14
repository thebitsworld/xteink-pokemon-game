-- Backgammon rules: the board, single steps and how many dice a roll lets
-- you play (the full moves of a roll are generated in ai.lua, the checks
-- for your own steps are in game.lua). No smudge calls, so the tests run
-- it on a computer. Usage:
--   local C = smudge.dofile("logic.lua")
-- Loaded only with game.lua (a move of yours) or ai.lua (the device's).
--
-- A side's checkers are counted in its own coordinates: me[1..24] the
-- points (1..6 its home board, 24 the farthest), me[25] the bar and me[26]
-- the checkers borne off. A side moves from high to low; the opponent's
-- point j is this side's point 25 - j. White numbers the board as shown, so
-- the board's point p holds white[p] white and black[25 - p] black checkers.
--
-- A step is a checker moved from point f (25: the bar) with one die d, kept
-- as two characters: char(64 + f) .. char(48 + d). A move is the steps of a
-- turn, one after the other in a string.

local C = {}
C.BAR, C.OFF = 25, 26

function C.newSide()
    local s = {}
    for i = 1, 26 do s[i] = 0 end
    s[24], s[13], s[8], s[6] = 2, 5, 3, 5
    return s
end

function C.copy(s, into)
    into = into or {}
    for i = 1, 26 do into[i] = s[i] end
    return into
end

-- The point a checker of `me` on f reaches with die d (0: borne off), or
-- nil if that step is not allowed on this board (blocked, a checker still
-- on the bar, not all home for bearing off, a higher checker for an
-- inexact bear-off). The dice rule is game.lua's G.legal.
function C.target(me, opp, f, d)
    if me[f] == 0 or (f ~= 25 and me[25] > 0) then return nil end
    local t = f - d
    if t >= 1 then
        if opp[25 - t] >= 2 then return nil end
        return t
    end
    for i = 7, 25 do
        if me[i] > 0 then return nil end
    end
    if t < 0 then
        for i = f + 1, 6 do
            if me[i] > 0 then return nil end
        end
    end
    return 0
end

-- Plays a step (allowed by C.target); returns whether it hit a blot.
function C.apply(me, opp, f, d)
    local t = f - d
    me[f] = me[f] - 1
    if t <= 0 then
        me[26] = me[26] + 1
        return false
    end
    me[t] = me[t] + 1
    if opp[25 - t] == 1 then
        opp[25 - t] = 0
        opp[25] = opp[25] + 1
        return true
    end
    return false
end

function C.undo(me, opp, f, d, hit)
    local t = f - d
    me[f] = me[f] + 1
    if t <= 0 then
        me[26] = me[26] - 1
        return
    end
    me[t] = me[t] - 1
    if hit then
        opp[25 - t] = 1
        opp[25] = opp[25] - 1
    end
end

-- How many of the dice R[1..n] can be played one after the other.
local function most(me, opp, R, n)
    if n == 0 then return 0 end
    local best = 0
    for k = 1, n do
        local d = R[k]
        local dup = false
        for j = 1, k - 1 do
            if R[j] == d then dup = true end
        end
        if not dup then
            local top = me[25] > 0 and 25 or 24
            for f = top, 1, -1 do
                if me[f] > 0 and C.target(me, opp, f, d) then
                    local hit = C.apply(me, opp, f, d)
                    R[k] = R[n]
                    R[n] = d
                    local u = 1 + most(me, opp, R, n - 1)
                    R[n] = R[k]
                    R[k] = d
                    C.undo(me, opp, f, d, hit)
                    if u > best then
                        best = u
                        if best == n then return n end
                    end
                end
            end
        end
    end
    return best
end
C.most = most

local RD = {}

-- The dice list of a roll into R; returns its length.
function C.dice(d1, d2, R)
    R[1], R[2], R[3], R[4] = d1, d2, nil, nil
    if d1 == d2 then
        R[3], R[4] = d1, d1
        return 4
    end
    return 2
end

-- What a roll asks of `me`: how many dice must be played, and the one die
-- that must be played when only one of two different dice can be (the
-- larger if it can be played, else the smaller; nil otherwise).
function C.turnInfo(me, opp, d1, d2)
    local n = C.dice(d1, d2, RD)
    local need = most(me, opp, RD, n)
    if need ~= 1 or d1 == d2 then return need, nil end
    local hi, lo = math.max(d1, d2), math.min(d1, d2)
    for f = 25, 1, -1 do
        if me[f] > 0 and C.target(me, opp, f, hi) then return need, hi end
    end
    return need, lo
end

function C.step(f, d) return string.char(64 + f, 48 + d) end

-- Plays the steps of a move string; returns how many blots it hit.
function C.play(me, opp, steps)
    local hits = 0
    for i = 1, #steps - 1, 2 do
        if C.apply(me, opp, steps:byte(i) - 64, steps:byte(i + 1) - 48) then hits = hits + 1 end
    end
    return hits
end

-- Pip count: what is left to bear off.
function C.pips(s)
    local p = 25 * s[25]
    for i = 1, 24 do p = p + i * s[i] end
    return p
end

-- Points for a win by `w` over `l`: 1, 2 (gammon: no checker borne off)
-- or 3 (backgammon: and still one on the bar or in the winner's home).
function C.score(w, l)
    if l[26] > 0 then return 1 end
    for i = 19, 25 do
        if l[i] > 0 then return 3 end
    end
    return 2
end

return C
