-- Saving a Sam Loc hand in progress. Usage:
--   local S = smudge.dofile("save.lua")(T)
-- with T the rules (logic.lua). Loaded only to save or restore a hand.
--
-- "turn|curBy|lead|ask|sam|passed|played|table|hand1|hand2|hand3|hand4",
-- ask empty when nobody is asked about Sam, sam 0 for no declarer, cards as
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

local function flags(list)
    local t = {}
    for s = 1, 4 do t[s] = list[s] and "1" or "0" end
    return table.concat(t)
end

function S.serialize(g)
    local parts = { tostring(g.turn), tostring(g.curBy), tostring(g.lead), g.ask and tostring(g.ask) or "",
                    tostring(g.sam or 0), flags(g.passed), flags(g.played), cardsText(g.cur or {}) }
    for s = 1, 4 do parts[#parts + 1] = cardsText(g.hands[s]) end
    return table.concat(parts, "|")
end

local function seatOk(n, low) return n and n >= low and n <= 4 end

-- A game from S.serialize, or nil if the text is not a valid hand in play.
function S.deserialize(text)
    local f = {}
    for part in (text .. "|"):gmatch("([^|]*)|") do f[#f + 1] = part end
    if #f ~= 12 then return nil end
    local g = { hands = {}, passed = {}, played = {}, over = false }
    g.turn, g.curBy, g.lead = tonumber(f[1]), tonumber(f[2]), tonumber(f[3])
    local sam = tonumber(f[5])
    if not seatOk(g.turn, 1) or not seatOk(g.curBy, 0) or not seatOk(g.lead, 1) or not seatOk(sam, 0) then
        return nil
    end
    if sam > 0 then g.sam = sam end
    if f[4] ~= "" then
        g.ask = tonumber(f[4])
        if not g.ask or g.ask < 0 or g.ask > 3 or g.sam then return nil end
    end
    if #f[6] ~= 4 or #f[7] ~= 4 then return nil end
    for s = 1, 4 do
        g.passed[s] = f[6]:sub(s, s) == "1"
        g.played[s] = f[7]:sub(s, s) == "1"
    end
    local seen = {}
    local function take(cards)
        if not cards then return false end
        for _, c in ipairs(cards) do
            if seen[c] then return false end
            seen[c] = true
        end
        return true
    end
    local cur = textCards(f[8])
    if not take(cur) then return nil end
    if #cur > 0 then
        g.cur = cur
        g.ck, g.cl, g.ct = T.classify(cur)
        if not g.ck or g.curBy < 1 then return nil end
    end
    for s = 1, 4 do
        local hand = textCards(f[8 + s])
        if not take(hand) or #hand == 0 or #hand > 10 or (#hand < 10 and not g.played[s]) then return nil end
        table.sort(hand)
        g.hands[s] = hand
    end
    return g
end

return S
end
