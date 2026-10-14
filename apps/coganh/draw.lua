-- Drawing the Co Ganh board screen. Usage:
--   local screen = smudge.dofile("draw.lua")(C, S)
-- with C the rules (logic.lua) and S the app's shared state (main.lua). The
-- screen's input is input.lua; main.lua holds only one of the two (or the
-- computer while it thinks) at a time. Drawing leaves the board's geometry
-- in S.B ({ x, y, c }: the top-left point and the spacing) for input.lua.
--
-- The board fills the screen's width, with the opponent's row above it and
-- yours below (against the device as Black the board is turned round), the
-- side to move framed. White pieces are drawn outlined, Black ones filled.
-- The last move: a small square where the piece came from, a box where it
-- went, and a ring and a solid centre on every piece it converted.

return function(C, S)
local V = {}
local B = {} -- geometry: x, y (top-left point), c (spacing), R (piece radius), rows

-- Point names as seen from White's side: columns a..e, rows 1..5 from White.
local function pointName(p)
    local r, c = C.rc(p)
    return string.char(97 + c) .. (5 - r)
end
V.pointName = pointName

-- "c2-c3".
function V.describe(mv) return pointName(mv % 32) .. "-" .. pointName(mv // 32) end

-- What the last move converted: ": ganh 2, surrounded 1", or "".
function V.gains(g)
    local t = ""
    if g.ganh > 0 then t = "ganh " .. g.ganh end
    if g.vay > 0 then t = t .. (t ~= "" and ", " or "") .. "surrounded " .. g.vay end
    return t ~= "" and (": " .. t) or ""
end

local function flipped() return S.vsDevice() and S.human == C.BLACK end

local function layout()
    local top = S.m.top_padding + S.m.header_height + 46
    local bottom = S.h - S.m.button_hints_height - 40
    local rowH, gap = 38, 10
    local room = bottom - top - 2 * (rowH + gap)
    local c = math.min((S.w - 100) // 4, room * 10 // 54)
    local R = c * 30 // 100
    local y0 = top + (room - 4 * c - 2 * R) // 2
    B.c, B.R = c, R
    B.x = (S.w - 4 * c) // 2
    B.rowTop = y0
    B.y = y0 + rowH + gap + R
    B.rowBottom = B.y + 4 * c + R + gap
    B.flip = flipped()
    S.B = B
end

local function px(p)
    local r, c = C.rc(p)
    if B.flip then r, c = 4 - r, 4 - c end
    return B.x + c * B.c, B.y + r * B.c
end

local function humanTurn()
    local g = S.game
    return not g.result and not S.thinking and (not S.vsDevice() or g.side == S.human)
end

-- Draws a piece of colour c at x, y (radius r).
local function piece(x, y, r, c)
    if c == C.WHITE then
        smudge.circle(x, y, r, true, false)
        smudge.circle(x, y, r, false, true, 3)
        smudge.circle(x, y, r * 45 // 100, false, true, 1)
    else
        smudge.circle(x, y, r, true, true)
        smudge.circle(x, y, r * 45 // 100, false, false, 1)
    end
end

local function colourName(c) return c == C.WHITE and "White" or "Black" end

local function playerName(c)
    local name = colourName(c)
    if S.vsDevice() then name = name .. (c == S.human and " (you)" or " (device)") end
    return name
end

local function playerRow(c, y)
    local g = S.game
    local h = 38
    if not g.result and g.side == c then smudge.rounded_rect(8, y, S.w - 16, h, 8, false, true, 3) end
    piece(30, y + h // 2, 10, c)
    smudge.text(48, y + 5, playerName(c), "ui12", "bold", "left", true)
    local n = g.on[c]
    smudge.text(S.w - 24, y + 8, n .. (n == 1 and " piece" or " pieces"), "ui10", "bold", "right", true)
end

local function statusText()
    local g = S.game
    if g.result then
        if g.result == "draw" then return "Draw: " .. g.reason end
        local white = g.result == "white"
        if S.vsDevice() then return (white == (S.human == C.WHITE)) and "You win!" or "The device wins" end
        return (white and "White" or "Black") .. " wins"
    end
    if S.thinking then return "Device is thinking..." end
    if S.deviceMoved and g.last then return "Device " .. V.describe(g.last) .. V.gains(g) end
    if S.note then return S.note end
    local who = S.vsDevice() and "Your turn" or (colourName(g.side) .. " to move")
    local f = C.forcedPoint(g)
    if f > 0 then return who .. ": must ganh at " .. pointName(f) end
    return who
end

function V.draw()
    local g = S.game
    layout()
    smudge.header("Co Ganh", S.vsDevice() and S.LEVELS[S.mode].name or "2 players")
    local top = S.m.top_padding + S.m.header_height + 6
    local text = statusText()
    local bx = S.w - 96
    local maxW = (S.touch and bx or S.w) - 24
    smudge.text(16, top + 6, text, smudge.text_width(text, "ui12", "bold") > maxW and "ui10" or "ui12", "bold", "left",
                true)
    if S.touch then
        smudge.rounded_rect(bx, top, 80, 34, 6, false, true, 2)
        smudge.text(bx + 40, top + 6, "Menu", "ui12", "bold", "center", true)
    end
    local low = B.flip and C.BLACK or C.WHITE
    playerRow(3 - low, B.rowTop)
    playerRow(low, B.rowBottom)
    -- The board: the grid, the long diagonals and the diamond.
    local x0, y0, c, R = B.x, B.y, B.c, B.R
    local x4, y4 = x0 + 4 * c, y0 + 4 * c
    for k = 0, 4 do
        smudge.line(x0, y0 + k * c, x4, y0 + k * c, 3, true)
        smudge.line(x0 + k * c, y0, x0 + k * c, y4, 3, true)
    end
    smudge.line(x0, y0, x4, y4, 3, true)
    smudge.line(x4, y0, x0, y4, 3, true)
    local mx, my = x0 + 2 * c, y0 + 2 * c
    smudge.line(mx, y0, x0, my, 3, true)
    smudge.line(x0, my, mx, y4, 3, true)
    smudge.line(mx, y4, x4, my, 3, true)
    smudge.line(x4, my, mx, y0, 3, true)
    -- The pieces.
    local b = g.b
    for p = 1, 25 do
        local x, y = px(p)
        if b[p] == 0 then
            smudge.circle(x, y, 6, true, true)
        else
            piece(x, y, R, b[p])
        end
    end
    -- The last move and what it converted.
    if g.last and not S.picked then
        local x, y = px(g.last % 32)
        smudge.rect(x - 9, y - 9, 18, 18, false, true, 2)
        x, y = px(g.last // 32)
        smudge.rect(x - R - 5, y - R - 5, 2 * R + 10, 2 * R + 10, false, true, 2)
        for _, q in ipairs(g.conv or {}) do
            -- A ring round it and a solid centre in the other colour.
            x, y = px(q)
            smudge.circle(x, y, R + 4, false, false, 3)
            smudge.circle(x, y, R + 8, false, true, 3)
            smudge.circle(x, y, R * 40 // 100, true, b[q] ~= C.BLACK)
        end
    end
    if S.picked then
        local x, y = px(S.picked)
        smudge.rect(x - R - 5, y - R - 5, 2 * R + 10, 2 * R + 10, false, true, 4)
        for _, mv in ipairs(S.legal) do
            if mv % 32 == S.picked then
                local tx, ty = px(mv // 32)
                smudge.circle(tx, ty, R * 6 // 10, false, true, 3)
            end
        end
    end
    if not S.touch and humanTurn() then
        local x, y = px(S.cur)
        local e = R + 11
        smudge.rect(x - e - 2, y - e - 2, 2 * e + 4, 2 * e + 4, false, false, 2)
        smudge.rect(x - e, y - e, 2 * e, 2 * e, false, true, 3)
    end
    -- Move number and the last move under the board.
    if g.last then
        local n = #g.history // 2
        local who = (n % 2 == 1) and "White" or "Black"
        smudge.centered_text(B.rowBottom + 46, n .. ". " .. who .. " " .. V.describe(g.last) .. V.gains(g), "ui10",
                             "bold", true)
    end
    if g.result then smudge.popup(text) end
    if S.touch then
        smudge.button_hints("", "", "", "")
    elseif g.result then
        smudge.button_hints("Menu", "Again", "", "")
    else
        smudge.button_hints(S.picked and "Cancel" or "Menu", S.picked and "Move" or "Pick", "<", ">")
    end
end

return V
end
