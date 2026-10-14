-- Patience table, the drawing half (input.lua takes the moves). Usage:
--   local screen = smudge.dofile("draw.lua")(P, S)   -- screen.draw()
-- with P the rules (logic.lua) and S the app's state (main.lua).
--
-- Red suits are drawn grey (dithered), black suits solid. Face-down cards
-- show as thin grey strips.

return function(P, S)
local M = {}
local w, touch = S.w, S.touch
local L = smudge.dofile("layout.lua")(P, S)
local layout, colX, cardY, steps = L.update, L.colX, L.cardY, L.steps
local stockRect, placeRect, touchButtons = L.stockRect, L.placeRect, L.touchButtons
local RANKS = { "A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K" }
local SPR = { [0] = "sprites/clubs", "sprites/diamonds", "sprites/hearts", "sprites/spades" }

-- Drawing --------------------------------------------------------------------------

local function face(x, y, c, whole)
    local cw, ch = L.cw, L.ch
    smudge.rounded_rect(x, y, cw, ch, 4, true, false)
    smudge.rounded_rect(x, y, cw, ch, 4, false, 1)
    local suit, red = P.suit(c), P.red(c)
    local text = RANKS[P.rank(c)]
    local font = (cw < 50 and #text > 1) and "ui10" or "ui12"
    smudge.text(x + 3, y + 1, text, font, "bold", "left", true)
    smudge.draw_sprite(x + 4 + smudge.text_width(text, font, "bold"), y + 4, 16, 16, SPR[suit] .. "16.raw", false, red)
    if whole and ch >= 58 then
        smudge.draw_sprite(x + (cw - 32) // 2, y + ch - 36, 32, 32, SPR[suit] .. ".raw", false, red)
    end
end

local function back(x, y)
    smudge.rounded_rect(x, y, L.cw, L.ch, 4, true, false)
    smudge.rounded_rect(x, y, L.cw, L.ch, 4, false, 1)
    smudge.rect_dither(x + 3, y + 3, L.cw - 6, L.ch - 6, true)
end

local function slot(x, y, rw, rh, label)
    smudge.rounded_rect(x, y, rw, rh, 4, false, 1)
    if label then smudge.text(x + rw // 2, y + rh // 2 - 10, label, "ui10", "regular", "center", true) end
end

-- The rectangle of the last `k` cards of column `col`.
local function runRect(col, k)
    local n = #S.game.cols[col]
    if n == 0 then return colX(col), L.tabY, L.cw, L.ch end
    local y = cardY(col, math.max(1, n - k + 1))
    return colX(col), y, L.cw, cardY(col, n) + L.ch - y
end

local function placeOrRun(loc, k)
    if loc <= L.n then return runRect(loc, k) end
    return placeRect(loc)
end

local function statusText()
    local g = S.game
    if P.won(g) then return "You won in " .. g.moves .. " moves!" end
    if S.note then return S.note end
    if g.kind == P.FREECELL then
        return string.format("Moves %d  Free %d  Max %d", g.moves, P.freeCells(g), P.maxMove(g, false))
    end
    return string.format("Moves %d  Runs %d/8", g.moves, #g.done)
end

local function drawTop()
    local g = S.game
    if g.kind == P.FREECELL then
        for i = 1, 4 do
            local x = colX(i)
            if g.cells[i] ~= 0 then face(x, L.topY, g.cells[i], true) else slot(x, L.topY, L.cw, L.ch, "Free") end
        end
        for s = 0, 3 do
            local x, r = colX(s + 5), g.found[s + 1]
            if r > 0 then
                face(x, L.topY, s * 13 + r, true)
            else
                slot(x, L.topY, L.cw, L.ch)
                smudge.draw_sprite(x + (L.cw - 32) // 2, L.topY + (L.ch - 32) // 2, 32, 32, SPR[s] .. ".raw", false, true)
            end
        end
        return
    end
    -- Spider: one small K per completed run, then the stock with its deals left.
    for i = 1, 8 do
        local x = 8 + (i - 1) * 34
        if i <= #g.done then
            local s = g.done:byte(i) - 48
            smudge.rounded_rect(x, L.topY, 30, L.topH, 4, false, 1)
            smudge.text(x + 15, L.topY + 2, "K", "ui10", "bold", "center", true)
            smudge.draw_sprite(x + 7, L.topY + 22, 16, 16, SPR[s] .. "16.raw", false, s == 1 or s == 2)
        else
            smudge.rounded_rect(x, L.topY, 30, L.topH, 4, false, 1)
        end
    end
    local x, y, rw, rh = stockRect()
    local deals = #g.stock // 10
    if deals > 0 then
        smudge.rounded_rect(x, y, rw, rh, 6, true, true)
        smudge.text(x + rw // 2, y + 10, "Deal (" .. deals .. ")", "ui12", "bold", "center", false)
    else
        slot(x, y, rw, rh, "No deals")
    end
end

local function drawColumns()
    local g = S.game
    for col = 1, L.n do
        local s = g.cols[col]
        local x = colX(col)
        if s == "" then
            slot(x, L.tabY, L.cw, L.ch)
        else
            local ds, us, down = steps(col)
            local y = L.tabY
            for i = 1, #s do
                if i <= down then
                    back(x, y)
                    y = y + ds
                else
                    face(x, y, P.at(s, i), i == #s)
                    y = y + us
                end
            end
        end
    end
end

function M.draw()
    local g = S.game
    layout()
    smudge.header(g.kind == P.FREECELL and "FreeCell" or "Spider",
                  g.kind == P.FREECELL and ("#" .. g.deal) or (g.suits .. (g.suits == 1 and " suit" or " suits")))
    local text = statusText()
    local maxW = (touch and (w - 8 - 180) or w) - 20
    smudge.text(10, L.status + 4, text, smudge.text_width(text, "ui12", "bold") > maxW and "ui10" or "ui12", "bold",
                "left", true)
    if touch then
        local tb = touchButtons()
        for key, label in pairs({ undo = "Undo", menu = "Menu" }) do
            local b = tb[key]
            smudge.rounded_rect(b[1], b[2], b[3], b[4], 6, false, 2)
            smudge.text(b[1] + b[3] // 2, b[2] + 6, label, "ui12", "bold", "center", true)
        end
    end
    drawTop()
    drawColumns()

    if S.picked then
        local x, y, rw, rh = placeOrRun(S.picked, S.pickN)
        if S.picked >= P.CELL and S.picked < P.CELL + 4 then rw, rh = L.cw, L.ch end
        smudge.invert_rect(x, y, rw, rh)
    end
    if not touch and not P.won(g) then
        local k = S.picked and 1 or L.count()
        if S.picked and S.cur == S.picked then k = S.pickN end
        local x, y, rw, rh = placeOrRun(S.cur, k)
        local o = (g.kind == P.SPIDER and S.cur == P.STOCK) and 5 or 2 -- clear of the black stock
        smudge.rounded_rect(x - o, y - o, rw + 2 * o, rh + 2 * o, 6, false, 3)
    end
    if P.won(g) then smudge.popup("You won in " .. g.moves .. " moves!") end

    if touch then
        smudge.button_hints("", "", "", "")
    elseif P.won(g) then
        smudge.button_hints("Menu", "Menu", "", "")
    elseif S.picked then
        smudge.button_hints("Cancel", S.cur == S.picked and "Send" or "Drop", "<", ">")
    else
        smudge.button_hints("Menu", (g.kind == P.SPIDER and S.cur == P.STOCK) and "Deal" or "Pick", "<", ">")
    end
end

return M
end
