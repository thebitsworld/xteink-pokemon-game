-- Tide & Paper table layout, shared by the drawing (view.lua) and the input
-- (input.lua) halves of the table screen:
--   local Lay = smudge.dofile("layout.lua")(T, S)

return function(T, S)
local Lay = {}

Lay.BUTTONS = { "Pair", "End turn", "Stop", "Last chance" }

local function contentTop() return S.m.top_padding + S.m.header_height + 6 end

local function layout()
    local w = S.w
    local L = { top = contentTop() }
    L.tw = math.min(96, (w - 32 - 3 * 8) // 4)
    L.th = L.tw * 5 // 4
    L.table = L.top + 70
    L.status = L.table + L.th + 26
    L.played = L.status + 44
    L.hw = math.min(84, (w - 32 - 4 * 6) // 5)
    L.hh = L.hw * 5 // 4
    L.hand = L.played + 28
    L.buttons = S.h - S.m.button_hints_height - 8 - 44
    L.points = L.buttons - 26
    return L
end

-- What the table row holds: { kind = "deck" | "pile" | "drawn", n = index }.
local function tableItems(g)
    local items = {}
    if g.phase == "keep" then
        for i = 1, #g.drawn do items[#items + 1] = { kind = "drawn", n = i } end
    else
        items[1] = { kind = "deck" }
    end
    items[#items + 1] = { kind = "pile", n = 1 }
    items[#items + 1] = { kind = "pile", n = 2 }
    return items
end

local function tableX(L, count, k) return (S.w - count * L.tw - (count - 1) * 8) // 2 + (k - 1) * (L.tw + 8) end

local function handPos(L, k)
    local col, row = (k - 1) % 5, (k - 1) // 5
    return 16 + col * (L.hw + 6), L.hand + row * (L.hh + 6)
end

local function buttonRect(L, k)
    local bw = (S.w - 32 - 3 * 8) // 4
    return 16 + (k - 1) * (bw + 8), L.buttons, bw, 44
end

-- The cards a crab pair can take: both discard piles, top first.
local function crabCards(g)
    local list = {}
    for q = 1, 2 do
        for i = #g.piles[q], 1, -1 do list[#list + 1] = g.piles[q][i] end
    end
    return list
end

local function buttonEnabled(g, k)
    if S.thinking or g.turn ~= 1 or g.phase ~= "play" then return false end
    if k == 1 then
        local h = g.hands[1]
        return #S.sel == 2 and T.isPair(h[S.sel[1]], h[S.sel[2]])
    end
    if k == 2 then return true end
    return T.canEnd(g)
end

-- Drawing --------------------------------------------------------------------

-- The six marks: circle, square, triangle; filled, then hollow.
Lay.layout, Lay.tableItems, Lay.tableX, Lay.handPos = layout, tableItems, tableX, handPos
Lay.buttonRect, Lay.crabCards, Lay.buttonEnabled = buttonRect, crabCards, buttonEnabled

return Lay
end
