-- Logic tests for apps/codebreak: scoring, the device's guesses (Knuth's
-- minimax on 4 x 6, random fitting codes on bigger games), wrong scores
-- caught, saving and restoring (run by ctest: LuaAppLogic_codebreak).
local L = dofile(APPS .. "/codebreak/logic.lua")
local A = dofile(APPS .. "/codebreak/ai.lua")(L)

math.randomseed(2026)
local function rand(n) return math.random(1, n) end

local function score(a, b, eb, ew)
    local x, y = L.score(a, b)
    assert(x == eb and y == ew, a .. " vs " .. b .. ": got " .. x .. "," .. y .. " expected " .. eb .. "," .. ew)
    -- Scoring is symmetric.
    local x2, y2 = L.score(b, a)
    assert(x2 == x and y2 == y, "symmetry " .. a .. " " .. b)
end

-- Scoring, with repeated symbols.
do
    score("1234", "1234", 4, 0)
    score("1234", "4321", 0, 4)
    score("1234", "5656", 0, 0)
    score("1122", "1212", 2, 2)
    score("1122", "2211", 0, 4)
    score("1111", "1222", 1, 0)   -- one 1 in the code: one peg only
    score("1112", "2111", 2, 2)
    score("1123", "3111", 1, 2)   -- 1 right place; extra 1s count once each
    score("1223", "2232", 1, 2)
    score("6655", "5566", 0, 4)
    score("1234", "1243", 2, 2)
    score("11223", "32211", 1, 4)
    score("88888", "18888", 4, 0)
    score("12345", "23451", 0, 5)
    score("11111", "11111", 5, 0)
end

-- Possible scores.
do
    assert(L.possible(4, 2, 2) and L.possible(4, 0, 0) and L.possible(4, 4, 0))
    assert(not L.possible(4, 3, 1), "3 black 1 white cannot happen")
    assert(not L.possible(4, 2, 3) and not L.possible(5, 4, 1) and L.possible(5, 3, 2))
    -- Every score of every pair of 4 x 6 codes with 1111 is possible.
    for c = 0, 1295 do
        local t, x = {}, c
        for i = 1, 4 do
            t[i] = string.char(49 + x % 6)
            x = x // 6
        end
        local b, w = L.score("1123", table.concat(t))
        assert(L.possible(4, b, w))
    end
end

-- Valid codes and random codes.
do
    local g = L.new("you", 1, 4, 6, false, 10, false)
    assert(L.valid(g, "1234") and not L.valid(g, "1123") and L.valid(g, "1123", true))
    assert(not L.valid(g, "1237") and not L.valid(g, "123") and not L.valid(g, "12345"))
    for _ = 1, 200 do
        assert(L.valid(g, L.randomCode(4, 6, false, rand)))
        local h = L.new("you", 3, 5, 8, true, 10, false)
        assert(L.valid(h, L.randomCode(5, 8, true, rand)))
    end
end

-- A game against the device's code: winning and losing.
do
    local g = L.new("you", 2, 4, 6, true, 3, false)
    g.secret = "1316"
    local b, w = L.play(g, "1111")
    assert(b == 2 and w == 0 and not g.result)
    b, w = L.play(g, "6131")
    assert(b == 0 and w == 4 and not g.result)
    L.play(g, "1316")
    assert(g.result == "won" and L.count(g) == 3)
    g = L.new("you", 2, 4, 6, true, 2, false)
    g.secret = "1316"
    L.play(g, "2222")
    L.play(g, "3333")
    assert(g.result == "lost")
    -- Taking a score back reopens the game.
    assert(L.drop(g) == "3333" and not g.result and L.count(g) == 1)
end

-- Saving and restoring.
do
    local g = L.new("you", 3, 5, 8, true, 12, false)
    g.secret = "87712"
    L.play(g, "12345")
    L.play(g, "88776")
    local s = L.encode(g)
    local h = L.decode(s)
    assert(h and h.mode == "you" and h.n == 5 and h.k == 8 and h.rep and h.max == 12 and h.secret == "87712")
    assert(h.guesses == g.guesses and h.scores == g.scores and L.count(h) == 2 and h.preset == 3)
    local b, w = L.scoreOf(h, 2)
    assert(b == 2 and w == 1, "88776 vs 87712")
    local d = L.new("dev", 4, 4, 6, false, 8, true)
    d.secret = "1234"
    d.pending = "5612"
    L.add(d, "1122", 1, 1)
    h = L.decode(L.encode(d))
    assert(h.mode == "dev" and not h.rep and h.auto and h.pending == "5612" and h.secret == "1234" and h.max == 8)
    -- A hand-scored game has no secret.
    d = L.new("dev", 2, 4, 6, true, 10, false)
    L.add(d, "1122", 0, 1)
    h = L.decode(L.encode(d))
    assert(h and h.secret == "" and not h.auto and L.count(h) == 1)
    -- Damaged text is refused.
    assert(not L.decode("") and not L.decode("you|1|4|6|0|10|0|1234|123|") and not L.decode("xyz"))
    assert(not L.decode("you|1|4|6|0|10|0|1124|||"), "secret with a repeat in a game without")
end

-- The device plays a game against `secret`, scoring itself; returns the
-- number of guesses. memo (optional) caches guesses by the scores so far.
local function solve(n, k, rep, secret, memo, limit)
    local g = L.new("dev", 4, n, k, rep, limit or 30, true)
    g.secret = secret
    local seen = {}
    while not g.result do
        local key = g.scores
        local code = memo and memo[key]
        if not code then
            local left
            code, left = A.choose(g, rand)
            assert(code, "no code fits for " .. secret)
            if memo then memo[key] = code end
        end
        assert(not seen[code], "guess repeated: " .. code)
        seen[code] = true
        assert(#code == n)
        L.add(g, code, L.score(code, secret))
    end
    assert(g.result == "won", "not solved: " .. secret)
    return L.count(g)
end

-- The second-guess table is the minimax itself.
do
    A.noTable = true
    for entry in A.KNUTH2:gmatch("%S+") do
        local b, w, code = entry:match("(%d)(%d):(%d+)")
        local g = L.new("dev", 2, 4, 6, true, 10, false)
        L.add(g, "1122", tonumber(b), tonumber(w))
        local saveBudget = A.BUDGET
        A.BUDGET = 1296 * 300
        local got = A.choose(g, rand)
        A.BUDGET = saveBudget
        assert(got == code, "table " .. entry .. " but minimax gives " .. tostring(got))
    end
    A.noTable = nil
end

-- Knuth's minimax solves every 4 x 6 code in at most five guesses. Its
-- choices do not depend on chance, so they are shared through a memo.
do
    local memo, worst, total = {}, 0, 0
    for c = 0, 1295 do
        local t, x = {}, c
        for i = 4, 1, -1 do
            t[i] = string.char(49 + x % 6)
            x = x // 6
        end
        local used = solve(4, 6, true, table.concat(t), memo)
        if used > worst then worst = used end
        total = total + used
    end
    assert(worst <= 5, "Knuth needed " .. worst .. " guesses")
    print(string.format("4x6 repeats: worst %d, average %.3f", worst, total / 1296))
end

-- Bigger games and games without repeats: every guess fits the scores so
-- far (the device never wastes a guess) and the code is found in time.
do
    local function fitsAll(g, code)
        return L.fits(g, code)
    end
    for _, cfg in ipairs({ { 4, 6, false, 7 }, { 5, 8, true, 9 }, { 5, 6, true, 9 }, { 4, 8, false, 8 }, { 5, 8, false, 9 } }) do
        local n, k, rep, cap = cfg[1], cfg[2], cfg[3], cfg[4]
        local worst, total, games = 0, 0, cfg[1] == 5 and cfg[2] == 8 and 25 or 60
        for _ = 1, games do
            local secret = L.randomCode(n, k, rep, rand)
            local g = L.new("dev", 4, n, k, rep, 30, true)
            g.secret = secret
            while not g.result do
                local code, left = A.choose(g, rand)
                assert(code, "no code fits")
                if L.count(g) > 0 and left and left > A.BUDGET // (k ^ n) then
                    assert(fitsAll(g, code), "random guess does not fit the scores")
                end
                if L.count(g) > 0 and left == 1 then assert(code == secret) end
                for i = 1, L.count(g) do assert(L.guess(g, i) ~= code, "guess repeated") end
                L.add(g, code, L.score(code, secret))
            end
            local used = L.count(g)
            if used > worst then worst = used end
            total = total + used
        end
        assert(worst <= cap, string.format("%dx%d: %d guesses", n, k, worst))
        print(string.format("%dx%d %s: worst %d, average %.2f over %d games", n, k, rep and "repeats" or "no repeats",
                            worst, total / games, games))
    end
end

-- Scores given wrong by hand are caught: no code fits them.
do
    local g = L.new("dev", 2, 4, 6, true, 10, false)
    L.add(g, "1122", 0, 0) -- no 1s or 2s
    L.add(g, "3344", 0, 0) -- no 3s or 4s
    L.add(g, "5566", 0, 0) -- and no 5s or 6s: impossible
    assert(A.choose(g, rand) == nil, "inconsistent scores not caught")
    L.drop(g)
    local code, left = A.choose(g, rand)
    assert(code and left == 16, "only 5s and 6s left")
    -- A subtler mistake: the secret is 1234 but one score is off by a peg.
    g = L.new("dev", 2, 4, 6, true, 10, false)
    local secret = "1234"
    for _, guess in ipairs({ "1122", "1344", "3135" }) do
        local b, w = L.score(guess, secret)
        if guess == "1344" then w = w + 1 end
        L.add(g, guess, b, w)
    end
    local c = A.choose(g, rand)
    assert(c == nil or not L.fits(g, secret), "the true code cannot fit a wrong score")
    -- Larger game: two contradicting scores of the same guess.
    g = L.new("dev", 3, 5, 8, true, 10, false)
    L.add(g, "12345", 2, 1)
    L.add(g, "12345", 1, 1)
    assert(A.choose(g, rand) == nil)
end

-- The first guesses.
do
    assert(A.choose(L.new("dev", 2, 4, 6, true, 10, false), rand) == "1122")
    assert(A.choose(L.new("dev", 1, 4, 6, false, 10, false), rand) == "1234")
    assert(A.choose(L.new("dev", 3, 5, 8, true, 10, false), rand) == "11223")
end
