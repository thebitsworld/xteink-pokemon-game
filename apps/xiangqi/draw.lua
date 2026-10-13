-- Drawing the xiangqi board screen. Usage:
--   local screen = smudge.dofile("draw.lua")(C, S)
-- with C the rules (logic.lua) and S the app's shared state (main.lua). The
-- screen's input is input.lua; main.lua holds only one of the two (or the
-- computer while it thinks) at a time. Drawing leaves the board's geometry in
-- S.B ({ x, y, cell }: the top-left intersection and the spacing) and whether
-- it is turned round in S.flip, for input.lua.
--
-- Pieces stand on the intersections. The device has no Chinese font, so a
-- piece is a disc with a letter: K general, A advisor, E elephant, H horse,
-- R chariot, C cannon, P soldier. Red is a white disc with a black ring,
-- Black a black disc.

return function(C, S)
local V = {}

local LETTER = "KAEHRCP"
local FILES = "abcdefghi"
local B, flip = nil, false

local function letterOf(p)
    local t = p >= 9 and p - 8 or p
    return LETTER:sub(t, t)
end

local function pointName(sq)
    local f = sq % 10
    return FILES:sub(f, f) .. (11 - sq // 10)
end

-- "Hb1-c3", "Ce3xe7" style notation, "+" for check and "#" for a win (the
-- move must already be played).
function V.describe(g, mv)
    local to = mv // 256
    local s = letterOf(g.board[to]) .. pointName(mv % 256) .. (g.lastCaptured and "x" or "-") .. pointName(to)
    if g.result and g.result ~= "draw" then return s .. "#" end
    if C.inCheck(g, g.side) then return s .. "+" end
    return s
end

-- Screen position of a point.
local function pointXY(sq)
    local col, row = sq % 10 - 1, sq // 10 - 1
    if flip then col, row = 8 - col, 9 - row end
    return B.x + col * B.cell, B.y + row * B.cell
end

local function drawGrid()
    local c = B.cell
    local x0, y0, x1, y1 = B.x, B.y, B.x + 8 * c, B.y + 9 * c
    for row = 0, 9 do
        local y = y0 + row * c
        smudge.line(x0, y, x1, y, (row == 0 or row == 9) and 3 or 1, true)
    end
    smudge.line(x0, y0, x0, y1, 3, true)
    smudge.line(x1, y0, x1, y1, 3, true)
    for col = 1, 7 do
        local x = x0 + col * c
        smudge.line(x, y0, x, y0 + 4 * c, 1, true)
        smudge.line(x, y0 + 5 * c, x, y1, 1, true)
    end
    -- The palaces' diagonals.
    for top = 0, 7, 7 do
        local ya, yb = y0 + top * c, y0 + (top + 2) * c
        smudge.line(x0 + 3 * c, ya, x0 + 5 * c, yb, 1, true)
        smudge.line(x0 + 5 * c, ya, x0 + 3 * c, yb, 1, true)
    end
    smudge.text(S.w // 2, y0 + 4 * c + (c - smudge.line_height("ui12")) // 2, "~  River  ~", "ui12", "bold", "center",
                true)
end

local function drawPiece(x, y, R, p)
    local red = p < 9
    if red then
        smudge.circle(x, y, R, true, false)
        smudge.circle(x, y, R, false, true, 3)
        smudge.circle(x, y, R - 6, false, true, 1)
    else
        smudge.circle(x, y, R, true, true)
        smudge.circle(x, y, R - 5, false, false, 1)
    end
    smudge.text(x, y - smudge.line_height("ui12") // 2, letterOf(p), "ui12", "bold", "center", red)
end

local function statusText()
    local game = S.game
    local vsDevice = S.vsDevice()
    if game.result then
        if game.result == "draw" then return "Draw: " .. game.reason end
        local how = game.reason == "stalemate" and " (no moves left)" or " by checkmate"
        if vsDevice then
            return ((game.result == "red") == (S.human == C.RED)) and "You win" .. how .. "!" or
                       "The device wins" .. how
        end
        return (game.result == "red" and "Red" or "Black") .. " wins" .. how .. "!"
    end
    if S.thinking then return "Device is thinking..." end
    if S.deviceMoved and game.last then return "Device: " .. V.describe(game, game.last) end
    if S.note then return S.note end
    if C.inCheck(game, game.side) then return "Check!" end
    if vsDevice then return "Your move" end
    return (game.side == C.RED and "Red" or "Black") .. " to move"
end

-- The board's geometry, kept in S.B for input.lua.
local function layout()
    local top = S.m.top_padding + S.m.header_height + 50
    local avail = S.h - S.m.button_hints_height - 34 - top
    local cell = math.min((S.w - 16) // 9, avail // 10)
    B = S.B or {}
    B.x, B.y, B.cell = (S.w - 8 * cell) // 2, top + cell // 2 + (avail - 10 * cell) // 3, cell
    S.B = B
    flip = S.vsDevice() and S.human == C.BLACK
    S.flip = flip
end

function V.draw()
    local game = S.game
    layout()
    smudge.header("Xiangqi", S.vsDevice() and S.LEVELS[S.mode].name or "2 players")
    local top = S.m.top_padding + S.m.header_height + 6
    local text = statusText()
    local bx = S.w - 96
    local maxW = (S.touch and bx or S.w) - 24
    smudge.text(16, top + 6, text, smudge.text_width(text, "ui12", "bold") > maxW and "ui10" or "ui12", "bold", "left",
                true)
    if S.touch then
        smudge.rounded_rect(bx, top, 80, 34, 6, false, 2)
        smudge.text(bx + 40, top + 6, "Menu", "ui12", "bold", "center", true)
    end
    local R = B.cell * 44 // 100
    drawGrid()
    local b = game.board
    -- The last move: a small square where it came from, a box where it went.
    if game.last and not S.picked then
        local x, y = pointXY(game.last % 256)
        smudge.rect(x - 7, y - 7, 14, 14, false, true, 2)
        x, y = pointXY(game.last // 256)
        smudge.rect(x - R - 3, y - R - 3, 2 * R + 6, 2 * R + 6, false, true, 2)
    end
    for sq = 11, 109 do
        local p = b[sq]
        if p ~= C.EMPTY and p ~= C.OFF then
            local x, y = pointXY(sq)
            drawPiece(x, y, R, p)
        end
    end
    -- The general in check.
    if not game.result and C.inCheck(game, game.side) then
        local x, y = pointXY(game.kings[game.side + 1])
        smudge.circle(x, y, R + 5, false, true, 3)
    end
    -- The picked piece and where it can go.
    if S.picked then
        local x, y = pointXY(S.picked)
        smudge.rect(x - R - 3, y - R - 3, 2 * R + 6, 2 * R + 6, false, true, 4)
        for _, mv in ipairs(S.legal) do
            if mv % 256 == S.picked then
                local to = mv // 256
                local tx, ty = pointXY(to)
                if b[to] ~= C.EMPTY then
                    smudge.circle(tx, ty, R + 4, false, true, 3)
                else
                    smudge.circle(tx, ty, 8, true, false)
                    smudge.circle(tx, ty, 6, true, true)
                end
            end
        end
    end
    if not S.touch and not game.result and not S.thinking then
        local x, y = pointXY((11 - S.curR) * 10 + S.curF)
        local e = R + 8
        smudge.rect(x - e - 2, y - e - 2, 2 * e + 4, 2 * e + 4, false, false, 2)
        smudge.rect(x - e, y - e, 2 * e, 2 * e, false, true, 3)
    end
    -- Move number and the last move under the board.
    if game.last then
        smudge.centered_text(B.y + 9 * B.cell + R + 8,
                             ((C.moveCount(game) + 1) // 2) .. ". " .. V.describe(game, game.last), "ui10", "bold", true)
    end
    if game.result then smudge.popup(text) end
    if S.touch then
        smudge.button_hints("", "", "", "")
    elseif game.result then
        smudge.button_hints("Menu", "Again", "", "")
    else
        smudge.button_hints(S.picked and "Drop" or "Menu", S.picked and "Move" or "Pick", "<", ">")
    end
end

return V
end
