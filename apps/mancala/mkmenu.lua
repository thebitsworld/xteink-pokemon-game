-- Mancala start menu: the shared menu.lua with this game's items, and a
-- second page for the options and the take-back (nine buttons with notes do
-- not fit one page). Usage:
--   local screen = smudge.dofile("mkmenu.lua")(C, S)
-- with C the rules (logic.lua) and S the app's state (main.lua).

return function(C, S)
local M = {}
local menu = smudge.dofile("menu.lua")("Mancala", { "Sow the seeds, fill your store." })
local opts = nil -- the options page while it shows

local function playing() return S.s and S.s[C.OVER] == 0 end

-- Moves to take back: one with two players; against the device back to
-- before your last move (the device's replies and extra turns go with it).
local function takeBackTo()
    local h = S.hist
    if not S.vsDevice() then return #h > 0 and #h - 1 or nil end
    for k = #h, 1, -1 do
        if h:byte(k) - 64 <= 6 then return k - 1 end
    end
    return nil
end

local function items()
    local out = {}
    if opts then
        out[1] = { label = S.first == 1 and "You play first" or "You play second", mode = -2,
                   note = "Next game vs device" }
        out[2] = { label = "Seeds per pit: " .. S.seeds, mode = -4, note = "Next game: 3, 4, 5 or 6" }
        out[3] = { label = "Empty capture: " .. (S.ec == 1 and "On" or "Off"), mode = -5,
                   note = "Next game: a lone last seed is taken" }
        if playing() and takeBackTo() then out[4] = { label = "Take back a move", mode = -3 } end
        out[#out + 1] = { label = "Done", mode = -6 }
        return out
    end
    if playing() then out[1] = { label = "Resume", mode = 0 } end
    for i, L in ipairs(S.LEVELS) do
        local r = S.records[i]
        out[#out + 1] = { label = "vs Device: " .. L.name, mode = i,
                          note = string.format("Won %d  Lost %d  Drawn %d", r[1], r[2], r[3]) }
    end
    out[#out + 1] = { label = "Two players", mode = #S.LEVELS + 1 }
    out[#out + 1] = { label = "Options", mode = -7, note = playing() and "Who opens, seeds, take back" or
                      "Who opens, seeds" }
    out[#out + 1] = { label = "Exit", mode = -1 }
    return out
end

local function page(on)
    if on then
        -- The options page has no "How to play" (menu.lua adds it when the
        -- app has a guide): its instance is made without the guide check.
        local has = smudge.has_guide
        smudge.has_guide = nil
        opts = smudge.dofile("menu.lua")("Mancala options", { "Options apply to the next new game." })
        smudge.has_guide = has
    else
        opts = nil
        collectgarbage("collect")
    end
    smudge.request_update()
end

local function choose(item)
    if not item then return end
    if item == "exit" then
        if opts then page(false) else smudge.exit() end
        return
    end
    local mode = item.mode
    if mode == -1 then
        smudge.exit()
    elseif mode == -7 then
        page(true)
    elseif mode == -6 then
        page(false)
    elseif mode == -2 or mode == -4 or mode == -5 then
        if mode == -2 then S.first = 3 - S.first end
        if mode == -4 then S.seeds = S.seeds == 6 and 3 or S.seeds + 1 end
        if mode == -5 then S.ec = 1 - S.ec end
        S.saveOptions()
        smudge.request_update()
    elseif mode == -3 then
        local n = takeBackTo()
        local g = S.g
        local s = n and C.replay(g[1], g[2], g[3] == 1, S.hist, n, S.info)
        if s then
            S.s, S.hist = s, S.hist:sub(1, n)
            S.lastSide = n == 0 and 0 or (S.hist:byte(n) - 64 <= 6 and 1 or 2)
            S.prev, S.note = {}, "Move taken back"
            S.save()
            S.refresh()
        end
        S.show("board")
    else
        if mode > 0 then S.newGame(mode) else S.refresh() end
        S.show("board")
    end
end

local function cur() return opts or menu end
function M.draw() cur().draw(items()) end
function M.button(btn) choose(cur().button(btn, items())) end
function M.tap(x, y) choose(cur().tap(x, y, items())) end

return M
end
