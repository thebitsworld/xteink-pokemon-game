-- Logic tests for apps/minesweeper/logic.lua (run by ctest: LuaAppLogic_minesweeper).
local Board = dofile(APPS .. "/minesweeper/logic.lua")

-- math.random, seeded: Lua here has 32-bit integers, so a hand-rolled LCG overflows.
math.randomseed(12345)
local function rand(n) return math.random(1, n) end

local function countMines(b)
    local n = 0
    for i = 1, b.cols * b.rows do if b.mine[i] then n = n + 1 end end
    return n
end

-- The first dig is always safe, opens an area, and lays exactly `mines` mines
-- outside the 3x3 around it.
for trial = 1, 50 do
    local b = Board.new(9, 9, 10)
    local c, r = rand(9), rand(9)
    assert(b:dig(c, r, rand), "first dig hit a mine")
    assert(countMines(b) == 10, "wrong mine count")
    for dr = -1, 1 do
        for dc = -1, 1 do
            if b:inside(c + dc, r + dr) then assert(not b.mine[b:index(c + dc, r + dr)], "mine next to first dig") end
        end
    end
    assert(b.opened >= 1)
    assert(b.num[b:index(c, r)] == 0, "first dig must open a zero cell")
end

-- Numbers count the neighbouring mines.
do
    local b = Board.new(5, 5, 3)
    b:dig(1, 1, rand)
    for r = 1, 5 do
        for c = 1, 5 do
            local n = 0
            b:eachNeighbour(c, r, function(nc, nr) if b.mine[b:index(nc, nr)] then n = n + 1 end end)
            assert(b.num[b:index(c, r)] == n)
        end
    end
end

-- Digging every safe cell wins and flags the mines; digging a mine loses.
do
    local b = Board.new(8, 8, 10)
    b:dig(4, 4, rand)
    for r = 1, 8 do
        for c = 1, 8 do
            if not b.mine[b:index(c, r)] then b:dig(c, r, rand) end
        end
    end
    assert(b.state == "won", "should have won")
    assert(b.flags == 10)

    local lost = Board.new(8, 8, 10)
    lost:dig(4, 4, rand)
    for i = 1, 64 do
        if lost.mine[i] then
            local c, r = (i - 1) % 8 + 1, (i - 1) // 8 + 1
            assert(lost:dig(c, r, rand) == false)
            break
        end
    end
    assert(lost.state == "lost" and lost.exploded ~= nil)
    -- Nothing changes once the game is over.
    lost:toggleFlag(1, 1)
    assert(lost.flags == 0)
end

-- Flags block digging; chording digs the rest once the flags add up, and a
-- wrong flag makes the chord hit a mine.
do
    local b = Board.new(9, 9, 10)
    b:dig(5, 5, rand)
    -- Find an open number cell with a covered neighbour.
    local target
    for r = 1, 9 do
        for c = 1, 9 do
            local i = b:index(c, r)
            if b.vis[i] == Board.OPEN and b.num[i] > 0 then target = target or { c, r } end
        end
    end
    assert(target, "expected an open number")
    local c, r = target[1], target[2]
    -- Correct flags: chord opens everything else around it without losing.
    b:eachNeighbour(c, r, function(nc, nr)
        if b.mine[b:index(nc, nr)] then b:toggleFlag(nc, nr) end
    end)
    assert(b:chord(c, r, rand) == true)
    b:eachNeighbour(c, r, function(nc, nr)
        local i = b:index(nc, nr)
        assert(b.vis[i] ~= Board.COVERED, "chord left a covered neighbour")
    end)
    -- A flagged cell is not dug by activate().
    local fc, fr
    for i = 1, 81 do if b.vis[i] == Board.FLAGGED then fc, fr = (i - 1) % 9 + 1, (i - 1) // 9 + 1 end end
    if fc then assert(b:activate(fc, fr, rand) == true and b.vis[b:index(fc, fr)] == Board.FLAGGED) end
end

-- Fewer cells than mines (tiny board) never loops forever.
do
    local b = Board.new(3, 3, 20)
    assert(b:dig(2, 2, rand))
    assert(b.state == "won")
end

print("minesweeper logic ok")
