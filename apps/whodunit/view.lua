-- The Whodunit case screen: the tabs and the verdict, with the open tab's
-- page loaded from a module of its own - clues.lua, grid.lua or accuse.lua -
-- so only one page is in memory at a time. Usage:
--   local screen = smudge.dofile("view.lua")(W, S)
-- with W the rules (logic.lua) and S the app's shared state (main.lua).

return function(W, S)
local V = {}

local TABS = { "Clues", "Grid", "Accuse" }
local PAGES = { "clues.lua", "grid.lua", "accuse.lua" }

-- Layout shared with the pages.
local U = {}
local function contentTop() return S.m.top_padding + S.m.header_height + 6 end
local function tabRect(k)
    local count = #TABS + (S.touch and 1 or 0)
    local tw = (S.w - 32 - (count - 1) * 8) // count
    return 16 + (k - 1) * (tw + 8), contentTop(), tw, 38
end
function U.bodyTop() return contentTop() + 50 end
function U.bodyBottom() return S.h - S.m.button_hints_height - 8 end

local page, pageTab = nil, nil

local function loadPage()
    if pageTab == S.tab then return page end
    page, pageTab = nil, nil
    collectgarbage("collect")
    page = smudge.dofile(PAGES[S.tab])(W, S, U)
    pageTab = S.tab
    return page
end

local function verdict()
    local case = S.case
    local s = case.murderer
    local text = string.format("%s, with the %s, in the %s", case.names[1][s], case.names[2][case.solution[2][s]],
                               case.names[3][case.solution[3][s]])
    if S.result == "right" then return "Solved in " .. S.formatTime(S.elapsed()) .. "! " .. text .. "." end
    return "Not quite - it was " .. text .. "."
end

-- The verdict in a box of its own: it is longer than a popup's one line.
local function drawVerdict()
    local bw = S.w - 48
    local res = smudge.wrapped_text(verdict(), bw - 32, "ui12", "bold")
    local lh = smudge.line_height("ui12")
    local bh = #res.lines * lh + 64
    local x, y = 24, (S.h - bh) // 2
    smudge.rounded_rect(x, y, bw, bh, 10, true, false)
    smudge.rounded_rect(x, y, bw, bh, 10, false, 3)
    for k, line in ipairs(res.lines) do
        smudge.text(S.w // 2, y + 16 + (k - 1) * lh, line, "ui12", "bold", "center", true)
    end
    smudge.text(S.w // 2, y + bh - 34, S.touch and "Tap for a new case" or "Confirm: a new case", "ui10", "regular",
                "center", true)
end

function V.draw()
    smudge.header("Whodunit", W.LEVELS[S.case.level].name)
    for k, label in ipairs(TABS) do
        local x, y, tw, th = tabRect(k)
        local on = S.tab == k
        smudge.rounded_rect(x, y, tw, th, 6, on, on and true or 2)
        smudge.text(x + tw // 2, y + 8, label, "ui12", "bold", "center", not on)
        if S.focus == "tabs" and S.tab == k and not S.touch then smudge.rect(x - 4, y - 4, tw + 8, th + 8, false, true, 3) end
    end
    if S.touch then
        local x, y, tw, th = tabRect(#TABS + 1)
        smudge.rounded_rect(x, y, tw, th, 6, false, 2)
        smudge.text(x + tw // 2, y + 8, "Menu", "ui12", "bold", "center", true)
    end
    loadPage().draw()
    if S.result then drawVerdict() end
    if S.touch then
        smudge.button_hints("", "", "", "")
    elseif S.result then
        smudge.button_hints("Menu", "New", "", "")
    elseif S.focus == "tabs" then
        smudge.button_hints("Menu", "Open", "<", ">")
    else
        smudge.button_hints("Menu", page.CONFIRM, page.PREV, page.NEXT)
    end
end

function V.button(btn)
    if btn == "back" then
        S.show("menu")
        return
    end
    if S.result then
        if btn == "confirm" then S.newCase(S.case.level) end
        return
    end
    if S.focus == "tabs" then
        if btn == "left" then S.tab = (S.tab - 2) % #TABS + 1
        elseif btn == "right" then S.tab = S.tab % #TABS + 1
        elseif btn == "confirm" or btn == "down" or btn == "page_forward" then S.focus = "content" end
        smudge.request_update()
        return
    end
    loadPage().button(btn)
end

function V.tap(x, y)
    if S.result then
        S.newCase(S.case.level)
        return
    end
    for k = 1, #TABS + 1 do
        if smudge.in_rect(x, y, tabRect(k)) then
            if k > #TABS then S.show("menu") else S.tab = k end
            smudge.request_update()
            return
        end
    end
    loadPage().tap(x, y)
end

return V
end
