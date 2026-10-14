-- Saving a Phom hand in progress. Usage:
--   local S = smudge.dofile("save.lua")(T)
-- with T the rules (logic.lua). Loaded only to save or restore a hand.
--
-- "first|turn|phase|topBy|nd|stock|hand1..4|eaten1..4|thrown1..4": phase
-- "t" (take) or "d" (discard), topBy 0 when the last card thrown may no
-- longer be eaten, nd four digits, cards one character each (48 + card).

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
    local parts = { tostring(g.first), tostring(g.turn), g.phase == "take" and "t" or "d",
                    tostring(g.top and g.topBy or 0), table.concat(g.nd), cardsText(g.stock) }
    for s = 1, 4 do parts[#parts + 1] = cardsText(g.hands[s]) end
    for s = 1, 4 do parts[#parts + 1] = cardsText(g.eaten[s]) end
    for s = 1, 4 do parts[#parts + 1] = cardsText(g.disc[s]) end
    return table.concat(parts, "|")
end

-- A game from S.serialize, or nil if the text is not a valid hand in play.
function S.deserialize(text)
    local f = {}
    for part in (text .. "|"):gmatch("([^|]*)|") do f[#f + 1] = part end
    if #f ~= 18 then return nil end
    local g = { hands = {}, eaten = {}, disc = {}, nd = {}, mlen = {}, over = false }
    g.first, g.turn, g.topBy = tonumber(f[1]), tonumber(f[2]), tonumber(f[4])
    if not g.first or g.first < 1 or g.first > 4 or not g.turn or g.turn < 1 or g.turn > 4 then return nil end
    if not g.topBy or g.topBy < 0 or g.topBy > 4 then return nil end
    g.phase = f[3] == "t" and "take" or "discard"
    if #f[5] ~= 4 then return nil end
    for s = 1, 4 do
        g.nd[s] = tonumber(f[5]:sub(s, s))
        if not g.nd[s] or g.nd[s] > 4 then return nil end
    end
    local seen, total = {}, 0
    local function take(cards)
        if not cards then return nil end
        for _, c in ipairs(cards) do
            if seen[c] then return nil end
            seen[c], total = true, total + 1
        end
        return cards
    end
    g.stock = take(textCards(f[6]))
    if not g.stock then return nil end
    for s = 1, 4 do
        g.hands[s] = take(textCards(f[6 + s]))
        g.eaten[s] = textCards(f[10 + s])
        g.disc[s] = take(textCards(f[14 + s]))
        if not g.hands[s] or not g.eaten[s] or not g.disc[s] or #g.disc[s] > g.nd[s] then return nil end
        for _, c in ipairs(g.eaten[s]) do
            if not T.has(g.hands[s], c) then return nil end
        end
        local want = 9 + ((s == g.turn and g.phase == "discard") and 1 or 0)
        if #g.hands[s] ~= want then return nil end
    end
    if total ~= 52 then return nil end
    if g.topBy > 0 then
        local d = g.disc[g.topBy]
        g.top = d[#d]
        if not g.top or g.phase ~= "take" then return nil end
        g.thrown, g.lastSeat = g.top, g.topBy
    end
    for s = 1, 4 do g.mlen[s] = T.arrange(g.hands[s], g.eaten[s]) end
    return g
end

return S
end
