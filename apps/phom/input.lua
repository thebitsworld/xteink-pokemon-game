-- Input on the Phom table. Usage:
--   local screen = smudge.dofile("input.lua")(S)
-- with S the app's state (main.lua). The drawing
-- is view.lua; both use the layout in S.L.

return function(S)
local V = {}
local L = S.L

-- Your moves are made by main.lua once this screen is out of memory.
local function take(eat)
    local g = S.game
    if not g.over and g.turn == 1 then S.act = eat and "eat" or "draw" end
end

local function throw()
    local g = S.game
    if not g.over and g.turn == 1 then S.act = "throw" end
end

local function pick(k)
    local c = S.game.hands[1][k]
    if not c then return end
    if S.sel == c then S.sel = nil else S.sel = c end
    S.note = nil
    smudge.request_update()
end

local function nextHand()
    S.deal = S.level
    smudge.request_update()
end

-- Down/page forward: draw while taking, throw the picked card after.
local function main()
    local g = S.game
    if g.phase == "take" then take(false) else throw() end
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
        if S.cursor == n + 1 then take(true)
        elseif S.cursor == n + 2 then take(false)
        elseif S.cursor == n + 3 then throw()
        else pick(S.cursor) end
    elseif btn == "left" then
        S.cursor = (S.cursor - 2) % (n + 3) + 1
        smudge.request_update()
    elseif btn == "right" then
        S.cursor = S.cursor % (n + 3) + 1
        smudge.request_update()
    elseif btn == "down" or btn == "page_forward" then
        main()
    elseif btn == "up" or btn == "page_back" then
        take(true)
    end
end

function V.tap(x, y)
    local g = S.game
    if smudge.in_rect(x, y, S.w - 96, L.top - 2, 80, 34) then return S.show("menu") end
    if g.over then return nextHand() end
    if g.turn == 1 then
        if smudge.in_rect(x, y, S.btnX(1), L.btnY, L.btnW, L.btnH) then return take(true) end
        if smudge.in_rect(x, y, S.btnX(2), L.btnY, L.btnW, L.btnH) then return take(false) end
        if smudge.in_rect(x, y, S.btnX(3), L.btnY, L.btnW, L.btnH) then return throw() end
    end
    -- The last card drawn is on top, so test from the end.
    local hand = g.hands[1]
    for k = #hand, 1, -1 do
        local up = S.sel == hand[k] and L.lift or 0
        if smudge.in_rect(x, y, S.handX(k, #hand), L.handY - up, L.cw, L.ch + up) then
            S.cursor = k
            return pick(k)
        end
    end
end

return V
end
