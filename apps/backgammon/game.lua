-- Backgammon games: your steps (which are allowed, where a checker can
-- go), setting a game up, the turns, the result and saving. Usage:
--   local G = smudge.dofile("game.lua")(C, S)
-- with C the rules (logic.lua) and S the app's state (main.lua). Loaded
-- (with logic.lua) only for a move, a new game or saving, and dropped
-- after: the board's drawing never has the rules in memory.
--
-- S.game: b = { white, black } (each side in its own numbering, see
-- logic.lua), turn (1 White, 2 Black), d1/d2 the roll, steps the steps
-- played this turn, start the boards when the turn began (for Undo), need
-- and only (logic.lua's C.turnInfo), opening (the first turn), last (the
-- previous turn: side, d1, d2, steps), result (the winner) and pts.

return function(C, S)
local G = {}

local function rand(n) return math.random(1, n) end

-- Your steps ----------------------------------------------------------------

-- Whether the step (f, d) may be played now: R[1..n] the dice left, `left`
-- how many of them must still be played, `only` from C.turnInfo.
function G.legal(me, opp, f, d, R, n, left, only)
    if left <= 0 or (only and d ~= only) or not C.target(me, opp, f, d) then return false end
    local k = 0
    for i = 1, n do
        if R[i] == d then k = i break end
    end
    if k == 0 then return false end
    local hit = C.apply(me, opp, f, d)
    R[k] = R[n]
    R[n] = d
    local u = 1 + C.most(me, opp, R, n - 1)
    R[n] = R[k]
    R[k] = d
    C.undo(me, opp, f, d, hit)
    return u >= left
end

-- The dice of the roll not used yet by the steps `steps`, into R; returns
-- how many.
function G.remaining(d1, d2, steps, R)
    local n = C.dice(d1, d2, R)
    for i = 2, #steps, 2 do
        local d = steps:byte(i) - 48
        for k = 1, n do
            if R[k] == d then
                R[k] = R[n]
                R[n] = nil
                n = n - 1
                break
            end
        end
    end
    return n
end

-- Where the checker on f can go now, with one die or several in a row: the
-- points (0: off) into T and the steps for each into P, nearest first.
local function chain(me, opp, f, R, n, left, only, prefix, T, P)
    for k = 1, n do
        local d = R[k]
        local dup = false
        for j = 1, k - 1 do
            if R[j] == d then dup = true end
        end
        if not dup and G.legal(me, opp, f, d, R, n, left, only) then
            local t = f - d
            if t < 0 then t = 0 end
            local s = prefix .. C.step(f, d)
            local known = false
            for i = 1, #T do
                if T[i] == t then known = true end
            end
            if not known then
                local i = #T + 1
                while i > 1 and T[i - 1] < t do
                    T[i], P[i] = T[i - 1], P[i - 1]
                    i = i - 1
                end
                T[i], P[i] = t, s
            end
            if t > 0 then
                local hit = C.apply(me, opp, f, d)
                R[k] = R[n]
                R[n] = d
                chain(me, opp, t, R, n - 1, left - 1, nil, s, T, P)
                R[n] = R[k]
                R[k] = d
                C.undo(me, opp, f, d, hit)
            end
        end
    end
end

function G.dests(me, opp, f, R, n, left, only, T, P)
    for i = #T, 1, -1 do T[i], P[i] = nil, nil end
    chain(me, opp, f, R, n, left, only, "", T, P)
end

-- Turns ---------------------------------------------------------------------

-- The boards as text (one character a count), and back.
local function snapshot(g)
    local t = {}
    for s = 1, 2 do
        for i = 1, 26 do t[#t + 1] = string.char(65 + g.b[s][i]) end
    end
    return table.concat(t)
end

local function restore(g, text)
    for s = 1, 2 do
        for i = 1, 26 do g.b[s][i] = text:byte((s - 1) * 26 + i) - 65 end
    end
end

-- "mode#turn#d1#d2#opening#start#steps#lastSide#lastD1#lastD2#lastSteps"
function G.serialize(mode, g)
    local L = g.last or { side = 0, d1 = 0, d2 = 0, steps = "" }
    return table.concat({ mode, g.turn, g.d1, g.d2, g.opening and 1 or 0, g.start, g.steps, L.side, L.d1, L.d2,
                          L.steps }, "#")
end

-- The mode and the game of a saved text, or nil.
function G.parse(text)
    local f, i = {}, 1
    while i <= #text + 1 do
        local j = text:find("#", i, true) or #text + 1
        f[#f + 1] = text:sub(i, j - 1)
        i = j + 1
    end
    if #f ~= 11 or #f[6] ~= 52 then return nil end
    local g = { b = { {}, {} }, turn = tonumber(f[2]), d1 = tonumber(f[3]), d2 = tonumber(f[4]),
                opening = f[5] == "1", start = f[6], steps = f[7] }
    restore(g, g.start)
    if tonumber(f[8]) > 0 then
        g.last = { side = tonumber(f[8]), d1 = tonumber(f[9]), d2 = tonumber(f[10]), steps = f[11] }
    end
    local me, opp = g.b[g.turn], g.b[3 - g.turn]
    g.need, g.only = C.turnInfo(me, opp, g.d1, g.d2)
    C.play(me, opp, g.steps)
    return tonumber(f[1]), g
end

function G.save()
    local g = S.game
    smudge.save("game", (g and not g.result) and G.serialize(S.mode, g) or "")
end

-- What the side to move may do now: the dice left, the movable checkers.
function G.refresh()
    local g = S.game
    local me, opp = g.b[g.turn], g.b[3 - g.turn]
    S.nR = G.remaining(g.d1, g.d2, g.steps, S.R)
    S.left = g.need - #g.steps // 2
    local src = S.sources
    for i = #src, 1, -1 do src[i] = nil end
    if not g.result and S.left > 0 then
        local only = g.steps == "" and g.only or nil
        for f = 25, 1, -1 do
            if me[f] > 0 then
                for k = 1, S.nR do
                    if G.legal(me, opp, f, S.R[k], S.R, S.nR, S.left, only) then
                        src[#src + 1] = f
                        break
                    end
                end
            end
        end
    end
    if S.cur > #src then S.cur = 1 end
    S.picked = nil
    S.thinking = S.vsDevice() and not g.result and g.turn == 2
end

-- Picks up the checker on f (own numbering): where it can go.
function G.pick(f)
    local g = S.game
    G.dests(g.b[g.turn], g.b[3 - g.turn], f, S.R, S.nR, S.left, g.steps == "" and g.only or nil, S.dt, S.ds)
    S.picked, S.dcur, S.note = f, 1, nil
end

-- Plays steps of this turn (still to be confirmed with G.done); the cursor
-- follows the checker if it can move on.
function G.move(steps)
    local g = S.game
    C.play(g.b[g.turn], g.b[3 - g.turn], steps)
    g.steps = g.steps .. steps
    local t = steps:byte(#steps - 1) - 64 - (steps:byte(#steps) - 48)
    G.refresh()
    for i, f in ipairs(S.sources) do
        if f == t then S.cur = i end
    end
end

-- Takes back the last step of this turn.
function G.undo()
    local g = S.game
    g.steps = g.steps:sub(1, -3)
    restore(g, g.start)
    C.play(g.b[g.turn], g.b[3 - g.turn], g.steps)
    S.note = nil
    G.refresh()
end

local function startTurn(g)
    g.start, g.steps = snapshot(g), ""
    g.need, g.only = C.turnInfo(g.b[g.turn], g.b[3 - g.turn], g.d1, g.d2)
    S.note, S.cur = nil, 1
    G.refresh()
end

-- The end of the turn whose steps are played: a win, or the other side
-- rolls.
function G.done()
    local g = S.game
    local w, l = g.b[g.turn], g.b[3 - g.turn]
    g.last = { side = g.turn, d1 = g.d1, d2 = g.d2, steps = g.steps }
    if w[26] == 15 then
        g.result, g.pts = g.turn, C.score(w, l)
        local r = S.records[S.mode]
        local i = g.turn == 1 and 1 or 2
        r[i] = r[i] + 1
        r[i + 2] = r[i + 2] + g.pts
        smudge.save("rec" .. S.mode, table.concat(r, ","))
        G.refresh()
    else
        g.turn, g.opening = 3 - g.turn, false
        g.d1, g.d2 = rand(6), rand(6)
        startTurn(g)
    end
    G.save()
end

-- The device's move (from ai.lua) played and its turn ended.
function G.deviceMove(steps)
    local g = S.game
    C.play(g.b[g.turn], g.b[3 - g.turn], steps)
    g.steps = steps
    G.done()
end

function G.new()
    local g = { b = { C.newSide(), C.newSide() }, opening = true }
    -- The opening roll: one die each, the higher starts with both numbers.
    repeat g.d1, g.d2 = rand(6), rand(6) until g.d1 ~= g.d2
    g.turn = g.d1 > g.d2 and 1 or 2
    S.game = g
    startTurn(g)
    G.save()
end

return G
end
