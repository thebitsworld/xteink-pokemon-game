-- Tests for apps/circle/words.lua (run by ctest: LuaAppLogic_circle).
local list = dofile(APPS .. "/circle/words.lua")
local seen, count = {}, 0
for word in (list .. "|"):gmatch("([^|]*)|") do
    assert(word ~= "" and not word:match("^%s") and not word:match("%s$"), "empty or padded word")
    assert(not seen[word], "repeated: " .. word)
    seen[word] = true
    count = count + 1
end
assert(count >= 100, "only " .. count .. " words")
print(string.format("circle: %d words ok", count))
