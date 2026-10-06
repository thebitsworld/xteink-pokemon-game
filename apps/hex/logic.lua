-- Hex rules and the computer opponent. No drawing, so it can be tested on a
-- computer (test/lua_apps/hex_test.lua).
--
-- A rhombus of hexagons, n by n. Black (who moves first) joins the top and
-- bottom edges, White the left and right edges; players take turns filling
-- one empty cell. The first to join their two edges with a chain wins - and
-- someone always does, because a full board has exactly one such chain.

local X = {}

X.EMPTY, X.BLACK, X.WHITE = 0, 1, 2

function X.new(n)
    local g = { n = n, cells = {}, turn = X.BLACK, winner = 0, moves = 0, last = nil }
    for i = 1, n * n do g.cells[i] = X.EMPTY end
    return g
end

function X.index(g, c, r) return (r - 1) * g.n + c end
function X.colOf(g, i) return (i - 1) % g.n + 1 end
function X.rowOf(g, i) return (i - 1) // g.n + 1 end

-- The six neighbours of a cell (left, right, up, down, up-right, down-left).
local DC = { -1, 1, 0, 0, 1, -1 }
local DR = { 0, 0, -1, 1, -1, 1 }

function X.neighbours(g, i, out)
    out = out or {}
    local n, c, r = g.n, X.colOf(g, i), X.rowOf(g, i)
    local k = 0
    for d = 1, 6 do
        local cc, rr = c + DC[d], r + DR[d]
        if cc >= 1 and cc <= n and rr >= 1 and rr <= n then
            k = k + 1
            out[k] = (rr - 1) * n + cc
        end
    end
    for j = k + 1, #out do out[j] = nil end
    return out
end

-- Whether `player` has joined their edges.
local function connected(g, player)
    local n, cells = g.n, g.cells
    local seen, stack = {}, {}
    for k = 1, n do
        local i = (player == X.BLACK) and k or ((k - 1) * n + 1) -- top row / left column
        if cells[i] == player then
            stack[#stack + 1] = i
            seen[i] = true
        end
    end
    local nb = {}
    while #stack > 0 do
        local i = table.remove(stack)
        if (player == X.BLACK and X.rowOf(g, i) == n) or (player == X.WHITE and X.colOf(g, i) == n) then
            return true
        end
        for _, j in ipairs(X.neighbours(g, i, nb)) do
            if not seen[j] and cells[j] == player then
                seen[j] = true
                stack[#stack + 1] = j
            end
        end
    end
    return false
end
X.connected = connected

function X.play(g, i)
    if g.winner ~= 0 or i < 1 or i > g.n * g.n or g.cells[i] ~= X.EMPTY then return false end
    g.cells[i] = g.turn
    g.moves = g.moves + 1
    g.last = i
    if connected(g, g.turn) then g.winner = g.turn else g.turn = 3 - g.turn end
    return true
end

-- Computer opponent ------------------------------------------------------------
-- A position is judged by how many more cells each side would still need to
-- join its edges (a shortest path where its own stones cost nothing and the
-- opponent's block). It plays the move that most improves its own distance
-- against the opponent's, among the cells on or near either shortest path.

-- Cells `player` still needs to fill to connect, by a 0-1 breadth-first
-- search; also marks, in `onPath`, the cells of one shortest path.
local INF = 9999
local dist, queue = {}, {}

local function onStartEdge(g, player, i)
    if player == X.BLACK then return X.rowOf(g, i) == 1 end
    return X.colOf(g, i) == 1
end

local function onEndEdge(g, player, i)
    if player == X.BLACK then return X.rowOf(g, i) == g.n end
    return X.colOf(g, i) == g.n
end

local function distance(g, player, onPath)
    local n, cells = g.n, g.cells
    local opp = 3 - player
    for i = 1, n * n do dist[i] = INF end
    -- A double-ended queue: zero-cost steps go to the front.
    local front = 2 * n * n + 1
    local back = front - 1
    local function push(i, d, free)
        if d >= dist[i] then return end
        dist[i] = d
        if free then
            front = front - 1
            queue[front] = i
        else
            back = back + 1
            queue[back] = i
        end
    end
    for k = 1, n do
        local i = (player == X.BLACK) and k or ((k - 1) * n + 1)
        local v = cells[i]
        if v ~= opp then push(i, (v == player) and 0 or 1, v == player) end
    end
    local nb = {}
    local best = INF
    while front <= back do
        local i = queue[front]
        front = front + 1
        local d = dist[i]
        if onEndEdge(g, player, i) and d < best then best = d end
        for _, j in ipairs(X.neighbours(g, i, nb)) do
            local v = cells[j]
            if v ~= opp then push(j, d + ((v == player) and 0 or 1), v == player) end
        end
    end
    if onPath and best < INF then
        -- Walk back from the far edge: each step's predecessor is a neighbour
        -- whose distance is this one's minus this cell's own cost.
        local cur = nil
        for k = 1, n do
            local i = (player == X.BLACK) and ((n - 1) * n + k) or (k * n)
            if dist[i] == best then cur = i break end
        end
        while cur do
            onPath[cur] = true
            local step = (cells[cur] == player) and 0 or 1
            local before = dist[cur] - step
            if before == 0 and onStartEdge(g, player, cur) then break end
            local nextCell = nil
            for _, j in ipairs(X.neighbours(g, cur, nb)) do
                if not onPath[j] and dist[j] == before then nextCell = j break end
            end
            cur = nextCell
        end
    end
    return best
end
X.distance = distance

-- Score of the position for `player`: positive is good.
local function evaluate(g, player)
    local mine, theirs = distance(g, player), distance(g, 3 - player)
    if mine == 0 then return 1000 end
    if theirs == 0 then return -1000 end
    return theirs - mine
end

-- Candidate cells: on either side's shortest path, or next to a stone.
local function candidates(g)
    local marks = {}
    distance(g, X.BLACK, marks)
    distance(g, X.WHITE, marks)
    local out, nb = {}, {}
    for i = 1, g.n * g.n do
        if g.cells[i] == X.EMPTY then
            local near = marks[i]
            if not near then
                for _, j in ipairs(X.neighbours(g, i, nb)) do
                    if g.cells[j] ~= X.EMPTY then near = true break end
                end
            end
            if near then out[#out + 1] = i end
        end
    end
    if #out == 0 then
        for i = 1, g.n * g.n do
            if g.cells[i] == X.EMPTY then out[#out + 1] = i end
        end
    end
    return out
end

-- The cell for the side to move. level 1: often random; 2: best by the
-- evaluation; 3: also looks at the opponent's best reply to its best moves.
-- `now`/`timeMs` (optional) bound the thinking time.
function X.chooseMove(g, level, rand, now, timeMs)
    local me = g.turn
    local n = g.n
    if g.moves == 0 then
        -- Open near the centre.
        local mid = (n + 1) // 2
        return X.index(g, mid + rand(3) - 2, mid + rand(3) - 2)
    end
    local cands = candidates(g)
    if level <= 1 and rand(2) == 1 then return cands[rand(#cands)] end
    local deadline = now and (now() + (timeMs or 1500)) or nil
    local scored = {}
    for _, i in ipairs(cands) do
        g.cells[i] = me
        local v = evaluate(g, me)
        g.cells[i] = X.EMPTY
        scored[#scored + 1] = { i = i, v = v + rand(100) / 1000 }
    end
    table.sort(scored, function(a, b) return a.v > b.v end)
    if level <= 2 or scored[1].v >= 1000 then return scored[1].i end
    -- Two plies over the most promising moves.
    local best, bestV = scored[1].i, nil
    for k = 1, math.min(6, #scored) do
        if deadline and now() > deadline then break end
        local i = scored[k].i
        g.cells[i] = me
        local worst = nil
        if evaluate(g, me) >= 1000 then
            worst = 1000
        else
            for _, j in ipairs(candidates(g)) do
                g.cells[j] = 3 - me
                local v = evaluate(g, me)
                g.cells[j] = X.EMPTY
                if not worst or v < worst then worst = v end
                if worst <= -1000 then break end
            end
        end
        g.cells[i] = X.EMPTY
        if worst and (not bestV or worst > bestV) then best, bestV = i, worst end
    end
    return best
end

-- Save/restore: n | turn | moves | cell digits.
function X.serialize(g)
    return table.concat({ g.n, g.turn, g.moves, table.concat(g.cells) }, "|")
end

function X.deserialize(s)
    local n, turn, moves, cells = s:match("^(%d+)|([12])|(%d+)|([012]+)$")
    n = tonumber(n)
    if not n or n < 3 or n > 13 or #cells ~= n * n then return nil end
    local g = X.new(n)
    g.turn, g.moves = tonumber(turn), tonumber(moves)
    for i = 1, n * n do g.cells[i] = cells:byte(i) - 48 end
    if connected(g, X.BLACK) then g.winner = X.BLACK elseif connected(g, X.WHITE) then g.winner = X.WHITE end
    return g
end

return X
