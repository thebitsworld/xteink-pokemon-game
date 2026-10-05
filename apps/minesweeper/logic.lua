-- Minesweeper board logic: generation, digging, flags, chording. No drawing,
-- so it can be tested on a computer (test/lua_apps/minesweeper_test.lua).
--
-- Cells are stored in flat arrays indexed (row - 1) * cols + col, which costs
-- far less Lua heap than a table per cell.

local Board = {}
Board.__index = Board

local COVERED, OPEN, FLAGGED = 0, 1, 2
Board.COVERED, Board.OPEN, Board.FLAGGED = COVERED, OPEN, FLAGGED

function Board.new(cols, rows, mines)
    local b = setmetatable({
        cols = cols, rows = rows, mines = mines,
        mine = {}, num = {}, vis = {},
        generated = false,
        state = "playing", -- "playing" | "won" | "lost"
        opened = 0, flags = 0,
        exploded = nil,    -- index of the mine that was dug
    }, Board)
    for i = 1, cols * rows do
        b.mine[i] = false
        b.num[i] = 0
        b.vis[i] = COVERED
    end
    return b
end

function Board:index(c, r)
    return (r - 1) * self.cols + c
end

function Board:inside(c, r)
    return c >= 1 and c <= self.cols and r >= 1 and r <= self.rows
end

-- Calls fn(c, r) for each of the up to 8 neighbours of (c, r).
function Board:eachNeighbour(c, r, fn)
    for dr = -1, 1 do
        for dc = -1, 1 do
            if (dc ~= 0 or dr ~= 0) and self:inside(c + dc, r + dr) then fn(c + dc, r + dr) end
        end
    end
end

-- Lays the mines after the first dig, keeping (safeC, safeR) and its
-- neighbours clear so the first dig always opens an area. `rand(n)` returns
-- an integer in 1..n.
function Board:generate(safeC, safeR, rand)
    local candidates = {}
    for r = 1, self.rows do
        for c = 1, self.cols do
            if math.abs(c - safeC) > 1 or math.abs(r - safeR) > 1 then
                candidates[#candidates + 1] = self:index(c, r)
            end
        end
    end
    local count = math.min(self.mines, #candidates)
    -- Partial Fisher-Yates: the first `count` entries become the mines.
    for i = 1, count do
        local j = i + rand(#candidates - i + 1) - 1
        candidates[i], candidates[j] = candidates[j], candidates[i]
        self.mine[candidates[i]] = true
    end
    self.mines = count
    for r = 1, self.rows do
        for c = 1, self.cols do
            local n = 0
            self:eachNeighbour(c, r, function(nc, nr)
                if self.mine[self:index(nc, nr)] then n = n + 1 end
            end)
            self.num[self:index(c, r)] = n
        end
    end
    self.generated = true
end

-- Opens (c, r), flooding outwards through cells with no neighbouring mines.
-- Returns false when it was a mine (the game is lost).
function Board:dig(c, r, rand)
    if self.state ~= "playing" or not self:inside(c, r) then return true end
    if not self.generated then self:generate(c, r, rand) end
    local i = self:index(c, r)
    if self.vis[i] ~= COVERED then return true end
    if self.mine[i] then
        self.vis[i] = OPEN
        self.exploded = i
        self.state = "lost"
        return false
    end
    local stack = { c, r }
    while #stack > 0 do
        local sr = table.remove(stack)
        local sc = table.remove(stack)
        local si = self:index(sc, sr)
        if self.vis[si] == COVERED and not self.mine[si] then
            self.vis[si] = OPEN
            self.opened = self.opened + 1
            if self.num[si] == 0 then
                self:eachNeighbour(sc, sr, function(nc, nr)
                    if self.vis[self:index(nc, nr)] == COVERED then
                        stack[#stack + 1] = nc
                        stack[#stack + 1] = nr
                    end
                end)
            end
        end
    end
    if self.opened == self.cols * self.rows - self.mines then
        self.state = "won"
        -- Flag the remaining mines, as most versions do on a win.
        for k = 1, self.cols * self.rows do
            if self.mine[k] and self.vis[k] ~= FLAGGED then
                self.vis[k] = FLAGGED
                self.flags = self.flags + 1
            end
        end
    end
    return true
end

function Board:toggleFlag(c, r)
    if self.state ~= "playing" or not self:inside(c, r) then return end
    local i = self:index(c, r)
    if self.vis[i] == COVERED then
        self.vis[i] = FLAGGED
        self.flags = self.flags + 1
    elseif self.vis[i] == FLAGGED then
        self.vis[i] = COVERED
        self.flags = self.flags - 1
    end
end

-- On an open number whose flags already add up, digs every other covered
-- neighbour. Returns false if that hit a mine.
function Board:chord(c, r, rand)
    if self.state ~= "playing" or not self:inside(c, r) then return true end
    local i = self:index(c, r)
    if self.vis[i] ~= OPEN or self.num[i] == 0 then return true end
    local flagged = 0
    self:eachNeighbour(c, r, function(nc, nr)
        if self.vis[self:index(nc, nr)] == FLAGGED then flagged = flagged + 1 end
    end)
    if flagged ~= self.num[i] then return true end
    local ok = true
    self:eachNeighbour(c, r, function(nc, nr)
        if self.vis[self:index(nc, nr)] == COVERED then
            if not self:dig(nc, nr, rand) then ok = false end
        end
    end)
    return ok
end

-- What a tap/Confirm on (c, r) does: dig a covered cell, chord an open one.
function Board:activate(c, r, rand)
    local i = self:index(c, r)
    if self.vis[i] == OPEN then return self:chord(c, r, rand) end
    if self.vis[i] == FLAGGED then return true end
    return self:dig(c, r, rand)
end

return Board
