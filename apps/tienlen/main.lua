-- Tien Len (Tiến lên miền Nam) for Xteink Pokemon (Lua app): you against
-- three computer players. The rules live in logic.lua, dealing and moves in
-- game.lua, the computer players in ai.lua and saving in save.lua. The table
-- screen is two modules, view.lua (drawing) and input.lua, and the start
-- menu is tlmenu.lua. Only one screen module - or the computer players
-- while they move - is in memory at a time: all of it at once does not fit
-- the X3. This file holds the state and the flow.
--
-- Buttons (X3/X4): Left/Right move along your cards and on to the Play and
-- Pass buttons, Confirm picks a card (or presses the button under the
-- cursor); the side buttons are shortcuts: Down plays the picked cards, Up
-- passes. Back opens the menu. Touch (X4 Pro): tap cards to pick them, tap
-- Play or Pass.

local T = smudge.dofile("logic.lua")

-- State shared with the screen modules.
local S = {
    LEVELS = { "Normal", "Hard" },
    NAMES = { "You", "East", "North", "West" },
    PLACES = { "1st", "2nd", "3rd", "4th" },
    w = 480, h = 800, m = {}, touch = false,
    L = {},           -- layout
    level = 1,
    game = nil,
    sel = {},         -- picked cards: sel[card] = true
    cursor = 1,       -- 1..#hand: a card; #hand + 1: Play; #hand + 2: Pass
    note = nil,       -- why a play was refused
    points = { 0, 0, 0, 0 },
    handsPlayed = 0,
    leader = 0,       -- who leads the next hand (0: the 3 of spades)
    stats = {},       -- per level: { won, played }
    want = "menu",    -- the screen to show: "menu" or "board"
    deal = nil,       -- a new hand at this level, dealt between screens
}
local AI_STEP_MS = 800

local screen, screenName = nil, nil
local stepDue, drawn = nil, false

local function rand(n) return math.random(1, n) end

-- game.lua, loaded for the moment it is needed: callers keep it in a local.
function S.rules() return smudge.dofile("game.lua")(T) end

function S.saveTable()
    local p = S.points
    smudge.save("pts", string.format("%d,%d,%d,%d,%d", p[1], p[2], p[3], p[4], S.handsPlayed))
    smudge.save("lead", tostring(S.leader))
end

function S.saveGame()
    local text = ""
    if S.game and not S.game.over then text = S.level .. "#" .. smudge.dofile("save.lua")(T).serialize(S.game) end
    smudge.save("game", text)
end

-- Starts the computer players' moves when it is their turn.
function S.wake()
    local g = S.game
    if g and not g.over and g.turn ~= 1 then stepDue, drawn = smudge.millis() + AI_STEP_MS, false end
end

function S.show(name)
    S.want = name
    if name == "menu" then
        stepDue = nil
        S.saveGame()
    end
    smudge.request_update()
end

-- After a real move (G: game.lua, already loaded by the caller).
function S.afterMove(G)
    local g = S.game
    if g.over then
        local pts = G.handPoints(g)
        for s = 1, 4 do S.points[s] = S.points[s] + pts[s] end
        S.handsPlayed = S.handsPlayed + 1
        S.leader = g.order[1]
        local st = S.stats[S.level]
        st[2] = st[2] + 1
        if S.leader == 1 then st[1] = st[1] + 1 end
        smudge.save("st" .. S.level, st[1] .. "," .. st[2])
        S.saveTable()
        S.saveGame()
    end
    S.wake()
    smudge.request_update()
end

-- The picked cards, in hand order.
function S.selected()
    local out = {}
    for _, c in ipairs(S.game.hands[1]) do
        if S.sel[c] then out[#out + 1] = c end
    end
    return out
end

-- Layout ---------------------------------------------------------------------

local function layout()
    local L, m, h, w = S.L, S.m, S.h, S.w
    local top = m.top_padding + m.header_height + 6
    L.top = top
    L.cw, L.ch, L.lift = 56, 84, 20
    L.handY = h - m.button_hints_height - 14 - L.ch
    L.btnW, L.btnH = 140, 50
    L.btnY = L.handY - L.lift - 10 - L.btnH
    L.playX, L.passX = w // 2 - L.btnW - 10, w // 2 + 10
    L.boxW, L.boxH = 150, 58
    L.tcw, L.tch = 60, 86
    -- The table (a line of text, the cards, a line under them) centred
    -- between the seats and the buttons.
    local above = top + 104 + L.boxH + 14
    L.tableY = math.max(above, (above + L.btnY - (L.tch + 70)) // 2)
end

-- Spacing of n cards cw wide in `width`.
function S.rowStep(n, cw, width)
    if n <= 1 then return 0 end
    return math.min(cw + 4, (width - cw) // (n - 1))
end

-- x of your card k of n, and the spacing.
function S.handX(k, n)
    local L = S.L
    local step = S.rowStep(n, L.cw, S.w - 24)
    return (S.w - (step * (n - 1) + L.cw)) // 2 + (k - 1) * step, step
end

-- Screens --------------------------------------------------------------------

-- Loads a screen module ("tlmenu", "view" or "input"), dropping the other
-- first; a new hand asked for is dealt in between.
local function loadScreen(name)
    if screenName == name and not S.deal then return end
    screen, screenName = nil, nil
    collectgarbage("collect")
    if S.deal then
        S.level, S.deal, S.game = S.deal, nil, nil
        collectgarbage("collect")
        local G = S.rules()
        S.game = G.new(rand, S.leader > 0 and S.leader or nil)
        S.sel, S.cursor, S.note = {}, 1, nil
        if S.game.over then S.afterMove(G) else S.wake() end -- an instant win at the deal
        G = nil
        collectgarbage("collect")
    end
    screen = smudge.dofile(name .. ".lua")(T, S)
    screenName = name
end

-- The computer players' moves: one at a time, or all of them once you are
-- out. The screen goes first to make room, and the players' module and
-- game.lua take turns: they are never in memory together.
local function step()
    local g = S.game
    if not g or g.over or g.turn == 1 then return end
    screen, screenName = nil, nil
    repeat
        collectgarbage("collect")
        local seat = g.turn
        local AI = smudge.dofile("ai.lua")(T)
        local c = AI.choose(g, seat, S.level, rand)
        AI = nil
        collectgarbage("collect")
        local G = S.rules()
        if not c or G.play(g, seat, c) then
            if G.pass(g, seat) then G.play(g, seat, { g.hands[seat][1] }) end
        end
        if g.over or not g.out[1] then
            S.note = nil
            S.afterMove(G)
        end
        G = nil
    until g.over or not g.out[1]
end

-- Callbacks ------------------------------------------------------------------

function on_init()
    S.w, S.h = smudge.get_bounds()
    S.m = smudge.get_metrics()
    S.touch = smudge.has_touch()
    layout()
    math.randomseed(smudge.millis() + smudge.time())
    for i = 1, #S.LEVELS do
        local a, b = (smudge.load("st" .. i, "0,0")):match("(%d+),(%d+)")
        S.stats[i] = { tonumber(a) or 0, tonumber(b) or 0 }
    end
    local p1, p2, p3, p4, n = smudge.load("pts", ""):match("^(%d+),(%d+),(%d+),(%d+),(%d+)$")
    if n then S.points, S.handsPlayed = { tonumber(p1), tonumber(p2), tonumber(p3), tonumber(p4) }, tonumber(n) end
    S.leader = tonumber(smudge.load("lead", "0")) or 0
    local lv, data = smudge.load("game", ""):match("^(%d)#(.+)$")
    if data then
        S.game = smudge.dofile("save.lua")(T).deserialize(data)
        S.level = math.max(1, math.min(#S.LEVELS, tonumber(lv) or 1))
        collectgarbage("collect")
    end
    S.show("menu")
end

function on_draw()
    smudge.clear()
    loadScreen(S.want == "menu" and "tlmenu" or "view")
    screen.draw()
    if stepDue then drawn = true end
end

function on_update()
    if S.want == "board" and stepDue and drawn and smudge.millis() >= stepDue then
        stepDue = nil
        step()
    end
end

function on_exit()
    screen, screenName = nil, nil
    collectgarbage("collect")
    S.saveGame()
end

function on_button(btn, pressed)
    if not pressed then return end
    loadScreen(S.want == "menu" and "tlmenu" or "input")
    screen.button(btn)
end

function on_tap(x, y)
    loadScreen(S.want == "menu" and "tlmenu" or "input")
    screen.tap(x, y)
end
