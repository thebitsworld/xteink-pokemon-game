-- The Tide & Paper table screen, input half (view.lua draws it; layout.lua
-- is shared). Usage:
--   local screen = smudge.dofile("input.lua")(T, S)
--   screen.button(btn) / screen.tap(x, y)

return function(T, S)
local V = {}
local Lay = smudge.dofile("layout.lua")(T, S)
local layout, tableItems, tableX, handPos = Lay.layout, Lay.tableItems, Lay.tableX, Lay.handPos
local buttonRect, crabCards, buttonEnabled, BUTTONS = Lay.buttonRect, Lay.crabCards, Lay.buttonEnabled, Lay.BUTTONS

-- The actions, local so that nothing outlives this module; main.lua's
-- S.after() then clears the selection and starts the device's turn if due.

local function drawTwo()
    if T.drawTwo(S.game) then
        S.deviceLog = nil
        S.row, S.col, S.sel, S.keepIdx = 1, 1, {}, nil
        smudge.request_update()
    end
end

local function take(pile)
    if T.takePile(S.game, pile) then
        S.deviceLog = nil
        S.row, S.col = 2, #S.game.hands[1]
        S.after()
    end
end

local function keep(i, pile)
    if T.keep(S.game, i, pile) then
        S.row, S.col = 2, #S.game.hands[1]
        S.after()
    end
end

local function pair(a, b)
    if T.playPair(S.game, a, b, S.rand) then
        if S.game.phase == "crab" then S.row, S.col = 2, 1 end
        S.after()
    end
end

local function crab(id)
    if T.crabTake(S.game, id) then
        S.row, S.col = 2, #S.game.hands[1]
        S.after()
    end
end

local function endTurn()
    if T.endTurn(S.game) then S.after() end
end

local function stop()
    if T.stop(S.game) then S.after() end
end

local function lastChance()
    if T.lastChance(S.game) then S.after() end
end

local function rowLength(g, row)
    if row == 1 then return #tableItems(g) end
    if row == 2 then return g.phase == "crab" and #crabCards(g) or #g.hands[1] end
    return #BUTTONS
end

local function toggleSelect(k)
    for i, s in ipairs(S.sel) do
        if s == k then
            table.remove(S.sel, i)
            return
        end
    end
    if #S.sel == 2 then table.remove(S.sel, 1) end
    S.sel[#S.sel + 1] = k
end

local function activateTable(g, k)
    local it = tableItems(g)[k]
    if not it then return end
    if g.phase == "draw" then
        if it.kind == "deck" then drawTwo() else take(it.n) end
    elseif g.phase == "keep" then
        if it.kind == "drawn" then
            S.keepIdx = it.n
            local piles = T.discardPiles(g)
            if #piles == 1 or not g.drawn[2] then keep(it.n, piles[1]) end
        elseif S.keepIdx then
            keep(S.keepIdx, it.n)
        end
    end
end

local function activateButton(g, k)
    if not buttonEnabled(g, k) then return end
    if k == 1 then
        local h = g.hands[1]
        pair(h[S.sel[1]], h[S.sel[2]])
    elseif k == 2 then
        endTurn()
    elseif k == 3 then
        stop()
    else
        lastChance()
    end
end

function V.button(btn)
    local g = S.game
    if btn == "back" then
        S.show("menu")
        return
    end
    if g.turn ~= 1 then return end
    if btn == "up" or btn == "page_back" then
        S.row = S.row == 1 and 3 or S.row - 1
        if g.phase == "crab" then S.row = 2 end
    elseif btn == "down" or btn == "page_forward" then
        S.row = S.row == 3 and 1 or S.row + 1
        if g.phase == "crab" then S.row = 2 end
    elseif btn == "left" then
        S.col = S.col - 1
    elseif btn == "right" then
        S.col = S.col + 1
    elseif btn == "confirm" then
        if S.row == 1 then
            activateTable(g, S.col)
        elseif S.row == 2 then
            if g.phase == "crab" then
                crab(crabCards(g)[S.col])
            elseif S.col <= #g.hands[1] then
                toggleSelect(S.col)
            end
        else
            activateButton(g, S.col)
        end
    end
    local n = rowLength(g, S.row)
    if n > 0 then S.col = (S.col - 1) % n + 1 else S.col = 1 end
    smudge.request_update()
end

function V.tap(x, y)
    local g, L = S.game, layout()
    if smudge.in_rect(x, y, S.w - 96, L.top - 4, 80, 36) then
        S.show("menu")
        return
    end
    if g.turn ~= 1 then return end
    if g.phase == "crab" then
        for k, id in ipairs(crabCards(g)) do
            local cx, cy = handPos(L, k)
            if smudge.in_rect(x, y, cx, cy + (L.table - L.hand) + 24, L.hw, L.hh) then
                crab(id)
                return
            end
        end
        return
    end
    local items = tableItems(g)
    for k = 1, #items do
        if smudge.in_rect(x, y, tableX(L, #items, k), L.table, L.tw, L.th) then
            activateTable(g, k)
            smudge.request_update()
            return
        end
    end
    for k = 1, #g.hands[1] do
        local cx, cy = handPos(L, k)
        if smudge.in_rect(x, y, cx, cy, L.hw, L.hh) then
            toggleSelect(k)
            smudge.request_update()
            return
        end
    end
    for k = 1, #BUTTONS do
        if smudge.in_rect(x, y, buttonRect(L, k)) then
            activateButton(g, k)
            smudge.request_update()
            return
        end
    end
end

return V
end
