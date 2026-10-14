-- Dots and Boxes rules: what a move and the device's player need, kept in
-- memory all game. Making a game, taking back and the board's geometry are
-- in game.lua, loaded when needed. No drawing, so both can be tested on a
-- computer (test/lua_apps/dots_test.lua).
--
-- A board of n x n boxes has (n + 1) x (n + 1) dots and 2n(n + 1) lines,
-- numbered from 1: first the horizontal ones row by row (line r * n + c + 1
-- runs from dot (r, c) to dot (r, c + 1), r = 0..n), then the vertical ones
-- (n(n + 1) + r * (n + 1) + c + 1 runs from dot (r, c) to dot (r + 1, c)).
-- Box r * n + c + 1 is the one whose top left dot is (r, c). Players are 1
-- and 2; drawing the fourth side of a box scores it and moves again. The
-- board's shape is kept in strings, one byte per entry (cheaper than tables
-- on the X3): g.b1:byte(l), g.b2:byte(l) are the boxes on each side of line l
-- (0 off the board), g.bl:byte((b - 1) * 4 + k) the four lines of box b.

local D = {}

function D.over(g) return #g.hist == g.L end

function D.canPlay(g, l)
    return l >= 1 and l <= g.L and g.lines[l] == 0
end

-- Draws line l for the player to move. Returns the number of boxes it
-- completed (the same player moves again when it is not 0), or nil when the
-- line cannot be drawn.
function D.play(g, l)
    if not D.canPlay(g, l) then return nil end
    local who = g.turn
    g.lines[l] = who
    g.hist[#g.hist + 1] = l
    local done = 0
    local a, b = g.b1:byte(l), g.b2:byte(l)
    for k = 1, 2 do
        local x = k == 1 and a or b
        if x > 0 then
            local s = g.sides[x] + 1
            g.sides[x] = s
            if s == 4 then
                g.owner[x] = who
                done = done + 1
            end
        end
    end
    g.score[who] = g.score[who] + done
    if done == 0 then g.turn = 3 - who end
    return done
end

-- Saved form: "n,first,l1,l2,...".
function D.encode(g)
    return g.n .. "," .. g.first .. (#g.hist > 0 and ("," .. table.concat(g.hist, ",")) or "")
end

-- 1 or 2 for the winner, 0 for a draw, nil while the game goes on.
function D.winner(g)
    if not D.over(g) then return nil end
    local a, b = g.score[1], g.score[2]
    return a > b and 1 or (b > a and 2 or 0)
end

return D
