-- Go rules and the computer opponent. No drawing, so it can be tested on a
-- computer (test/lua_apps/go_test.lua).
--
-- Tromp-Taylor rules: Black and White take turns placing a stone on an empty
-- point or passing. A group with no empty neighbouring point (liberty) is
-- captured; a move that would leave its own group without liberties (and
-- captures nothing) is not allowed, nor is retaking a single stone straight
-- back (ko). Two passes in a row end the game. Each side scores its stones
-- plus the empty areas that touch only its own stones; White adds 7.5 komi.
-- Dead stones are not removed by agreement: capture them before passing.

local G = {}

G.EMPTY, G.BLACK, G.WHITE = 0, 1, 2
G.KOMI = 7.5

function G.new(n)
    local g = { n = n, cells = {}, turn = G.BLACK, passes = 0, ko = 0, over = false, last = nil, moves = 0,
                captured = { 0, 0 } }
    for i = 1, n * n do g.cells[i] = G.EMPTY end
    return g
end

function G.index(g, c, r) return (r - 1) * g.n + c end
function G.colOf(g, i) return (i - 1) % g.n + 1 end
function G.rowOf(g, i) return (i - 1) // g.n + 1 end

-- Up to four neighbours of i, written into `out` (returns the count).
function G.neighbours(g, i, out)
    local n = g.n
    local c, r = (i - 1) % n + 1, (i - 1) // n + 1
    local k = 0
    if c > 1 then k = k + 1 out[k] = i - 1 end
    if c < n then k = k + 1 out[k] = i + 1 end
    if r > 1 then k = k + 1 out[k] = i - n end
    if r < n then k = k + 1 out[k] = i + n end
    return k
end

-- Group scans mark what they visit on the board itself - a stone of colour
-- v becomes v + 4, a counted liberty 8 - and put it back before returning,
-- so they need no "visited" array as big as the board (the X3's Lua memory
-- is tight on 13x13). Visited stones and liberties go into reused buffers.
local visitBuf, libBuf, nbs = {}, {}, { {}, {}, {}, {}, {}, {} }
local LIBERTY = 8

-- The group containing stone i: its size and number of liberties, and (when
-- `members` is given) the list of its stones. `depth` picks a scratch
-- neighbour array for callers that keep their own in use.
local function scanGroup(g, i, members, depth)
    local cells = g.cells
    local colour = cells[i]
    local visit, libList, nb = visitBuf, libBuf, nbs[depth or 1]
    local count, k, libs = 1, 1, 0
    visit[1] = i
    cells[i] = colour + 4
    while k <= count do
        local j = visit[k]
        k = k + 1
        for m = 1, G.neighbours(g, j, nb) do
            local x = nb[m]
            local v = cells[x]
            if v == colour then
                cells[x] = colour + 4
                count = count + 1
                visit[count] = x
            elseif v == G.EMPTY then
                cells[x] = LIBERTY
                libs = libs + 1
                libList[libs] = x
            end
        end
    end
    for q = 1, count do
        cells[visit[q]] = colour
        if members then members[q] = visit[q] end
    end
    for q = 1, libs do cells[libList[q]] = G.EMPTY end
    return count, libs
end
G.scanGroup = scanGroup

local playNb, playRemoved, groupMembers, replyNb = {}, {}, {}, {}

local function removeGroup(g, i)
    local members = groupMembers
    local size = scanGroup(g, i, members)
    for k = 1, size do g.cells[members[k]] = G.EMPTY end
    return size, members
end

-- Plays a stone for the side to move at i. Returns the number of stones it
-- captured, or nil if the move is not allowed. With `dryRun` the board is
-- left unchanged.
function G.play(g, i, dryRun)
    if g.over or i < 1 or i > g.n * g.n or g.cells[i] ~= G.EMPTY or i == g.ko then return nil end
    local me, opp = g.turn, 3 - g.turn
    local cells = g.cells
    cells[i] = me
    local nb = playNb
    local captured, lastCaptured = 0, nil
    local removed = playRemoved
    local nRemoved = 0
    for k = 1, G.neighbours(g, i, nb) do
        local x = nb[k]
        if cells[x] == opp then
            local _, libs = scanGroup(g, x)
            if libs == 0 then
                local size, members = removeGroup(g, x)
                captured = captured + size
                lastCaptured = x
                for m = 1, size do
                    nRemoved = nRemoved + 1
                    removed[nRemoved] = members[m]
                end
            end
        end
    end
    local size, libs = scanGroup(g, i)
    if libs == 0 or dryRun then
        -- Suicide, or only trying: undo.
        cells[i] = G.EMPTY
        for k = 1, nRemoved do cells[removed[k]] = opp end
        if libs == 0 then return nil end
        return captured
    end
    -- Ko: a lone stone that took a lone stone may not be retaken at once.
    g.ko = (captured == 1 and size == 1 and libs == 1) and lastCaptured or 0
    g.captured[me] = g.captured[me] + captured
    g.passes = 0
    g.last = i
    g.moves = g.moves + 1
    g.turn = opp
    return captured
end

function G.pass(g)
    if g.over then return false end
    g.passes = g.passes + 1
    g.ko = 0
    g.last = nil
    g.moves = g.moves + 1
    g.turn = 3 - g.turn
    if g.passes >= 2 then g.over = true end
    return true
end

-- Empty regions: for each empty point, which colours its region touches.
-- Returns owner[i] = BLACK/WHITE for territory, 0 for neutral, plus the
-- region list itself.
-- For scoring and the end-of-game display.
function G.territory(g)
    local n, cells = g.n, g.cells
    local owner, region, seen = {}, {}, {}
    local nb = {}
    for i = 1, n * n do
        if cells[i] == G.EMPTY and not seen[i] then
            local count, touches = 1, 0
            region[1] = i
            seen[i] = true
            local k = 1
            while k <= count do
                local j = region[k]
                k = k + 1
                for m = 1, G.neighbours(g, j, nb) do
                    local x = nb[m]
                    local v = cells[x]
                    if v == G.EMPTY then
                        if not seen[x] then
                            seen[x] = true
                            count = count + 1
                            region[count] = x
                        end
                    else
                        touches = touches | v -- 1 black, 2 white, 3 both
                    end
                end
            end
            local who = (touches == 1 or touches == 2) and touches or 0
            for q = 1, count do owner[region[q]] = who end
        end
    end
    return owner
end

-- Area score: stones plus territory; White's includes komi.
function G.score(g)
    local owner = G.territory(g)
    local s = { 0, G.KOMI }
    for i = 1, g.n * g.n do
        local v = g.cells[i]
        if v ~= G.EMPTY then
            s[v] = s[v] + 1
        elseif owner[i] ~= 0 then
            s[owner[i]] = s[owner[i]] + 1
        end
    end
    return s[1], s[2]
end

function G.winner(g)
    local b, w = G.score(g)
    return b > w and G.BLACK or G.WHITE
end

-- Computer opponent ------------------------------------------------------------
-- Rules of thumb rather than reading: capture, save groups in atari, put
-- the opponent's in atari, never fill its own eyes or play inside settled
-- territory, avoid self-atari, and otherwise play near the action on good
-- lines. It passes when nothing useful is left. The hard level also checks
-- that its move does not hand over a big capture.

local function lineValue(g, i)
    local n = g.n
    local c, r = G.colOf(g, i), G.rowOf(g, i)
    local edge = math.min(c, r, n + 1 - c, n + 1 - r)
    if n <= 9 then return ({ -6, 0, 3, 2, 1 })[math.min(edge, 5)] end
    return ({ -6, 0, 3, 3, 1, 0, 0 })[math.min(edge, 7)]
end

local valueNb, regionNb = {}, {}

-- Whose area the empty point i is in (0: neither yet). The flood marks the
-- board like scanGroup, and stops as soon as it meets both colours or grows
-- past REGION_LIMIT points (an area that big is not settled).
local REGION_LIMIT = 40

local function regionOwner(g, i)
    local cells, visit = g.cells, visitBuf
    local count, k, touches = 1, 1, 0
    visit[1] = i
    cells[i] = LIBERTY
    while k <= count and touches ~= 3 and count < REGION_LIMIT do
        local j = visit[k]
        k = k + 1
        for m = 1, G.neighbours(g, j, regionNb) do
            local x = regionNb[m]
            local v = cells[x]
            if v == G.EMPTY then
                cells[x] = LIBERTY
                count = count + 1
                visit[count] = x
            elseif v ~= LIBERTY then
                touches = touches | v
            end
        end
    end
    for q = 1, count do cells[visit[q]] = G.EMPTY end
    if touches == 3 or count >= REGION_LIMIT then return 0 end
    return touches
end

-- Value of a move at i for the side to move (nil when not worth playing).
local function moveValue(g, i, level)
    local me, opp = g.turn, 3 - g.turn
    local cells = g.cells
    local nb = valueNb
    -- Inside settled territory: one's own (pointless) or the opponent's
    -- (hopeless, unless it captures - checked below).
    local area = regionOwner(g, i)
    local inOwn = area == me
    local count = G.neighbours(g, i, nb)
    local value = 0
    local captures = G.play(g, i, true)
    if not captures then return nil end
    if captures > 0 then value = value + 12 * captures + 4 end
    -- Groups next to it: rescue mine in atari, press theirs.
    local ownNear, oppNear, emptyNear = 0, 0, 0
    for k = 1, count do
        local x = nb[k]
        local v = cells[x]
        if v == me then
            ownNear = ownNear + 1
            local size, libs = scanGroup(g, x, nil, 2)
            if libs == 1 then value = value + 10 * size end
        elseif v == opp then
            oppNear = oppNear + 1
            local size, libs = scanGroup(g, x, nil, 2)
            if libs == 2 then value = value + 3 * size end
            if libs == 3 then value = value + 1 end
        else
            emptyNear = emptyNear + 1
        end
    end
    -- An eye of mine: every neighbour mine - never fill it.
    if ownNear == count and captures == 0 then return nil end
    if inOwn and captures == 0 then
        -- Only worth it to save an atari group (counted above).
        if value < 10 then return nil end
    end
    if area == opp and captures == 0 and value < 3 then
        -- Invading a sealed area on a small board rarely works.
        value = value - 4
    end
    -- After the move: its own liberties.
    cells[i] = me
    local size, libs = scanGroup(g, i, nil, 2)
    cells[i] = G.EMPTY
    if libs == 1 and captures == 0 then value = value - 8 - 3 * size end
    if libs == 2 then value = value - 1 end
    -- Shape and position. Late in the game the edge lines matter as much
    -- as any: that is where boundaries are closed.
    local late = g.moves > g.n * g.n // 2
    local line = lineValue(g, i)
    value = value + (late and math.max(line, 0) or line)
    -- A boundary point between the two colours, in no one's area yet.
    if area == 0 and ownNear + oppNear > 0 then value = value + (late and 4 or 1) end
    if ownNear > 0 and oppNear > 0 then value = value + 2 end -- contact fights matter
    if g.moves < 6 and ownNear + oppNear == 0 then value = value + 2 end
    -- Closeness to the last move keeps the play local.
    if g.last then
        local d = math.abs(G.colOf(g, i) - G.colOf(g, g.last)) + math.abs(G.rowOf(g, i) - G.rowOf(g, g.last))
        if d <= 2 then value = value + 2 elseif d <= 4 then value = value + 1 end
    end
    return value
end

-- Biggest capture the opponent could make right after the side to move
-- plays at i. The move is played and then taken back exactly (the stones it
-- captured are put back), so no copy of the board is needed.
local function biggestReplyCapture(g, i)
    local me = g.turn
    local saved = { ko = g.ko, passes = g.passes, last = g.last, moves = g.moves, capB = g.captured[1],
                    capW = g.captured[2] }
    local caps = G.play(g, i)
    local taken = nil
    if caps and caps > 0 then
        taken = {}
        for k = 1, caps do taken[k] = playRemoved[k] end
    end
    local worst = 0
    -- Only liberties of my groups in atari can capture.
    local nb = replyNb
    for x = 1, g.n * g.n do
        if g.cells[x] == G.EMPTY then
            local threat = false
            for k = 1, G.neighbours(g, x, nb) do
                if g.cells[nb[k]] == me then
                    local _, libs = scanGroup(g, nb[k], nil, 3)
                    if libs == 1 then threat = true end
                end
            end
            if threat then
                local c = G.play(g, x, true)
                if c and c > worst then worst = c end
            end
        end
    end
    g.cells[i] = G.EMPTY
    if taken then
        for _, x in ipairs(taken) do g.cells[x] = 3 - me end
    end
    g.turn, g.ko, g.passes, g.last, g.moves = me, saved.ko, saved.passes, saved.last, saved.moves
    g.captured[1], g.captured[2] = saved.capB, saved.capW
    return worst
end

-- The point for the side to move, or nil to pass. level 1 adds a lot of
-- randomness, 2 plays the rules of thumb, 3 also avoids moves that hand over
-- a capture.
function G.chooseMove(g, level, rand)
    local move = G.pickMove(g, level, rand)
    -- Let the scratch arrays go between moves: on 13x13 they are several KB.
    visitBuf, libBuf, groupMembers, playRemoved = {}, {}, {}, {}
    return move
end

function G.pickMove(g, level, rand)
    -- Keep only the six best moves (points and values in two short arrays):
    -- a table per candidate would not fit 13x13 on the X3.
    local topI, topV, count = {}, {}, 0
    for i = 1, g.n * g.n do
        if g.cells[i] == G.EMPTY then
            local v = moveValue(g, i, level)
            if v then
                v = v + ((level <= 1) and rand(80) / 10 or rand(10) / 10)
                if count < 6 or v > topV[count] then
                    if count < 6 then count = count + 1 end
                    local k = count
                    while k > 1 and topV[k - 1] < v do
                        topI[k], topV[k] = topI[k - 1], topV[k - 1]
                        k = k - 1
                    end
                    topI[k], topV[k] = i, v
                end
            end
        end
    end
    if count == 0 then return nil end
    -- Nothing worth a stone: pass (always once the opponent has passed and
    -- the best move is only shape).
    if topV[1] < 1 or (g.passes > 0 and topV[1] < 4) then return nil end
    if level >= 3 then
        local best, bestV = nil, nil
        for k = 1, count do
            local v = topV[k] - 10 * biggestReplyCapture(g, topI[k])
            if not bestV or v > bestV then best, bestV = topI[k], v end
        end
        if bestV < 1 and g.passes > 0 then return nil end
        return best
    end
    return topI[1]
end

-- Save/restore: n | turn | passes | ko | moves | capB | capW | cells.
function G.serialize(g)
    return table.concat({ g.n, g.turn, g.passes, g.ko, g.moves, g.captured[1], g.captured[2], table.concat(g.cells) },
                        "|")
end

function G.deserialize(s)
    local f = {}
    for x in (s .. "|"):gmatch("([^|]*)|") do f[#f + 1] = x end
    if #f ~= 8 then return nil end
    local n = tonumber(f[1])
    if not n or n < 5 or n > 19 or #f[8] ~= n * n or f[8]:find("[^012]") then return nil end
    local g = G.new(n)
    g.turn, g.passes, g.ko, g.moves = tonumber(f[2]), tonumber(f[3]), tonumber(f[4]), tonumber(f[5])
    g.captured = { tonumber(f[6]), tonumber(f[7]) }
    if (g.turn ~= 1 and g.turn ~= 2) or not g.passes or not g.ko or not g.moves then return nil end
    for i = 1, n * n do g.cells[i] = f[8]:byte(i) - 48 end
    g.over = g.passes >= 2
    return g
end

return G
