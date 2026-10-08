-- Notes lists as plain text: one item per line, "[x] " in front of a ticked
-- one ("[ ] " or nothing in front of the others). No drawing, so it can be
-- tested on a computer (test/lua_apps/notes_test.lua).
--
-- Each list is a .txt file in the app's data folder on the SD card
-- (/.crosspoint/apps-data/notes/), so lists can also be written or read on a
-- computer through File Transfer.

local Store = {}

Store.DIR = "/.crosspoint/apps-data/notes"
Store.MAX_NAME = 28

-- Items from a list file's text: { { text = , done = }, ... }.
function Store.parse(s)
    local items = {}
    for line in (s .. "\n"):gmatch("([^\n]*)\n") do
        line = line:match("^%s*(.-)%s*$")
        local done = false
        local mark, rest = line:match("^%[([ xX])%]%s*(.*)$")
        if mark then
            done, line = mark ~= " ", rest
        end
        if line ~= "" then items[#items + 1] = { text = line, done = done } end
    end
    return items
end

function Store.format(items)
    local lines = {}
    for i, it in ipairs(items) do lines[i] = (it.done and "[x] " or "[ ] ") .. it.text end
    return table.concat(lines, "\n") .. (#lines > 0 and "\n" or "")
end

-- A file name for a list name: letters, digits, spaces, - and _ kept.
function Store.fileName(name)
    local clean = name:gsub("[^%w %-_]", ""):match("^%s*(.-)%s*$"):sub(1, Store.MAX_NAME)
    if clean == "" then return nil end
    return clean .. ".txt"
end

-- List names (file names without .txt), sorted.
function Store.names(files)
    local out = {}
    for _, f in ipairs(files) do
        local name = f:match("^(.+)%.txt$")
        if name then out[#out + 1] = name end
    end
    table.sort(out, function(a, b) return a:lower() < b:lower() end)
    return out
end

function Store.doneCount(items)
    local n = 0
    for _, it in ipairs(items) do
        if it.done then n = n + 1 end
    end
    return n
end

-- Without the ticked items.
function Store.clearDone(items)
    local out = {}
    for _, it in ipairs(items) do
        if not it.done then out[#out + 1] = it end
    end
    return out
end

return Store
