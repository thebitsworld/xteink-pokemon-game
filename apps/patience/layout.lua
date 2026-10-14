-- Patience table layout, shared by draw.lua and input.lua (each loads its own
-- copy; only one of them is in memory at a time). Usage:
--   local L = smudge.dofile("layout.lua")(P, S)
--   L.update()   -- before using it: the sizes depend on the game
-- Cards in a long column close up (face-down ones first) so the column stays
-- above the button hints.

return function(P, S)
local w, h, m = S.w, S.h, S.m
local L = {}

local function layout()
    local g = S.game
    local n = #g.cols
    local gap = n == 8 and 4 or 3
    L.n, L.gap = n, gap
    L.cw = (w - 8 - (n - 1) * gap) // n
    L.ch = L.cw * 7 // 5
    L.left = (w - (n * L.cw + (n - 1) * gap)) // 2
    L.status = m.top_padding + m.header_height + 4
    L.topY = L.status + 38
    L.topH = g.kind == P.FREECELL and L.ch or 44
    L.tabY = L.topY + L.topH + 12
    L.bottom = h - m.button_hints_height - 6
    L.downStep, L.upStep = 8, n == 8 and 32 or 28
end

local function colX(c) return L.left + (c - 1) * (L.cw + L.gap) end

-- Steps between face-down and face-up cards of a column, closed up to fit.
local function steps(col)
    local g = S.game
    local n = #g.cols[col]
    local down = g.down and g.down[col] or 0
    local up = n - down
    local ds, us = L.downStep, L.upStep
    local avail = L.bottom - L.tabY - L.ch
    if down * ds + (up - 1) * us > avail then
        if down > 0 then ds = math.max(3, math.min(ds, (avail - (up - 1) * us) // down)) end
        if up > 1 and down * ds + (up - 1) * us > avail then us = math.max(6, (avail - down * ds) // (up - 1)) end
    end
    return ds, us, down
end

local function cardY(col, i)
    local ds, us, down = steps(col)
    if i <= down then return L.tabY + (i - 1) * ds end
    return L.tabY + down * ds + (i - 1 - down) * us
end

-- Spider's top row: completed runs on the left, the stock on the right.
local function stockRect() return w - 8 - 132, L.topY, 132, L.topH end

-- FreeCell's top row: cells in columns 1..4, foundations in 5..8.
local function placeRect(loc)
    if S.game.kind == P.SPIDER then return stockRect() end
    if loc >= P.CELL and loc < P.CELL + 4 then return colX(loc - P.CELL + 1), L.topY, L.cw, L.ch end
    if loc == 13 then return colX(5), L.topY, 4 * L.cw + 3 * L.gap, L.ch end
    return nil
end

local function touchButtons()
    local y = L.status - 2
    return { undo = { w - 8 - 180, y, 84, 34 }, menu = { w - 8 - 88, y, 88, 34 } }
end
-- Cards to pick at the cursor: the player's choice, or the whole run that
-- can move.
function L.count()
    local g = S.game
    if S.cur > #g.cols then return 1 end
    local run = P.runLength(g, S.cur)
    if S.count then return math.max(1, math.min(S.count, run)) end
    if g.kind == P.FREECELL then run = math.min(run, P.maxMove(g, false)) end
    return math.max(1, run)
end

L.FOUNDATIONS = 13 -- the cursor's place for all four foundations
L.update, L.colX, L.steps, L.cardY = layout, colX, steps, cardY
L.stockRect, L.placeRect, L.touchButtons = stockRect, placeRect, touchButtons
return L
end
