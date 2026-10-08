-- Tests for apps/forehead/words.lua (run by ctest: LuaAppLogic_forehead):
-- every category has plenty of words, none empty or repeated.
local cats = dofile(APPS .. "/forehead/words.lua")
assert(#cats >= 6)
local names = {}
for _, c in ipairs(cats) do
    local name, list = c[1], c[2]
    assert(not names[name], "category repeated: " .. name)
    names[name] = true
    local seen, count = {}, 0
    for word in (list .. "|"):gmatch("([^|]*)|") do
        assert(word:match("^%S.*%S$") or word:match("^%S$"), name .. ": empty or padded word '" .. word .. "'")
        assert(not seen[word], name .. ": repeated " .. word)
        seen[word] = true
        count = count + 1
    end
    assert(count >= 40, name .. " has only " .. count .. " words")
end
print(string.format("forehead: %d categories ok", #cats))
