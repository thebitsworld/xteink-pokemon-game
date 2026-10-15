-- Drawing the Code Breaker board screen. Usage:
--   local screen = smudge.dofile("draw.lua")(L, S)
-- with L the rules (logic.lua) and S the app's state (main.lua). Its input
-- is input.lua; main.lua holds only one of the two at a time. Drawing leaves
-- the geometry in S.B for input.lua.
--
-- From the top: the status line, the code row (hidden until the end when you
-- break the device's code), one row per guess with its pegs, and below them
-- the symbol palette and Submit (entering a guess or your code), the score
-- buttons (scoring the device's guess by hand) or one big button.
-- Pegs: black = a filled dot, white = a ring; a tiny dot is an empty hole.

return function(L, S)
local V = {}
local B = {} -- geometry, worked out by layout()

local function g() return S.game end

local function layout()
    local game = g()
    local top = S.m.top_padding + S.m.header_height
    B.statusY = top + 4
    B.bottomH = 176
    B.bottomTop = S.h - S.m.button_hints_height - 8 - B.bottomH
    B.codeY = top + 50
    local area = B.bottomTop - 8 - B.codeY - 6
    B.rowH = math.min(50, area // (game.max + 1))
    B.rowsY = B.codeY + B.rowH + 6
    B.cw = math.min(60, (S.w - 40 - 34 - 62) // game.n)
    local rowW = 34 + game.n * B.cw + 62
    B.x = (S.w - rowW) // 2
    B.cellX = B.x + 34
    B.pegX = B.cellX + game.n * B.cw + 12
    B.r = math.min(B.cw, B.rowH) * 34 // 100
    -- The palette and the big button.
    B.pw = math.min(58, (S.w - 24) // game.k)
    B.palX = (S.w - B.pw * game.k) // 2
    B.palY = B.bottomTop + 4
    B.palH = 62
    B.btnX, B.btnW = 24, S.w - 48
    B.btnY, B.btnH = B.bottomTop + B.bottomH - 56, 54
    -- The score buttons (black row, white row).
    B.sw = math.min(56, (S.w - 24 - 92) // (game.n + 1))
    B.sx = S.w - 12 - B.sw * (game.n + 1)
    B.sy1, B.sy2, B.sh = B.bottomTop + 2, B.bottomTop + 58, 48
    S.B = B
end

-- Symbol s (1..8) centred at cx, cy, about 2r across, in ink colour.
local function sym(s, cx, cy, r, ink)
    if s == 1 then
        smudge.circle(cx, cy, r, true, ink)
    elseif s == 2 then
        smudge.circle(cx, cy, r, false, ink, math.max(3, r // 4))
    elseif s == 3 then
        local a = r * 88 // 100
        smudge.rect(cx - a, cy - a, 2 * a, 2 * a, true, ink)
    elseif s == 4 then
        local a = r * 88 // 100
        smudge.rect(cx - a, cy - a, 2 * a, 2 * a, false, ink, math.max(3, r // 4))
    elseif s == 5 then
        smudge.triangle(cx, cy - r, cx - r, cy + r * 8 // 10, cx + r, cy + r * 8 // 10, true, ink)
    elseif s == 6 then
        local a, t = r * 82 // 100, math.max(4, r // 3)
        smudge.line(cx - a, cy - a, cx + a, cy + a, t, ink)
        smudge.line(cx - a, cy + a, cx + a, cy - a, t, ink)
    elseif s == 7 then
        local d, e = r * 87 // 100, r // 2
        smudge.triangle(cx, cy - r, cx - d, cy + e, cx + d, cy + e, true, ink)
        smudge.triangle(cx, cy + r, cx - d, cy - e, cx + d, cy - e, true, ink)
    elseif s == 8 then
        local t = math.max(3, r // 4)
        smudge.line(cx, cy - r, cx + r, cy, t, ink)
        smudge.line(cx + r, cy, cx, cy + r, t, ink)
        smudge.line(cx, cy + r, cx - r, cy, t, ink)
        smudge.line(cx - r, cy, cx, cy - r, t, ink)
    else
        smudge.circle(cx, cy, 2, true, ink) -- an empty position
    end
end
V.sym = sym

local function cellCenter(i, y) return B.cellX + (i - 1) * B.cw + B.cw // 2, y + B.rowH // 2 end

-- A row of symbols from a code string (or S.row when code is nil).
local function symbols(code, y, n)
    for i = 1, n do
        local s = code and (code:byte(i) - 48) or S.row[i]
        local cx, cy = cellCenter(i, y)
        sym(s, cx, cy, B.r, true)
    end
end

local function pegs(b, w, y, n)
    local cols = (n + 1) // 2
    local dy = math.min(9, B.rowH // 4)
    for p = 0, n - 1 do
        local cx = B.pegX + (p % cols) * 16 + 6
        local cy = y + B.rowH // 2 + (p // cols == 0 and -dy or dy)
        if p < b then
            smudge.circle(cx, cy, 6, true, true)
        elseif p < b + w then
            smudge.circle(cx, cy, 6, false, true, 2)
        else
            smudge.circle(cx, cy, 1, true, true)
        end
    end
end

local function frame(y)
    smudge.rounded_rect(B.cellX - 6, y + 1, B.pegX + 16 * ((g().n + 1) // 2) + 8 - (B.cellX - 6), B.rowH - 2, 8, false, true, 2)
end

local function cursorBox(i, y)
    local cx, cy = cellCenter(i, y)
    local a = math.min(B.cw, B.rowH) // 2 - 1
    smudge.rect(cx - a, cy - a, 2 * a, 2 * a, false, true, 3)
end

local function bigButton(label, focus)
    smudge.rounded_rect(B.btnX, B.btnY, B.btnW, B.btnH, 10, focus, true, 3)
    smudge.text(S.w // 2, B.btnY + (B.btnH - smudge.line_height("ui12")) // 2, label, "ui12", "bold", "center",
                not focus)
end

local function statusText()
    local game, ph = g(), S.phase()
    local c = L.count(game)
    if game.result then
        if game.mode == "you" then
            if game.result == "won" then
                return c == 1 and "First guess! You broke the code." or ("You broke the code in " .. c .. " guesses!")
            end
            return "Out of guesses. The code is shown above."
        end
        if game.result == "won" then return "The device broke your code in " .. c .. " guesses." end
        return "The device ran out of guesses: you win!"
    end
    if S.note then return S.note end
    if ph == "thinking" then return "The device is thinking..." end
    if ph == "edit" then return string.format("Guess %d of %d", c + 1, game.max) end
    if ph == "secret" then return "Enter your secret code for the device" end
    local left = S.left and (S.left == 1 and " (1 code fits)" or string.format(" (%d codes fit)", S.left)) or ""
    if ph == "auto" then return "Device's guess " .. (c + 1) .. left .. ": score it" end
    return "Score the device's guess " .. (c + 1) .. left
end

local function drawStatus()
    local text = statusText()
    local maxW = S.w - 24 - (S.touch and 96 or 0)
    if smudge.text_width(text, "ui12", "bold") <= maxW then
        smudge.text(16, B.statusY + 8, text, "ui12", "bold", "left", true)
    else
        local res = smudge.wrapped_text(text, maxW, "ui10", "bold", 2)
        for i, line in ipairs(res.lines) do
            smudge.text(16, B.statusY + (i - 1) * 20, line, "ui10", "bold", "left", true)
        end
    end
    if S.touch then
        smudge.rounded_rect(S.w - 92, B.statusY, 80, 36, 6, false, true, 2)
        smudge.text(S.w - 52, B.statusY + 7, "Menu", "ui12", "bold", "center", true)
    end
end

local function drawCodeRow(ph)
    local game, y = g(), B.codeY
    smudge.text(B.x + 28, y + (B.rowH - smudge.line_height("small")) // 2, "Code", "small", "bold", "right", true)
    if ph == "secret" then
        frame(y)
        symbols(nil, y, game.n)
        if S.cur <= game.n then cursorBox(S.cur, y) end
    elseif game.mode == "you" then
        if game.result then
            symbols(game.secret, y, game.n)
        else
            for i = 1, game.n do
                local cx, cy = cellCenter(i, y)
                local a = B.r
                smudge.rect(cx - a, cy - a, 2 * a, 2 * a, true, true)
                smudge.text(cx, cy - smudge.line_height("ui12") // 2, "?", "ui12", "bold", "center", false)
            end
        end
    elseif game.secret ~= "" then
        symbols(game.secret, y, game.n)
    else
        smudge.text(B.cellX, y + (B.rowH - smudge.line_height("ui10")) // 2, "Your code: keep it in mind", "ui10",
                    "regular", "left", true)
    end
    smudge.line(B.x - 6, y + B.rowH + 2, S.w - B.x + 6, y + B.rowH + 2, 2, true)
end

local function drawRows(ph)
    local game = g()
    local c = L.count(game)
    for i = 1, game.max do
        local y = B.rowsY + (i - 1) * B.rowH
        smudge.text(B.x + 24, y + (B.rowH - smudge.line_height("small")) // 2, tostring(i), "small", "regular", "right",
                    true)
        if i <= c then
            symbols(L.guess(game, i), y, game.n)
            local b, w = L.scoreOf(game, i)
            pegs(b, w, y, game.n)
        elseif i == c + 1 and (ph == "edit" or ph == "score" or ph == "auto") then
            frame(y)
            if ph == "edit" then
                symbols(nil, y, game.n)
                if S.cur <= game.n then cursorBox(S.cur, y) end
                pegs(0, 0, y, game.n)
            else
                symbols(game.pending, y, game.n)
                if ph == "score" then pegs(S.sb, S.sw, y, game.n) else pegs(0, 0, y, game.n) end
            end
        else
            for p = 1, game.n do
                local cx, cy = cellCenter(p, y)
                smudge.circle(cx, cy, 2, true, true)
            end
        end
    end
end

local function drawPalette()
    local game = g()
    local curSym = S.cur <= game.n and S.row[S.cur] or 0
    for s = 1, game.k do
        local x = B.palX + (s - 1) * B.pw
        local sel = s == curSym
        smudge.rounded_rect(x + 3, B.palY, B.pw - 6, B.palH, 8, false, true, sel and 4 or 1)
        sym(s, x + B.pw // 2, B.palY + B.palH // 2, B.pw * 30 // 100, true)
    end
end

local function scoreRow(label, y, value, focus)
    local game = g()
    smudge.text(16, y + (B.sh - smudge.line_height("ui12")) // 2, label, "ui12", "bold", "left", true)
    for v = 0, game.n do
        local x = B.sx + v * B.sw
        local on = v == value
        smudge.rounded_rect(x + 3, y, B.sw - 6, B.sh, 8, on, true, 2)
        smudge.text(x + B.sw // 2, y + (B.sh - smudge.line_height("ui12")) // 2, tostring(v), "ui12", "bold", "center",
                    not on)
    end
    if focus then smudge.rounded_rect(8, y - 3, S.w - 16, B.sh + 6, 10, false, true, 3) end
end

function V.draw()
    local game = g()
    layout()
    local P = L.PRESETS[game.preset]
    smudge.header("Code Breaker", game.mode == "dev" and "Device breaks" or (P and P.name or "Custom"))
    local ph = S.phase()
    drawStatus()
    drawCodeRow(ph)
    drawRows(ph)
    local buttons = not S.touch
    if ph == "edit" or ph == "secret" then
        drawPalette()
        bigButton(ph == "secret" and "Set my code" or "Submit guess", buttons and S.cur > game.n)
    elseif ph == "score" then
        scoreRow("Black", B.sy1, S.sb, buttons and S.field == 1)
        scoreRow("White", B.sy2, S.sw, buttons and S.field == 2)
        bigButton("Score it", buttons and S.field == 3)
    elseif ph == "auto" then
        local b, w = L.score(game.pending, game.secret)
        smudge.centered_text(B.bottomTop + 30, string.format("Against your code: %d black, %d white", b, w), "ui12",
                             "regular", true)
        bigButton("Score it", buttons)
    elseif ph == "over" then
        local s = S.stat(S.statKey(game.mode, game.n, game.k, game.rep))
        if s[2] > 0 then
            smudge.centered_text(B.bottomTop + 30, string.format("%s %d of %d, average %.1f, best %d",
                                                                 game.mode == "you" and "Won" or "Solved", s[2], s[1],
                                                                 s[3] / s[2], s[4]), "ui10", "regular", true)
        end
        bigButton("Play again", buttons)
    end
    if S.touch then
        smudge.button_hints("", "", "", "")
    elseif ph == "edit" or ph == "secret" then
        smudge.button_hints("Menu", "Submit", "<", ">")
    elseif ph == "score" then
        smudge.button_hints("Menu", "Score", "<", ">")
    elseif ph == "auto" then
        smudge.button_hints("Menu", "Score", "", "")
    elseif ph == "over" then
        smudge.button_hints("Menu", "Again", "", "")
    else
        smudge.button_hints("Menu", "", "", "")
    end
end

return V
end
