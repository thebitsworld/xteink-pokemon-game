-- Toy Front: the rules. No drawing, so it can be tested on a computer
-- (test/lua_apps/toyfront_test.lua).
--
-- Two toy armies fight over a 4x4 board of bases joined by paths; each base
-- is worth 1 to 3 medals. Each side has twelve toys of strength 1 to 6 (two
-- of each), three in hand. A turn plays one toy:
--   * on an empty base in your home row, or one joined by a path to a base you
--     hold - it is yours, with the toy's strength;
--   * on a base you hold - its strength goes up (to at most 9);
--   * on an enemy base joined to one you hold - an attack: a stronger toy takes
--     it, keeping the difference; a weaker one wears it down; equal ones
--     leave it empty.
-- With nowhere to play, a toy is given up instead. When both armies are
-- spent, the side holding more medals wins.
-- Player 1 (you) has the bottom row as home, player 2 (the device) the top.

local T = {}

T.SIZE = 4
T.MAX_STRENGTH = 9
T.HAND = 3
T.ARMY = { 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6 }

-- Boards: medals per base, row by row, and the paths that are missing, as
-- pairs of base numbers (1..16, row by row). Designed for this game.
T.BOARDS = {
    { name = "Meadow", medals = "1221" .. "2332" .. "2332" .. "1221", walls = {} },
    { name = "River", medals = "1212" .. "3113" .. "3113" .. "2121", walls = { { 5, 9 }, { 6, 10 }, { 11, 15 } } },
    { name = "Castle", medals = "1111" .. "1331" .. "1331" .. "1111",
      walls = { { 2, 6 }, { 3, 7 }, { 5, 6 }, { 7, 8 }, { 10, 14 }, { 11, 15 } } },
    { name = "Corners", medals = "3113" .. "1221" .. "1221" .. "3113", walls = { { 2, 3 }, { 14, 15 } } },
    { name = "Ridge", medals = "1122" .. "1232" .. "2321" .. "2211", walls = { { 6, 7 }, { 10, 11 } } },
    { name = "Bridges", medals = "2112" .. "1331" .. "1331" .. "2112",
      walls = { { 5, 9 }, { 7, 11 }, { 8, 12 }, { 2, 6 }, { 15, 11 } } },
}

local function idx(r, c) return (r - 1) * T.SIZE + c end
T.idx = idx

-- Whether bases a and b (numbers) are joined by a path on board g.
function T.joined(g, a, b)
    local ra, ca = (a - 1) // T.SIZE, (a - 1) % T.SIZE
    local rb, cb = (b - 1) // T.SIZE, (b - 1) % T.SIZE
    if math.abs(ra - rb) + math.abs(ca - cb) ~= 1 then return false end
    for _, wl in ipairs(T.BOARDS[g.board].walls) do
        if (wl[1] == a and wl[2] == b) or (wl[1] == b and wl[2] == a) then return false end
    end
    return true
end

function T.medals(g, i) return tonumber(T.BOARDS[g.board].medals:sub(i, i)) end

local function shuffle(list, rand)
    for i = #list, 2, -1 do
        local j = rand(i)
        list[i], list[j] = list[j], list[i]
    end
end

-- A new game on `board`; `starter` (1 or 2, default 1) moves first.
function T.new(board, rand, starter)
    local g = { board = board, owner = {}, strength = {}, decks = { {}, {} }, hands = { {}, {} }, turn = starter or 1,
                over = false }
    for i = 1, T.SIZE * T.SIZE do g.owner[i], g.strength[i] = 0, 0 end
    for p = 1, 2 do
        for i, v in ipairs(T.ARMY) do g.decks[p][i] = v end
        shuffle(g.decks[p], rand)
        for _ = 1, T.HAND do g.hands[p][#g.hands[p] + 1] = table.remove(g.decks[p]) end
    end
    return g
end

local function homeRow(p) return p == 1 and T.SIZE or 1 end

-- Whether player p may play on base i (whatever the toy).
function T.canPlay(g, p, i)
    if g.owner[i] == p then return true end
    for _, j in ipairs({ i - T.SIZE, i + T.SIZE, (i - 1) % T.SIZE > 0 and i - 1 or 0, i % T.SIZE > 0 and i + 1 or 0 }) do
        if j >= 1 and j <= T.SIZE * T.SIZE and g.owner[j] == p and T.joined(g, i, j) then return true end
    end
    if g.owner[i] == 0 and (i - 1) // T.SIZE + 1 == homeRow(p) then return true end
    return false
end

-- The medals player p holds.
function T.score(g, p)
    local s = 0
    for i = 1, T.SIZE * T.SIZE do
        if g.owner[i] == p then s = s + T.medals(g, i) end
    end
    return s
end

-- Plays hand card k of the player to move on base i. Returns what happened
-- ("take", "build", "boost", "attack", "even"), or nil if not allowed.
function T.play(g, k, i)
    local p = g.turn
    local v = g.hands[p][k]
    if g.over or not v or not T.canPlay(g, p, i) then return nil end
    table.remove(g.hands[p], k)
    if #g.decks[p] > 0 then g.hands[p][#g.hands[p] + 1] = table.remove(g.decks[p]) end
    local what
    if g.owner[i] == 0 then
        g.owner[i], g.strength[i], what = p, v, "build"
    elseif g.owner[i] == p then
        g.strength[i], what = math.min(T.MAX_STRENGTH, g.strength[i] + v), "boost"
    elseif v > g.strength[i] then
        g.owner[i], g.strength[i], what = p, v - g.strength[i], "take"
    elseif v < g.strength[i] then
        g.strength[i], what = g.strength[i] - v, "attack"
    else
        g.owner[i], g.strength[i], what = 0, 0, "even"
    end
    -- The other side moves next, if it has toys left; the game ends when no one does.
    local q = 3 - p
    if #g.hands[q] > 0 then
        g.turn = q
    elseif #g.hands[p] == 0 then
        g.over = true
    end
    return what
end

-- Whether player p has any legal move (a hand and somewhere to play).
function T.hasMove(g, p)
    if #g.hands[p] == 0 then return false end
    for i = 1, T.SIZE * T.SIZE do
        if T.canPlay(g, p, i) then return true end
    end
    return false
end

-- With no base to play on, the player to move gives up hand card k instead.
function T.discard(g, k)
    local p = g.turn
    if g.over or T.hasMove(g, p) or not g.hands[p][k] then return false end
    table.remove(g.hands[p], k)
    if #g.decks[p] > 0 then g.hands[p][#g.hands[p] + 1] = table.remove(g.decks[p]) end
    local q = 3 - p
    if #g.hands[q] > 0 then
        g.turn = q
    elseif #g.hands[p] == 0 then
        g.over = true
    end
    return true
end

-- The winner (1 or 2) or 0 for a draw, once the game is over.
function T.winner(g)
    local a, b = T.score(g, 1), T.score(g, 2)
    return a > b and 1 or (b > a and 2 or 0)
end

-- Saving: board, turn, then owner*10+strength per base, then decks and hands.
function T.serialize(g)
    local cells = {}
    for i = 1, T.SIZE * T.SIZE do cells[i] = g.owner[i] * 10 + g.strength[i] end
    return table.concat({ g.board, g.turn, table.concat(cells, ","), table.concat(g.decks[1], ","),
                          table.concat(g.decks[2], ","), table.concat(g.hands[1], ","), table.concat(g.hands[2], ",") },
                        "|")
end

function T.deserialize(s)
    local f = {}
    for part in (s .. "|"):gmatch("([^|]*)|") do f[#f + 1] = part end
    if #f ~= 7 then return nil end
    local function nums(x)
        local t = {}
        for n in x:gmatch("%d+") do t[#t + 1] = tonumber(n) end
        return t
    end
    local board, turn = tonumber(f[1]), tonumber(f[2])
    if not T.BOARDS[board or 0] or (turn ~= 1 and turn ~= 2) then return nil end
    local cells = nums(f[3])
    if #cells ~= T.SIZE * T.SIZE then return nil end
    local g = { board = board, turn = turn, owner = {}, strength = {}, decks = { nums(f[4]), nums(f[5]) },
                hands = { nums(f[6]), nums(f[7]) }, over = false }
    for i, c in ipairs(cells) do g.owner[i], g.strength[i] = c // 10, c % 10 end
    if #g.hands[1] == 0 and #g.hands[2] == 0 then return nil end
    return g
end

return T
