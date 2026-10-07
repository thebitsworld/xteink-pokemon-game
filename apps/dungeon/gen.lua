-- Dungeon Map generator and solver. Usage:
--   local p = smudge.dofile("gen.lua")(D).generate(level, rand)   -- D: logic.lua
-- Loaded only while a map is being made, then dropped.
--
-- A map grows as a maze of one-cell passages from a random start (or from the
-- door of its treasure room), never closing a loop or a 2x2 square; its dead
-- ends get the monsters. It is kept only if its clues have one solution.

return function(D)
local G = {}
local POP, bit = D.POP, D.bit

-- Solver ----------------------------------------------------------------------
-- Row by row: each row is one of the masks with the right number of walls, and
-- every finished row is checked against its neighbours (dead ends, 2x2
-- squares) and the column counts. Module-level state, no closures per call.

local n, p, cand, rows, colWalls, found, budget, first
local nearChest -- nearChest[r]: columns c where a chest lies within 2 of the 2x2 square at rows r..r+1, cols c..c+1

local function deadEndsOK(r)
    -- Row r is final once rows r-1 and r+1 are placed (or off the map).
    local up, mid, down = rows[r - 1] or 0xFF, rows[r], rows[r + 1] or 0xFF
    local monsters = p.monsters[r]
    for c = 1, n do
        local b = bit(c)
        if mid & b == 0 then
            local k = (up & b == 0 and 1 or 0) + (down & b == 0 and 1 or 0)
            if c > 1 and mid & (b >> 1) == 0 then k = k + 1 end
            if c < n and mid & (b << 1) == 0 then k = k + 1 end
            if (k == 1) ~= (monsters & b ~= 0) or k == 0 then return false end
        end
    end
    return true
end

local function squaresOK(r)
    local openPair = ~rows[r] & ~rows[r + 1]
    local square = openPair & (openPair >> 1) & ((1 << (n - 1)) - 1)
    return square & ~nearChest[r] == 0
end

local function search(r)
    if found >= 2 or budget <= 0 then return end
    budget = budget - 1
    if r > n then
        if deadEndsOK(n) and D.solved(p, rows) then
            found = found + 1
            if found == 1 then
                first = {}
                for i = 1, n do first[i] = rows[i] end
            end
        end
        return
    end
    local list = cand[r]
    local left = n - r
    local fixed = p.monsters[r] | p.chests[r]
    for i = 1, #list do
        local mask = list[i]
        rows[r] = mask
        local ok = mask & fixed == 0
        if ok then
            for c = 1, n do
                local w = colWalls[c] + ((mask >> (c - 1)) & 1)
                if w > p.colClue[c] or w + left < p.colClue[c] then
                    ok = false
                    break
                end
            end
        end
        if ok and r > 1 then ok = squaresOK(r - 1) and deadEndsOK(r - 1) end
        if ok then
            for c = 1, n do colWalls[c] = colWalls[c] + ((mask >> (c - 1)) & 1) end
            search(r + 1)
            for c = 1, n do colWalls[c] = colWalls[c] - ((mask >> (c - 1)) & 1) end
            if found >= 2 or budget <= 0 then break end
        end
    end
    rows[r] = nil
end

-- How many solutions the puzzle has, up to 2 (nil when it gave up), and the first.
function G.solve(puzzle, maxNodes)
    p, n = puzzle, puzzle.size
    cand, rows, colWalls, found, budget, first = {}, {}, {}, 0, maxNodes or 200000, nil
    for c = 1, n do colWalls[c] = 0 end
    -- Row masks grouped by their number of walls, shared by every row with that count.
    local byCount = {}
    for mask = 0, (1 << n) - 1 do
        local k = POP[mask]
        byCount[k] = byCount[k] or {}
        local list = byCount[k]
        list[#list + 1] = mask
    end
    for r = 1, n do cand[r] = byCount[p.rowClue[r]] or {} end
    nearChest = {}
    for r = 1, n - 1 do
        local m = 0
        for c = 1, n - 1 do
            for i = math.max(1, r - 1), math.min(n, r + 2) do
                for j = math.max(1, c - 1), math.min(n, c + 2) do
                    if p.chests[i] & bit(j) ~= 0 then m = m | bit(c) end
                end
            end
        end
        nearChest[r] = m
    end
    search(1)
    G.lastNodes = (maxNodes or 200000) - budget
    local result = (budget > 0 or found >= 2) and found or nil
    local sol = first
    cand, rows, colWalls, nearChest, first = nil, nil, nil, nil, nil
    return result, sol
end

-- Generator --------------------------------------------------------------------

local function attempt(level, rand)
    local L = D.LEVELS[level]
    local size = L.size
    local open, room = {}, {}
    for r = 1, size do
        open[r], room[r] = 0, 0
    end
    local chests = {}
    for r = 1, size do chests[r] = 0 end
    local function isOpen(r, c) return r >= 1 and r <= size and c >= 1 and c <= size and open[r] & bit(c) ~= 0 end
    local function inRoom(r, c) return r >= 1 and r <= size and c >= 1 and c <= size and room[r] & bit(c) ~= 0 end
    -- Whether opening (r, c) would complete a 2x2 square of open cells.
    local function closesSquare(r, c)
        for tr = r - 1, r do
            for tc = c - 1, c do
                if tr >= 1 and tc >= 1 and tr < size and tc < size then
                    local k = 0
                    for i = tr, tr + 1 do
                        for j = tc, tc + 1 do
                            if (i ~= r or j ~= c) and isOpen(i, j) then k = k + 1 end
                        end
                    end
                    if k == 3 then return true end
                end
            end
        end
        return false
    end
    if L.rooms > 0 then
        local r0, c0 = rand(size - 2), rand(size - 2)
        for r = r0, r0 + 2 do
            open[r] = open[r] | (7 << (c0 - 1))
            room[r] = room[r] | (7 << (c0 - 1))
        end
        chests[r0 + rand(3) - 1] = bit(c0 + rand(3) - 1)
        -- The door: one cell just outside the room.
        local doors = {}
        for k = 0, 2 do
            if r0 > 1 then doors[#doors + 1] = { r0 - 1, c0 + k } end
            if r0 + 3 <= size then doors[#doors + 1] = { r0 + 3, c0 + k } end
            if c0 > 1 then doors[#doors + 1] = { r0 + k, c0 - 1 } end
            if c0 + 3 <= size then doors[#doors + 1] = { r0 + k, c0 + 3 } end
        end
        local d = doors[rand(#doors)]
        open[d[1]] = open[d[1]] | bit(d[2])
    else
        local r, c = rand(size), rand(size)
        open[r] = bit(c)
    end
    local last = nil
    -- Grow: open a closed cell next to exactly one open cell, outside and not
    -- touching the room, closing no 2x2 square; until none is left.
    local options, near = {}, {}
    while true do
        for i = #options, 1, -1 do options[i] = nil end
        for r = 1, size do
            for c = 1, size do
                if not isOpen(r, c) then
                    local k = (isOpen(r - 1, c) and 1 or 0) + (isOpen(r + 1, c) and 1 or 0) +
                              (isOpen(r, c - 1) and 1 or 0) + (isOpen(r, c + 1) and 1 or 0)
                    local touchesRoom = inRoom(r - 1, c) or inRoom(r + 1, c) or inRoom(r, c - 1) or inRoom(r, c + 1)
                    -- Joining two passages (k == 2) makes a loop: fewer dead ends, so
                    -- fewer monsters to go on, and a harder map.
                    if (k == 1 or (k == 2 and rand(100) <= L.loops)) and not touchesRoom and
                        not closesSquare(r, c) then
                        options[#options + 1] = r * 16 + c
                    end
                end
            end
        end
        if #options == 0 then break end
        -- Mostly carry on from the cell just opened (long winding passages, few
        -- dead ends); otherwise branch off anywhere.
        local v = nil
        if last and rand(100) <= 90 then
            for i = #near, 1, -1 do near[i] = nil end
            for _, o in ipairs(options) do
                local dr, dc = o // 16 - last // 16, o % 16 - last % 16
                if dr * dr + dc * dc == 1 then near[#near + 1] = o end
            end
            if #near > 0 then v = near[rand(#near)] end
        end
        v = v or options[rand(#options)]
        last = v
        local r, c = v // 16, v % 16
        open[r] = open[r] | bit(c)
    end
    local full = (1 << size) - 1
    local walls, monsters = {}, {}
    for r = 1, size do walls[r] = full & ~open[r] end
    for r = 1, size do
        monsters[r] = 0
        for c = 1, size do
            if open[r] & bit(c) ~= 0 and room[r] & bit(c) == 0 and D.openNeighbours(walls, size, r, c) == 1 then
                monsters[r] = monsters[r] | bit(c)
            end
        end
    end
    local rowClue, colClue = D.clues(walls, size)
    local puzzle = { size = size, level = level, monsters = monsters, chests = chests, rowClue = rowClue,
                     colClue = colClue, elapsedMs = 0 }
    return puzzle, walls
end

-- A new puzzle at `level` with one solution (nil if none turned up).
function G.generate(level, rand, tries)
    for _ = 1, tries or 40 do
        local puzzle, walls = attempt(level, rand)
        if D.solved(puzzle, walls) and G.solve(puzzle, 60000) == 1 then return puzzle, walls end
        collectgarbage("collect")
    end
    return nil
end

return G
end
