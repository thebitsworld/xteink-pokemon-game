-- Whodunit case generator and solver. Usage:
--   local case = smudge.dofile("gen.lua")(W).generate(level, seed)  -- W: logic.lua
--   smudge.dofile("words.lua")(W).finish(case)   -- after gen.lua is dropped
-- Loaded only to build (or rebuild from its seed) a case, then dropped; the
-- case comes out as numbers, and words.lua writes its clues and case file.
--
-- A case is built from a seed: a random solution, then clues drawn from the
-- true statements about it until a solver that reasons the way a player does
-- (eliminate, fill the only cell left, carry yes and no across the grid)
-- fills the whole grid; then every clue it can do without is dropped. So a
-- case always has exactly one answer and never needs a guess.

return function(W)
local G = {}
local key = W.key

-- The cast. Names within a category start with different letters, so the
-- grid can label its columns with initials.
local SUSPECTS = { "Aunt Hazel", "Baron Cobb", "Doctor Finch", "Lady Juniper", "Major Birch", "Nurse Ivy",
                   "Professor Quill", "Sister Rowan", "Chef Basil", "Captain Moss", "Madame Opal", "Mister Vance" }
local WEAPONS = { "rope", "letter opener", "teapot", "brick", "umbrella", "fountain pen", "skillet", "garden shears",
                  "bottle", "hatpin", "candlestick", "walking stick" }
local PLACES = { "library", "greenhouse", "kitchen", "boathouse", "attic", "wine cellar", "observatory", "stables",
                 "ballroom", "chapel", "garage", "orchard" }
local MOTIVES = { "jealousy", "revenge", "money", "fear", "ambition", "love", "pride", "secrets" }
local POOLS = { SUSPECTS, WEAPONS, PLACES, MOTIVES }

-- Traits: each item of a category gets one value of each of its traits
-- (the words for them are in words.lua; here only how many values each has).
local TRAIT_VALUES = { [1] = { 2, 2, 3 }, [2] = { 2, 2 }, [3] = { 2 } }

-- Weapons and places have fixed traits (a brick is heavy, the orchard is
-- outdoors): value indexes into the trait values (TRAIT_VALUES), by name.
local FIXED = {
    ["rope"] = { 2, 2 }, ["letter opener"] = { 2, 1 }, ["teapot"] = { 2, 2 }, ["brick"] = { 1, 2 },
    ["umbrella"] = { 2, 2 }, ["fountain pen"] = { 2, 1 }, ["skillet"] = { 1, 1 }, ["garden shears"] = { 1, 1 },
    ["bottle"] = { 1, 2 }, ["hatpin"] = { 2, 1 }, ["candlestick"] = { 1, 1 }, ["walking stick"] = { 1, 2 },
    ["library"] = { 1 }, ["greenhouse"] = { 2 }, ["kitchen"] = { 1 }, ["boathouse"] = { 2 }, ["attic"] = { 1 },
    ["wine cellar"] = { 1 }, ["observatory"] = { 1 }, ["stables"] = { 2 }, ["ballroom"] = { 1 }, ["chapel"] = { 1 },
    ["garage"] = { 1 }, ["orchard"] = { 2 },
}

local DETAIL_COUNT = 10 -- places' details, in words.lua

-- Random numbers --------------------------------------------------------------
-- xorshift32: a case is rebuilt exactly from its seed (a saved game keeps
-- only the seed). Lua here has 32-bit integers, which is what it needs.

local function generator(seed)
    local x = (seed ~= 0) and seed or 0x2545F491
    return function(n)
        x = x ~ (x << 13)
        x = x ~ (x >> 17)
        x = x ~ (x << 5)
        return (x & 0x7fffffff) % n + 1
    end
end

local function shuffle(t, rnd)
    for i = #t, 2, -1 do
        local j = rnd(i)
        t[i], t[j] = t[j], t[i]
    end
end

-- Picks `k` names from a pool with different first letters.
local function pickNames(pool, k, rnd)
    local order = {}
    for i = 1, #pool do order[i] = i end
    shuffle(order, rnd)
    local out, used = {}, {}
    for _, i in ipairs(order) do
        local name = pool[i]
        local initial = name:match("(%a)%a*$"):upper() -- last word for people, first letter otherwise
        if pool ~= SUSPECTS then initial = name:sub(1, 1):upper() end
        if not used[initial] then
            used[initial] = true
            out[#out + 1] = name
            if #out == k then break end
        end
    end
    return out
end

-- The solver ----------------------------------------------------------------

-- The solver writes through these instead of a closure per call, and the
-- generator reuses one scratch grid: building a case runs the solver a few
-- dozen times, and fresh tables for each run do not fit the X3.
local grid, changed = nil, false
local scratch = {}

local function set(a, i, b, j, v)
    local kk = key(a, i, b, j)
    if (grid[kk] or 0) == 0 then
        grid[kk] = v
        changed = true
    end
end

local function cleared()
    for kk = 1, 96 do scratch[kk] = nil end
    return scratch
end

-- Applies the clues to `g` and reasons to a fixpoint. Returns true when
-- every pair is decided.
function G.solve(case, g, clues)
    local n, k = case.categories, case.items
    grid, changed = g, true
    while changed do
        changed = false
        -- Clues: the row holding item x of category A has, in B, a value in S.
        for _, c in ipairs(clues) do
            local a, x, b, cs
            if type(c) == "number" then
                a, x, b, cs = c & 7, (c >> 3) & 7, (c >> 6) & 7, (c >> 9) & 15
            else
                a, x, b, cs = c.a, c.x, c.b, c.set
            end
            for v = 1, k do
                if cs & (1 << (v - 1)) == 0 then set(a, x, b, v, W.NO) end
            end
        end
        for a = 1, n do
            for b = a + 1, n do
                for i = 1, k do
                    -- One yes crosses out the rest of its row and column; one
                    -- cell left in a row or column is the yes.
                    local open, yes = 0, nil
                    for j = 1, k do
                        local v = grid[key(a, i, b, j)] or 0
                        if v == W.YES then yes = j end
                        if v ~= W.NO then open = open + 1 end
                    end
                    if yes then
                        for j = 1, k do
                            if j ~= yes then set(a, i, b, j, W.NO) end
                        end
                        for i2 = 1, k do
                            if i2 ~= i then set(a, i2, b, yes, W.NO) end
                        end
                    elseif open == 1 then
                        for j = 1, k do
                            if (grid[key(a, i, b, j)] or 0) ~= W.NO then set(a, i, b, j, W.YES) end
                        end
                    end
                    open = 0
                    for i2 = 1, k do
                        if (grid[key(a, i2, b, i)] or 0) ~= W.NO then open = open + 1 end
                    end
                    if open == 1 then
                        for i2 = 1, k do
                            if (grid[key(a, i2, b, i)] or 0) ~= W.NO then set(a, i2, b, i, W.YES) end
                        end
                    end
                end
            end
        end
        -- Across categories: if a_i is with b_j, then a_i and b_j agree on
        -- every item of a third category.
        for a = 1, n do
            for b = 1, n do
                if a ~= b then
                    for i = 1, k do
                        for j = 1, k do
                            if (grid[key(a, i, b, j)] or 0) == W.YES then
                                for c = 1, n do
                                    if c ~= a and c ~= b then
                                        for l = 1, k do
                                            local v = grid[key(b, j, c, l)] or 0
                                            if v ~= 0 then set(a, i, c, l, v) end
                                        end
                                    end
                                end
                            end
                        end
                    end
                end
            end
        end
    end
    for a = 1, n do
        for b = a + 1, n do
            for i = 1, k do
                for j = 1, k do
                    if (grid[key(a, i, b, j)] or 0) == 0 then return false end
                end
            end
        end
    end
    return true
end

-- Generation ----------------------------------------------------------------

-- The item of category b in the row holding item x of category a.
function G.answer(case, a, x, b)
    for s = 1, case.items do
        if case.solution[a][s] == x then return case.solution[b][s] end
    end
end

-- A candidate clue is one integer - a, x, b, the set, and for a trait clue
-- which trait and value - so the pool of candidates stays small:
-- a | x << 3 | b << 6 | set << 9 | trait << 13 | value << 15.
local function pack(a, x, b, set, t, value) return a | (x << 3) | (b << 6) | (set << 9) | ((t or 0) << 13) | ((value or 0) << 15) end

-- Every true clue the level allows, in a random order.
local function cluePool(case, level, rnd)
    local n, k = case.categories, case.items
    local pool = {}
    local forms = W.LEVELS[level].forms
    for _ = 1, 2 do
        for a = 1, n do
            for b = 1, n do
                if a ~= b then
                    for x = 1, k do
                        local y = G.answer(case, a, x, b)
                        local form = forms[rnd(#forms)]
                        if form == "yes" then
                            pool[#pool + 1] = pack(a, x, b, 1 << (y - 1))
                        elseif form == "no" or form == "either" then
                            local z = rnd(k - 1)
                            if z >= y then z = z + 1 end
                            local set = (form == "no") and (((1 << k) - 1) & ~(1 << (z - 1))) or ((1 << (y - 1)) | (1 << (z - 1)))
                            pool[#pool + 1] = pack(a, x, b, set)
                        elseif form == "trait" and TRAIT_VALUES[b] then
                            -- "Whoever was in the attic was tall": the set of
                            -- b's items sharing y's value of a trait.
                            local t = rnd(#TRAIT_VALUES[b])
                            local value = case.traits[b][y][t]
                            local set = 0
                            for v = 1, k do
                                if case.traits[b][v][t] == value then set = set | (1 << (v - 1)) end
                            end
                            if set ~= (1 << k) - 1 then pool[#pool + 1] = pack(a, x, b, set, t, value) end
                        end
                    end
                end
            end
        end
    end
    shuffle(pool, rnd)
    return pool
end

local function attempt(level, rnd)
    local L = W.LEVELS[level]
    local n, k = L.categories, L.items
    local case = { level = level, categories = n, items = k, names = {}, solution = {}, traits = {} }
    for c = 1, n do
        case.names[c] = pickNames(POOLS[c], k, rnd)
        local perm = {}
        for i = 1, k do perm[i] = i end
        if c ~= W.SUSPECT then shuffle(perm, rnd) end
        case.solution[c] = perm -- solution[c][s]: the item of c that suspect s had
        case.traits[c] = {}
        for i = 1, k do
            local t = {}
            for q, count in ipairs(TRAIT_VALUES[c] or {}) do
                local fixed = FIXED[case.names[c][i]]
                t[q] = fixed and fixed[q] or rnd(count)
            end
            case.traits[c][i] = t
        end
    end
    -- Where each place is: a detail, one per place, and the murder.
    local details = {}
    for i = 1, DETAIL_COUNT do details[i] = i end
    shuffle(details, rnd)
    case.details = {} -- indexes; words.lua turns them into text
    for i = 1, k do case.details[i] = details[i] end
    case.murderer = rnd(k)
    -- Clues until the solver fills the grid, then drop the ones it does not need.
    local pool, clues = cluePool(case, level, rnd), {}
    local solved = false
    for _, c in ipairs(pool) do
        clues[#clues + 1] = c
        if G.solve(case, cleared(), clues) then
            solved = true
            break
        end
    end
    if not solved then return nil end
    local order = {}
    for i = 1, #clues do order[i] = i end
    shuffle(order, rnd)
    local keep, trial = {}, {}
    for i = 1, #clues do keep[i] = true end
    for _, drop in ipairs(order) do
        keep[drop] = false
        local t = 0
        for i, c in ipairs(clues) do
            if keep[i] then
                t = t + 1
                trial[t] = c
            end
        end
        for i = t + 1, #trial do trial[i] = nil end
        if not G.solve(case, cleared(), trial) then keep[drop] = true end
    end
    case.clues = {} -- packed integers; words.lua turns them into sentences
    for i, c in ipairs(clues) do
        if keep[i] then case.clues[#case.clues + 1] = c end
    end
    return case
end

-- Builds the case for `seed` at `level` (1..3).
function G.generate(level, seed)
    local rnd = generator(seed)
    for _ = 1, 64 do
        local case = attempt(level, rnd)
        if case then
            case.seed = seed
            return case
        end
        collectgarbage("collect") -- a failed attempt's tables
    end
    return nil
end

return G
end
