-- Logic tests for apps/dungeon/logic.lua and gen.lua (run by ctest: LuaAppLogic_dungeon).
local D = dofile(APPS .. "/dungeon/logic.lua")
local Gen = dofile(APPS .. "/dungeon/gen.lua")(D)

math.randomseed(2024)
local function rand(n) return math.random(1, n) end

-- A map from rows of text: # wall, . open, M monster, T chest.
local function parse(lines)
    local n = #lines
    local p = { size = n, level = 1, monsters = {}, chests = {}, elapsedMs = 0 }
    local walls = {}
    for r, line in ipairs(lines) do
        walls[r], p.monsters[r], p.chests[r] = 0, 0, 0
        for c = 1, n do
            local ch = line:sub(c, c)
            if ch == "#" then walls[r] = walls[r] | D.bit(c) end
            if ch == "M" then p.monsters[r] = p.monsters[r] | D.bit(c) end
            if ch == "T" then p.chests[r] = p.chests[r] | D.bit(c) end
        end
    end
    p.rowClue, p.colClue = D.clues(walls, n)
    return p, walls
end

-- The rules, one at a time.
do
    local p, walls = parse({
        "M.....",
        "#####.",
        "......",
        ".#####",
        ".....M",
        "######",
    })
    assert(D.solved(p, walls), "a winding passage with monsters at both ends")

    walls[6] = walls[6] & ~D.bit(1) -- opens a dead end with no monster
    p.rowClue, p.colClue = D.clues(walls, 6)
    assert(not D.solved(p, walls), "a dead end needs a monster")

    local q, w2 = parse({
        "M..###",
        "#..###",
        "##.###",
        "##...M",
        "######",
        "######",
    })
    assert(not D.solved(q, w2), "a 2x2 open square outside a treasure room")

    local r, w3 = parse({
        "M..M##",
        "######",
        "M..M##",
        "######",
        "######",
        "######",
    })
    assert(not D.solved(r, w3), "the open cells must be joined up")

    local t, w4 = parse({
        "...###",
        ".T.###",
        "...###",
        "#.####",
        "#..M##",
        "######",
    })
    assert(D.solved(t, w4), "a treasure room with one door")
    w4[4] = w4[4] & ~D.bit(3) -- a second door
    t.rowClue, t.colClue = D.clues(w4, 6)
    assert(not D.solved(t, w4), "a treasure room has exactly one way out")
end

-- Generated maps, every level: valid, a single solution the solver finds
-- again, and a sensible number of monsters.
for level = 1, 3 do
    local monsters = 0
    for _ = 1, 30 do
        local p, walls = Gen.generate(level, rand, 60)
        assert(p, "a map for level " .. level)
        assert(p.size == D.LEVELS[level].size)
        assert(D.solved(p, walls), "the generated walls solve it")
        local count, sol = Gen.solve(p, 200000)
        assert(count == 1, "exactly one solution")
        for r = 1, p.size do assert(sol[r] == walls[r], "the solver finds those walls") end
        local chests = 0
        for r = 1, p.size do
            monsters = monsters + D.POP[p.monsters[r]]
            chests = chests + D.POP[p.chests[r]]
        end
        assert(chests == D.LEVELS[level].rooms)
    end
    print(string.format("level %d: %.1f monsters on average", level, monsters / 30))
end

-- Saving and loading a game in progress.
do
    local p = Gen.generate(3, rand, 60)
    local marks = { wall = {}, open = {} }
    for r = 1, p.size do marks.wall[r], marks.open[r] = r % 3, r % 2 * 4 end
    p.elapsedMs = 61234
    local q, m2 = D.deserialize(D.serialize(p, marks))
    assert(q.size == p.size and q.level == 3 and q.elapsedMs == 61234)
    for r = 1, p.size do
        assert(q.monsters[r] == p.monsters[r] and q.chests[r] == p.chests[r])
        assert(q.rowClue[r] == p.rowClue[r] and q.colClue[r] == p.colClue[r])
        assert(m2.wall[r] == marks.wall[r] and m2.open[r] == marks.open[r])
    end
    assert(D.deserialize("") == nil and D.deserialize("9,1") == nil)
end

print("dungeon logic ok")
