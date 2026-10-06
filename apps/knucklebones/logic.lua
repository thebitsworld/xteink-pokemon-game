-- Knucklebones rules and the computer opponent. No drawing, so it can be
-- tested on a computer (test/lua_apps/knucklebones_test.lua).
--
-- Two players, a 3x3 grid each. On your turn you roll a d6 and put it in one of
-- your three columns (3 dice at most). Putting a value there removes every die
-- of that value from the opponent's facing column. A column scores each value
-- as value * count * count (three 4s = 36). The game ends the moment either
-- grid is full; the higher total wins.

local K = {}

K.COLS = 3
K.DEPTH = 3

-- `rand(n)` returns 1..n. `first` is the player who moves first (1 or 2).
function K.new(rand, first)
    local g = { grid = { {}, {} }, turn = first or 1, die = rand(6), winner = 0, last = nil }
    for p = 1, 2 do
        for c = 1, K.COLS do g.grid[p][c] = {} end
    end
    return g
end

function K.columnScore(col)
    local count = {}
    for _, v in ipairs(col) do count[v] = (count[v] or 0) + 1 end
    local total = 0
    for v, n in pairs(count) do total = total + v * n * n end
    return total
end

function K.score(g, p)
    local total = 0
    for c = 1, K.COLS do total = total + K.columnScore(g.grid[p][c]) end
    return total
end

function K.canPlace(g, c)
    return g.winner == 0 and c >= 1 and c <= K.COLS and #g.grid[g.turn][c] < K.DEPTH
end

local function full(grid)
    for c = 1, K.COLS do
        if #grid[c] < K.DEPTH then return false end
    end
    return true
end

-- How many of the opponent's dice placing `value` in column `c` would remove.
function K.wouldRemove(g, c, value)
    local n = 0
    for _, v in ipairs(g.grid[3 - g.turn][c]) do
        if v == value then n = n + 1 end
    end
    return n
end

-- Places the current die in column `c` for the player to move, then rolls the
-- next player's die. Returns false if the column is full or the game is over.
function K.place(g, c, rand)
    if not K.canPlace(g, c) then return false end
    local p, value = g.turn, g.die
    local mine = g.grid[p][c]
    mine[#mine + 1] = value
    -- Remove the matching dice opposite; the survivors close up.
    local theirs, kept = g.grid[3 - p][c], {}
    for _, v in ipairs(theirs) do
        if v ~= value then kept[#kept + 1] = v end
    end
    local removed = #theirs - #kept
    g.grid[3 - p][c] = kept
    g.last = { player = p, col = c, value = value, removed = removed }
    if full(g.grid[p]) then
        local a, b = K.score(g, 1), K.score(g, 2)
        g.winner = (a > b) and 1 or ((b > a) and 2 or -1)
        g.die = 0
        return true
    end
    g.turn = 3 - p
    g.die = rand(6)
    return true
end

-- Computer opponent ---------------------------------------------------------

local function copyGrid(grid)
    local out = { {}, {} }
    for p = 1, 2 do
        for c = 1, K.COLS do
            local src, dst = grid[p][c], {}
            for i = 1, #src do dst[i] = src[i] end
            out[p][c] = dst
        end
    end
    return out
end

-- The position after `p` puts `value` in column `c` (no new die).
local function after(grid, p, c, value)
    local next = copyGrid(grid)
    local mine = next[p][c]
    mine[#mine + 1] = value
    local kept = {}
    for _, v in ipairs(next[3 - p][c]) do
        if v ~= value then kept[#kept + 1] = v end
    end
    next[3 - p][c] = kept
    return next
end

local function gridScore(grid, p)
    local total = 0
    for c = 1, K.COLS do total = total + K.columnScore(grid[p][c]) end
    return total
end

-- Score margin for `p`; a filled grid is settled, so it counts as won or lost.
local function margin(grid, p)
    local d = gridScore(grid, p) - gridScore(grid, 3 - p)
    if full(grid[p]) or full(grid[3 - p]) then
        if d > 0 then return d + 1000 elseif d < 0 then return d - 1000 end
    end
    return d
end

-- Expected margin for `p` when `mover` is about to roll, searching `plies`
-- more placements (each one averaged over the six dice it could roll).
local function expected(grid, p, mover, plies)
    if plies == 0 or full(grid[1]) or full(grid[2]) then return margin(grid, p) end
    local sum = 0
    for d = 1, 6 do
        local best = nil
        for c = 1, K.COLS do
            if #grid[mover][c] < K.DEPTH then
                local v = expected(after(grid, mover, c, d), mover, 3 - mover, plies - 1)
                if not best or v > best then best = v end
            end
        end
        -- `best` is from the mover's side; turn it into `p`'s.
        sum = sum + (mover == p and best or -best)
    end
    return sum / 6
end

-- The column the player to move should use. level 1: half the time a random
-- column, otherwise greedy; level 2: greedy (best margin now); level 3: also
-- looks two placements ahead (the opponent's reply and its own next move,
-- averaged over the dice they could roll). Only the die already rolled is
-- used, never the future ones.
function K.chooseColumn(g, level, rand)
    local p, value = g.turn, g.die
    local open = {}
    for c = 1, K.COLS do
        if #g.grid[p][c] < K.DEPTH then open[#open + 1] = c end
    end
    if #open == 0 then return nil end
    if level <= 1 and rand(2) == 1 then return open[rand(#open)] end

    local best, choice = nil, nil
    for _, c in ipairs(open) do
        local grid = after(g.grid, p, c, value)
        local v = expected(grid, p, 3 - p, level >= 3 and 2 or 0)
        -- Ties: prefer the column with more room, so the opening is not
        -- always stacked into one column.
        v = v + (K.DEPTH - #g.grid[p][c]) * 0.01
        if not best or v > best then best, choice = v, c end
    end
    return choice
end

-- Save/restore ---------------------------------------------------------------

function K.serialize(g)
    local parts = { tostring(g.turn), tostring(g.die), tostring(g.winner) }
    for p = 1, 2 do
        for c = 1, K.COLS do parts[#parts + 1] = table.concat(g.grid[p][c], ".") end
    end
    return table.concat(parts, "|")
end

function K.deserialize(s)
    local fields = {}
    for f in (s .. "|"):gmatch("([^|]*)|") do fields[#fields + 1] = f end
    if #fields ~= 3 + 2 * K.COLS then return nil end
    local g = { grid = { {}, {} }, turn = tonumber(fields[1]), die = tonumber(fields[2]),
                winner = tonumber(fields[3]) }
    if (g.turn ~= 1 and g.turn ~= 2) or not g.die or not g.winner then return nil end
    local i = 4
    for p = 1, 2 do
        for c = 1, K.COLS do
            local col = {}
            for n in fields[i]:gmatch("%d+") do
                local v = tonumber(n)
                if v < 1 or v > 6 then return nil end
                col[#col + 1] = v
            end
            if #col > K.DEPTH then return nil end
            g.grid[p][c] = col
            i = i + 1
        end
    end
    if g.winner == 0 and (g.die < 1 or g.die > 6) then return nil end
    return g
end

return K
