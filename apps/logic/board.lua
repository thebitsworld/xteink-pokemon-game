-- What the puzzle screens share: the header and status line, the board's
-- room, the help line, the solved popup and the input they all handle.
-- Usage (in a screen module): local B = smudge.dofile("board.lua")(S)

return function(S)
local B = {}

function B.top() return S.m.top_padding + S.m.header_height + 8 end

-- The Menu button (touch only), right of the status line.
function B.menuRect() return S.w - 16 - 96, B.top(), 96, 44 end

-- The board's room under the status line, above a line of help: x, y, w, h.
function B.boardRoom()
    local y = B.top() + 56
    return 8, y, S.w - 16, S.h - S.m.button_hints_height - 8 - 40 - y
end

-- Header and status line.
function B.drawTop(status)
    smudge.header(S.KINDS[S.kind].name, S.levelText(S.kind, S.level))
    local y = B.top()
    smudge.text(16, y + 12, S.formatTime(S.elapsed()) .. "   " .. status, "ui12", "bold", "left", true)
    if S.touch then
        local x, ry, w, h = B.menuRect()
        smudge.rounded_rect(x, ry, w, h, 6, false, 2)
        smudge.text(x + w // 2, ry + 12, "Menu", "ui12", "bold", "center", true)
    end
end

-- The line under the board: the note if there is one, else `help`.
function B.drawHelp(y, help)
    local text = S.note or help
    if text then smudge.centered_text(y, text, "ui10", "regular", true) end
end

function B.drawSolved()
    if not S.P.solved then return false end
    local s = S.stats[S.kind][S.level]
    smudge.popup(string.format("Solved in %s!  Best %s", S.formatTime(S.P.elapsedMs), S.formatTime(s[2])))
    if S.touch then
        smudge.button_hints("", "", "", "")
    else
        smudge.button_hints("Menu", "Next", "", "")
    end
    return true
end

-- After a move: counts a solve, clears the save, redraws.
function B.moved()
    local P = S.P
    S.note = nil
    if P.solved and not P.counted then
        P.counted = true
        P.elapsedMs = P.elapsedMs + (smudge.millis() - S.since)
        local s = S.stats[S.kind][S.level]
        s[1] = s[1] + 1
        if s[2] == 0 or P.elapsedMs < s[2] then s[2] = P.elapsedMs end
        smudge.save("st_" .. S.KINDS[S.kind].id .. S.level, s[1] .. "," .. s[2])
        S.savePuzzle()
    end
    smudge.request_update()
end

-- Handles the input every puzzle screen shares; true when it was used.
function B.commonButton(btn)
    if S.P.solved then
        if btn == "back" then S.show("pause") end
        if btn == "confirm" then S.newPuzzle(S.kind, S.level) end
        return true
    end
    return false
end

function B.commonTap(x, y)
    local rx, ry, rw, rh = B.menuRect()
    if smudge.in_rect(x, y, rx, ry, rw, rh) then
        S.show("pause")
        return true
    end
    if S.P.solved then
        S.newPuzzle(S.kind, S.level)
        return true
    end
    return false
end

return B
end
