-- Saving a Tien Len hand in progress. Usage:
--   local S = smudge.dofile("save.lua")(T)
-- with T the rules (logic.lua). Loaded only to save or restore a hand.
--
-- "turn|curBy|need3|passed|order|table|hand1|hand2|hand3|hand4", cards as
-- one character each (48 + card).

return function(T)
local S = {}

local function cardsText(cards)
    local t = {}
    for i, c in ipairs(cards) do t[i] = string.char(48 + c) end
    return table.concat(t)
end

local function textCards(s)
    local out = {}
    for i = 1, #s do
        local c = s:byte(i) - 48
        if c < 0 or c > 51 then return nil end
        out[i] = c
    end
    return out
end

function S.serialize(g)
    local passed = {}
    for s = 1, 4 do passed[s] = g.passed[s] and "1" or "0" end
    local order = {}
    for i, s in ipairs(g.order) do order[i] = tostring(s) end
    local parts = { tostring(g.turn), tostring(g.curBy), g.need3 and "1" or "0", table.concat(passed),
                    table.concat(order), cardsText(g.cur or {}) }
    for s = 1, 4 do parts[#parts + 1] = cardsText(g.hands[s]) end
    return table.concat(parts, "|")
end

-- A game from S.serialize, or nil if the text is not a valid hand in play.
function S.deserialize(text)
    local f = {}
    for part in (text .. "|"):gmatch("([^|]*)|") do f[#f + 1] = part end
    if #f ~= 10 then return nil end
    local g = { hands = {}, passed = {}, out = {}, order = {}, over = false }
    g.turn, g.curBy = tonumber(f[1]), tonumber(f[2])
    if not g.turn or g.turn < 1 or g.turn > 4 or not g.curBy or g.curBy < 0 or g.curBy > 4 then return nil end
    g.need3 = f[3] == "1"
    for s = 1, 4 do g.passed[s] = f[4]:sub(s, s) == "1" end
    for i = 1, #f[5] do
        local s = tonumber(f[5]:sub(i, i))
        if not s or s < 1 or s > 4 or g.out[s] then return nil end
        g.order[i], g.out[s] = s, true
    end
    if #g.order >= 3 then return nil end
    local seen = {}
    local function take(cards)
        if not cards then return false end
        for _, c in ipairs(cards) do
            if seen[c] then return false end
            seen[c] = true
        end
        return true
    end
    local cur = textCards(f[6])
    if not take(cur) then return nil end
    if #cur > 0 then
        g.cur = cur
        g.ck, g.cl, g.ct = T.classify(cur)
        if not g.ck or g.curBy < 1 then return nil end
    end
    for s = 1, 4 do
        local hand = textCards(f[6 + s])
        if not take(hand) then return nil end
        table.sort(hand)
        g.hands[s] = hand
        if (#hand == 0) ~= (g.out[s] == true) then return nil end
    end
    if g.out[g.turn] then return nil end
    return g
end

return S
end
