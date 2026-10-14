-- Patience: a game as text and back, loaded only to save or restore. Usage:
--   local Save = smudge.dofile("save.lua")(P)   -- Save.serialize(g), Save.deserialize(text)
-- with P the rules (logic.lua).

return function(P)
local char, byte = string.char, string.byte
local Save = {}

-- "F|deal|moves|cells|foundations|col,col,...|log" or
-- "S|suits|moves|stock|done|downs|col,col,...|log". Cards are 65..116 and
-- log characters 48..64, so neither '|' nor ',' can occur inside a field.

function Save.serialize(g)
    local head
    if g.kind == P.FREECELL then
        local cells = {}
        for i = 1, 4 do cells[i] = char(g.cells[i] + 64) end
        head = "F|" .. g.deal .. "|" .. g.moves .. "|" .. table.concat(cells) .. "|" ..
               char(g.found[1] + 64, g.found[2] + 64, g.found[3] + 64, g.found[4] + 64)
    else
        local d = {}
        for i = 1, 10 do d[i] = char(g.down[i] + 48) end
        head = "S|" .. g.suits .. "|" .. g.moves .. "|" .. g.stock .. "|" .. g.done .. "|" .. table.concat(d)
    end
    return head .. "|" .. table.concat(g.cols, ",") .. "|" .. g.log
end

function Save.deserialize(text)
    local f = {}
    for part in (text .. "|"):gmatch("([^|]*)|") do f[#f + 1] = part end
    local g
    if f[1] == "F" and #f == 7 then
        g = { kind = P.FREECELL, deal = tonumber(f[2]), moves = tonumber(f[3]), cells = {}, found = {} }
        if #f[4] ~= 4 or #f[5] ~= 4 then return nil end
        for i = 1, 4 do g.cells[i] = byte(f[4], i) - 64 g.found[i] = byte(f[5], i) - 64 end
    elseif f[1] == "S" and #f == 8 then
        g = { kind = P.SPIDER, suits = tonumber(f[2]), moves = tonumber(f[3]), stock = f[4], done = f[5], down = {} }
        if #f[6] ~= 10 then return nil end
        for i = 1, 10 do g.down[i] = byte(f[6], i) - 48 end
    else
        return nil
    end
    g.cols = {}
    for col in (f[#f - 1] .. ","):gmatch("([^,]*),") do g.cols[#g.cols + 1] = col end
    g.log = f[#f]
    if #g.cols ~= (g.kind == P.FREECELL and 8 or 10) or not g.moves or #g.log % 4 ~= 0 then return nil end
    return g
end

return Save
end
