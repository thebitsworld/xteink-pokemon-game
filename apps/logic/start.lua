-- Reads the stats and saves when the app starts, and opens a kind's saved
-- puzzle (make.lua makes new ones). Loaded only for that moment. Usage:
--   smudge.dofile("start.lua")(S).init()
--   smudge.dofile("start.lua")(S).continue(k)
-- S: main.lua's state.

return function(S)
local M = {}
-- Stats and saved puzzles, when the app starts.
function M.init()
    for k, info in ipairs(S.KINDS) do
        S.stats[k] = {}
        for lv = 1, #info.levels do
            local a, b = smudge.load("st_" .. info.id .. lv, "0,0"):match("(%d+),(%d+)")
            S.stats[k][lv] = { tonumber(a) or 0, tonumber(b) or 0 }
        end
        local lv, ms = smudge.load("pz_" .. info.id, ""):match("^(%d+);.*,(%d+)$")
        if lv then S.saved[k] = { tonumber(lv), tonumber(ms) } end
    end
end


-- Opens kind k's saved puzzle (or the one in memory).
function M.continue(k)
    if S.kind ~= k or not S.P then
        S.savePuzzle()
        S.drop(true)
        local K = smudge.dofile(S.KINDS[k].logic)
        local lv, data = smudge.load("pz_" .. S.KINDS[k].id, ""):match("^(%d+);(.*)$")
        local P = data and K.deserialize(data)
        lv = tonumber(lv)
        if not P or not S.KINDS[k].levels[lv] then
            S.saved[k] = nil
            S.failed = "The saved puzzle could not be read."
            S.show("options")
            return
        end
        S.kind, S.level, S.K, S.P = k, lv, K, P
        S.resetCursor()
    end
    S.show("play")
end

return M
end
