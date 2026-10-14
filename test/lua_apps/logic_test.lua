-- Logic tests for apps/logic (Logic Puzzles): Hashi, Lights Out,
-- Slitherlink, Kakuro and Light Up rules and generators (run by ctest:
-- LuaAppLogic_logic).
local H = dofile(APPS .. "/logic/hashi.lua")
local HG = dofile(APPS .. "/logic/hashigen.lua")()
local L = dofile(APPS .. "/logic/lights.lua")
local SL = dofile(APPS .. "/logic/slither.lua")
local SG = dofile(APPS .. "/logic/slithergen.lua")()
local SLP = dofile(APPS .. "/logic/slitherloop.lua")()
local K = dofile(APPS .. "/logic/kakuro.lua")
local KL = dofile(APPS .. "/logic/kakurolay.lua")()
local KG = dofile(APPS .. "/logic/kakurogen.lua")()
local A = dofile(APPS .. "/logic/akari.lua")
local AG = dofile(APPS .. "/logic/akarigen.lua")()

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

-- Kakuro ----------------------------------------------------------------------

-- make.lua's Kakuro steps: a filled layout, then its digits changed until
-- reasoning alone solves it; again when that gets stuck.
local function kakuroGen(n, clock)
    clock = clock or millis
    local start = clock()
    repeat
        local _, d = KL.make(n, rand)
        local grid = KG.reduce(n, true, d, rand, clock, start + 10 ^ 7)
        if grid then return grid end
    until false
end

-- Kakuro solutions, counted by a plain search independent of the
-- generator's solver (0, 1 or 2 meaning two or more; nil past `cap`): cells
-- in reading order, each digit checked against the digits and sums of its
-- two runs (the rest of a run must still be able to reach its sum).
local function kakuroCount(grid, n, cap)
    local p = K.new(n, grid)
    local N, cells, d = n * n, {}, {}
    for i = 1, N do
        d[i] = 0
        if K.isWhite(p, i) then cells[#cells + 1] = i end
    end
    local function fits(i, digit)
        for _, step in ipairs({ 1, n }) do
            local h = i - step
            while K.isWhite(p, h) do h = h - step end
            local want = step == 1 and p.ca:byte(h) or p.cd:byte(h)
            local sum, used, left, j = digit, 1 << digit, 0, h + step
            while j <= N and (step == n or (j - 1) % n > 0) and K.isWhite(p, j) do
                if j ~= i then
                    if d[j] == 0 then
                        left = left + 1
                    else
                        if used & (1 << d[j]) ~= 0 then return false end
                        used, sum = used | (1 << d[j]), sum + d[j]
                    end
                end
                j = j + step
            end
            local lo, hi, k = 0, 0, 0
            for x = 1, 9 do
                if k < left and used & (1 << x) == 0 then lo, k = lo + x, k + 1 end
            end
            k = 0
            for x = 9, 1, -1 do
                if k < left and used & (1 << x) == 0 then hi, k = hi + x, k + 1 end
            end
            if want - sum < lo or want - sum > hi then return false end
        end
        return true
    end
    local count, nodes = 0, 0
    local function dfs(k)
        if k > #cells then
            count = count + 1
            return count >= 2
        end
        nodes = nodes + 1
        if nodes > cap then error("cap") end
        local i = cells[k]
        for digit = 1, 9 do
            if fits(i, digit) then
                d[i] = digit
                if dfs(k + 1) then return true end
                d[i] = 0
            end
        end
        return false
    end
    if not pcall(dfs, 1) then return nil end
    return count
end

-- Clues, runs, solved detection, mistakes, save and restore on a hand grid:
--   # # # #        a 2x2 block: 1 2 / 3 4, so the sums are 3 and 7
--   # # # #        across, 4 and 6 down.
--   # # 1 2
--   # # 3 4
do
    local grid = "####" .. "####" .. "##12" .. "##34"
    local p = K.new(4, grid)
    assert(p.ca:byte(10) == 3 and p.ca:byte(14) == 7, "across sums")
    assert(p.cd:byte(7) == 4 and p.cd:byte(8) == 6, "down sums")
    assert(p.ca:byte(1) == 0 and p.cd:byte(1) == 0, "no run after a corner cell")
    -- 2 1 / 2 5 meets the sums too, but repeats the 2 down.
    assert(kakuroCount(grid, 4, 1000) == 1, "one answer")
    assert(not p.solved and K.runState(p, 10, 1) == 0)
    assert(K.set(p, 11, 1) and K.set(p, 12, 1))
    assert(K.runState(p, 10, 1) == 2, "a repeated digit is wrong")
    assert(K.set(p, 12, 2) and K.runState(p, 10, 1) == 1, "a full run with its sum")
    assert(not K.set(p, 12, 2), "the same digit again changes nothing")
    assert(not K.set(p, 1, 5), "black cells take no digit")
    assert(K.set(p, 15, 9) and K.runState(p, 14, 1) == 2, "9 alone is past 7")
    assert(K.mistakes(p) == 1 and p.marks[15])
    assert(K.set(p, 15, 3) and p.marks == nil, "a change clears the marks")
    assert(K.set(p, 16, 4) and p.solved, "solved")
    assert(not K.set(p, 16, 0), "no changes once solved")
    K.restart(p)
    assert(not p.solved and p.cur[11] == 0)
    K.set(p, 11, 1)
    p.elapsedMs = 4321
    local q = K.deserialize(K.serialize(p))
    assert(q and q.grid == grid and q.cur[11] == 1 and q.cur[12] == 0 and q.elapsedMs == 4321 and not q.solved)
    assert(K.deserialize("4,####,0,0") == nil and K.deserialize("garbage") == nil)
end

-- Generated puzzles: a valid grid whose digits meet every clue, runs of 2
-- to 5 cells without a repeat, reasoning alone finds it, and it is the only
-- solution; solved by entering it.
local function checkKakuro(n)
    local grid = kakuroGen(n)
    assert(grid and #grid == n * n, "kakuro generated")
    for i = 1, n do
        assert(grid:byte(i) == 35 and grid:byte((i - 1) * n + 1) == 35, "first row and column black")
    end
    local p = K.new(n, grid)
    for i = 1, n * n do
        if not K.isWhite(p, i) then
            for _, step in ipairs({ 1, n }) do
                local len, seen, sum, j = 0, 0, 0, i + step
                while j <= n * n and (step == n or (j - 1) % n > 0) and K.isWhite(p, j) do
                    local digit = grid:byte(j) - 48
                    assert(seen & (1 << digit) == 0, "no repeat in a run")
                    seen, sum, len, j = seen | (1 << digit), sum + digit, len + 1, j + step
                end
                assert(len == 0 or (len >= 2 and len <= 5), "runs of 2 to 5")
                assert(sum == (step == 1 and p.ca:byte(i) or p.cd:byte(i)), "clue is the run's sum")
            end
        end
    end
    local d = {}
    for i = 1, n * n do d[i] = K.isWhite(p, i) and grid:byte(i) - 48 or 0 end
    local solver = KG.solver(d, n)
    solver.clues(d)
    assert(solver.deduce() == 0, "reasoning alone solves it")
    assert(kakuroCount(grid, n, 2000000) == 1, "unique")
    for i = 1, n * n do
        if K.isWhite(p, i) then K.set(p, i, grid:byte(i) - 48) end
    end
    assert(p.solved and K.mistakes(p) == 0, "entering the solution solves it")
    local q = K.deserialize(K.serialize(p))
    assert(q and q.solved)
end
for _ = 1, 12 do checkKakuro(6) end
for _ = 1, 8 do checkKakuro(8) end
for _ = 1, 5 do checkKakuro(10) end

-- Light Up --------------------------------------------------------------------

-- Light Up solutions, counted by a plain search independent of the
-- generator's solver (0, 1 or 2 meaning two or more; nil past `cap`): white
-- cells in reading order, bulb or none; a bulb may not be lit by another
-- nor pass a number; a number is checked again once its last neighbour is
-- decided, a cell once the last cell that could light it is.
local function akariCount(grid, n, cap)
    local p = A.new(n, grid, string.rep("0", n * n))
    local N, cells, lit, checkAt = n * n, {}, {}, {}
    for i = 1, N do
        lit[i], checkAt[i] = 0, {}
        if A.isWhite(p, i) then cells[#cells + 1] = i end
    end
    -- The cells a bulb at i lights (i too).
    local function rays(i, by)
        lit[i] = lit[i] + by
        for _, step in ipairs({ -n, n, -1, 1 }) do
            local j = i + step
            while j >= 1 and j <= N and (step == n or step == -n or (j - 1) // n == (i - 1) // n) and A.isWhite(p, j) do
                lit[j] = lit[j] + by
                j = j + step
            end
        end
    end
    -- Each white cell is checked for light after the last cell of its row
    -- and column stretches; each number after its last white neighbour.
    for _, i in ipairs(cells) do
        local j = i
        while (j - 1) % n < n - 1 and A.isWhite(p, j + 1) do j = j + 1 end
        local k = i
        while k + n <= N and A.isWhite(p, k + n) do k = k + n end
        local at = checkAt[math.max(j, k)]
        at[#at + 1] = i
    end
    local function around(i)
        local c, out = (i - 1) % n, {}
        for _, j in ipairs({ i - n, i + n, c > 0 and i - 1 or 0, c < n - 1 and i + 1 or 0 }) do
            if j >= 1 and j <= N then out[#out + 1] = j end
        end
        return out
    end
    for i = 1, N do
        if A.number(p, i) then
            local lastWhite = 0
            for _, j in ipairs(around(i)) do
                if A.isWhite(p, j) then lastWhite = math.max(lastWhite, j) end
            end
            local at = checkAt[lastWhite > 0 and lastWhite or cells[1]]
            at[#at + 1] = -i
        end
    end
    local count, nodes = 0, 0
    local function fine(i)
        if p.cur[i] == 1 then
            for _, j in ipairs(around(i)) do
                local k = A.number(p, j)
                if k and A.bulbsAround(p, j) > k then return false end
            end
        end
        for _, c in ipairs(checkAt[i]) do
            if c > 0 and lit[c] == 0 then return false end
            if c < 0 and A.bulbsAround(p, -c) ~= A.number(p, -c) then return false end
        end
        return true
    end
    local function dfs(k)
        if k > #cells then
            count = count + 1
            return count >= 2
        end
        nodes = nodes + 1
        if nodes > cap then error("cap") end
        local i = cells[k]
        if lit[i] == 0 then
            p.cur[i] = 1
            rays(i, 1)
            if fine(i) and dfs(k + 1) then return true end
            rays(i, -1)
        end
        p.cur[i] = 0
        if fine(i) and dfs(k + 1) then return true end
        return false
    end
    if not pcall(dfs, 1) then return nil end
    return count
end

-- Light, conflicts, numbers, solved detection, mistakes, save and restore
-- on a hand grid; the 2 needs bulbs left and right of it, which light all:
--   . # .
--   . 2 .
--   . # .
do
    local grid = ".#." .. ".2." .. ".#."
    local p = A.new(3, grid, "000" .. "101" .. "000")
    assert(A.number(p, 5) == 2 and A.number(p, 2) == nil and A.isWhite(p, 1) and not A.isWhite(p, 8))
    assert(A.cycle(p, 4) and p.cur[4] == A.BULB)
    local lit = A.lighting(p)
    assert(lit[1] == 1 and lit[4] == 1 and lit[7] == 1, "a bulb lights its column")
    assert(lit[6] == 0 and lit[3] == 0, "not past the number")
    assert(not p.solved and A.bulbsAround(p, 5) == 1)
    assert(A.cycle(p, 1) and A.lighting(p)[1] == 2, "two bulbs see each other")
    assert(A.mistakes(p) == 1 and p.marks[1])
    assert(A.cycle(p, 1) and p.cur[1] == A.DOT and p.marks == nil, "bulb -> dot")
    assert(A.mistakes(p) == 0, "a dot off the solution is fine")
    assert(A.cycle(p, 6, A.BULB) and p.solved, "solved: all lit, the 2 met")
    assert(not A.cycle(p, 3), "no changes once solved")
    assert(not A.cycle(A.new(3, grid, p.sol), 5), "black cells do not change")
    A.restart(p)
    assert(not p.solved and p.cur[4] == 0 and p.cur[6] == 0)
    A.cycle(p, 4)
    A.cycle(p, 3, A.DOT)
    A.cycle(p, 7)
    p.elapsedMs = 999
    local q = A.deserialize(A.serialize(p))
    assert(q and q.grid == grid and q.sol == p.sol and q.cur[4] == 1 and q.cur[3] == 2 and q.elapsedMs == 999)
    assert(not q.solved and A.lighting(q)[7] == 2)
    assert(akariCount(grid, 3, 10000) == 1, "one solution")
    assert(A.deserialize("3,.#.,0,0,0") == nil and A.deserialize("x") == nil)
end

-- The generator's solver on hand grids.
do
    local function g(grid)
        local t = {}
        for i = 1, #grid do
            local b = grid:byte(i)
            t[i] = b == 46 and -1 or b == 35 and 5 or b - 48
        end
        return t
    end
    assert(AG.solver(g("........."), 3).count(100) == 2, "an open board has many answers")
    assert(AG.solver(g(".#..2..#."), 3).count(100) == 1 and AG.solver(g(".#..2..#."), 3).deduce())
    assert(AG.solver(g("..#.0.#.."), 3).count(100) == 1 and AG.solver(g("..#.0.#.."), 3).deduce())
    assert(AG.solver(g("..#.2.#.."), 3).count(100) == 2, "bulbs 2, 4, 9 or 1, 6, 8")
    assert(not AG.solver(g("..#.2.#.."), 3).deduce(), "reasoning cannot pick")
    assert(AG.solver(g("..#.1.#.."), 3).count(100) == 0, "no answer")
    for _, grid in ipairs({ ".#..2..#.", "..#.2.#..", "..#.1.#..", "...#2#..." }) do
        assert(akariCount(grid, 3, 10000) == AG.solver(g(grid), 3).count(100), "both counts agree")
    end
end

-- Generated puzzles: the solution lights every cell and meets the numbers,
-- it is the only one (an easy one: reasoning alone finds it), entering it
-- solves the puzzle, and the save restores it.
local function checkAkari(n, easy)
    local grid, sol = AG.generate(n, rand, millis, 1, easy)
    assert(grid and #grid == n * n and #sol == n * n, "light up generated")
    local p = A.new(n, grid, sol)
    for i = 1, n * n do
        if sol:byte(i) == 49 then
            assert(A.isWhite(p, i))
            p.cur[i] = 1
        end
    end
    assert(A.isSolved(p), "the solution solves it")
    assert(akariCount(grid, n, 2000000) == 1, "unique")
    local t = {}
    for i = 1, n * n do
        local b = grid:byte(i)
        t[i] = b == 46 and -1 or b == 35 and 5 or b - 48
    end
    if easy then assert(AG.solver(t, n).deduce(), "reasoning alone solves it") end
    A.restart(p)
    for i = 1, n * n do
        if sol:byte(i) == 49 then A.cycle(p, i) end
    end
    assert(p.solved and A.mistakes(p) == 0, "entering the solution solves it")
    local q = A.deserialize(A.serialize(p))
    assert(q and q.solved and q.sol == sol)
end
for _ = 1, 12 do checkAkari(7, true) end
for _ = 1, 8 do checkAkari(10, true) end
for _ = 1, 6 do checkAkari(11, false) end

-- Worst generation times on this computer (the reader is far slower; the
-- simulator's GENTIME and the app's 6 s cap are what count there).
do
    local function ms() return os.clock() * 1000 end
    local worst = {}
    for _, cfg in ipairs({ { "kakuro", 10 }, { "akari", 11 } }) do
        local w = 0
        for _ = 1, 15 do
            local t0 = ms()
            if cfg[1] == "kakuro" then kakuroGen(cfg[2], ms) else AG.generate(cfg[2], rand, ms, 6000, false) end
            w = math.max(w, ms() - t0)
        end
        worst[#worst + 1] = string.format("%s %dx%d %.0f ms", cfg[1], cfg[2], cfg[2], w)
        assert(w < 3000, "generation on this computer stays well under the app's 6 s")
    end
    print("logic: worst generation " .. table.concat(worst, ", "))
end

print("logic: all tests passed")
