-- Input on the backgammon board screen. Usage:
--   local screen = smudge.dofile("input.lua")(S)
-- with S the app's shared state (main.lua). The drawing is draw.lua, which
-- leaves the board's geometry in S.B. A move is only asked for (S.act and
-- S.arg, game.lua's function and its argument): main.lua plays it once
-- this module is out of memory.

return function(S)
local V = {}

local function again()
    S.newGame(S.mode)
    smudge.request_update()
end

-- Back: drop the picked checker, take back a step, or open the menu.
local function back()
    if S.picked then
        S.picked = nil
        smudge.request_update()
    elseif S.humanTurn() and #S.game.steps > 0 then
        S.act = "undo"
        smudge.request_update()
    else
        S.show("menu")
    end
end

-- A point of the side to move (own numbering, 25 the bar, 0 the tray)
-- chosen: pick it up, or move the picked checker there.
local function choose(i)
    if S.picked then
        for k, t in ipairs(S.dt) do
            if t == i then
                S.act, S.arg = "move", S.ds[k]
                smudge.request_update()
                return
            end
        end
    end
    for k, f in ipairs(S.sources) do
        if f == i then
            S.cur = k
            S.act, S.arg = "pick", f
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
    local g = S.game
    if btn == "back" then
        back()
    elseif g.result then
        if btn == "confirm" then again() end
    elseif not S.humanTurn() then
        return
    elseif btn == "confirm" then
        if S.picked then
            choose(S.dt[S.dcur])
        elseif S.left == 0 then
            S.act = "done"
        elseif #S.sources > 0 then
            choose(S.sources[S.cur])
        end
    else
        local step = (btn == "left" or btn == "up" or btn == "page_back") and -1 or 1
        if S.picked then
            S.dcur = (S.dcur - 1 + step) % #S.dt + 1
        elseif #S.sources > 0 then
            S.cur = (S.cur - 1 + step) % #S.sources + 1
        else
            return
        end
        smudge.request_update()
    end
end

function V.tap(x, y)
    local B, g = S.B, S.game
    if not B then return end
    if smudge.in_rect(x, y, S.w - B.bw - 8, B.infoY, B.bw, 30) then
        S.show("menu")
        return
    end
    if g.result then
        again()
        return
    end
    if not S.humanTurn() then return end
    if y >= B.diceY and y < B.diceY + 34 then
        if x >= S.w - B.bw - 8 then
            if S.left == 0 then S.act = "done" end
        elseif x >= S.w - 2 * B.bw - 16 and #g.steps > 0 then
            S.act = "undo"
            smudge.request_update()
        end
        return
    end
    -- Which point, bar half or tray was tapped (in the mover's numbering).
    local side = g.turn
    local i
    if y >= B.offY and y < B.offY + B.offH then
        i = 0
    elseif y >= B.barY and y < B.barY + B.barH then
        i = 25
    elseif y >= B.y0 and y < B.offY then
        local row
        if y < B.barY then row = (y - B.y0) // B.rowH + 1 else row = (y - B.barY - B.barH) // B.rowH + 7 end
        if row < 1 or row > 12 then return end
        local p = x < S.w // 2 and row + 12 or 13 - row
        i = side == 1 and p or 25 - p
    else
        return
    end
    choose(i)
end

return V
end
