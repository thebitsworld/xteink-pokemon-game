-- Phom (Phỏm, tá lả) for Xteink Pokemon (Lua app): you against three
-- computer players. The melds live in logic.lua, dealing and moves in
-- game.lua, the computer players in ai.lua and saving in save.lua; they are
-- loaded only for a move (game.lua leaves what the table screen needs in
-- the hand: the melds, whether you may eat, the cards you may throw). The
-- table screen is two modules, view.lua (drawing) and input.lua, and the
-- start menu is phmenu.lua. Only one screen module - or the computer
-- players while they move - is in memory at a time. This file holds the
-- state and the flow.
--
-- Starting up (the layout, the records, a saved hand) is boot.lua, run once.
--
-- Buttons (X3/X4): Left/Right move along your cards and on to the Eat,
-- Draw and Discard buttons, Confirm picks a card (or presses the button
-- under the cursor); the side buttons are shortcuts: Down draws or throws
-- the picked card, Up eats. Back opens the menu. Touch (X4 Pro): tap a card
-- to pick it, tap Eat, Draw or Discard.

-- State shared with the screen modules.
local S = {
    LEVELS = { "Normal", "Hard" },
    NAMES = { "You", "East", "North", "West" },
    PLACES = { "1st", "2nd", "3rd", "4th" },
    w = 480, h = 800, m = {}, touch = false,
    L = {},           -- layout
    level = 1,
    game = nil,
    sel = nil,        -- the picked card
    cursor = 1,       -- 1..#hand: a card; then Eat, Draw, Discard
    note = nil,       -- why a move was refused
    points = { 0, 0, 0, 0 },
    handsPlayed = 0,
    leader = 0,       -- who gets 10 cards next hand (0: a random seat)
    stats = {},       -- per level: { won, played }
    want = "menu",    -- the screen to show: "menu" or "board"
    deal = nil,       -- a new hand at this level, dealt between screens
    act = nil,        -- your move, asked for by input.lua
}
local AI_STEP_MS = 900

local screen, screenName = nil, nil
local stepDue, drawn = nil, false

local function rand(n) return math.random(1, n) end

-- The rules (logic.lua), loaded for the moment they are needed.
local function rulesOf(file) return smudge.dofile(file)(smudge.dofile("logic.lua")) end

-- game.lua, loaded for the moment it is needed: callers keep it in a local.
function S.rules() return rulesOf("game.lua") end

function S.saveTable()
    local p = S.points
    smudge.save("pts", string.format("%d,%d,%d,%d,%d", p[1], p[2], p[3], p[4], S.handsPlayed))
    smudge.save("lead", tostring(S.leader))
end

function S.saveGame()
    local text = ""
    if S.game and not S.game.over then text = S.level .. "#" .. rulesOf("save.lua").serialize(S.game) end
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
        for s = 1, 4 do S.points[s] = S.points[s] + g.pts[s] end
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

-- Layout (set in boot.lua) --------------------------------------------------

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

-- x of the Eat (1), Draw (2) and Discard (3) buttons.
function S.btnX(i) return S.L.btnX + (i - 1) * (S.L.btnW + 12) end

-- Screens --------------------------------------------------------------------

-- Loads a screen module ("phmenu", "view" or "input"), dropping the other
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
        S.sel, S.cursor, S.note = nil, 1, nil
        if S.game.over then S.afterMove(G) else S.wake() end -- ù at the deal
        G = nil
        collectgarbage("collect")
    end
    screen = smudge.dofile(name .. ".lua")(S)
    screenName = name
end

-- One computer player's turn: eat or draw, then throw. The screen goes
-- first to make room, and the players' module and game.lua take turns:
-- they are never in memory together.
local function step()
    local g = S.game
    if not g or g.over or g.turn == 1 then return end
    screen, screenName = nil, nil
    collectgarbage("collect")
    local seat = g.turn
    local AI, G
    if g.phase == "take" then
        AI = rulesOf("ai.lua")
        local eat = AI.take(g, seat, S.level)
        AI = nil
        collectgarbage("collect")
        G = S.rules()
        G.take(g, seat, eat)
        G = nil
        collectgarbage("collect")
    end
    if not g.over then
        AI = rulesOf("ai.lua")
        local c = AI.discard(g, seat, S.level, rand)
        AI = nil
        collectgarbage("collect")
        G = S.rules()
        G.discard(g, seat, c)
    else
        G = S.rules()
    end
    S.note = nil
    S.afterMove(G)
    G = nil
end

-- Your move asked for by input.lua ("eat", "draw" or "throw"), made once
-- the screen is out of memory.
local function act(what)
    local g = S.game
    screen, screenName = nil, nil
    collectgarbage("collect")
    local G = S.rules()
    local why
    if what == "throw" then why = G.discard(g, 1, S.sel) else why = G.take(g, 1, what == "eat") end
    if why then
        S.note = why
        smudge.request_update()
        return
    end
    S.sel, S.note = nil, nil
    local hand = g.hands[1]
    if what ~= "throw" then
        for i, c in ipairs(hand) do
            if c == g.got then S.cursor = i end
        end
    elseif S.cursor > #hand then
        S.cursor = math.max(1, #hand)
    end
    S.afterMove(G)
end

-- Callbacks ------------------------------------------------------------------

function on_init()
    smudge.dofile("boot.lua")(S, rulesOf)
    collectgarbage("collect")
    S.show("menu")
end

function on_draw()
    smudge.clear()
    loadScreen(S.want == "menu" and "phmenu" or "view")
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
    loadScreen(S.want == "menu" and "phmenu" or "input")
    screen.button(btn)
    local a = S.act
    if a then
        S.act = nil
        act(a)
    end
end

function on_tap(x, y)
    loadScreen(S.want == "menu" and "phmenu" or "input")
    screen.tap(x, y)
    local a = S.act
    if a then
        S.act = nil
        act(a)
    end
end
