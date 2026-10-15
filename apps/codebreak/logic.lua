-- Code Breaker rules: codes, scores, a game's record, saving and restoring.
-- No smudge.* calls, so test/lua_apps/codebreak_test.lua runs it on a
-- computer. Usage: local L = smudge.dofile("logic.lua")
--
-- A code is a string of n digits "1".."k" (k symbols, n positions). A game
-- keeps its guesses as one string (n characters each) and their scores as
-- another (two digits each: black pegs, then white pegs).

local L = {}

L.PRESETS = {
    { name = "Easy", n = 4, k = 6, rep = false },
    { name = "Normal", n = 4, k = 6, rep = true },
    { name = "Hard", n = 5, k = 8, rep = true },
}

local ca, cb = {}, {}

-- Black pegs (right symbol, right place) and white pegs (right symbol, wrong
-- place) for guess a against code b.
function L.score(a, b)
    local black = 0
    for s = 1, 8 do ca[s], cb[s] = 0, 0 end
    for i = 1, #a do
        local x, y = a:byte(i) - 48, b:byte(i) - 48
        if x == y then
            black = black + 1
        else
            ca[x], cb[y] = ca[x] + 1, cb[y] + 1
        end
    end
    local white = 0
    for s = 1, 8 do white = white + (ca[s] < cb[s] and ca[s] or cb[s]) end
    return black, white
end

-- Whether a score can happen at all with n positions.
function L.possible(n, b, w)
    return b >= 0 and w >= 0 and b + w <= n and not (b == n - 1 and w == 1)
end

-- Whether `code` is a full code for this game: repeats only when allowed,
-- unless `guess` (a guess may repeat symbols in any game).
function L.valid(g, code, guess)
    if #code ~= g.n then return false end
    local seen = 0
    for i = 1, g.n do
        local s = code:byte(i) - 48
        if s < 1 or s > g.k then return false end
        local bit = 1 << s
        if not g.rep and not guess and seen & bit ~= 0 then return false end
        seen = seen | bit
    end
    return true
end

function L.randomCode(n, k, rep, rand)
    local t, used = {}, {}
    for i = 1, n do
        local s
        repeat s = rand(k) until rep or not used[s]
        used[s] = true
        t[i] = string.char(48 + s)
    end
    return table.concat(t)
end

-- mode: "you" (you break the device's code) or "dev" (the device breaks
-- yours). preset: 1..3 or 4 (custom). auto: the app scores the device's
-- guesses from the code you entered (secret ~= "").
function L.new(mode, preset, n, k, rep, max, auto)
    return { mode = mode, preset = preset, n = n, k = k, rep = rep, max = max, auto = auto,
             secret = "", guesses = "", scores = "", result = nil }
end

function L.count(g) return #g.guesses // g.n end
function L.guess(g, i) return g.guesses:sub((i - 1) * g.n + 1, i * g.n) end
function L.scoreOf(g, i)
    local b, w = g.scores:byte(2 * i - 1, 2 * i)
    return b - 48, w - 48
end

local function finish(g)
    local c, b = L.count(g), nil
    if c > 0 then b = L.scoreOf(g, c) end
    if b == g.n then
        g.result = "won" -- the code was broken (by you, or by the device)
    elseif c >= g.max then
        g.result = "lost"
    else
        g.result = nil
    end
end

-- Records a guess and its score.
function L.add(g, code, b, w)
    g.guesses = g.guesses .. code
    g.scores = g.scores .. string.char(48 + b, 48 + w)
    finish(g)
end

-- Removes the last guess (a score taken back). Returns that guess.
function L.drop(g)
    local c = L.count(g)
    if c == 0 then return nil end
    local last = L.guess(g, c)
    g.guesses = g.guesses:sub(1, (c - 1) * g.n)
    g.scores = g.scores:sub(1, 2 * (c - 1))
    finish(g)
    return last
end

-- Your guess against the device's secret code.
function L.play(g, code)
    local b, w = L.score(code, g.secret)
    L.add(g, code, b, w)
    return b, w
end

-- Whether a code fits every score so far.
function L.fits(g, code)
    for i = 1, L.count(g) do
        local b, w = L.score(L.guess(g, i), code)
        local sb, sw = L.scoreOf(g, i)
        if b ~= sb or w ~= sw then return false end
    end
    return true
end

-- One line of text: "you|1|4|6|0|10|0|secret|guesses|scores|pending".
function L.encode(g)
    return table.concat({ g.mode, g.preset, g.n, g.k, g.rep and 1 or 0, g.max, g.auto and 1 or 0,
                          g.secret, g.guesses, g.scores, g.pending or "" }, "|")
end

function L.decode(s)
    local mode, p, n, k, r, m, a, secret, guesses, scores, pending =
        s:match("^(%a+)|(%d)|(%d)|(%d)|(%d)|(%d+)|(%d)|(%d*)|(%d*)|(%d*)|(%d*)$")
    if not mode or (mode ~= "you" and mode ~= "dev") then return nil end
    n, k, m = tonumber(n), tonumber(k), tonumber(m)
    if n < 4 or n > 5 or k < 6 or k > 8 or m < 1 or m > 12 then return nil end
    local g = L.new(mode, tonumber(p), n, k, r == "1", m, a == "1")
    g.secret = secret
    if #guesses % n ~= 0 or #scores ~= 2 * (#guesses // n) then return nil end
    if mode == "you" and not L.valid(g, secret) then return nil end
    if secret ~= "" and #secret ~= n then return nil end
    g.guesses, g.scores = guesses, scores
    if pending ~= "" then
        if #pending ~= n then return nil end
        g.pending = pending
    end
    finish(g)
    return g
end

return L
