-- Input on the xiangqi board screen. Usage:
--   local screen = smudge.dofile("input.lua")(C, S)
-- with C the rules (logic.lua) and S the app's shared state (main.lua). The
-- drawing is draw.lua, which leaves the board's geometry in S.B and S.flip.

return function(C, S)
local V = {}

local function humanTurn()
    local game = S.game
    return not game.result and not S.thinking and (not S.vsDevice() or game.side == S.human)
end

-- Confirm or a tap on a point: pick up a piece, or move the one picked up.
local function activate(f, r)
    if not humanTurn() then return end
    local sq = (11 - r) * 10 + f
    if S.picked then
        for _, mv in ipairs(S.legal) do
            if mv % 256 == S.picked and mv // 256 == sq then
                S.play(mv)
                return
            end
        end
    end
    -- Pick (or switch to) one of your pieces that can move.
    for _, mv in ipairs(S.legal) do
        if mv % 256 == sq then
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

function V.button(btn)
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
        if S.flip then df, dr = -df, -dr end
        S.curF = (S.curF - 1 + df) % 9 + 1
        S.curR = (S.curR - 1 + dr) % 10 + 1
        smudge.request_update()
    end
end

function V.tap(x, y)
    if smudge.in_rect(x, y, S.w - 96, S.m.top_padding + S.m.header_height + 6, 80, 34) then
        S.show("menu")
        return
    end
    if S.game.result then
        S.newGame(S.mode)
        smudge.request_update()
        return
    end
    local B = S.B
    if not B then return end
    local col, row = (x - B.x + B.cell // 2) // B.cell, (y - B.y + B.cell // 2) // B.cell
    if col < 0 or col > 8 or row < 0 or row > 9 then return end
    local f, r = col + 1, 10 - row
    if S.flip then f, r = 9 - col, row + 1 end
    S.curF, S.curR = f, r
    activate(f, r)
end

return V
end
