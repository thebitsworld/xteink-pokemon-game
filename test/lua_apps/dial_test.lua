-- Tests for apps/dial/cards.lua (run by ctest: LuaAppLogic_dial): every card
-- has two ends, and no card appears twice.
local text = dofile(APPS .. "/dial/cards.lua")
local seen, count = {}, 0
for line in text:gmatch("[^\n]+") do
    local left, right = line:match("^(.-)|(.-)$")
    assert(left and right and left ~= "" and right ~= "", "two ends: " .. line)
    assert(not right:find("|"), "one bar: " .. line)
    assert(not seen[line], "repeated: " .. line)
    seen[line] = true
    count = count + 1
end
assert(count >= 60, "enough cards for several games")
print(string.format("dial: %d cards ok", count))
