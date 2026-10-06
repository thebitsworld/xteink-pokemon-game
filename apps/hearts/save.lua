-- Saving and restoring a Hearts game. Usage:
--   local Save = smudge.dofile("save.lua")(H)   -- H: the rules (logic.lua)
-- The app loads it only when it starts and when it closes.

return function(H)
local Save = {}

-- Save/restore (between tricks or mid-trick): every hand, the trick, the
-- scores. The cards must add up to the deck exactly once.
local function list(t) return table.concat(t, ".") end
local function parse(s)
    local out = {}
    for n in s:gmatch("%d+") do out[#out + 1] = tonumber(n) end
    return out
end

function Save.serialize(g)
    local trick = {}
    for k, t in ipairs(g.trick) do trick[k] = t.seat * 100 + t.card end
    return table.concat({ g.hand, g.phase, g.turn or 0, g.leader or 0, g.tricks, g.broken and 1 or 0, list(g.scores),
                          list(g.taken), list(trick), g.played, list(g.hands[1]), list(g.hands[2]),
                          list(g.hands[3]), list(g.hands[4]) }, "|")
end

function Save.deserialize(s)
    local f = {}
    for x in (s .. "|"):gmatch("([^|]*)|") do f[#f + 1] = x end
    if #f ~= 14 or (f[2] ~= "pass" and f[2] ~= "play") then return nil end
    local g = { hand = tonumber(f[1]), phase = f[2], turn = tonumber(f[3]), leader = tonumber(f[4]),
                tricks = tonumber(f[5]), broken = f[6] == "1", scores = parse(f[7]), taken = parse(f[8]),
                trick = {}, played = f[10], hands = {}, over = false }
    if not g.hand or #g.scores ~= 4 or #g.taken ~= 4 or not g.tricks then return nil end
    for _, v in ipairs(parse(f[9])) do g.trick[#g.trick + 1] = { seat = v // 100, card = v % 100 } end
    if #g.played ~= 52 or g.played:find("[^01]") then return nil end
    local seen, count = {}, 0
    local function mark(c)
        if c < 1 or c > 52 or seen[c] then return false end
        seen[c], count = true, count + 1
        return true
    end
    for c = 1, 52 do
        if H.wasPlayed(g, c) and not mark(c) then return nil end
    end
    for s = 1, 4 do
        g.hands[s] = parse(f[10 + s])
        for _, c in ipairs(g.hands[s]) do
            if not mark(c) then return nil end
        end
    end
    if count ~= 52 then return nil end
    for _, t in ipairs(g.trick) do
        if not H.wasPlayed(g, t.card) or t.seat < 1 or t.seat > 4 then return nil end
    end
    if g.phase == "play" and (not g.turn or g.turn < 1 or g.turn > 4) then return nil end
    return g
end

return Save
end
