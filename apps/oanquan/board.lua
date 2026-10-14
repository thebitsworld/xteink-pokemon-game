-- The O An Quan board screen: drawing and input. Usage:
--   local screen = smudge.dofile("board.lua")(C, S)
-- with C the rules (logic.lua) and S the app's shared state (main.lua).
--
-- The board runs across the screen as on paper: the top row (player 2, the
-- device) over the bottom row (player 1, you), a mandarin square at each
-- end. Stones are dots (a number past 10), a mandarin a big disc. E-ink does
-- not animate, so after a move the squares it changed are framed, the ones
-- it captured shaded, the square it started from carries an arrow, and a few
-- lines under the board tell what happened.

return function(C, S)
local V = {}

local B = {}

local function layout()
    local w = S.w
    local top = S.m.top_padding + S.m.header_height + 6
    B.top = top
    B.cw = (w - 16) * 100 // 725
    B.qw = (w - 16 - 5 * B.cw) // 2
    B.ch = 96
    B.x = (w - 2 * B.qw - 5 * B.cw) // 2
    B.y = top + 96
    B.btnY = B.y + 2 * B.ch + 56
    B.textY = B.btnY + 74
end

-- Rectangle of square p.
local function rectOf(p)
    if p == 1 then return B.x, B.y, B.qw, 2 * B.ch end
    if p == 7 then return B.x + B.qw + 5 * B.cw, B.y, B.qw, 2 * B.ch end
    if p <= 6 then return B.x + B.qw + (p - 2) * B.cw, B.y + B.ch, B.cw, B.ch end
    return B.x + B.qw + (12 - p) * B.cw, B.y, B.cw, B.ch
end

local function humanTurn()
    local s = S.s
    return s[C.OVER] == 0 and not S.thinking and (not S.vsDevice() or s[C.SIDE] == 1)
end

-- Drawing ----------------------------------------------------------------

-- n stones as dots around (cx, cy), or a number past 10.
local function stones(cx, cy, n)
    if n == 0 then return end
    if n > 10 then
        smudge.circle(cx, cy, 20, false, 2, true)
        local t = tostring(n)
        smudge.text(cx, cy - smudge.line_height("ui12") // 2, t, "ui12", "bold", "center", true)
        return
    end
    local cols = n == 1 and 1 or (n <= 4 and 2 or 3)
    local rows = (n + cols - 1) // cols
    local sp = 17
    local y = cy - (rows - 1) * sp // 2
    local k = 0
    for r = 1, rows do
        local inRow = math.min(cols, n - k)
        local x = cx - (inRow - 1) * sp // 2
        for _ = 1, inRow do
            smudge.circle(x, y, 6, true, 1, true)
            x = x + sp
        end
        k = k + inRow
        y = y + sp
    end
end

local function bit(mask, p) return (mask >> (p - 1)) & 1 == 1 end

local function drawSquare(p, info, marks)
    local s = S.s
    local x, y, w, h = rectOf(p)
    local quanSq = p == 1 or p == 7
    if marks and bit(info.captured, p) then smudge.rect_dither(x + 6, y + 6, w - 12, h - 12, false) end
    if quanSq then
        smudge.rounded_rect(x, y, w, h, 30, false, 3, true)
    else
        smudge.rect(x, y, w, h, false, 2, true)
    end
    local cx = x + w // 2
    local q = s[C.QUAN[p] or 0] or 0
    if quanSq and q == 1 then
        smudge.circle(cx, y + h // 4 + 4, 24, true, 1, true)
        smudge.circle(cx, y + h // 4 + 4, 16, false, 2, false)
        stones(cx, y + h * 5 // 8 + 4, s[p])
    else
        stones(cx, y + h // 2, s[p])
    end
    if s[p] >= 4 and s[p] <= 10 then
        smudge.text(x + w - 6, y + h - smudge.line_height("small") - 2, tostring(s[p]), "small", "bold", "right", true)
    end
    if marks and bit(info.changed, p) and not bit(info.captured, p) then
        if quanSq then
            smudge.rounded_rect(x + 5, y + 5, w - 10, h - 10, 26, false, 3, true)
        else
            smudge.rect(x + 4, y + 4, w - 8, h - 8, false, 3, true)
        end
    end
end

-- A small arrow at the top of the square a move started from.
local function startArrow(p, right)
    local x, y, w = rectOf(p)
    local cx, ay = x + w // 2, y + 14
    if right then
        smudge.triangle(cx - 8, ay - 7, cx - 8, ay + 7, cx + 8, ay, true)
    else
        smudge.triangle(cx + 8, ay - 7, cx + 8, ay + 7, cx - 8, ay, true)
    end
end

local function scoreLine(side)
    local s = S.s
    local t = S.name(side) .. ": " .. C.score(s, side) .. " points"
    local q = s[C.CAPQ + side - 1]
    if q > 0 then t = t .. (q == 1 and " (1 mandarin)" or " (2 mandarins)") end
    local debt = s[C.DEBT + side - 1]
    if debt > 0 then t = t .. ", owes " .. debt end
    return t
end

local function resultText()
    local s = S.s
    local a, b = C.score(s, 1), C.score(s, 2)
    local w = C.winner(s)
    if w == 0 then return "Draw, " .. a .. " to " .. b end
    local hi, lo = math.max(a, b), math.min(a, b)
    if S.vsDevice() then
        return (w == 1 and "You win " or "The device wins ") .. hi .. " to " .. lo
    end
    return S.name(w) .. " wins " .. hi .. " to " .. lo
end

local function endReason()
    local s = S.s
    if C.empty(s, 1) and C.empty(s, 7) then return "Both mandarin squares are empty" end
    if s[C.SINCE] >= C.QUIET then return C.QUIET .. " moves without a capture" end
    if s[C.PLY] >= C.MAX_PLY then return "Move limit reached" end
    return "A row is empty and cannot be filled again"
end

local function statusText()
    local s = S.s
    if s[C.OVER] == 1 then return resultText() end
    if S.thinking then return "Device is thinking..." end
    if S.picked then return "Sow " .. S.letter(S.picked) .. ": left or right?" end
    if S.vsDevice() then return "Your move" end
    return S.name(s[C.SIDE]) .. " to move"
end

local function rowOwnerText(side)
    if S.vsDevice() and side == 1 then return "Your row" end
    return S.name(side) .. "'s row"
end

-- What the last move did, a few short lines.
local function summary(lines)
    local i, m = S.info, S.lastMove
    local p = m // 2
    local right = S.goesRight(p, m % 2 == 1 and 1 or -1)
    local t = S.name(S.lastSide) .. " sowed " .. S.letter(p) .. " to the " .. (right and "right" or "left")
    if i.laps > 0 then t = t .. ", picked up " .. i.laps .. (i.laps == 1 and " more time" or " more times") end
    lines[#lines + 1] = t
    if i.capD + i.capQ > 0 then
        t = "Took " .. i.capD .. (i.capD == 1 and " stone" or " stones")
        if i.capQ > 0 then t = t .. (i.capQ == 1 and " and a mandarin" or " and both mandarins") end
        lines[#lines + 1] = t .. " (+" .. (i.capD + C.QUAN_VALUE * i.capQ) .. ")"
    else
        lines[#lines + 1] = "No capture"
    end
    if i.young then lines[#lines + 1] = "Young mandarin (under " .. C.NON .. " stones): not taken" end
    if i.stopped then lines[#lines + 1] = "Stopped after " .. C.MAX_LAPS .. " pick-ups" end
    if i.refill > 0 then
        t = rowOwnerText(S.s[C.SIDE]) .. " was empty: " .. i.refill .. " put back"
        if i.borrowed > 0 then t = t .. " (" .. i.borrowed .. " borrowed)" end
        lines[#lines + 1] = t
    end
end

local function arrowButtons()
    local w = S.w
    local bw = (w - 48 - 16) // 2
    return 24, B.btnY, bw, 56, 24 + bw + 16
end

function V.draw()
    layout()
    local s = S.s
    local w = S.w
    smudge.header("O An Quan", S.vsDevice() and S.LEVELS[S.mode].name or "2 players")
    local top = B.top
    local text = statusText()
    local maxW = (S.touch and w - 96 or w) - 24
    smudge.text(16, top + 6, text, smudge.text_width(text, "ui12", "bold") > maxW and "ui10" or "ui12", "bold", "left",
                true)
    if S.touch then
        smudge.rounded_rect(w - 96, top, 80, 34, 6, false, 2)
        smudge.text(w - 56, top + 6, "Menu", "ui12", "bold", "center", true)
    end
    local mover = s[C.SIDE]
    local over = s[C.OVER] == 1
    for side = 2, 1, -1 do
        local y = side == 2 and B.y - 50 or B.y + 2 * B.ch + 22
        local t = scoreLine(side)
        if not over and side == mover then t = "> " .. t .. " <" end
        smudge.centered_text(y, t, "ui10", side == mover and not over and "bold" or "regular", true)
    end

    local marks = S.lastMove ~= nil and not S.picked
    for p = 1, 12 do drawSquare(p, S.info, marks) end
    if marks then
        local m = S.lastMove
        startArrow(m // 2, S.goesRight(m // 2, m % 2 == 1 and 1 or -1))
    end

    -- The cursor (buttons): a pointer outside the board at the square.
    if humanTurn() and not S.touch then
        local x, y, cw, ch = rectOf(S.cursor)
        local cx = x + cw // 2
        if S.cursor <= 6 then
            local py = y + ch + 3
            smudge.triangle(cx, py, cx - 10, py + 14, cx + 10, py + 14, true)
        else
            local py = y - 3
            smudge.triangle(cx, py, cx - 10, py - 14, cx + 10, py - 14, true)
        end
    end
    if S.picked then
        local x, y, cw, ch = rectOf(S.picked)
        smudge.invert_rect(x + 3, y + 3, cw - 6, ch - 6)
    end

    -- The two directions, or the result.
    local bx, by, bw, bh, bx2 = arrowButtons()
    if over then
        smudge.rounded_rect(24, by, w - 48, bh, 8, false, 3)
        smudge.text(w // 2, by + 6, endReason(), "ui10", "regular", "center", true)
        smudge.text(w // 2, by + 28, S.touch and "Tap to play again" or "Confirm to play again", "ui10", "bold",
                    "center", true)
    elseif S.picked then
        smudge.rounded_rect(bx, by, bw, bh, 8, true, true)
        smudge.text(bx + bw // 2, by + 16, "< Sow left", "ui12", "bold", "center", false)
        smudge.rounded_rect(bx2, by, bw, bh, 8, true, true)
        smudge.text(bx2 + bw // 2, by + 16, "Sow right >", "ui12", "bold", "center", false)
    elseif humanTurn() then
        smudge.centered_text(by + 16, S.touch and "Tap one of your squares" or "Pick one of your squares",
                             "ui10", "regular", true)
    end

    -- What happened.
    local y = B.textY
    local lh = smudge.line_height("ui10") + 4
    if S.prevText then
        smudge.text(24, y, S.prevText, "ui10", "regular", "left", true)
        y = y + lh + 4
    end
    if S.lastMove then
        local lines = {}
        summary(lines)
        for k, line in ipairs(lines) do
            smudge.text(24, y, line, "ui10", k == 1 and "bold" or "regular", "left", true)
            y = y + lh
        end
    end

    if S.touch then
        smudge.button_hints("", "", "", "")
    elseif over then
        smudge.button_hints("Menu", "Again", "", "")
    elseif S.picked then
        smudge.button_hints("Cancel", "", "Sow <", "Sow >")
    else
        smudge.button_hints("Menu", "Pick", "<", ">")
    end
end

-- Input ------------------------------------------------------------------

local function sow(right)
    local p = S.picked
    if not p then return end
    local d = (p <= 6) == right and 1 or -1
    S.picked = nil
    S.play(p * 2 + (d > 0 and 1 or 0))
end

-- The next non-empty square of the player to move, `dir` columns on screen.
local function moveCursor(dir)
    local s = S.s
    local side = s[C.SIDE]
    local p = S.cursor
    local col = p <= 6 and p - 1 or 13 - p
    for _ = 1, 5 do
        col = (col - 1 + dir) % 5 + 1
        p = side == 1 and col + 1 or 13 - col
        if s[p] > 0 then break end
    end
    S.cursor = p
end

function V.button(btn)
    layout()
    local s = S.s
    if btn == "back" then
        if S.picked then
            S.picked = nil
            smudge.request_update()
        else
            S.show("menu")
        end
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
    local left = btn == "left" or btn == "up" or btn == "page_back"
    local right = btn == "right" or btn == "down" or btn == "page_forward"
    if S.picked then
        if left or right then sow(right) end
        return
    end
    if btn == "confirm" then
        if s[S.cursor] > 0 then S.picked = S.cursor end
    elseif left then
        moveCursor(-1)
    elseif right then
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
        S.picked = nil
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
    if S.picked then
        local bx, by, bw, bh, bx2 = arrowButtons()
        if smudge.in_rect(x, y, bx, by, bw, bh) then return sow(false) end
        if smudge.in_rect(x, y, bx2, by, bw, bh) then return sow(true) end
    end
    local f = C.first(s[C.SIDE])
    for p = f, f + 4 do
        local rx, ry, rw, rh = rectOf(p)
        if smudge.in_rect(x, y, rx, ry, rw, rh) then
            if s[p] > 0 then
                S.cursor = p
                S.picked = S.picked ~= p and p or nil
                smudge.request_update()
            end
            return
        end
    end
end

return V
end
