-- The Reversi board screen: drawing and input. Usage:
--   local screen = smudge.dofile("board.lua")(L, S)
-- with L the rules (logic.lua) and S the app's shared state (main.lua).
--
-- Black discs are filled, white discs are rings. The last disc played has a
-- small square of the other colour in its middle; with "Show moves" on, the
-- legal squares of the side to move carry a small dot.

return function(L, S)
local V = {}
local B = {} -- board geometry: x, y (top-left), cell

local function top() return S.m.top_padding + S.m.header_height + 6 end

local function layout()
    local boardTop = top() + 86
    local avail = S.h - S.m.button_hints_height - 72 - boardTop
    local cell = math.min((S.w - 48) // 8, avail // 8)
    B.x, B.y, B.cell = (S.w - 8 * cell) // 2 + 8, boardTop, cell
end

local function humanTurn()
    local g = S.game
    return not g.result and not S.thinking and (not S.vsDevice() or g.side == S.human)
end

local function cellXY(sq) return B.x + (sq % 10 - 1) * B.cell, B.y + (sq // 10 - 1) * B.cell end

local function drawDisc(cx, cy, r, side)
    if side == L.BLACK then
        smudge.circle(cx, cy, r, true, true)
    else
        smudge.circle(cx, cy, r, true, false)
        smudge.circle(cx, cy, r, false, true, 3)
    end
end

local function statusText()
    local g = S.game
    if g.result then
        local nb, nw = L.counts(g.b)
        local hi, lo = math.max(nb, nw), math.min(nb, nw)
        if g.result == "draw" then return "Draw, " .. nb .. "-" .. nw end
        local winner = g.result == "black" and L.BLACK or L.WHITE
        if S.vsDevice() then
            return winner == S.human and ("You win " .. hi .. "-" .. lo .. "!") or ("The device wins " .. hi .. "-" .. lo)
        end
        return (winner == L.BLACK and "Black" or "White") .. " wins " .. hi .. "-" .. lo .. "!"
    end
    if S.thinking then return "Device is thinking..." end
    if S.note then return S.note end
    if S.vsDevice() then return "Your move" end
    return (g.side == L.BLACK and "Black" or "White") .. " to move"
end

-- A disc, its count and whose it is; the side to move has a thick frame.
local function drawScore(x, y, side, count)
    local g = S.game
    local moving = not g.result and g.side == side
    smudge.rounded_rect(x, y, 140, 40, 8, false, true, moving and 4 or 1)
    drawDisc(x + 22, y + 20, 12, side)
    smudge.text(x + 42, y + 8, tostring(count), "ui12", "bold", "left", true)
    local who = S.vsDevice() and (side == S.human and "You" or "Device") or (side == L.BLACK and "Black" or "White")
    smudge.text(x + 132, y + 11, who, "ui10", "regular", "right", true)
end

function V.draw()
    local g = S.game
    layout()
    smudge.header("Reversi", S.vsDevice() and S.LEVELS[S.mode] or "2 players")
    local t = top()
    local nb, nw = L.counts(g.b)
    drawScore(16, t, L.BLACK, nb)
    drawScore(166, t, L.WHITE, nw)
    if S.touch then
        smudge.rounded_rect(S.w - 96, t, 80, 40, 6, false, true, 2)
        smudge.text(S.w - 56, t + 8, "Menu", "ui12", "bold", "center", true)
    end
    local text = statusText()
    smudge.text(16, t + 50, text, smudge.text_width(text, "ui12", "bold") > S.w - 32 and "ui10" or "ui12", "bold",
                "left", true)

    -- The grid, its coordinates and the four small marks.
    local c, x0, y0 = B.cell, B.x, B.y
    local size = 8 * c
    for k = 1, 7 do
        smudge.line(x0 + k * c, y0, x0 + k * c, y0 + size, 1, true)
        smudge.line(x0, y0 + k * c, x0 + size, y0 + k * c, 1, true)
    end
    smudge.rect(x0, y0, size, size, false, true, 3)
    for k = 2, 6, 4 do
        for j = 2, 6, 4 do smudge.circle(x0 + k * c, y0 + j * c, 3, true, true) end
    end
    for k = 1, 8 do
        smudge.text(x0 - 12, y0 + (k - 1) * c + (c - 18) // 2, tostring(k), "ui10", "regular", "center", true)
        smudge.text(x0 + (k - 1) * c + c // 2, y0 + size + 4, string.char(96 + k), "ui10", "regular", "center", true)
    end

    -- Discs, the last move, the legal squares and the cursor.
    local R, b = c * 40 // 100, g.b
    for sq = 11, 88 do
        local v = b[sq]
        if v == L.BLACK or v == L.WHITE then
            local x, y = cellXY(sq)
            drawDisc(x + c // 2, y + c // 2, R, v)
        end
    end
    if g.last then
        local x, y = cellXY(g.last)
        smudge.rect(x + c // 2 - 4, y + c // 2 - 4, 8, 8, true, b[g.last] ~= L.BLACK)
    end
    local mine = humanTurn()
    if mine and S.hints then
        for i = 1, S.nlegal do
            local x, y = cellXY(S.legal[i])
            smudge.circle(x + c // 2, y + c // 2, 4, true, true)
        end
    end
    if mine and not S.touch then
        local x, y = cellXY(S.cur)
        smudge.rect(x + 3, y + 3, c - 6, c - 6, false, true, 3)
    end

    -- The last move and any pass under the board.
    local y = y0 + size + 28
    if S.info then
        smudge.centered_text(y, S.info, "ui10", "bold", true)
        y = y + 24
    end
    if S.passInfo then smudge.centered_text(y, S.passInfo, "ui10", "bold", true) end

    if g.result then smudge.popup(text) end
    if S.touch then
        smudge.button_hints("", "", "", "")
    elseif g.result then
        smudge.button_hints("Menu", "Again", "", "")
    elseif S.hints then
        smudge.button_hints("Menu", "Play", "<", ">")
    else
        smudge.button_hints("Menu", "Play", "Left", "Right")
    end
end

-- Confirm or a tap on a square.
local function activate(sq)
    if not humanTurn() then return end
    if L.canPlay(S.game.b, sq, S.game.side) then
        S.play(sq)
        return
    end
    S.note = S.game.b[sq] == L.EMPTY and "That square flips nothing" or "That square is taken"
    smudge.request_update()
end

-- With moves shown: the previous/next legal square in reading order.
local function stepLegal(dir)
    local n = S.nlegal
    if n == 0 then return end
    local at = 0
    for i = 1, n do
        if S.legal[i] == S.cur then at = i end
    end
    if at == 0 then at = dir > 0 and 0 or 1 end
    S.cur = S.legal[(at - 1 + dir) % n + 1]
end

-- With moves shown: the nearest legal square in the nearest row above
-- (dir -1) or below (dir 1) that has one, wrapping round.
local function rowLegal(dir)
    local n = S.nlegal
    if n == 0 then return end
    local r0, c0 = S.cur // 10, S.cur % 10
    for step = 1, 8 do
        local r = (r0 - 1 + dir * step) % 8 + 1
        local best, bestD = nil, 99
        for i = 1, n do
            local sq = S.legal[i]
            if sq // 10 == r then
                local d = math.abs(sq % 10 - c0)
                if d < bestD then best, bestD = sq, d end
            end
        end
        if best then
            S.cur = best
            return
        end
    end
end

function V.button(btn)
    if btn == "back" then
        S.show("menu")
        return
    end
    local g = S.game
    if g.result then
        if btn == "confirm" then
            S.newGame(S.mode)
            smudge.request_update()
        end
        return
    end
    if btn == "confirm" then
        activate(S.cur)
        return
    end
    local r, c = S.cur // 10, S.cur % 10
    local left = btn == "left"
    local right = btn == "right"
    local up = btn == "up" or btn == "page_back"
    local down = btn == "down" or btn == "page_forward"
    if S.hints then
        if left or right then stepLegal(left and -1 or 1)
        elseif up or down then rowLegal(up and -1 or 1)
        else return end
    else
        -- Free movement: Left/Right along the rows (into the next row at the
        -- end), Up/Down along the columns, wrapping round.
        local i = (r - 1) * 8 + c - 1
        if left then i = (i - 1) % 64
        elseif right then i = (i + 1) % 64
        elseif up then i = (i - 8) % 64
        elseif down then i = (i + 8) % 64
        else return end
        S.cur = (i // 8 + 1) * 10 + i % 8 + 1
    end
    S.note = nil
    smudge.request_update()
end

function V.tap(x, y)
    if smudge.in_rect(x, y, S.w - 96, top(), 80, 40) then
        S.show("menu")
        return
    end
    layout()
    local g = S.game
    if g.result then
        if smudge.in_rect(x, y, 0, B.y, S.w, 8 * B.cell + 60) then
            S.newGame(S.mode)
            smudge.request_update()
        end
        return
    end
    if not smudge.in_rect(x, y, B.x, B.y, 8 * B.cell, 8 * B.cell) then return end
    local sq = ((y - B.y) // B.cell + 1) * 10 + (x - B.x) // B.cell + 1
    S.cur = sq
    activate(sq)
end

return V
end
