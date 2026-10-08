-- Notes for Xteink Pokemon (Lua app): lists you tick off with one hand, kept
-- as plain text files on the SD card (store.lua says how). The on-screen
-- keyboard (keyboard.lua) is loaded only while typing.
--
-- Buttons (X3/X4): Up/Down move through the lists or items and on to the
-- buttons under them, Left/Right along the buttons, Confirm opens a list or
-- ticks an item, Back goes back. Touch (X4 Pro): tap.

local Store = smudge.dofile("store.lua")

local w, h, m, touch = 480, 800, {}, false
local screen = "lists"  -- "lists" or "list"; kb is the keyboard while typing
local kb, kbFor = nil, nil
local names = {}        -- the lists, by name
local counts = {}       -- per list name: { done, total }
local listName, items = nil, {}
local sel = 1           -- the selected row (lists or items), or a button past them
local top = 1           -- the first row shown
local confirmDelete = false
-- Row heights: a list shows its name and a count, an item one line.
local function rowH() return screen == "lists" and 76 or 58 end

local function path(name) return Store.DIR .. "/" .. name .. ".txt" end

local function readList(name) return Store.parse(smudge.read_file(path(name)) or "") end

local function refreshLists()
    names = Store.names(smudge.list_files(Store.DIR))
    counts = {}
    for _, n in ipairs(names) do
        local its = readList(n)
        counts[n] = { Store.doneCount(its), #its }
    end
end

local function saveList() smudge.write_file(path(listName), Store.format(items)) end

-- The action buttons under the rows on each screen.
local function buttons()
    if screen == "lists" then return { "New list" } end
    local out = { "Add", "Clear done", confirmDelete and "Delete?" or "Delete" }
    -- Touch has no Back button: the list screen gets its own.
    if touch then table.insert(out, 1, "Back") end
    return out
end

local function rowCount() return screen == "lists" and #names or #items end

local function layout()
    local first = m.top_padding + m.header_height + 12
    local actions = h - m.button_hints_height - 8 - 48
    local visible = math.max(1, (actions - 12 - first) // rowH())
    return { first = first, actions = actions, visible = visible }
end

local function buttonRect(k, count)
    local L = layout()
    local bw = (w - 32 - (count - 1) * 8) // count
    return 16 + (k - 1) * (bw + 8), L.actions, bw, 48
end

-- Keeps the selected row on screen.
local function scrollTo()
    local L = layout()
    if sel <= rowCount() then
        if sel < top then top = sel end
        if sel >= top + L.visible then top = sel - L.visible + 1 end
    end
    top = math.max(1, math.min(top, math.max(1, rowCount() - L.visible + 1)))
end

-- Drawing --------------------------------------------------------------------

local function checkbox(x, y, done)
    smudge.rect(x, y, 26, 26, false, true, 2)
    if done then
        smudge.thick_line(x + 5, y + 13, x + 11, y + 20, 3, true)
        smudge.thick_line(x + 11, y + 20, x + 22, y + 6, 3, true)
    end
end

function on_draw()
    smudge.clear()
    if kb then
        kb.draw()
        return
    end
    local L = layout()
    local n = rowCount()
    if screen == "lists" then
        smudge.header("Notes", #names == 1 and "1 list" or (#names .. " lists"))
    else
        smudge.header(listName, string.format("%d of %d ticked", Store.doneCount(items), #items))
    end
    if n == 0 then
        smudge.centered_text(L.first + 40, screen == "lists" and "No lists yet." or "This list is empty.", "ui12", "bold",
                             true)
        smudge.centered_text(L.first + 76, screen == "lists" and "Make one with New list." or "Add items with Add item.",
                             "ui10", "regular", true)
    end
    for i = top, math.min(n, top + L.visible - 1) do
        local y = L.first + (i - top) * rowH()
        local selected = not touch and i == sel
        smudge.rounded_rect(16, y, w - 32, rowH() - 8, 8, selected, selected and true or 1)
        if screen == "lists" then
            local name = names[i]
            smudge.text(30, y + 8, name, "ui12", "bold", "left", not selected)
            local c = counts[name]
            smudge.text(30, y + 38, string.format("%d of %d ticked", c[1], c[2]), "ui10", "regular", "left", not selected)
        else
            local it = items[i]
            if selected then smudge.rect(28, y + 11, 30, 30, true, false) end
            checkbox(30, y + 13, it.done)
            local text = it.text
            while smudge.text_width(text, "ui12", "regular") > w - 110 and #text > 1 do text = text:sub(1, -2) end
            if text ~= it.text then text = text:sub(1, -2) .. "..." end
            smudge.text(70, y + 13, text, "ui12", it.done and "regular" or "bold", "left", not selected)
            if it.done then
                local tw = smudge.text_width(text, "ui12", "regular")
                smudge.line(70, y + 25, 70 + tw, y + 25, 2, not selected)
            end
        end
    end
    if n > L.visible then
        smudge.text(w - 20, L.actions - 26, string.format("%d-%d of %d", top, math.min(n, top + L.visible - 1), n),
                    "ui10", "regular", "right", true)
    end
    local bs = buttons()
    for k, label in ipairs(bs) do
        local x, y, bw, bh = buttonRect(k, #bs)
        local selected = not touch and sel == n + k
        smudge.rounded_rect(x, y, bw, bh, 8, selected, selected and true or 2)
        local font = smudge.text_width(label, "ui12", "bold") > bw - 8 and "ui10" or "ui12"
        smudge.text(x + bw // 2, y + 12, label, font, "bold", "center", not selected)
    end
    if touch then
        smudge.button_hints("", "", "", "")
    else
        smudge.button_hints(screen == "lists" and "Exit" or "Back", screen == "lists" and "Open" or "Tick", "Up", "Down")
    end
end

-- Actions --------------------------------------------------------------------

local function openList(name)
    screen, listName, items = "list", name, readList(name)
    sel, top, confirmDelete = 1, 1, false
end

local function showLists()
    screen, listName, items, confirmDelete = "lists", nil, {}, false
    refreshLists()
    sel, top = 1, 1
end

local function startTyping(what)
    kb = smudge.dofile("keyboard.lua")(what == "list" and "New list" or "New item", "",
                                       what == "list" and Store.MAX_NAME or 60)
    kbFor = what
end

local function finishTyping(result)
    kb = nil
    collectgarbage("collect")
    if result and result ~= "" then
        if kbFor == "list" then
            local file = Store.fileName(result)
            if file then
                local name = file:sub(1, -5)
                if not smudge.file_exists(path(name)) then smudge.write_file(path(name), "") end
                refreshLists()
                openList(name)
            end
        else
            items[#items + 1] = { text = result, done = false }
            saveList()
            sel = #items
            scrollTo()
        end
    end
    smudge.request_update()
end

local function activate(i)
    local n = rowCount()
    if i <= n then
        if screen == "lists" then
            openList(names[i])
        else
            items[i].done = not items[i].done
            saveList()
        end
    else
        local label = buttons()[i - n]
        local deleting = label == "Delete" or label == "Delete?"
        if label == "New list" then
            startTyping("list")
        elseif label == "Back" then
            showLists()
        elseif label == "Add" then
            startTyping("item")
        elseif label == "Clear done" then
            items = Store.clearDone(items)
            saveList()
            sel = math.min(sel, #items + #buttons())
        elseif deleting and confirmDelete then
            smudge.delete_file(path(listName))
            showLists()
        elseif deleting then
            -- Asks once more first.
            confirmDelete = true
        end
        if not deleting then confirmDelete = false end
    end
    scrollTo()
    smudge.request_update()
end

-- Callbacks ------------------------------------------------------------------

function on_init()
    w, h = smudge.get_bounds()
    m = smudge.get_metrics()
    touch = smudge.has_touch()
    refreshLists()
    if #names == 0 and smudge.load("seeded", "") == "" then
        smudge.write_file(path("Example"), Store.format({
            { text = "Tap an item to tick it", done = false },
            { text = "Add your own with Add item", done = false },
            { text = "Lists are .txt files in /.crosspoint/apps-data/notes", done = true },
        }))
        smudge.save("seeded", "1")
        refreshLists()
    end
end

function on_button(btn, pressed)
    if not pressed then return end
    if kb then
        local r = kb.button(btn)
        if r ~= nil then finishTyping(r) end
        return
    end
    local total = rowCount() + #buttons()
    if btn == "back" then
        if screen == "list" then showLists() else smudge.exit() end
    elseif btn == "confirm" then
        activate(sel)
        return
    elseif btn == "up" or btn == "page_back" then
        sel = (sel - 2) % total + 1
    elseif btn == "down" or btn == "page_forward" then
        sel = sel % total + 1
    elseif btn == "left" and sel > rowCount() + 1 then
        sel = sel - 1
    elseif btn == "right" and sel > rowCount() and sel < total then
        sel = sel + 1
    end
    if sel <= rowCount() then confirmDelete = false end
    scrollTo()
    smudge.request_update()
end

function on_tap(x, y)
    if kb then
        local r = kb.tap(x, y)
        if r ~= nil then finishTyping(r) end
        return
    end
    local L = layout()
    local bs = buttons()
    for k = 1, #bs do
        if smudge.in_rect(x, y, buttonRect(k, #bs)) then
            activate(rowCount() + k)
            return
        end
    end
    -- The page label scrolls on.
    if rowCount() > L.visible and y >= L.actions - 30 and y < L.actions then
        top = top + L.visible
        if top > rowCount() then top = 1 end
        smudge.request_update()
        return
    end
    if y >= L.first and y < L.first + L.visible * rowH() then
        local i = top + (y - L.first) // rowH()
        if i <= rowCount() then
            sel = i
            activate(i)
        end
    end
end

-- Firmware that reports touch long presses: back to the lists from a list.
function on_touch(x, y, event)
    if event == "long_press" and not kb and screen == "list" then
        showLists()
        smudge.request_update()
    end
end
