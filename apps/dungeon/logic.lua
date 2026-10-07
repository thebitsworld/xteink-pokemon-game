-- Dungeon Map: the rules. No drawing, so it can be tested on a computer
-- (test/lua_apps/dungeon_test.lua).
--
-- Wall in a dungeon map from the numbers along its edges - how many walls each
-- row and column holds - and the monsters and treasure already marked on it:
--   * every dead end (an open cell with one open neighbour) holds a monster,
--     and every monster stands in a dead end;
--   * every chest lies in a 3x3 treasure room of open cells with exactly one
--     way out, and only there may open cells form a 2x2 square - elsewhere the
--     passages are one cell wide;
--   * all the open cells are joined up.
--
-- A map is a list of rows, each a bit mask: bit c-1 set means column c is a
-- wall. Monsters and chests are masks in the same form.

local D = {}

-- loops: the chance (in %) that a growing passage may join another one.
D.LEVELS = {
    { name = "Easy", size = 6, rooms = 0, loops = 15 },
    { name = "Medium", size = 8, rooms = 0, loops = 25 },
    { name = "Hard", size = 8, rooms = 1, loops = 25 },
}

local POP = {}
for v = 0, 255 do
    local c, x = 0, v
    while x > 0 do
        c = c + (x & 1)
        x = x >> 1
    end
    POP[v] = c
end
D.POP = POP

local function bit(c) return 1 << (c - 1) end
D.bit = bit

function D.wall(rows, r, c) return rows[r] & bit(c) ~= 0 end

-- The numbers along the edges.
function D.clues(rows, n)
    local rowClue, colClue = {}, {}
    for c = 1, n do colClue[c] = 0 end
    for r = 1, n do
        rowClue[r] = POP[rows[r]]
        for c = 1, n do
            if rows[r] & bit(c) ~= 0 then colClue[c] = colClue[c] + 1 end
        end
    end
    return rowClue, colClue
end

local function open(rows, n, r, c)
    return r >= 1 and r <= n and c >= 1 and c <= n and rows[r] & bit(c) == 0
end

local function openNeighbours(rows, n, r, c)
    return (open(rows, n, r - 1, c) and 1 or 0) + (open(rows, n, r + 1, c) and 1 or 0) +
           (open(rows, n, r, c - 1) and 1 or 0) + (open(rows, n, r, c + 1) and 1 or 0)
end
D.openNeighbours = openNeighbours

-- The treasure room around the chest at (r, c), as its top-left corner: a 3x3
-- block of open cells holding that one chest, with exactly one way out.
local function roomFor(p, rows, r, c)
    local n = p.size
    for r0 = math.max(1, r - 2), math.min(r, n - 2) do
        for c0 = math.max(1, c - 2), math.min(c, n - 2) do
            local ok, chests = true, 0
            for i = r0, r0 + 2 do
                for j = c0, c0 + 2 do
                    if rows[i] & bit(j) ~= 0 then ok = false end
                    if p.chests[i] & bit(j) ~= 0 then chests = chests + 1 end
                end
            end
            if ok and chests == 1 then
                local exits = 0
                for k = 0, 2 do
                    if open(rows, n, r0 - 1, c0 + k) then exits = exits + 1 end
                    if open(rows, n, r0 + 3, c0 + k) then exits = exits + 1 end
                    if open(rows, n, r0 + k, c0 - 1) then exits = exits + 1 end
                    if open(rows, n, r0 + k, c0 + 3) then exits = exits + 1 end
                end
                if exits == 1 then return r0, c0 end
            end
        end
    end
    return nil
end

-- Whether `rows` solves the puzzle p ({ size, rowClue, colClue, monsters, chests }).
function D.solved(p, rows)
    local n = p.size
    local rowClue, colClue = D.clues(rows, n)
    for i = 1, n do
        if rowClue[i] ~= p.rowClue[i] or colClue[i] ~= p.colClue[i] then return false end
        if rows[i] & (p.monsters[i] | p.chests[i]) ~= 0 then return false end
    end
    -- Dead ends and monsters; and the open cells, for the connection check.
    local openCount, sr, sc = 0, nil, nil
    for r = 1, n do
        for c = 1, n do
            if rows[r] & bit(c) == 0 then
                openCount = openCount + 1
                sr, sc = r, c
                local k = openNeighbours(rows, n, r, c)
                local monster = p.monsters[r] & bit(c) ~= 0
                if (k == 1) ~= monster then return false end
            end
        end
    end
    if openCount == 0 then return false end
    -- Joined up: a flood from one open cell reaches them all.
    local seen, stack, reached = {}, { sr * 16 + sc }, 0
    seen[sr * 16 + sc] = true
    while #stack > 0 do
        local v = table.remove(stack)
        reached = reached + 1
        local r, c = v // 16, v % 16
        for d = 1, 4 do
            local rr = r + (d == 1 and -1 or d == 2 and 1 or 0)
            local cc = c + (d == 3 and -1 or d == 4 and 1 or 0)
            local key = rr * 16 + cc
            if open(rows, n, rr, cc) and not seen[key] then
                seen[key] = true
                stack[#stack + 1] = key
            end
        end
    end
    if reached ~= openCount then return false end
    -- Treasure rooms, and 2x2 open squares only inside them.
    local inRoom = {}
    for r = 1, n do inRoom[r] = 0 end
    for r = 1, n do
        for c = 1, n do
            if p.chests[r] & bit(c) ~= 0 then
                local r0, c0 = roomFor(p, rows, r, c)
                if not r0 then return false end
                for i = r0, r0 + 2 do
                    inRoom[i] = inRoom[i] | (7 << (c0 - 1))
                end
            end
        end
    end
    for r = 1, n - 1 do
        local openPair = ~rows[r] & ~rows[r + 1]
        local square = openPair & (openPair >> 1) & ((1 << (n - 1)) - 1)
        if square ~= 0 then
            local roomPair = inRoom[r] & inRoom[r + 1]
            if square & ~(roomPair & (roomPair >> 1)) ~= 0 then return false end
        end
    end
    return true
end

-- Saving a game in progress: size, the puzzle's masks, then the player's marks.
function D.serialize(p, marks)
    local parts = { p.size, p.level }
    for r = 1, p.size do
        parts[#parts + 1] = p.monsters[r]
        parts[#parts + 1] = p.chests[r]
        parts[#parts + 1] = p.rowClue[r]
        parts[#parts + 1] = p.colClue[r]
        parts[#parts + 1] = marks.wall[r]
        parts[#parts + 1] = marks.open[r]
    end
    parts[#parts + 1] = p.elapsedMs or 0
    return table.concat(parts, ",")
end

function D.deserialize(s)
    local v = {}
    for x in s:gmatch("%d+") do v[#v + 1] = tonumber(x) end
    local n, level = v[1], v[2]
    if not n or n < 4 or n > 8 or not D.LEVELS[level or 0] or #v ~= 3 + 6 * n then return nil end
    local p = { size = n, level = level, monsters = {}, chests = {}, rowClue = {}, colClue = {} }
    local marks = { wall = {}, open = {} }
    local k = 3
    for r = 1, n do
        p.monsters[r], p.chests[r], p.rowClue[r], p.colClue[r] = v[k], v[k + 1], v[k + 2], v[k + 3]
        marks.wall[r], marks.open[r] = v[k + 4], v[k + 5]
        k = k + 6
    end
    p.elapsedMs = v[k]
    return p, marks
end

return D
