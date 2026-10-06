-- Sea Battle rules and the computer's shots. No drawing, so it can be tested
-- on a computer (test/lua_apps/seabattle_test.lua).
--
-- Each side hides a fleet of five ships (5, 4, 3, 3 and 2 squares long) on a
-- 10x10 grid; ships never touch, not even at a corner. The sides take turns
-- firing at one square; the first to sink the whole enemy fleet wins.

local S = {}

S.N = 10
S.FLEET = { 5, 4, 3, 3, 2 }
S.NAMES = { "Carrier", "Battleship", "Cruiser", "Submarine", "Destroyer" }
-- What a side knows about a square of the enemy grid.
S.UNKNOWN, S.MISS, S.HIT, S.SUNK = 0, 1, 2, 3

-- Grids are strings of 100 digits (row by row): as Lua tables they would
-- take several KB of the X3's 75 KB. `at(grid, i)` reads square i.
local function at(grid, i) return grid:byte(i) - 48 end
local function put(grid, i, v) return grid:sub(1, i - 1) .. v .. grid:sub(i + 1) end
S.at = at

function S.index(c, r) return (r - 1) * S.N + c end
function S.colOf(i) return (i - 1) % S.N + 1 end
function S.rowOf(i) return (i - 1) // S.N + 1 end

-- The squares of a ship of length `len` starting at (c, r), across (dir 1) or
-- down (dir 2), or nil if it does not fit on the grid.
local function shipCells(c, r, dir, len)
    local cells = {}
    for k = 0, len - 1 do
        local cc, rr = c + (dir == 1 and k or 0), r + (dir == 2 and k or 0)
        if cc > S.N or rr > S.N then return nil end
        cells[#cells + 1] = S.index(cc, rr)
    end
    return cells
end

-- Whether a ship on `cells` would overlap or touch one already on `occ`.
local function blocked(occ, cells)
    for _, i in ipairs(cells) do
        local c, r = S.colOf(i), S.rowOf(i)
        for dr = -1, 1 do
            for dc = -1, 1 do
                local cc, rr = c + dc, r + dr
                if cc >= 1 and cc <= S.N and rr >= 1 and rr <= S.N and occ[S.index(cc, rr)] ~= 0 then
                    return true
                end
            end
        end
    end
    return false
end

-- A fleet: { ships = { { c, r, dir, len, hits } ... }, occ = which ship (0
-- for none) is on each square }. occ is a table while ships are being placed
-- and a digit string afterwards (see finish()).
local function emptyFleet()
    local f = { ships = {}, occ = {} }
    for i = 1, S.N * S.N do f.occ[i] = 0 end
    return f
end

local function finish(f)
    f.occ = table.concat(f.occ)
    return f
end

local function addShip(f, c, r, dir, len)
    local cells = shipCells(c, r, dir, len)
    if not cells or blocked(f.occ, cells) then return false end
    local n = #f.ships + 1
    f.ships[n] = { c = c, r = r, dir = dir, len = len, hits = 0 }
    for _, i in ipairs(cells) do f.occ[i] = n end
    return true
end

-- A random fleet; `rand(n)` returns 1..n.
function S.randomFleet(rand)
    while true do
        local f = emptyFleet()
        local ok = true
        for _, len in ipairs(S.FLEET) do
            local placed = false
            for _ = 1, 200 do
                if addShip(f, rand(S.N), rand(S.N), rand(2), len) then
                    placed = true
                    break
                end
            end
            if not placed then ok = false break end
        end
        if ok then return finish(f) end
    end
end

function S.shipSquares(ship) return shipCells(ship.c, ship.r, ship.dir, ship.len) end

local NO_SHOTS = string.rep("0", 100)

function S.new(rand)
    return { fleets = { S.randomFleet(rand), S.randomFleet(rand) }, shots = { NO_SHOTS, NO_SHOTS }, turn = 1,
             winner = 0, last = nil }
end

-- Fires player `p`'s shot at square `i` of the enemy grid. Returns "miss",
-- "hit" or "sunk" (and the ship), or nil if the square was already shot.
function S.fire(g, i)
    if g.winner ~= 0 then return nil end
    local p = g.turn
    local shots, enemy = g.shots[p], g.fleets[3 - p]
    if at(shots, i) ~= S.UNKNOWN then return nil end
    local n = at(enemy.occ, i)
    local result, ship = "miss", nil
    if n == 0 then
        shots = put(shots, i, S.MISS)
    else
        ship = enemy.ships[n]
        ship.hits = ship.hits + 1
        shots = put(shots, i, S.HIT)
        result = "hit"
        if ship.hits == ship.len then
            result = "sunk"
            for _, s in ipairs(S.shipSquares(ship)) do shots = put(shots, s, S.SUNK) end
            -- No ship touches it, so its neighbours are water.
            for _, s in ipairs(S.shipSquares(ship)) do
                for dr = -1, 1 do
                    for dc = -1, 1 do
                        local c, r = S.colOf(s) + dc, S.rowOf(s) + dr
                        if c >= 1 and c <= S.N and r >= 1 and r <= S.N and at(shots, S.index(c, r)) == S.UNKNOWN then
                            shots = put(shots, S.index(c, r), S.MISS)
                        end
                    end
                end
            end
            local left = 0
            for _, s in ipairs(enemy.ships) do
                if s.hits < s.len then left = left + 1 end
            end
            if left == 0 then g.winner = p end
        end
    end
    g.shots[p] = shots
    g.last = { player = p, square = i, result = result, ship = n }
    if g.winner == 0 then g.turn = 3 - p end
    return result, ship
end

-- How many of `p`'s enemy ships are still afloat, by fleet slot.
function S.afloat(g, p)
    local out = {}
    for n, s in ipairs(g.fleets[3 - p].ships) do out[n] = s.hits < s.len end
    return out
end

-- Computer opponent ------------------------------------------------------------
-- It sees only its own shot grid and which enemy ships are sunk - the same as
-- a player at the table.

-- Level 1: any square not yet shot.
local function randomShot(shots, rand)
    local open = {}
    for i = 1, S.N * S.N do
        if at(shots, i) == S.UNKNOWN then open[#open + 1] = i end
    end
    return open[rand(#open)]
end

-- Squares next to a hit that is not yet part of a sunk ship, along the line
-- when two hits are in a row.
local function targetShots(shots)
    local out = {}
    for i = 1, S.N * S.N do
        if at(shots, i) == S.HIT then
            local c, r = S.colOf(i), S.rowOf(i)
            local across = (c > 1 and at(shots, i - 1) == S.HIT) or (c < S.N and at(shots, i + 1) == S.HIT)
            local down = (r > 1 and at(shots, i - S.N) == S.HIT) or (r < S.N and at(shots, i + S.N) == S.HIT)
            local dirs = {}
            if not down then dirs[#dirs + 1] = { -1, 0 } dirs[#dirs + 1] = { 1, 0 } end
            if not across then dirs[#dirs + 1] = { 0, -1 } dirs[#dirs + 1] = { 0, 1 } end
            for _, d in ipairs(dirs) do
                local cc, rr = c + d[1], r + d[2]
                if cc >= 1 and cc <= S.N and rr >= 1 and rr <= S.N and at(shots, S.index(cc, rr)) == S.UNKNOWN then
                    out[#out + 1] = S.index(cc, rr)
                end
            end
        end
    end
    return out
end

-- Level 3: how many ways each square could hold a ship still afloat, given
-- every shot so far; while a hit is unsunk, only ways through a hit count.
local function densityShot(shots, afloat, rand)
    local score = {}
    for i = 1, S.N * S.N do score[i] = 0 end
    local hunting = true
    for i = 1, S.N * S.N do
        if at(shots, i) == S.HIT then hunting = false end
    end
    for n, len in ipairs(S.FLEET) do
        if afloat[n] then
            for r = 1, S.N do
                for c = 1, S.N do
                    for dir = 1, 2 do
                        local cells = shipCells(c, r, dir, len)
                        if cells then
                            local ok, hits = true, 0
                            for _, i in ipairs(cells) do
                                local v = at(shots, i)
                                if v == S.MISS or v == S.SUNK then ok = false break end
                                if v == S.HIT then hits = hits + 1 end
                            end
                            if ok and (hunting or hits > 0) then
                                local weight = 1 + hits * 10
                                for _, i in ipairs(cells) do
                                    if at(shots, i) == S.UNKNOWN then score[i] = score[i] + weight end
                                end
                            end
                        end
                    end
                end
            end
        end
    end
    local best, choices = 0, {}
    for i = 1, S.N * S.N do
        if score[i] > best then
            best, choices = score[i], { i }
        elseif score[i] == best and best > 0 then
            choices[#choices + 1] = i
        end
    end
    if #choices == 0 then return randomShot(shots, rand) end
    return choices[rand(#choices)]
end

-- The square the side to move fires at. level 1 random, 2 hunt on a
-- checkerboard and finish what it hits, 3 by probability.
function S.chooseShot(g, level, rand)
    local shots = g.shots[g.turn]
    if level <= 1 then return randomShot(shots, rand) end
    if level >= 3 then return densityShot(shots, S.afloat(g, g.turn), rand) end
    local targets = targetShots(shots)
    if #targets > 0 then return targets[rand(#targets)] end
    -- Every ship is at least 2 long, so half the squares are enough to find them.
    local open = {}
    for i = 1, S.N * S.N do
        if at(shots, i) == S.UNKNOWN and (S.colOf(i) + S.rowOf(i)) % 2 == 0 then open[#open + 1] = i end
    end
    if #open == 0 then return randomShot(shots, rand) end
    return open[rand(#open)]
end

-- Save/restore ---------------------------------------------------------------
-- turn | then per side: ships as c.r.dir.len.hits joined by "," | shot digits.

function S.serialize(g)
    local parts = { tostring(g.turn) }
    for p = 1, 2 do
        local ships = {}
        for n, s in ipairs(g.fleets[p].ships) do
            ships[n] = table.concat({ s.c, s.r, s.dir, s.len, s.hits }, ".")
        end
        parts[#parts + 1] = table.concat(ships, ",")
        parts[#parts + 1] = g.shots[p]
    end
    return table.concat(parts, "|")
end

function S.deserialize(text)
    local fields = {}
    for f in (text .. "|"):gmatch("([^|]*)|") do fields[#fields + 1] = f end
    if #fields ~= 5 then return nil end
    local g = { fleets = {}, shots = {}, turn = tonumber(fields[1]), winner = 0 }
    if g.turn ~= 1 and g.turn ~= 2 then return nil end
    for p = 1, 2 do
        local f = emptyFleet()
        local n = 0
        for c, r, dir, len, hits in fields[2 * p]:gmatch("(%d+)%.(%d+)%.(%d+)%.(%d+)%.(%d+)") do
            n = n + 1
            if tonumber(len) ~= S.FLEET[n] or not addShip(f, tonumber(c), tonumber(r), tonumber(dir), tonumber(len)) then
                return nil
            end
            f.ships[n].hits = tonumber(hits)
        end
        if n ~= #S.FLEET then return nil end
        g.fleets[p] = finish(f)
        local shots = fields[2 * p + 1]
        if #shots ~= S.N * S.N or shots:find("[^0-3]") then return nil end
        g.shots[p] = shots
    end
    return g
end

return S
