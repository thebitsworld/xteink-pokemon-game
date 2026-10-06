-- The chess board screen: drawing and input. Usage:
--   local screen = smudge.dofile("view.lua")(C, S)
-- with C the rules (logic.lua) and S the app's shared state (main.lua).
-- main.lua drops this module while the computer is thinking - the search
-- needs the memory - and loads it again to draw.

return function(C, S)
local V = {}

local LETTER = { "P", "N", "B", "R", "Q", "K" }
local PROMO_NAMES = { "Queen", "Rook", "Bishop", "Knight" }

-- Long algebraic notation: "e2-e4", "Ng1xf3", "O-O", "e7-e8=Q", with "+"
-- or "#" for check and mate (the move must already be played).
local FILES = "abcdefgh"
local LETTERS = { "", "N", "B", "R", "Q", "K" }

local function squareName(sq)
    local f, r = C.fileOf(sq), C.rankOf(sq)
    return FILES:sub(f, f) .. r
end

function V.describe(g, mv)
    local from, to, flag = C.from(mv), C.to(mv), C.flag(mv)
    local s
    if flag == C.CASTLE then
        s = (to > from) and "O-O" or "O-O-O"
    else
        local p = g.board[to]
        local t = C.promo(mv) > 0 and C.PAWN or C.typeOf(p)
        s = LETTERS[t] .. squareName(from) .. (g.lastCaptured and "x" or "-") .. squareName(to)
        if C.promo(mv) > 0 then s = s .. "=" .. LETTERS[C.promo(mv)] end
    end
    if g.result and g.reason == "checkmate" then return s .. "#" end
    if C.inCheck(g, g.side) then return s .. "+" end
    return s
end

local function contentTop() return S.m.top_padding + S.m.header_height + 6 end
function V.menuButton() return S.w - 96, contentTop(), 80, 34 end
local function flipped() return S.vsDevice() and S.human == C.BLACK end

function V.boardRect()
    local top = contentTop() + 40
    local cell = math.min((S.w - 16) // 8, (S.h - S.m.button_hints_height - 44 - top) // 8)
    return { x = (S.w - 8 * cell) // 2, y = top, cell = cell }
end

-- Screen position of a file/rank, and back.
local function cellXY(B, f, r)
    local col, row = f - 1, 8 - r
    if flipped() then col, row = 8 - f, r - 1 end
    return B.x + col * B.cell, B.y + row * B.cell
end

function V.fileRankAt(B, x, y)
    local col, row = (x - B.x) // B.cell, (y - B.y) // B.cell
    if col < 0 or col > 7 or row < 0 or row > 7 then return nil end
    if flipped() then return 8 - col, row + 1 end
    return col + 1, 8 - row
end

function V.promoButtons(B)
    local bw = B.cell + 8
    local x0 = (S.w - 4 * bw - 3 * 8) // 2
    local y = B.y + 3 * B.cell
    local out = {}
    for k = 1, 4 do out[k] = { x = x0 + (k - 1) * (bw + 8), y = y, w = bw, h = bw + 22 } end
    return out
end

-- A piece: a white halo (its silhouette), then the piece itself.
local function drawPiece(x, y, s, p)
    local t = LETTER[C.typeOf(p)]
    local ox, oy = x + (s - 48) // 2, y + (s - 48) // 2
    smudge.draw_sprite(ox, oy, 48, 48, "sprites/m" .. t .. ".raw", true, false)
    smudge.draw_sprite(ox, oy, 48, 48, "sprites/" .. (p < 8 and "w" or "b") .. t .. ".raw", false, false)
end

function V.statusText()
    local game = S.game
    local vsDevice = S.vsDevice()
    if game.result then
        if game.result == "draw" then return "Draw: " .. game.reason end
        if vsDevice then
            return ((game.result == "white") == (S.human == C.WHITE)) and "You win by checkmate!" or
                       "Checkmate - the device wins"
        end
        return (game.result == "white" and "White" or "Black") .. " wins by checkmate!"
    end
    if S.thinking then return "Device is thinking..." end
    if S.promo then return "Promote to which piece?" end
    if S.deviceMoved and game.last then return "Device: " .. V.describe(game, game.last) end
    if S.note then return S.note end
    local check = C.inCheck(game, game.side) and " - check!" or ""
    if vsDevice then return "Your move" .. check end
    return (game.side == C.WHITE and "White" or "Black") .. " to move" .. check
end

function V.draw()
    local game = S.game
    local vsDevice = S.vsDevice()
    smudge.header("Chess", vsDevice and S.LEVELS[S.mode].name or "2 players")
    local top = contentTop()
    local text = V.statusText()
    local bx = V.menuButton()
    local maxW = (S.touch and bx or S.w) - 24
    smudge.text(16, top + 6, text, smudge.text_width(text, "ui12", "bold") > maxW and "ui10" or "ui12", "bold", "left",
                true)
    if S.touch then
        smudge.rounded_rect(bx, top, 80, 34, 6, false, 2)
        smudge.text(bx + 40, top + 6, "Menu", "ui12", "bold", "center", true)
    end
    local B = V.boardRect()
    local s = B.cell
    smudge.rect(B.x - 2, B.y - 2, 8 * s + 4, 8 * s + 4, false, true, 2)
    for r = 1, 8 do
        for f = 1, 8 do
            local x, y = cellXY(B, f, r)
            if (f + r) % 2 == 0 then smudge.rect_dither(x, y, s, s, false) end
            local p = game.board[C.square(f, r)]
            if p ~= C.EMPTY then drawPiece(x, y, s, p) end
        end
    end
    -- The last move, and the king in check.
    if game.last and not S.picked then
        for _, sq in ipairs({ C.from(game.last), C.to(game.last) }) do
            local x, y = cellXY(B, C.fileOf(sq), C.rankOf(sq))
            smudge.rect(x + 2, y + 2, s - 4, s - 4, false, true, 2)
        end
    end
    if not game.result and C.inCheck(game, game.side) then
        local k = game.kings[game.side + 1]
        local x, y = cellXY(B, C.fileOf(k), C.rankOf(k))
        smudge.rect(x + 1, y + 1, s - 2, s - 2, false, true, 4)
    end
    -- The picked piece and where it can go.
    if S.picked then
        local x, y = cellXY(B, C.fileOf(S.picked), C.rankOf(S.picked))
        smudge.rect(x, y, s, s, false, true, 5)
        for _, mv in ipairs(S.legal) do
            if C.from(mv) == S.picked then
                local to = C.to(mv)
                local tx, ty = cellXY(B, C.fileOf(to), C.rankOf(to))
                if game.board[to] ~= C.EMPTY then
                    smudge.circle(tx + s // 2, ty + s // 2, s // 2 - 3, false, 3)
                else
                    smudge.circle(tx + s // 2, ty + s // 2, s // 7, true, false)
                    smudge.circle(tx + s // 2, ty + s // 2, s // 7, false, 2)
                end
            end
        end
    end
    if not S.touch and not game.result and not S.thinking and not S.promo then
        local x, y = cellXY(B, S.curF, S.curR)
        smudge.rect(x - 3, y - 3, s + 6, s + 6, false, false, 3)
        smudge.rect(x, y, s, s, false, true, 4)
    end
    -- Move number and the last move under the board.
    if game.last then
        local moveNo = (C.moveCount(game) + 1) // 2
        smudge.centered_text(B.y + 8 * s + 10, string.format("%d. %s", moveNo, V.describe(game, game.last)), "ui10",
                             "bold", true)
    end
    if S.promo then
        for k, b in ipairs(V.promoButtons(B)) do
            local focused = k == S.promo.choice and not S.touch
            smudge.rect(b.x, b.y, b.w, b.h, true, false)
            smudge.rounded_rect(b.x, b.y, b.w, b.h, 6, false, focused and 4 or 2)
            drawPiece(b.x + 4, b.y + 2, b.w - 8, S.PROMOTIONS[k] + (game.side == C.BLACK and 8 or 0))
            smudge.text(b.x + b.w // 2, b.y + b.h - 22, PROMO_NAMES[k], "ui10", "regular", "center", true)
        end
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


-- Input ------------------------------------------------------------------------

local function humanTurn()
    local game = S.game
    return not game.result and not S.thinking and (not S.vsDevice() or game.side == S.human)
end

-- Confirm or a tap on a square: pick up a piece, or move the one picked up.
local function activate(f, r)
    if not humanTurn() then return end
    local sq = C.square(f, r)
    if S.picked then
        local matches = {}
        for _, mv in ipairs(S.legal) do
            if C.from(mv) == S.picked and C.to(mv) == sq then matches[#matches + 1] = mv end
        end
        if #matches == 1 then
            S.play(matches[1])
            return
        elseif #matches > 1 then
            S.promo = { from = S.picked, to = sq, choice = 1 }
            smudge.request_update()
            return
        end
    end
    -- Pick (or switch to) one of your pieces that can move.
    for _, mv in ipairs(S.legal) do
        if C.from(mv) == sq then
            S.picked, S.note, S.deviceMoved = sq, nil, false
            smudge.request_update()
            return
        end
    end
    if S.picked then
        S.picked = nil
        smudge.request_update()
    end
end

local function promote(k)
    for _, mv in ipairs(S.legal) do
        if C.from(mv) == S.promo.from and C.to(mv) == S.promo.to and C.promo(mv) == S.PROMOTIONS[k] then
            S.play(mv)
            return
        end
    end
end

function V.button(btn)
    if S.promo then
        if btn == "left" or btn == "up" then S.promo.choice = (S.promo.choice - 2) % 4 + 1
        elseif btn == "right" or btn == "down" then S.promo.choice = S.promo.choice % 4 + 1
        elseif btn == "confirm" then promote(S.promo.choice) return
        elseif btn == "back" then S.promo, S.picked = nil, nil end
        smudge.request_update()
        return
    end
    if btn == "back" then
        if S.picked then
            S.picked = nil
            smudge.request_update()
        else
            S.show("menu")
        end
    elseif S.game.result then
        if btn == "confirm" then
            S.newGame(S.mode)
            smudge.request_update()
        end
    elseif btn == "confirm" then
        activate(S.curF, S.curR)
    else
        -- The arrows move on the screen, whichever way the board faces.
        local df, dr = 0, 0
        if btn == "left" then df = -1 elseif btn == "right" then df = 1
        elseif btn == "up" or btn == "page_back" then dr = 1 elseif btn == "down" or btn == "page_forward" then dr = -1 end
        if flipped() then df, dr = -df, -dr end
        S.curF = (S.curF - 1 + df) % 8 + 1
        S.curR = (S.curR - 1 + dr) % 8 + 1
        smudge.request_update()
    end
end

function V.tap(x, y)
    if smudge.in_rect(x, y, V.menuButton()) then
        S.show("menu")
        return
    end
    if S.game.result then
        S.newGame(S.mode)
        smudge.request_update()
        return
    end
    local B = V.boardRect()
    if S.promo then
        for k, b in ipairs(V.promoButtons(B)) do
            if smudge.in_rect(x, y, b.x, b.y, b.w, b.h) then
                promote(k)
                return
            end
        end
        return
    end
    local f, r = V.fileRankAt(B, x, y)
    if f then
        S.curF, S.curR = f, r
        activate(f, r)
    end
end
return V
end
