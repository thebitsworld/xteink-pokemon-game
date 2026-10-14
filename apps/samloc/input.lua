-- Input on the Sam Loc table. Usage:
--   local screen = smudge.dofile("input.lua")(T, S)
-- with T the rules (logic.lua) and S the app's state (main.lua). The drawing
-- is view.lua; both use the layout in S.L.

return function(T, S)
local V = {}
local L = S.L

local function refused(why)
    S.note = why
    smudge.request_update()
end

-- Plays the picked cards (pass = false) or passes; while you are asked
-- about Sam, declares it (pass = false) or not.
local function move(pass)
    local g = S.game
    if g.over or g.turn ~= 1 then return end
    local G = S.rules()
    local why
    if g.ask then why = G.declare(g, 1, not pass)
    elseif pass then why = G.pass(g, 1)
    else why = G.play(g, 1, S.selected()) end
    if why then return refused(why) end
    S.sel, S.note = {}, nil
    local n = #g.hands[1]
    if S.cursor > n then S.cursor = math.max(1, n) end
    S.afterMove(G)
end

local function toggle(k)
    local c = S.game.hands[1][k]
    if not c then return end
    S.sel[c] = not S.sel[c] or nil
    S.note = nil
    smudge.request_update()
end

local function nextHand()
    S.deal = S.level
    smudge.request_update()
end

function V.button(btn)
    local g = S.game
    if btn == "back" then return S.show("menu") end
    if g.over then
        if btn == "confirm" then nextHand() end
        return
    end
    local n = #g.hands[1]
    if btn == "confirm" then
        if S.cursor == n + 1 then move(false)
        elseif S.cursor == n + 2 then move(true)
        else toggle(S.cursor) end
    elseif btn == "left" then
        S.cursor = (S.cursor - 2) % (n + 2) + 1
        smudge.request_update()
    elseif btn == "right" then
        S.cursor = S.cursor % (n + 2) + 1
        smudge.request_update()
    elseif btn == "down" or btn == "page_forward" then
        move(false)
    elseif btn == "up" or btn == "page_back" then
        move(true)
    end
end

function V.tap(x, y)
    local g = S.game
    if smudge.in_rect(x, y, S.w - 96, L.top - 2, 80, 34) then return S.show("menu") end
    if g.over then return nextHand() end
    if g.turn == 1 then
        if smudge.in_rect(x, y, L.playX, L.btnY, L.btnW, L.btnH) then return move(false) end
        if smudge.in_rect(x, y, L.passX, L.btnY, L.btnW, L.btnH) then return move(true) end
    end
    -- The last card drawn is on top, so test from the end.
    local hand = g.hands[1]
    for k = #hand, 1, -1 do
        local up = S.sel[hand[k]] and L.lift or 0
        if smudge.in_rect(x, y, S.handX(k, #hand), L.handY - up, L.cw, L.ch + up) then
            S.cursor = k
            return toggle(k)
        end
    end
end

return V
end
