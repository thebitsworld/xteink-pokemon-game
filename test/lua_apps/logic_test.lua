-- Logic tests for apps/logic (Logic Puzzles): Hashi, Lights Out and
-- Slitherlink rules and generators (run by ctest: LuaAppLogic_logic).
local H = dofile(APPS .. "/logic/hashi.lua")
local HG = dofile(APPS .. "/logic/hashigen.lua")()
local L = dofile(APPS .. "/logic/lights.lua")
local SL = dofile(APPS .. "/logic/slither.lua")
local SG = dofile(APPS .. "/logic/slithergen.lua")()
local SLP = dofile(APPS .. "/logic/slitherloop.lua")()

-- make.lua's Slitherlink steps: a loop, then its clues thinned; again while
-- deduction cannot solve the full set.
function SG.generate(n, rand, millis, capMs, minClues)
    local start, T = millis(), SLP.topology(n)
    repeat
        local sol, all = SLP.make(n, rand)
        local clues = SG.reduce(n, T, all, rand, millis, start + capMs, minClues)
        if clues then return clues, sol end
    until millis() - start > capMs
    return nil
end

-- Slitherlink solutions (0, 1 or 2 meaning two or more; nil past `cap`
-- steps): a search over the generator's solver, line first then empty.
function SG.count(n, clues, cap, solver)
    local T = SLP.topology(n)
    solver = solver or SG.solver(n, T)
    local V, C, st, vd, cs = (n + 1) * (n + 1), n * n, solver.st, solver.vd, solver.cs
    -- An open edge to branch on: one at a loose end, else one by the biggest clue.
    local function choose()
        for v = 1, V do
            if vd[v] // 8 == 1 then
                for k = (v - 1) * 4 + 1, v * 4 do
                    local e = T.vinc:byte(k)
                    if e > 0 and st[e] == -1 then return e end
                end
            end
        end
        local best, bestK = nil, -2
        for c = 1, C do
            if clues[c] > bestK and cs[c] % 8 > 0 then
                for k = (c - 1) * 4 + 1, c * 4 do
                    if st[T.cedge:byte(k)] == -1 then
                        best, bestK = T.cedge:byte(k), clues[c]
                        break
                    end
                end
            end
        end
        return best
    end
    solver.reset(clues)
    local bE, bM, bV = {}, {}, {}
    local d, nodes, count = 0, 0, 0
    local ok = solver.propagate()
    while true do
        if ok then
            if solver.closed() then
                if solver.finish() then
                    count = count + 1
                    if count >= 2 then return 2 end
                end
                ok = false
            else
                local e = choose()
                if not e then
                    ok = false
                else
                    d = d + 1
                    bE[d], bV[d] = e, 1
                    bM[d] = solver.mark()
                    ok = solver.set(e, 1) and solver.propagate()
                end
            end
        end
        if not ok then
            while d > 0 and bV[d] == 0 do
                solver.undo(bM[d])
                d = d - 1
            end
            if d == 0 then return count end
            solver.undo(bM[d])
            bV[d] = 0
            ok = solver.set(bE[d], 0) and solver.propagate()
        end
        nodes = nodes + 1
        if nodes > cap then return nil end
    end
end

-- math.random, seeded: Lua here has 32-bit integers, so a hand-rolled LCG overflows.
math.randomseed(4242)
local function rand(n) return math.random(1, n) end
local clock = 0
local function millis() return clock end -- generators never time out here

-- Hashi -----------------------------------------------------------------------

-- Islands, neighbours and crossings of a small hand-made grid:
--   2 . 2
--   . . .
--   2 . 2
do
    local p = H.build(3, "2.2" .. "..." .. "2.2")
    assert(p.islands == 4 and p.edges == 4)
    assert(H.neighbour(p, 1, 4) == 2 and H.neighbour(p, 1, 2) == 3)
    assert(H.neighbour(p, 1, 1) == nil and H.neighbour(p, 1, 3) == nil)
    assert(H.edgeBetween(p, 1, 4) == nil, "diagonal islands are not neighbours")
    -- The ring 1-2-4-3 with one bridge each; two opposite doubles would leave
    -- two separate pairs.
    assert(HG.count(p, 1000) == 1)
    -- With 4s: doubles all round, the only way.
    local four = H.build(3, "4.4" .. "..." .. "4.4")
    assert(HG.count(four, 1000) == 1)
    -- With 3s: one double and three singles round the ring, four ways.
    assert(HG.count(H.build(3, "3.3" .. "..." .. "3.3"), 1000) == 2)
end

-- A plus shape: the middle row's bridge crosses the middle column's.
do
    local p = H.build(5, "..1.." .. "....." .. "1...1" .. "....." .. "..1..")
    local hz, vt
    for e = 1, p.edges do
        if p.horiz:byte(e) == 1 then hz = e else vt = e end
    end
    assert(p.edges == 2 and H.crosses(p, hz, vt), "crossing found")
    assert(HG.count(p, 1000) == 0, "two 1-1 pairs can never join")
    local q = H.new(5, p.grid, "00")
    assert(H.cycle(q, hz))
    local ok, why = H.cycle(q, vt)
    assert(not ok and why == "cross", "a crossing bridge is refused")
end

-- Bridges cycle 0 -> 1 -> 2 -> 0; solved needs every number and one network.
do
    local q = H.new(3, "1.1" .. "..." .. "...", "1")
    assert(q.edges == 1)
    assert(H.cycle(q, 1) and q.solved, "one bridge joins the pair")
    q.solved = false
    assert(H.cycle(q, 1) and q.cur[1] == 2 and not q.solved and H.mistakes(q) == 1)
    assert(H.cycle(q, 1) and q.cur[1] == 0 and H.mistakes(q) == 0)
    -- Two separate pairs with their numbers met are not solved.
    local two = H.new(3, "1.1" .. "..." .. "1.1", "1001")
    for e = 1, two.edges do
        if two.horiz:byte(e) == 1 then two.cur[e] = 1 end
    end
    assert(not H.isSolved(two), "two islands groups")
end

-- Generated puzzles: the solution fits the numbers, joins every island, has
-- no crossings, and is the only one; easy ones need no guessing.
for _, cfg in ipairs({ { 7, true }, { 9, true }, { 11, false } }) do
    local size, easy = cfg[1], cfg[2]
    for _ = 1, 8 do
        local grid, sol = HG.generate(size, rand, millis, 1, easy)
        assert(grid and #grid == size * size, "hashi generated")
        local p = H.new(size, grid, sol)
        assert(#sol == p.edges)
        for e = 1, p.edges do p.cur[e] = sol:byte(e) - 48 end
        assert(H.isSolved(p), "solution is valid")
        for e = 1, p.edges do
            for f = e + 1, p.edges do
                assert(not (p.cur[e] > 0 and p.cur[f] > 0 and H.crosses(p, e, f)), "no crossing")
            end
        end
        local count, guessed = HG.count(p, 100000)
        assert(count == 1, "unique")
        assert(not (easy and guessed), "easy without guessing")
        -- No two islands side by side; first and last rows and columns used.
        for k = 1, p.islands do
            local r, c = p.ir:byte(k), p.ic:byte(k)
            assert(not (c + 1 < size and p.at:byte(r * size + c + 2) > 0), "islands not touching")
            assert(not (r + 1 < size and p.at:byte((r + 1) * size + c + 1) > 0), "islands not touching")
        end
        -- Save and restore, halfway through.
        for e = 1, p.edges, 2 do p.cur[e] = 0 end
        p.solved, p.elapsedMs = H.isSolved(p), 65432
        local q = H.deserialize(H.serialize(p))
        assert(q and q.size == size and q.grid == grid and q.sol == sol and q.elapsedMs == 65432)
        for e = 1, p.edges do assert(q.cur[e] == p.cur[e]) end
        assert(q.solved == p.solved)
        H.restart(q)
        for e = 1, q.edges do assert(q.cur[e] == 0) end
    end
end
assert(H.deserialize("garbage") == nil and H.deserialize("3,1.1,1,1,0") == nil)

-- Lights Out ------------------------------------------------------------------

do
    -- A press toggles the plus shape, clipped at the edges.
    local cells = {}
    for i = 1, 25 do cells[i] = 0 end
    L.press(cells, 5, 0, 0)
    local lit = 0
    for i = 1, 25 do lit = lit + cells[i] end
    assert(lit == 3 and cells[1] == 1 and cells[2] == 1 and cells[6] == 1)
    L.press(cells, 5, 2, 2)
    assert(cells[13] == 1 and cells[8] == 1 and cells[12] == 1 and cells[14] == 1 and cells[18] == 1)

    for _, n in ipairs({ 5, 6, 7 }) do
        for _ = 1, 20 do
            local presses = {}
            local p = L.new(n, rand, presses)
            assert(not p.solved and #presses > 0)
            -- Pressing the generator's lights again solves it.
            for k = 1, #presses do
                local i = presses[k]
                L.play(p, (i - 1) // n, (i - 1) % n)
            end
            assert(p.solved and p.moves == #presses, "solvable by its presses")
            assert(not L.play(p, 0, 0), "no moves once solved")
            L.restart(p)
            assert(not p.solved and p.moves == 0 and table.concat(p.cells) == p.start)
            L.play(p, 1, 1)
            p.elapsedMs = 1234
            local q = L.deserialize(L.serialize(p))
            assert(q and q.moves == 1 and q.start == p.start and q.elapsedMs == 1234)
            assert(table.concat(q.cells) == table.concat(p.cells))
        end
    end
    assert(L.deserialize("5,0,0,0,0") == nil)
end

-- Slitherlink -----------------------------------------------------------------

-- Edge numbering: ends of the first horizontal and the first vertical edge.
do
    local n = 3
    assert(SL.edgeCount(n) == 24)
    local r1, c1, r2, c2 = SL.ends(n, SL.hEdge(n, 1, 2))
    assert(r1 == 1 and c1 == 2 and r2 == 1 and c2 == 3)
    r1, c1, r2, c2 = SL.ends(n, SL.vEdge(n, 2, 3))
    assert(r1 == 2 and c1 == 3 and r2 == 3 and c2 == 3)
end

-- Solved detection: one loop meeting the clues; two loops or a dead end are not.
do
    local n = 3
    local p = SL.new(n, ".........", string.rep("0", SL.edgeCount(n)))
    local function sq(r, c)
        p.cur[SL.hEdge(n, r, c)] = 1 - p.cur[SL.hEdge(n, r, c)]
        p.cur[SL.hEdge(n, r + 1, c)] = 1 - p.cur[SL.hEdge(n, r + 1, c)]
        p.cur[SL.vEdge(n, r, c)] = 1 - p.cur[SL.vEdge(n, r, c)]
        p.cur[SL.vEdge(n, r, c + 1)] = 1 - p.cur[SL.vEdge(n, r, c + 1)]
    end
    assert(not SL.isSolved(p), "no loop at all")
    sq(0, 0)
    assert(SL.isSolved(p), "one square")
    sq(2, 2)
    assert(not SL.isSolved(p), "two loops")
    sq(2, 2)
    sq(0, 1) -- two cells side by side: the shared edge goes, a 2x1 loop
    assert(SL.isSolved(p), "a 2x1 loop")
    p.cur[SL.hEdge(n, 0, 0)] = 0
    assert(not SL.isSolved(p), "an open path")
    p.cur[SL.hEdge(n, 0, 0)] = 2
    assert(not SL.isSolved(p), "a cross is not a line")
    p.cur[SL.hEdge(n, 0, 0)] = 1
    p.clues = "3........"
    assert(SL.isSolved(p))
    p.clues = "2........"
    assert(not SL.isSolved(p), "a clue not met")
end

-- Blobs: the loop around each is a single loop, its clues are right.
do
    for _, n in ipairs({ 5, 7, 8 }) do
        for _ = 1, 30 do
            local sol, clues = SLP.loopOf(n, SLP.blob(n, rand, 45 + rand(20)))
            local p = SL.new(n, string.rep(".", n * n), sol)
            for e = 1, #sol do p.cur[e] = sol:byte(e) - 48 end
            assert(SL.isSolved(p), "a blob's edge is one loop")
            for r = 0, n - 1 do
                for c = 0, n - 1 do assert(SL.linesAround(p, r, c) == clues[r * n + c + 1]) end
            end
        end
    end
end

-- The solver on hand-made clues. On one cell the only loop is its square
-- (four lines, so a 3 has no solution). On 2x2 the loops are four single
-- cells, four dominoes, four L shapes and the whole square.
do
    assert(SG.count(1, { 3 }, 1000) == 0)
    assert(SG.count(1, { -1 }, 1000) == 1, "the only loop on one cell")
    local s2 = SG.solver(2, SLP.topology(2))
    -- No clues on 2x2: many loops.
    assert(SG.count(2, { -1, -1, -1, -1 }, 1000, s2) == 2)
    assert(SG.count(2, { 2, 2, 2, 2 }, 1000, s2) == 1, "the whole square")
    assert(SG.count(2, { 3, 3, 1, 1 }, 1000, s2) == 1, "the top domino")
    assert(SG.count(2, { 1, -1, -1, -1 }, 1000, s2) == 2, "the top right cell or the bottom left one, or...")
    assert(s2.deduce({ 3, 3, 1, 1 }))
    assert(not s2.deduce({ -1, -1, -1, -1 }), "deduction does not guess")
end

-- Generated puzzles: the solution meets the clues, it is the only one, and
-- deduction finds it.
local function checkSlither(n, minClues)
    local clues, sol = SG.generate(n, rand, millis, 1, minClues)
    assert(clues and #clues == n * n and #sol == SL.edgeCount(n), "slitherlink generated")
    local p = SL.new(n, clues, sol)
    for e = 1, #sol do p.cur[e] = sol:byte(e) - 48 end
    assert(SL.isSolved(p), "solution meets the clues")
    local t, given = {}, 0
    for i = 1, n * n do
        local b = clues:byte(i)
        t[i] = b == 46 and -1 or b - 48
        if b ~= 46 then given = given + 1 end
    end
    assert(given >= (minClues or 0))
    local solver = SG.solver(n, SLP.topology(n))
    assert(SG.count(n, t, 200000, solver) == 1, "unique")
    assert(solver.deduce(t), "no guessing")
    -- Mistakes, solved detection through cycle, save and restore.
    SL.restart(p)
    assert(not p.solved and SL.mistakes(p) == 0)
    local wrong
    for e = 1, #sol do
        if sol:byte(e) == 48 then wrong = e break end
    end
    SL.cycle(p, wrong, SL.LINE)
    assert(SL.mistakes(p) == 1)
    SL.cycle(p, wrong, SL.CROSS)
    assert(SL.mistakes(p) == 0)
    p.elapsedMs = 777
    local q = SL.deserialize(SL.serialize(p))
    assert(q and q.clues == clues and q.sol == sol and q.elapsedMs == 777 and q.cur[wrong] == SL.CROSS)
    for e = 1, #sol do
        if sol:byte(e) == 49 then SL.cycle(q, e, SL.LINE) end
    end
    assert(q.solved, "drawing the loop solves it")
    assert(not SL.cycle(q, 1, SL.EMPTY), "no changes once solved")
    return given
end
for _ = 1, 10 do checkSlither(5, 13) end
for _ = 1, 6 do checkSlither(7, 0) end
for _ = 1, 4 do checkSlither(8, 0) end
assert(SL.deserialize("5,abc") == nil)

-- A generator out of time keeps every clue it still has (but still gives one).
do
    local n, calls = 7, 0
    local function late()
        calls = calls + 1
        return calls > 1 and 10 ^ 6 or 0
    end
    local clues, sol = SG.generate(n, rand, late, 1000, 0)
    if clues then
        local given = 0
        for i = 1, n * n do
            if clues:byte(i) ~= 46 then given = given + 1 end
        end
        assert(given == n * n, "no time: all clues stay")
        local p = SL.new(n, clues, sol)
        for e = 1, #sol do p.cur[e] = sol:byte(e) - 48 end
        assert(SL.isSolved(p))
    end
end

print("logic: all tests passed")
