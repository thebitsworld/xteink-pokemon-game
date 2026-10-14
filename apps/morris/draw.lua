-- Drawing the Nine Men's Morris board screen. Usage:
--   local screen = smudge.dofile("draw.lua")(C, S)
-- with C the rules (logic.lua) and S the app's shared state (main.lua). The
-- screen's input is input.lua; main.lua holds only one of the two (or the
-- computer while it thinks) at a time. Drawing leaves the board's geometry
-- in S.B ({ x, y, c }: point 1 and the spacing) for input.lua.
--
-- The board fills the screen's width; Black's row (its pieces still to
-- place) is above it, White's below, the side to move framed. White pieces
-- are drawn outlined, Black ones filled. The last move: a small square where
-- the piece came from, a box where it went, a cross where a piece was taken.
-- Taking a piece after a mill: the pieces that may go are crossed and ringed.

return function(C, S)
local V = {}
local B = {} -- geometry: x, y (point 1), c (spacing), R (piece radius), rows

local function pointName(p)
    local x, y = C.xy(p)
    return string.char(97 + x) .. (7 - y)
end

-- "d6", "a7-d7", "a7-d7 xg1".
function V.describe(mv)
    local from, to, rem = mv % 32, (mv // 32) % 32, mv // 1024
    local s = from > 0 and (pointName(from) .. "-" .. pointName(to)) or pointName(to)
    if rem > 0 then s = s .. " x" .. pointName(rem) end
    return s
end

local function layout()
    local top = S.m.top_padding + S.m.header_height + 42
    local bottom = S.h - S.m.button_hints_height - 40
    local rowH, gap = 38, 8
    -- The rows, the gaps and the board (6 spacings plus a piece each end).
    local room = bottom - top - 2 * (rowH + gap)
    local c = math.min((S.w - 88) // 6, room * 10 // 70)
    local R = c * 30 // 100
    local y0 = top + (room - 6 * c - 2 * R) // 2
    B.c, B.R = c, R
    B.x = (S.w - 6 * c) // 2
    B.rowB = y0
    B.y = y0 + rowH + gap + R
    B.rowW = B.y + 6 * c + R + gap
    S.B = B
end

local function px(p)
    local x, y = C.xy(p)
    return B.x + x * B.c, B.y + y * B.c
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

local function playerName(c)
    local name = c == C.WHITE and "White" or "Black"
    if S.vsDevice() then name = name .. (c == S.human and " (you)" or " (device)") end
    return name
end

local function playerRow(c, y)
    local g = S.game
    local h = 38
    if not g.result and g.side == c then smudge.rounded_rect(8, y, S.w - 16, h, 8, false, true, 3) end
    piece(30, y + h // 2, 10, c)
    local label = playerName(c)
    smudge.text(48, y + 5, label, "ui12", "bold", "left", true)
    local lx = 48 + smudge.text_width(label, "ui12", "bold") + 10
    local onBoard = g.on[c] .. " on board"
    smudge.text(lx, y + 10, onBoard, "small", "regular", "left", true)
    -- The pieces still to place, from the right, closer and smaller when
    -- they would reach the text (nine of them on a 480-pixel screen).
    local n = g.hand[c]
    if n > 0 then
        local room = S.w - 30 - (lx + smudge.text_width(onBoard, "small", "regular") + 18)
        local step = n > 1 and math.min(21, room // (n - 1)) or 21
        local r = step >= 21 and 9 or math.max(4, step // 2 - 1)
        for i = 1, n do piece(S.w - 30 - (i - 1) * step, y + h // 2, r, c) end
    end
end

local function statusText()
    local g = S.game
    if g.result then
        if g.result == "draw" then return "Draw: " .. g.reason end
        local how = g.reason == "blocked" and " (no moves left)" or " (two pieces left)"
        local white = g.result == "white"
        if S.vsDevice() then
            return (white == (S.human == C.WHITE)) and "You win!" or ("The device wins" .. how)
        end
        return (white and "White" or "Black") .. " wins" .. how
    end
    if S.thinking then return "Device is thinking..." end
    local who = S.vsDevice() and "" or ((g.side == C.WHITE and "White" or "Black") .. ": ")
    if S.pending then return who .. "Mill! Take a piece" end
    if S.deviceMoved and g.last then return "Device: " .. V.describe(g.last) end
    if S.note then return S.note end
    local ph = C.phase(g, g.side)
    local act = ph == 1 and "place a piece" or (ph == 3 and "fly a piece" or "move a piece")
    if S.vsDevice() then return "Your turn: " .. act end
    return who .. act
end

-- The move from S.picked (0 when placing) to S.pending, for each removal.
local function pendingFrom() return S.picked or 0 end

function V.draw()
    local g = S.game
    layout()
    smudge.header("Nine Men's Morris", S.vsDevice() and S.LEVELS[S.mode].name or "2 players")
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
    playerRow(C.BLACK, B.rowB)
    playerRow(C.WHITE, B.rowW)
    -- The board: three squares and the four joins.
    local c, R = B.c, B.R
    for k = 0, 2 do
        smudge.rect(B.x + k * c, B.y + k * c, (6 - 2 * k) * c, (6 - 2 * k) * c, false, true, 3)
    end
    local mx, my = B.x + 3 * c, B.y + 3 * c
    smudge.line(mx, B.y, mx, B.y + 2 * c, 3, true)
    smudge.line(mx, B.y + 4 * c, mx, B.y + 6 * c, 3, true)
    smudge.line(B.x, my, B.x + 2 * c, my, 3, true)
    smudge.line(B.x + 4 * c, my, B.x + 6 * c, my, 3, true)
    -- The pieces, with a move in progress shown as made.
    local b = g.b
    local from, pend = pendingFrom(), S.pending
    for p = 1, 24 do
        local x, y = px(p)
        local v = b[p]
        if pend then
            if p == from then v = 0 elseif p == pend then v = g.side end
        end
        if v == 0 then
            smudge.circle(x, y, 6, true, true)
        else
            piece(x, y, R, v)
        end
    end
    -- The last move.
    if g.last and not S.picked and not pend then
        local lf, lt, lr = g.last % 32, (g.last // 32) % 32, g.last // 1024
        if lf > 0 then
            local x, y = px(lf)
            smudge.rect(x - 9, y - 9, 18, 18, false, true, 2)
        end
        local x, y = px(lt)
        smudge.rect(x - R - 4, y - R - 4, 2 * R + 8, 2 * R + 8, false, true, 2)
        if lr > 0 then
            x, y = px(lr)
            smudge.line(x - 12, y - 12, x + 12, y + 12, 3, true)
            smudge.line(x - 12, y + 12, x + 12, y - 12, 3, true)
        end
    end
    if pend then
        -- The pieces the mill may take.
        local x, y = px(pend)
        smudge.rect(x - R - 4, y - R - 4, 2 * R + 8, 2 * R + 8, false, true, 3)
        for _, mv in ipairs(S.legal) do
            if mv % 32 == from and (mv // 32) % 32 == pend and mv >= 1024 then
                local q = mv // 1024
                local x, y = px(q)
                local ink = b[q] == C.WHITE
                local d = R * 6 // 10
                smudge.line(x - d, y - d, x + d, y + d, 3, ink)
                smudge.line(x - d, y + d, x + d, y - d, 3, ink)
                smudge.circle(x, y, R + 5, false, true, 3)
            end
        end
    elseif S.picked then
        local x, y = px(S.picked)
        smudge.rect(x - R - 4, y - R - 4, 2 * R + 8, 2 * R + 8, false, true, 4)
        for _, mv in ipairs(S.legal) do
            if mv % 32 == S.picked then
                local tx, ty = px((mv // 32) % 32)
                smudge.circle(tx, ty, 13, false, true, 3)
            end
        end
    end
    if not S.touch and humanTurn() then
        local x, y = px(S.cur)
        local e = R + 9
        smudge.rect(x - e - 2, y - e - 2, 2 * e + 4, 2 * e + 4, false, false, 2)
        smudge.rect(x - e, y - e, 2 * e, 2 * e, false, true, 3)
    end
    -- Move number and the last move under the board.
    if g.last then
        local n = #g.history // 3
        local who = (n % 2 == 1) and "White" or "Black"
        smudge.centered_text(B.rowW + 44, n .. ". " .. who .. " " .. V.describe(g.last), "ui10", "bold", true)
    end
    if g.result then smudge.popup(text) end
    if S.touch then
        smudge.button_hints("", "", "", "")
    elseif g.result then
        smudge.button_hints("Menu", "Again", "", "")
    else
        local act = "Place"
        if S.pending then act = "Take"
        elseif S.picked then act = "Move"
        elseif C.phase(g, g.side) ~= 1 then act = "Pick" end
        smudge.button_hints((S.picked or S.pending) and "Cancel" or "Menu", act, "<", ">")
    end
end

return V
end
