-- Tests for apps/notes/store.lua (run by ctest: LuaAppLogic_notes).
local Store = dofile(APPS .. "/notes/store.lua")

-- Reading: ticked and unticked marks, plain lines, blank lines and CRs.
local items = Store.parse("[x] Milk\n[ ] Eggs\r\nBread\n\n  [X]   Tea  \n[ ]\n")
assert(#items == 4)
assert(items[1].text == "Milk" and items[1].done)
assert(items[2].text == "Eggs" and not items[2].done)
assert(items[3].text == "Bread" and not items[3].done)
assert(items[4].text == "Tea" and items[4].done)
assert(#Store.parse("") == 0)

-- Writing it back, and reading that again.
local text = Store.format(items)
assert(text == "[x] Milk\n[ ] Eggs\n[ ] Bread\n[x] Tea\n")
local again = Store.parse(text)
for i, it in ipairs(items) do assert(again[i].text == it.text and again[i].done == it.done) end
assert(Store.format({}) == "")

-- Counting and clearing ticked items.
assert(Store.doneCount(items) == 2)
local left = Store.clearDone(items)
assert(#left == 2 and left[1].text == "Eggs" and left[2].text == "Bread")

-- File names for list names.
assert(Store.fileName("Shopping") == "Shopping.txt")
assert(Store.fileName("  To do: today!  ") == "To do today.txt")
assert(Store.fileName("a/b\\c") == "abc.txt")
assert(Store.fileName("???") == nil)
assert(#Store.fileName(string.rep("x", 50)) == Store.MAX_NAME + 4)

-- List names from a folder listing.
local names = Store.names({ "b.txt", "seeded.dat", "A list.txt", "c.TXT", "notes" })
assert(#names == 2 and names[1] == "A list" and names[2] == "b")

print("notes ok")
