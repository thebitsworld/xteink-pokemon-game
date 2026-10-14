-- Dots and Boxes: the parts of the rules needed only now and then - making
-- or loading a game, taking back, the board's geometry. Usage:
--   local G = smudge.dofile("game.lua")(D)
-- with D the rules (logic.lua); see there for how lines and boxes are
-- numbered. No drawing (start() only saves), so it can be tested.

return function(D)
local G = {}

-- Entry i of a shape string: kind 1 and 2 the boxes on each side of line i
-- (0 off the board), kind 3 line (i - 1) % 4 + 1 of box (i - 1) // 4 + 1
-- (top, bottom, left, right).
local function shape(n, kind, i)
    local nh = n * (n + 1)
    if kind == 3 then
        local b, k = (i - 1) // 4, (i - 1) % 4
        local r, c = b // n, b % n
        if k == 0 then return r * n + c + 1 end
        if k == 1 then return (r + 1) * n + c + 1 end
        return nh + r * (n + 1) + c + k - 1
    end
    local r, c, first, last
    if i <= nh then
        r, c = (i - 1) // n, (i - 1) % n
        first, last = r == 0, r == n
        if kind == 1 then return first and 0 or (r - 1) * n + c + 1 end
    else
        r, c = (i - nh - 1) // (n + 1), (i - nh - 1) % (n + 1)
        first, last = c == 0, c == n
        if kind == 1 then return first and 0 or r * n + c end
    end
    return last and 0 or r * n + c + 1
end

-- A shape string built 16 bytes at a time (no board-sized tables).
local function shapeString(n, kind, count)
    local parts, row = {}, {}
    for i = 1, count do
        row[#row + 1] = shape(n, kind, i)
        if #row == 16 or i == count then
            parts[#parts + 1] = string.char(table.unpack(row))
            for k = #row, 1, -1 do row[k] = nil end
        end
    end
    return table.concat(parts)
end

-- A new game: `first` (1 or 2) draws the first line.
function G.new(n, first)
    first = first or 1
    local nh, L = n * (n + 1), 2 * n * (n + 1)
    local g = { n = n, nh = nh, L = L, nb = n * n, first = first, turn = first, score = { 0, 0 },
                hist = {}, lines = {}, sides = { [0] = 0 }, owner = { [0] = 0 } }
    for l = 1, L do g.lines[l] = 0 end
    for b = 1, n * n do g.sides[b], g.owner[b] = 0, 0 end
    g.b1, g.b2, g.bl = shapeString(n, 1, L), shapeString(n, 2, L), shapeString(n, 3, 4 * n * n)
    return g
end

-- The line's two dots as (row, col) pairs.
function G.ends(g, l)
    local n = g.n
    if l <= g.nh then
        local r, c = (l - 1) // n, (l - 1) % n
        return r, c, r, c + 1
    end
    local i = l - g.nh - 1
    local r, c = i // (n + 1), i % (n + 1)
    return r, c, r + 1, c
end

-- The line's midpoint in half steps between dots: (2 * col, 2 * row).
function G.mid(g, l)
    local r1, c1, r2, c2 = G.ends(g, l)
    return c1 + c2, r1 + r2
end

-- Takes back the last line. Returns it, or nil at the start.
function G.undo(g)
    local l = g.hist[#g.hist]
    if not l then return nil end
    g.hist[#g.hist] = nil
    local who = g.lines[l]
    for k = 1, 2 do
        local x = (k == 1 and g.b1 or g.b2):byte(l)
        if x > 0 then
            if g.sides[x] == 4 then
                g.owner[x] = 0
                g.score[who] = g.score[who] - 1
            end
            g.sides[x] = g.sides[x] - 1
        end
    end
    g.lines[l] = 0
    g.turn = who
    return l
end

-- The player who drew the last line, or nil.
function G.lastPlayer(g)
    local l = g.hist[#g.hist]
    return l and g.lines[l] or nil
end

-- Lines a player drew in their last turn, as a set { [line] = true }: the
-- lines at the end of the history drawn by whoever drew the last one.
function G.lastTurn(g)
    local set, who = {}, G.lastPlayer(g)
    for i = #g.hist, 1, -1 do
        local l = g.hist[i]
        if g.lines[l] ~= who then break end
        set[l] = true
    end
    return set
end

function G.decode(s)
    local n, first, rest = s:match("^(%d+),(%d)(.*)$")
    n, first = tonumber(n), tonumber(first)
    if not n or n < 2 or n > 8 or (first ~= 1 and first ~= 2) then return nil end
    local g = G.new(n, first)
    for l in rest:gmatch("%d+") do
        if not D.play(g, tonumber(l)) then return nil end
    end
    return g
end


-- A new game in `mode` for the app's state S (main.lua): the menu's board
-- size, and who starts alternates.
function G.start(S, mode)
    S.mode = mode
    S.game = nil
    collectgarbage("collect")
    S.game = G.new(S.SIZES[S.size], S.nextFirst)
    S.nextFirst = 3 - S.nextFirst
    smudge.save("first", tostring(S.nextFirst))
    S.cursor = S.game.n // 2 + 1 -- the middle of the top row
    S.note = nil
end

return G
end
