-- Hashi puzzle generator, loaded only while a puzzle is made. Usage:
--   local G = smudge.dofile("hashigen.lua")()
--   local grid, sol = G.generate(size, rand, millis, capMs, easy)
--
-- A layout grows from one island: a bridge of 1 or 2 goes from a random
-- island in a random direction to a new island, over empty water only, so no
-- bridges cross. A few more bridges then join neighbours already there. The
-- numbers are the bridges of each island. The solver below counts the
-- solutions (up to two); only a layout with exactly one is kept, and the
-- next layout is tried until one is or the time is up.

return function()
local G = {}

-- Islands and edges of a grid string, as hashi.lua's H.build (copied so the
-- rules need not be loaded while a puzzle is made).
local function build(size, grid)
    local ir, ic, num, at, ea, eb, hz = {}, {}, {}, {}, {}, {}, {}
    local n = 0
    for i = 1, size * size do
        at[i] = 0
        local b = grid:byte(i)
        if b ~= 46 then
            n = n + 1
            ir[n], ic[n], num[n], at[i] = (i - 1) // size, (i - 1) % size, b - 48, n
        end
    end
    local E = 0
    for k = 1, n do
        local r, c = ir[k], ic[k]
        for cc = c + 1, size - 1 do
            local j = at[r * size + cc + 1]
            if j > 0 then
                E = E + 1
                ea[E], eb[E], hz[E] = k, j, 1
                break
            end
        end
        for rr = r + 1, size - 1 do
            local j = at[rr * size + c + 1]
            if j > 0 then
                E = E + 1
                ea[E], eb[E], hz[E] = k, j, 0
                break
            end
        end
    end
    local char, unpack = string.char, table.unpack
    return { size = size, grid = grid, islands = n, edges = E, ir = char(unpack(ir, 1, n)), ic = char(unpack(ic, 1, n)),
             num = char(unpack(num, 1, n)), at = char(unpack(at)), ea = char(unpack(ea, 1, E)),
             eb = char(unpack(eb, 1, E)), horiz = char(unpack(hz, 1, E)) }
end

-- Whether edges e and f cross (one across the other, not at an island).
local function crosses(p, e, f)
    if p.horiz:byte(e) == p.horiz:byte(f) then return false end
    if p.horiz:byte(e) ~= 1 then e, f = f, e end
    local r, c1, c2 = p.ir:byte(p.ea:byte(e)), p.ic:byte(p.ea:byte(e)), p.ic:byte(p.eb:byte(e))
    local c, r1, r2 = p.ic:byte(p.ea:byte(f)), p.ir:byte(p.ea:byte(f)), p.ir:byte(p.eb:byte(f))
    return c1 < c and c < c2 and r1 < r and r < r2
end

local DR = { -1, 1, 0, 0 }
local DC = { 0, 0, -1, 1 }

-- One random layout: the grid string and the bridges, or nil.
function G.layout(size, rand, target)
    local occ, isl, ba, bb, bn = {}, {}, {}, {}, {}
    for i = 1, size * size do occ[i] = 0 end
    local function island(r, c) return r >= 0 and r < size and c >= 0 and c < size and occ[r * size + c + 1] == 1 end
    local r0, c0 = rand(size) - 1, rand(size) - 1
    occ[r0 * size + c0 + 1] = 1
    isl[1] = r0 * size + c0
    local maxLen = math.max(3, size // 2 + 1)
    local tries = 0
    while #isl < target and tries < target * 40 do
        tries = tries + 1
        local from = rand(#isl)
        local r, c = isl[from] // size, isl[from] % size
        local d, len = rand(4), rand(maxLen - 1) + 1
        local ok = true
        for step = 1, len do
            local rr, cc = r + DR[d] * step, c + DC[d] * step
            if rr < 0 or rr >= size or cc < 0 or cc >= size or occ[rr * size + cc + 1] ~= 0 then
                ok = false
                break
            end
        end
        local tr, tc = r + DR[d] * len, c + DC[d] * len
        if ok then
            for k = 1, 4 do
                if island(tr + DR[k], tc + DC[k]) then ok = false end
            end
        end
        if ok then
            for step = 1, len - 1 do occ[(r + DR[d] * step) * size + c + DC[d] * step + 1] = 2 end
            occ[tr * size + tc + 1] = 1
            isl[#isl + 1] = tr * size + tc
            ba[#ba + 1], bb[#ba + 1], bn[#ba + 1] = isl[from], tr * size + tc, rand(10) <= 4 and 2 or 1
        end
    end
    if #isl < target * 3 // 4 then return nil end
    -- Extra bridges between neighbours with clear water between them.
    for k = 1, #isl do
        local r, c = isl[k] // size, isl[k] % size
        for d = 2, 4, 2 do
            if rand(4) == 1 then
                local step, clear = 1, true
                while true do
                    local rr, cc = r + DR[d] * step, c + DC[d] * step
                    if rr >= size or cc >= size then clear = false break end
                    local o = occ[rr * size + cc + 1]
                    if o == 1 then break end
                    if o ~= 0 then clear = false break end
                    step = step + 1
                end
                if clear and step > 1 then
                    local t = (r + DR[d] * step) * size + c + DC[d] * step
                    for s = 1, step - 1 do occ[(r + DR[d] * s) * size + c + DC[d] * s + 1] = 2 end
                    ba[#ba + 1], bb[#ba + 1], bn[#ba + 1] = isl[k], t, rand(10) <= 3 and 2 or 1
                end
            end
        end
    end
    local num = {}
    for k = 1, #ba do
        num[ba[k]] = (num[ba[k]] or 0) + bn[k]
        num[bb[k]] = (num[bb[k]] or 0) + bn[k]
    end
    local cells = {}
    for i = 1, size * size do cells[i] = num[i - 1] and tostring(num[i - 1]) or "." end
    local grid = table.concat(cells)
    cells, occ, num, isl = nil, nil, nil, nil
    collectgarbage("collect") -- the X3 has little room: scratch goes before the next step
    local p = build(size, grid)
    local sol = {}
    for e = 1, p.edges do sol[e] = 0 end
    for k = 1, #ba do
        local a, b, e = p.at:byte(ba[k] + 1), p.at:byte(bb[k] + 1), nil
        for f = 1, p.edges do
            if (p.ea:byte(f) == a and p.eb:byte(f) == b) or (p.ea:byte(f) == b and p.eb:byte(f) == a) then e = f end
        end
        if not e then return nil end
        sol[e] = bn[k]
    end
    return grid, table.concat(sol), p
end

-- Solutions of puzzle p (as hashi.lua's H.build makes it): 0, 1 or 2 (meaning two or more),
-- or nil when the search took more than `cap` steps; and whether it had to
-- guess (a puzzle solved without guessing is an easy one). Bounds per edge
-- (lo..hi bridges) are narrowed by the islands' numbers, crossings and the
-- rule that every island must stay reachable; then the search tries each
-- value of an open edge in turn. No recursion: the branches are a stack.
function G.count(p, cap)
    local E, n = p.edges, p.islands
    -- Few, flat tables (each table slot costs the X3 8-16 bytes): inc is a
    -- string of four edges per island (0: none), cr the crossing pairs as
    -- e * 256 + f, the trail an edge and its old bounds as e * 16 + lo * 4
    -- + hi per change, a branch as (trail mark * 256 + edge) * 16 + value
    -- * 4 + its upper bound.
    local lo, hi, inc, cr, nc = {}, {}, {}, {}, 0
    for i = 1, 4 * n do inc[i] = 0 end
    for e = 1, E do
        lo[e], hi[e] = 0, math.min(2, p.num:byte(p.ea:byte(e)), p.num:byte(p.eb:byte(e)))
        for side = 1, 2 do
            local i = ((side == 1 and p.ea:byte(e) or p.eb:byte(e)) - 1) * 4 + 1
            while inc[i] ~= 0 do i = i + 1 end
            inc[i] = e
        end
    end
    inc = string.char(table.unpack(inc))
    for e = 1, E do
        for f = e + 1, E do
            if crosses(p, e, f) then
                nc = nc + 1
                cr[nc] = e * 256 + f
            end
        end
    end
    local tr, tn, byte = {}, 0, string.byte
    local seen, stack = {}, {}

    local function set(e, l, h)
        if l > h then return false end
        if l == lo[e] and h == hi[e] then return true end
        tn = tn + 1
        tr[tn] = e * 16 + lo[e] * 4 + hi[e]
        lo[e], hi[e] = l, h
        return true
    end
    local function undo(mark)
        while tn > mark do
            local x = tr[tn]
            local e = x // 16
            lo[e], hi[e] = x % 16 // 4, x % 4
            tn = tn - 1
        end
    end
    local function connected()
        for k = 1, n do seen[k] = false end
        seen[1], stack[1] = true, 1
        local top, count = 1, 1
        while top > 0 do
            local k = stack[top]
            top = top - 1
            for i = (k - 1) * 4 + 1, k * 4 do
                local e = byte(inc, i)
                if e > 0 and hi[e] > 0 then
                    local o = p.ea:byte(e) == k and p.eb:byte(e) or p.ea:byte(e)
                    if not seen[o] then
                        seen[o], count, top = true, count + 1, top + 1
                        stack[top] = o
                    end
                end
            end
        end
        return count == n
    end
    local function propagate()
        local changed = true
        while changed do
            changed = false
            for k = 1, n do
                local base, need, sl, sh = (k - 1) * 4, p.num:byte(k), 0, 0
                for i = base + 1, base + 4 do
                    local e = byte(inc, i)
                    if e > 0 then sl, sh = sl + lo[e], sh + hi[e] end
                end
                if sl > need or sh < need then return false end
                if sl < need or sh > need then
                    for i = base + 1, base + 4 do
                        local e = byte(inc, i)
                        if e > 0 then
                            local ol, oh = lo[e], hi[e]
                            local l = math.max(ol, need - (sh - oh))
                            local h = math.min(oh, need - (sl - ol))
                            if l ~= ol or h ~= oh then
                                if not set(e, l, h) then return false end
                                changed = true
                                sl, sh = sl + l - ol, sh + h - oh
                            end
                        end
                    end
                end
            end
            for i = 1, nc do
                local e, f = cr[i] // 256, cr[i] % 256
                if lo[e] > 0 or lo[f] > 0 then
                    if lo[e] > 0 and lo[f] > 0 then return false end
                    if lo[e] > 0 then e, f = f, e end
                    -- f has a bridge: e cannot have one.
                    if hi[e] > 0 then
                        set(e, 0, 0)
                        changed = true
                    end
                end
            end
        end
        return n == 1 or connected()
    end
    -- An open edge of the island with the fewest open edges.
    local function choose()
        local best, bestOpen = nil, 99
        for k = 1, n do
            local open, pick = 0, nil
            for i = (k - 1) * 4 + 1, k * 4 do
                local e = byte(inc, i)
                if e > 0 and lo[e] < hi[e] then open, pick = open + 1, e end
            end
            if open > 0 and open < bestOpen then best, bestOpen = pick, open end
        end
        return best
    end

    local br = {}
    local d, nodes, count, deepest = 0, 0, 0, 0
    local ok = propagate()
    while true do
        if ok then
            local e = choose()
            if not e then
                count = count + 1
                if count >= 2 then return 2, deepest > 0 end
                ok = false
            else
                d = d + 1
                if d > deepest then deepest = d end
                br[d] = (tn * 256 + e) * 16 + lo[e] * 4 + hi[e]
                ok = set(e, lo[e], lo[e]) and propagate()
            end
        end
        if not ok then
            while d > 0 and br[d] % 16 // 4 >= br[d] % 4 do
                undo(br[d] // 4096)
                d = d - 1
            end
            if d == 0 then return count, deepest > 0 end
            undo(br[d] // 4096)
            br[d] = br[d] + 4
            local v = br[d] % 16 // 4
            ok = set(br[d] // 16 % 256, v, v) and propagate()
        end
        nodes = nodes + 1
        if nodes > cap then return nil end
    end
end

-- Whether a layout uses its first and last rows and columns.
local function fills(p)
    local s, r0, r1, c0, c1 = p.size, false, false, false, false
    for k = 1, p.islands do
        r0, r1 = r0 or p.ir:byte(k) == 0, r1 or p.ir:byte(k) == s - 1
        c0, c1 = c0 or p.ic:byte(k) == 0, c1 or p.ic:byte(k) == s - 1
    end
    return r0 and r1 and c0 and c1
end

-- A puzzle with one solution: grid, solution, or nil when none was found in
-- capMs milliseconds (millis: a clock in ms). An easy one (`easy`) is solved
-- without guessing.
function G.generate(size, rand, millis, capMs, easy)
    local target = size * size // 4
    local start = millis()
    repeat
        local grid, sol, p = G.layout(size, rand, target)
        if grid and fills(p) then
            collectgarbage("collect") -- the layout's scratch tables, before the search
            local c, guessed = G.count(p, 3000)
            if c == 1 and not (easy and guessed) then return grid, sol end
        end
        p = nil
        collectgarbage("collect")
    until millis() - start > capMs
    return nil
end

return G
end
