-- The Mancala board screen: drawing and input. Usage:
--   local screen = smudge.dofile("board.lua")(C, S)
-- with C the rules (logic.lua) and S the app's shared state (main.lua).
--
-- The board runs across the screen: the top row (player 2, the device) over
-- the bottom row (player 1, you), the device's store at the left end and
-- yours at the right. Seeds are dots (a number past 10) and every count is
-- also printed outside the board. E-ink does not animate, so after a move the
-- pit it started from carries an arrow, the pits it changed are framed, the
-- ones a capture emptied are shaded, and a few lines tell what happened.

return function(C, S)
local V = {}

local B = {}

local function layout()
    local w = S.w
    local top = S.m.top_padding + S.m.header_height + 6
    B.top = top
    B.cw = (w - 16) * 10 // 75
    B.sw = (w - 16 - 6 * B.cw) // 2
    B.ch = B.cw + 24
    B.x = (w - 2 * B.sw - 6 * B.cw) // 2
    B.y = top + 104
    B.bottom = B.y + 2 * B.ch
    B.textY = B.bottom + 96
end

-- Rectangle of pit or store p.
local function rectOf(p)
    if p == 14 then return B.x, B.y, B.sw, 2 * B.ch end
    if p == 7 then return B.x + B.sw + 6 * B.cw, B.y, B.sw, 2 * B.ch end
    if p <= 6 then return B.x + B.sw + (p - 1) * B.cw, B.y + B.ch, B.cw, B.ch end
    return B.x + B.sw + (13 - p) * B.cw, B.y, B.cw, B.ch
end

local function humanTurn()
    local s = S.s
    return s[C.OVER] == 0 and not S.thinking and (not S.vsDevice() or s[C.SIDE] == 1)
end

-- Drawing ----------------------------------------------------------------

-- n seeds as dots around (cx, cy), or a number past 10.
local function seeds(cx, cy, n, store)
    if n == 0 then return end
    if n > 10 then
        if not store then smudge.circle(cx, cy, 18, false, 2, true) end
        smudge.text(cx, cy - smudge.line_height("ui12") // 2, tostring(n), "ui12", "bold", "center", true)
        return
    end
    local cols = n == 1 and 1 or (n <= 4 and 2 or 3)
    local rows = (n + cols - 1) // cols
    local sp = 14
    local y = cy - (rows - 1) * sp // 2
    local k = 0
    for _ = 1, rows do
        local inRow = math.min(cols, n - k)
        local x = cx - (inRow - 1) * sp // 2
        for _ = 1, inRow do
            smudge.circle(x, y, 5, true, 1, true)
            x = x + sp
        end
        k = k + inRow
        y = y + sp
    end
end

local function bit(mask, p) return (mask >> (p - 1)) & 1 == 1 end

local function drawPit(p, info, marks)
    local s = S.s
    local x, y, w, h = rectOf(p)
    local store = p == 7 or p == 14
    local r = store and 20 or 12
    local captured = marks and info.cap > 0 and (p == info.capPit or p == 14 - info.capPit)
    if captured then smudge.rect_dither(x + 6, y + 6, w - 12, h - 12, false) end
    smudge.rounded_rect(x + 2, y + 2, w - 4, h - 4, r, false, store and 3 or 2, true)
    seeds(x + w // 2, y + h // 2, s[p], store)
    if marks and bit(info.changed, p) and not captured then
        smudge.rounded_rect(x + 6, y + 6, w - 12, h - 12, r - 4, false, 2, true)
    end
    -- The count outside the board: the top row and the device's store above,
    -- the rest below.
    local lh = smudge.line_height("small")
    local ty = (p >= 8) and B.y - lh - 4 or B.bottom + 4
    smudge.text(x + w // 2, ty, tostring(s[p]), "small", store and "bold" or "regular", "center", true)
end

-- An arrow in the pit a move started from, pointing the way it sowed.
local function startArrow(p)
    local x, y, w = rectOf(p)
    local cx, ay = x + w // 2, y + 16
    if p <= 6 then
        smudge.triangle(cx - 8, ay - 7, cx - 8, ay + 7, cx + 8, ay, true)
    else
        smudge.triangle(cx + 8, ay - 7, cx + 8, ay + 7, cx - 8, ay, true)
    end
end

local function resultText()
    local s = S.s
    local a, b = s[7], s[14]
    local w = C.winner(s)
    if w == 0 then return "Draw, " .. a .. " to " .. b end
    local hi, lo = math.max(a, b), math.min(a, b)
    if S.vsDevice() then return (w == 1 and "You win " or "The device wins ") .. hi .. " to " .. lo end
    return S.name(w) .. " wins " .. hi .. " to " .. lo
end

local function statusText()
    local s = S.s
    if s[C.OVER] == 1 then return resultText() end
    if S.thinking then return "Device is thinking..." end
    local again = S.lastSide == s[C.SIDE] and S.info.extra and " again" or ""
    if S.vsDevice() then return "Your move" .. again end
    return S.name(s[C.SIDE]) .. " to move" .. again
end

-- What the last move did, a few short lines.
local function summary(lines)
    local i = S.info
    local who = S.name(S.lastSide)
    local t = who .. " sowed pit " .. S.col(i.from)
    if i.extra then t = t .. ": extra turn" end
    lines[#lines + 1] = t
    if i.cap > 0 then
        t = who .. " captured " .. i.cap
        if i.cap == 1 then t = t .. " (empty capture)" end
        lines[#lines + 1] = t
    end
    if S.s[C.OVER] == 1 then
        lines[#lines + 1] = "A side is empty: the game is over"
        lines[#lines + 1] = "Seeds left to the stores: " .. S.name(1) .. " +" .. i.sweep1 .. ", " .. S.name(2) .. " +"
            .. i.sweep2
    end
end

function V.draw()
    layout()
    local s = S.s
    local w = S.w
    smudge.header("Mancala", S.vsDevice() and S.LEVELS[S.mode].name or "2 players")
    local top = B.top
    local text = statusText()
    smudge.text(16, top + 6, text, "ui12", "bold", "left", true)
    if S.touch then
        smudge.rounded_rect(w - 96, top, 80, 34, 6, false, 2)
        smudge.text(w - 56, top + 6, "Menu", "ui12", "bold", "center", true)
    end
    local mover = s[C.SIDE]
    local over = s[C.OVER] == 1
    for side = 2, 1, -1 do
        local y = side == 2 and B.y - 58 or B.bottom + 52
        local t = S.name(side) .. ": " .. s[C.STORE[side]] .. " in store, " .. C.onSide(s, side) .. " on side"
        if not over and side == mover then t = "> " .. t .. " <" end
        smudge.centered_text(y, t, "ui10", side == mover and not over and "bold" or "regular", true)
    end

    local marks = S.lastSide > 0
    for p = 1, 14 do drawPit(p, S.info, marks) end
    if marks then startArrow(S.info.from) end

    -- The pointer (buttons): a triangle outside the board at the pit.
    if humanTurn() and not S.touch then
        local x, y, cw = rectOf(S.cursor)
        local cx = x + cw // 2
        if S.cursor <= 6 then
            local py = B.bottom + 26
            smudge.triangle(cx, py, cx - 10, py + 14, cx + 10, py + 14, true)
        else
            local py = B.y - 26
            smudge.triangle(cx, py, cx - 10, py - 14, cx + 10, py - 14, true)
        end
    end

    -- What happened.
    local y = B.textY
    local lh = smudge.line_height("ui10") + 4
    for _, line in ipairs(S.prev) do
        smudge.text(24, y, line, "ui10", "regular", "left", true)
        y = y + lh
    end
    if S.note then
        smudge.text(24, y, S.note, "ui10", "bold", "left", true)
        y = y + lh
    end
    if S.lastSide > 0 then
        local lines = {}
        summary(lines)
        for k, line in ipairs(lines) do
            smudge.text(24, y, line, "ui10", k == 1 and "bold" or "regular", "left", true)
            y = y + lh
        end
    end
    y = y + 12
    if over then
        smudge.rounded_rect(24, y, w - 48, 40, 8, false, 3)
        smudge.text(w // 2, y + 8, S.touch and "Tap to play again" or "Confirm to play again", "ui10", "bold",
                    "center", true)
    elseif humanTurn() then
        smudge.text(24, y, S.touch and "Tap one of your pits to sow it" or "Left/Right pick a pit, Confirm sows it",
                    "ui10", "regular", "left", true)
    end

    if S.touch then
        smudge.button_hints("", "", "", "")
    elseif over then
        smudge.button_hints("Menu", "Again", "", "")
    else
        smudge.button_hints("Menu", "Sow", "<", ">")
    end
end

-- Input ------------------------------------------------------------------

-- The next non-empty pit of the player to move, `dir` columns on screen.
local function moveCursor(dir)
    local s = S.s
    local side = s[C.SIDE]
    local col = S.col(S.cursor)
    local p = S.cursor
    for _ = 1, 6 do
        col = (col - 1 + dir) % 6 + 1
        p = side == 1 and col or 14 - col
        if s[p] > 0 then break end
    end
    S.cursor = p
end

function V.button(btn)
    local s = S.s
    if btn == "back" then
        S.show("menu")
        return
    end
    if s[C.OVER] == 1 then
        if btn == "confirm" then
            S.newGame(S.mode)
            smudge.request_update()
        end
        return
    end
    if not humanTurn() then return end
    if btn == "confirm" then
        if C.isLegal(s, S.cursor) then S.play(S.cursor) end
        return
    elseif btn == "left" or btn == "up" or btn == "page_back" then
        moveCursor(-1)
    elseif btn == "right" or btn == "down" or btn == "page_forward" then
        moveCursor(1)
    else
        return
    end
    smudge.request_update()
end

function V.tap(x, y)
    layout()
    local s = S.s
    if smudge.in_rect(x, y, S.w - 96, B.top, 80, 34) then
        S.show("menu")
        return
    end
    if s[C.OVER] == 1 then
        if y > B.top + 40 then
            S.newGame(S.mode)
            smudge.request_update()
        end
        return
    end
    if not humanTurn() then return end
    local f = C.first(s[C.SIDE])
    for p = f, f + 5 do
        local rx, ry, rw, rh = rectOf(p)
        if smudge.in_rect(x, y, rx, ry, rw, rh) then
            if s[p] > 0 then
                S.cursor = p
                S.play(p)
            end
            return
        end
    end
end

return V
end
