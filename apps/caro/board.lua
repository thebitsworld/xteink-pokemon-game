-- The Caro board screen: drawing and input. Usage:
--   local screen = smudge.dofile("board.lua")(C, S)
--   screen.draw(); screen.button(btn); screen.tap(x, y)
-- with C the rules (logic.lua) and S the app's shared state (main.lua).
-- main.lua drops it while the menu shows or the device thinks.

return function(C, S)
local V = {}
local N = C.N

local function contentTop() return S.m.top_padding + S.m.header_height + 6 end
local function menuButtonX() return S.w - 96 end

-- The board: x, y of its top-left corner and the cell size.
local function geometry()
    local top = contentTop() + 40
    local avail = S.h - S.m.button_hints_height - 30 - top
    local cell = math.min((S.w - 8) // N, avail // N)
    return (S.w - N * cell) // 2, top + (avail - N * cell) // 2, cell
end

local function markName(p) return p == C.X and "X" or "O" end

local function statusText()
    local game = S.game
    if game.winner == -1 then return "Draw: the board is full" end
    if game.winner > 0 then
        if S.vsDevice() then return game.winner == S.human and "You win!" or "The device wins" end
        return markName(game.winner) .. " wins!"
    end
    if S.thinking then return "Device is thinking..." end
    if S.vsDevice() then return "Your move (" .. markName(S.human) .. ")" end
    return markName(game.turn) .. " to move"
end

local function drawMark(x, y, cell, p, black)
    local pad = math.max(3, cell * 22 // 100)
    local t = cell >= 24 and 3 or 2
    if p == C.X then
        smudge.line(x + pad, y + pad, x + cell - pad, y + cell - pad, t, black)
        smudge.line(x + pad, y + cell - pad, x + cell - pad, y + pad, t, black)
    else
        smudge.circle(x + cell // 2, y + cell // 2, cell // 2 - pad + 1, false, t, black)
    end
end

-- Whether cell i is drawn white on black: the last move, the winning line.
local function lit(game, i)
    if i == C.last(game) then return true end
    if game.line then
        for _, j in ipairs(game.line) do
            if j == i then return true end
        end
    end
    return false
end

local function drawBoard(x0, y0, cell)
    local game = S.game
    local size = N * cell
    for k = 1, N - 1 do
        smudge.line(x0 + k * cell, y0, x0 + k * cell, y0 + size, 1, true)
        smudge.line(x0, y0 + k * cell, x0 + size, y0 + k * cell, 1, true)
    end
    smudge.rect(x0 - 1, y0 - 1, size + 3, size + 3, false, 2)
    local b = game.b
    for i = 1, N * N do
        local p = b[i]
        if p ~= 0 then
            local x, y = x0 + (i - 1) % N * cell, y0 + (i - 1) // N * cell
            local inv = lit(game, i)
            if inv then smudge.rect(x + 1, y + 1, cell - 1, cell - 1, true, true) end
            drawMark(x, y, cell, p, not inv)
        end
    end
    -- The cursor (buttons), or the cell tapped once (touch).
    if game.winner == 0 and not S.thinking and (not S.touch or S.pending) then
        local x, y = x0 + S.curC * cell, y0 + S.curR * cell
        smudge.rect(x - 3, y - 3, cell + 7, cell + 7, false, 4)
    end
end

function V.draw()
    local game = S.game
    smudge.header("Caro", S.vsDevice() and S.LEVELS[S.mode] or "2 players")
    local top = contentTop()
    local text = statusText()
    local bx = menuButtonX()
    local maxW = (S.touch and bx or S.w) - 24
    smudge.text(16, top + 4, text, smudge.text_width(text, "ui12", "bold") > maxW and "ui10" or "ui12", "bold", "left",
                true)
    if S.touch then
        smudge.rounded_rect(bx, top - 2, 80, 34, 6, false, 2)
        smudge.text(bx + 40, top + 4, "Menu", "ui12", "bold", "center", true)
    end
    local x0, y0, cell = geometry()
    drawBoard(x0, y0, cell)
    local below
    if game.winner ~= 0 then
        below = S.touch and "Tap the board to play again" or "Confirm to play again"
    elseif S.pending then
        below = "Tap the marked cell again to play"
    elseif game.n > 0 then
        below = "Move " .. game.n
    end
    if below then smudge.centered_text(y0 + N * cell + 8, below, "ui10", "regular", true) end
    if S.touch then
        smudge.button_hints("", "", "", "")
    elseif game.winner ~= 0 then
        smudge.button_hints("Menu", "Again", "", "")
    else
        smudge.button_hints("Menu", "Place", "<", ">")
    end
end

function V.button(btn)
    local game = S.game
    if btn == "back" then
        S.show("menu")
    elseif game.winner ~= 0 then
        if btn == "confirm" then S.newGame(S.mode) end
    elseif S.thinking then
        return
    elseif btn == "confirm" then
        S.place(C.idx(S.curR, S.curC))
    else
        if btn == "left" then S.curC = (S.curC - 1) % N
        elseif btn == "right" then S.curC = (S.curC + 1) % N
        elseif btn == "up" or btn == "page_back" then S.curR = (S.curR - 1) % N
        elseif btn == "down" or btn == "page_forward" then S.curR = (S.curR + 1) % N end
        smudge.request_update()
    end
end

function V.tap(x, y)
    if smudge.in_rect(x, y, menuButtonX(), contentTop() - 2, 80, 34) then
        S.show("menu")
        return
    end
    local game = S.game
    local x0, y0, cell = geometry()
    if game.winner ~= 0 then
        if smudge.in_rect(x, y, x0, y0, N * cell, N * cell + 30) then S.newGame(S.mode) end
        return
    end
    if S.thinking or not smudge.in_rect(x, y, x0, y0, N * cell, N * cell) then return end
    local r, c = math.min(N - 1, (y - y0) // cell), math.min(N - 1, (x - x0) // cell)
    local i = C.idx(r, c)
    if game.b[i] ~= 0 then return end
    -- Cells smaller than a finger: a first tap marks the cell, a second plays.
    if cell < 28 and S.pending ~= i then
        S.curR, S.curC, S.pending = r, c, i
        smudge.request_update()
        return
    end
    S.curR, S.curC = r, c
    S.place(i)
end

return V
end
