-- The Dots and Boxes board screen: drawing and input. Usage:
--   local screen = smudge.dofile("board.lua")(D, S)
--   screen.draw(); screen.button(btn); screen.tap(x, y)
-- with D the rules (logic.lua) and S the app's shared state (main.lua).
-- main.lua drops it while the menu shows or the device thinks.

return function(D, S)
local V = {}
local G = smudge.dofile("game.lua")(D)

local function playerName(p)
    if S.vsDevice() then return p == 1 and "You" or "Device" end
    return "Player " .. p
end

-- Puts the cursor on the free line nearest to where it is.
local function fixCursor()
    local g = S.game
    if D.over(g) or g.lines[S.cursor] == 0 then return end
    local x, y = G.mid(g, S.cursor)
    local best, bestD = nil, 1e9
    for l = 1, g.L do
        if g.lines[l] == 0 then
            local mx, my = G.mid(g, l)
            local d = (mx - x) * (mx - x) + (my - y) * (my - y)
            if d < bestD then best, bestD = l, d end
        end
    end
    S.cursor = best or S.cursor
end

local function contentTop() return S.m.top_padding + S.m.header_height + 6 end
local function menuButton() return S.w - 96, contentTop(), 80, 36 end

-- The board: top-left dot and the step between dots.
local function geometry()
    local n = S.game.n
    local top = contentTop() + 96
    local bottom = S.h - S.m.button_hints_height - 40
    local step = math.min((S.w - 48) // n, (bottom - top) // n)
    return (S.w - step * n) // 2, top + (bottom - top - step * n) // 2, step
end

-- Line l's two dots on the screen.
local function lineXY(x0, y0, s, l)
    local r1, c1, r2, c2 = G.ends(S.game, l)
    return x0 + c1 * s, y0 + r1 * s, x0 + c2 * s, y0 + r2 * s
end

local function statusText()
    local game = S.game
    if D.over(game) then
        local a, b = game.score[1], game.score[2]
        local win = D.winner(game)
        if win == 0 then return string.format("Draw, %d-%d", a, b) end
        if S.vsDevice() then
            return win == 1 and string.format("You win %d-%d!", a, b) or string.format("The device wins %d-%d", b, a)
        end
        return string.format("Player %d wins %d-%d!", win, math.max(a, b), math.min(a, b))
    end
    if S.thinking then return "Device is thinking..." end
    if S.note then return S.note end
    if S.vsDevice() then return "Your move" end
    return "Player " .. game.turn .. " to move"
end

-- The owner's mark in a box: solid for player 1, a cross for player 2.
local function drawBoxMark(x, y, s, p, black)
    if p == 1 then
        local pad = math.max(6, s // 8)
        smudge.rect(x + pad, y + pad, s - 2 * pad, s - 2 * pad, true, 1, black)
    else
        local pad = math.max(7, s * 22 // 100)
        local t = s >= 50 and 5 or 3
        smudge.thick_line(x + pad, y + pad, x + s - pad, y + s - pad, t, black)
        smudge.thick_line(x + pad, y + s - pad, x + s - pad, y + pad, t, black)
    end
end

-- The two score chips: the player to move is drawn white on black.
local function drawScores(y)
    local game = S.game
    local cw = (S.w - 48) // 2
    for p = 1, 2 do
        local x = 16 + (p - 1) * (cw + 16)
        local active = not D.over(game) and game.turn == p
        smudge.rounded_rect(x, y, cw, 44, 8, active, active and true or 2)
        drawBoxMark(x + 8, y + 6, 32, p, not active)
        smudge.rect(x + 8, y + 6, 32, 32, false, 1, not active)
        smudge.text(x + 50, y + 10, playerName(p), "ui12", "bold", "left", not active)
        smudge.text(x + cw - 12, y + 10, tostring(game.score[p]), "ui12", "bold", "right", not active)
    end
end

-- A drawn line; the last turn's lines are thicker with a white stripe.
local function drawLine(x1, y1, x2, y2, last)
    local t = last and 11 or 5
    local half = t // 2
    if y1 == y2 then
        smudge.rect(x1, y1 - half, x2 - x1, t, true, 1, true)
        if last then smudge.rect(x1 + 8, y1 - 1, x2 - x1 - 16, 3, true, 1, false) end
    else
        smudge.rect(x1 - half, y1, t, y2 - y1, true, 1, true)
        if last then smudge.rect(x1 - 1, y1 + 8, 3, y2 - y1 - 16, true, 1, false) end
    end
end

local function drawBoard()
    local game = S.game
    local x0, y0, s = geometry()
    local n = game.n
    for b = 1, game.nb do
        local p = game.owner[b]
        if p ~= 0 then drawBoxMark(x0 + (b - 1) % n * s, y0 + (b - 1) // n * s, s, p, true) end
    end
    local last = G.lastTurn(game)
    for l = 1, game.L do
        local x1, y1, x2, y2 = lineXY(x0, y0, s, l)
        if game.lines[l] ~= 0 then
            drawLine(x1, y1, x2, y2, last[l])
        elseif y1 == y2 then
            -- A free line: a faint dotted guide.
            for x = x1 + 10, x2 - 10, 8 do smudge.pixel(x, y1) end
        else
            for y = y1 + 10, y2 - 10, 8 do smudge.pixel(x1, y) end
        end
    end
    for r = 0, n do
        for c = 0, n do smudge.circle(x0 + c * s, y0 + r * s, 5, true, 1, true) end
    end
    -- The cursor (buttons only): a frame around the free line.
    if not S.touch and not D.over(game) and not S.thinking and game.lines[S.cursor] == 0 then
        local x1, y1, x2, y2 = lineXY(x0, y0, s, S.cursor)
        if y1 == y2 then
            smudge.rounded_rect(x1 + 9, y1 - 9, x2 - x1 - 18, 19, 6, false, 3)
        else
            smudge.rounded_rect(x1 - 9, y1 + 9, 19, y2 - y1 - 18, 6, false, 3)
        end
    end
end

function V.draw()
    local game = S.game
    fixCursor()
    smudge.header("Dots and Boxes",
                  string.format("%s  %dx%d", S.vsDevice() and S.LEVELS[S.mode] or "2 players", game.n, game.n))
    local top = contentTop()
    local text = statusText()
    local maxW = (S.touch and S.w - 96 or S.w) - 24
    smudge.text(16, top + 6, text, smudge.text_width(text, "ui12", "bold") > maxW and "ui10" or "ui12", "bold", "left",
                true)
    if S.touch then
        local x, y, bw, bh = menuButton()
        smudge.rounded_rect(x, y, bw, bh, 6, false, 2)
        smudge.text(x + bw // 2, y + 6, "Menu", "ui12", "bold", "center", true)
    end
    drawScores(top + 44)
    drawBoard()
    if D.over(game) then
        smudge.centered_text(S.h - S.m.button_hints_height - 30,
                             S.touch and "Tap the board to play again" or "Confirm to play again", "ui10", "regular", true)
    end
    if S.touch then
        smudge.button_hints("", "", "", "")
    elseif D.over(game) then
        smudge.button_hints("Menu", "Again", "", "")
    else
        smudge.button_hints("Menu", "Draw", "<", ">")
    end
end

-- Moves the cursor to the nearest free line in direction (dx, dy): a step
-- straight on counts double a step sideways, and a tie sideways goes back
-- the other way from the last one, so repeated presses zigzag along a row.
local function move(dx, dy)
    local game = S.game
    local x, y = G.mid(game, S.cursor)
    local best, bestCost, bestSide = nil, 1e9, 0
    for l = 1, game.L do
        if l ~= S.cursor and game.lines[l] == 0 then
            local mx, my = G.mid(game, l)
            local ox, oy = mx - x, my - y
            local along = ox * dx + oy * dy
            if along > 0 then
                local side = dx ~= 0 and oy or ox -- sideways offset (signed)
                local cost = along * 2 + math.abs(side)
                if cost < bestCost or (cost == bestCost and side * S.drift < 0 and bestSide * S.drift >= 0) then
                    best, bestCost, bestSide = l, cost, side
                end
            end
        end
    end
    if best then
        S.cursor = best
        if bestSide ~= 0 then S.drift = bestSide > 0 and 1 or -1 end
        smudge.request_update()
    end
end

-- The free line nearest a tap, if it is close enough to one.
local function lineAt(px, py)
    local game = S.game
    local x0, y0, s = geometry()
    local best, bestD = nil, (s * 2 // 5) * (s * 2 // 5)
    for l = 1, game.L do
        if game.lines[l] == 0 then
            local x1, y1, x2, y2 = lineXY(x0, y0, s, l)
            local qx = math.max(x1, math.min(x2, px))
            local qy = math.max(y1, math.min(y2, py))
            local d = (qx - px) * (qx - px) + (qy - py) * (qy - py)
            if d < bestD then best, bestD = l, d end
        end
    end
    return best
end

-- Input -------------------------------------------------------------------------

local function humanTurn()
    return not D.over(S.game) and not S.thinking and (not S.vsDevice() or S.game.turn == 1)
end

local function playLine(l)
    local g = S.game
    if not humanTurn() or not D.canPlay(g, l) then return end
    local who = g.turn
    local done = D.play(g, l)
    if done > 0 and not D.over(g) then
        S.note = (done == 2 and "Two boxes! " or "Box! ") .. (S.vsDevice() and "Draw again" or playerName(who) .. " again")
    else
        S.note = nil
    end
    fixCursor()
    S.moved()
end

local function again()
    G.start(S, S.mode)
    S.showBoard()
end

function V.button(btn)
    if btn == "back" then
        S.openMenu()
    elseif D.over(S.game) then
        if btn == "confirm" then again() end
    elseif btn == "confirm" then
        playLine(S.cursor)
    elseif btn == "left" then
        move(-1, 0)
    elseif btn == "right" then
        move(1, 0)
    elseif btn == "up" or btn == "page_back" then
        move(0, -1)
    elseif btn == "down" or btn == "page_forward" then
        move(0, 1)
    end
end

function V.tap(x, y)
    local mx, my, mw, mh = menuButton()
    if smudge.in_rect(x, y, mx, my, mw, mh) then
        S.openMenu()
    elseif D.over(S.game) then
        again()
    else
        local l = lineAt(x, y)
        if l then
            S.cursor = l
            playLine(l)
        end
    end
end

return V
end
