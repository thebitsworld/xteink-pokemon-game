-- Checkers for Xteink Pokemon (Lua app).
-- Rules live in logic.lua, the computer opponent in ai.lua (loaded for each
-- of its moves) and the start menu in menu.lua (loaded while it is on
-- screen); this file draws the game and handles input.
--
-- Buttons (X3/X4): the arrows move the cursor, Confirm picks a piece and then
-- the square to move it to (each landing square in turn for a chain of
-- jumps), Back drops the piece or opens the menu. Touch (X4 Pro): tap the
-- piece, then the square(s).

local C = smudge.dofile("logic.lua")

local LEVELS = {
    { name = "Easy", depth = 2, nodes = 600, random = 25 },
    { name = "Normal", depth = 4, nodes = 5000, random = 0 },
    { name = "Hard", depth = 6, nodes = 40000, random = 0 },
}
local THINK_MS = 1500

local w, h = 480, 800
local m = {}
local touch = false

local menu = nil
local mode = 1        -- 1..3: against the device at that level; 4: two players
local game = nil
local legal = {}      -- the legal moves of the side to move
local picked = {}     -- squares picked so far: the piece, then landing squares
local curC, curR = 3, 6
local thinking, thinkingDrawn = false, false
local records = {}

local function rand(n) return math.random(1, n) end
local function vsDevice() return mode <= #LEVELS end
local function humanTurn() return game.winner == 0 and (not vsDevice() or game.turn == C.LIGHT) end

local function refresh()
    legal = C.moves(game)
    picked = {}
    thinking = vsDevice() and game.winner == 0 and game.turn == C.DARK
    thinkingDrawn = false
end

local function newGame(newMode)
    mode = newMode
    game = C.new()
    curC, curR = 3, 6
    refresh()
end

local function menuItems()
    local items = {}
    if game and game.winner == 0 then items[1] = { label = "Resume", mode = 0 } end
    for i, L in ipairs(LEVELS) do
        local r = records[i]
        items[#items + 1] = { label = "vs Device: " .. L.name, mode = i,
                              note = string.format("Won %d  Lost %d  Drawn %d", r[1], r[2], r[3]) }
    end
    items[#items + 1] = { label = "Two players", mode = #LEVELS + 1, note = "Take turns on this reader" }
    items[#items + 1] = { label = "Exit", mode = -1 }
    return items
end

local function openMenu()
    thinking = false
    collectgarbage("collect")
    menu = smudge.dofile("menu.lua")("Checkers", { "Jump to capture; captures are compulsory.",
                                                   "Reach the far row to crown a king." })
    smudge.request_update()
end

local function choose(item)
    if not item then return end
    if item == "exit" or item.mode < 0 then
        smudge.exit()
        return
    end
    if item.mode > 0 then newGame(item.mode) else refresh() end
    menu = nil
    collectgarbage("collect")
    smudge.request_update()
end

-- Layout ---------------------------------------------------------------------

local function contentTop() return m.top_padding + m.header_height + 8 end

local function boardRect()
    local top = contentTop() + 44
    local cell = math.min((w - 16) // 8, (h - m.button_hints_height - 40 - top) // 8)
    return { x = (w - 8 * cell) // 2, y = top, cell = cell }
end

local function menuButtonX() return w - 96 end

-- Drawing --------------------------------------------------------------------

local function pieceAt(B, i, v)
    local s = B.cell
    local cx = B.x + (C.colOf(i) - 1) * s + s // 2
    local cy = B.y + (C.rowOf(i) - 1) * s + s // 2
    local r = s * 38 // 100
    local light = C.owner(v) == C.LIGHT
    smudge.circle(cx, cy, r, true, not light)
    smudge.circle(cx, cy, r, false, 2)
    if C.isKing(v) then
        -- A king carries a ring in the opposite colour.
        local k = r * 6 // 10
        smudge.circle(cx, cy, k, true, light)
        smudge.circle(cx, cy, k - 4, true, not light)
    end
end

local function cellOutline(B, i, inset, thick, black)
    local s = B.cell
    local x, y = B.x + (C.colOf(i) - 1) * s, B.y + (C.rowOf(i) - 1) * s
    smudge.rect(x + inset, y + inset, s - 2 * inset, s - 2 * inset, false, black, thick)
end

local function statusText()
    if game.winner ~= 0 then
        if game.winner == -1 then return "Draw: forty moves each without a capture" end
        if vsDevice() then return game.winner == C.LIGHT and "You win!" or "The device wins" end
        return (game.winner == C.LIGHT and "White" or "Black") .. " wins!"
    end
    if thinking then return "Device is thinking..." end
    local who = vsDevice() and "Your move" or ((game.turn == C.LIGHT and "White" or "Black") .. " to move")
    if #legal > 0 and C.capCount(legal[1]) > 0 then who = who .. " - you must capture" end
    return who
end

local function drawPlay()
    smudge.header("Checkers", vsDevice() and LEVELS[mode].name or "2 players")
    local top = contentTop()
    local text = statusText()
    local maxW = (touch and menuButtonX() or w) - 24
    smudge.text(16, top + 10, text, smudge.text_width(text, "ui12", "bold") > maxW and "ui10" or "ui12", "bold",
                "left", true)
    if touch then
        smudge.rounded_rect(menuButtonX(), top, 80, 38, 6, false, 2)
        smudge.text(menuButtonX() + 40, top + 8, "Menu", "ui12", "bold", "center", true)
    end

    local B = boardRect()
    local s = B.cell
    smudge.rect(B.x - 2, B.y - 2, 8 * s + 4, 8 * s + 4, false, true, 2)
    local human = humanTurn() and not thinking
    local options = (human and #picked > 0) and C.matching(legal, picked) or {}
    for i = 1, 64 do
        local c, r = C.colOf(i), C.rowOf(i)
        if C.isDark(c, r) then
            smudge.rect_dither(B.x + (c - 1) * s, B.y + (r - 1) * s, s, s, false)
        end
        local v = game.board[i]
        if v ~= 0 then pieceAt(B, i, v) end
    end
    -- The last move: its first and last squares outlined.
    if game.last and #picked == 0 then
        cellOutline(B, C.from(game.last), 2, 2, true)
        cellOutline(B, C.to(game.last), 2, 2, true)
    end
    if human then
        if #picked == 0 then
            -- Pieces that can move get a dot (only the capturing ones when a
            -- capture is compulsory).
            local seen = {}
            for _, mv in ipairs(legal) do
                local i = C.from(mv)
                if not seen[i] then
                    seen[i] = true
                    smudge.circle(B.x + (C.colOf(i) - 1) * s + s // 2, B.y + (C.rowOf(i) - 1) * s + s // 2, s // 10,
                                  true, C.owner(game.board[i]) == C.LIGHT)
                end
            end
        else
            cellOutline(B, picked[1], 1, 4, true)
            for n = 2, #picked do
                local i = picked[n]
                smudge.circle(B.x + (C.colOf(i) - 1) * s + s // 2, B.y + (C.rowOf(i) - 1) * s + s // 2, s // 6, true,
                              true)
            end
            -- Where the piece can go next.
            for _, mv in ipairs(options) do
                local i = C.pathLen(mv) > #picked and C.pathAt(mv, #picked + 1)
                if i then
                    local cx, cy = B.x + (C.colOf(i) - 1) * s + s // 2, B.y + (C.rowOf(i) - 1) * s + s // 2
                    smudge.circle(cx, cy, s // 4, true, false)
                    smudge.circle(cx, cy, s // 4, false, 3)
                end
            end
        end
    end
    if not touch and human then
        local x, y = B.x + (curC - 1) * s, B.y + (curR - 1) * s
        smudge.rect(x - 3, y - 3, s + 6, s + 6, false, false, 3)
        smudge.rect(x, y, s, s, false, true, 4)
        smudge.rect(x + 4, y + 4, s - 8, s - 8, false, false, 2)
    end
    -- Pieces left, under the board.
    local lm, lk = C.count(game.board, C.LIGHT)
    local dm, dk = C.count(game.board, C.DARK)
    smudge.centered_text(B.y + 8 * s + 10, string.format("%s %d   %s %d", vsDevice() and "You" or "White", lm + lk,
                         vsDevice() and "Device" or "Black", dm + dk), "ui10", "bold", true)
    if game.winner ~= 0 then smudge.popup(text) end
    if touch then
        smudge.button_hints("", "", "", "")
    elseif game.winner ~= 0 then
        smudge.button_hints("Menu", "Again", "", "")
    else
        smudge.button_hints(#picked > 0 and "Drop" or "Menu", #picked > 0 and "Move" or "Pick", "<", ">")
    end
end

-- Game flow ------------------------------------------------------------------

local function finishIfOver()
    if game.winner == 0 or not vsDevice() then return end
    local r = records[mode]
    local i = game.winner == C.LIGHT and 1 or (game.winner == C.DARK and 2 or 3)
    r[i] = r[i] + 1
    smudge.save("rec" .. mode, string.format("%d,%d,%d", r[1], r[2], r[3]))
end

local function play(mv)
    C.play(game, mv)
    finishIfOver()
    refresh()
    smudge.request_update()
end

-- Confirm or a tap on square `i`.
local function activate(i)
    if not humanTurn() or thinking then return end
    if #picked > 0 then
        local options = C.matching(legal, picked)
        for _, mv in ipairs(options) do
            if C.pathLen(mv) > #picked and C.pathAt(mv, #picked + 1) == i then
                picked[#picked + 1] = i
                for _, done in ipairs(C.matching(legal, picked)) do
                    if C.pathLen(done) == #picked then
                        play(done)
                        return
                    end
                end
                smudge.request_update()
                return
            end
        end
        if i == picked[#picked] then
            picked[#picked] = nil -- step back
            smudge.request_update()
            return
        end
    end
    -- Pick (or switch to) a piece that can move.
    for _, mv in ipairs(legal) do
        if C.from(mv) == i then
            picked = { i }
            smudge.request_update()
            return
        end
    end
end

local function deviceMove()
    collectgarbage("collect")
    local AI = smudge.dofile("ai.lua")(C)
    local L = LEVELS[mode]
    local mv = AI.choose(game, L.depth, L.nodes, L.random, rand, smudge.millis, THINK_MS)
    AI = nil
    if mv then play(mv) else refresh() end
    collectgarbage("collect")
end

-- Callbacks ------------------------------------------------------------------

function on_init()
    w, h = smudge.get_bounds()
    m = smudge.get_metrics()
    touch = smudge.has_touch()
    math.randomseed(smudge.millis() + smudge.time())
    for i = 1, #LEVELS do
        local a, b, c = (smudge.load("rec" .. i, "0,0,0")):match("(%d+),(%d+),(%d+)")
        records[i] = { tonumber(a) or 0, tonumber(b) or 0, tonumber(c) or 0 }
    end
    local savedMode, data = smudge.load("game", ""):match("^(%d+)#(.+)$")
    local g = data and C.deserialize(data)
    if g and g.winner == 0 then
        mode, game = tonumber(savedMode), g
        refresh()
    end
    openMenu()
end

function on_draw()
    smudge.clear()
    if menu then menu.draw(menuItems()) else drawPlay() end
    if thinking then thinkingDrawn = true end
end

function on_update()
    if not menu and thinking and thinkingDrawn then deviceMove() end
end

function on_exit()
    smudge.save("game", (game and game.winner == 0) and (mode .. "#" .. C.serialize(game)) or "")
end

function on_button(btn, pressed)
    if not pressed then return end
    if menu then
        choose(menu.button(btn, menuItems()))
    elseif btn == "back" then
        if #picked > 0 then
            picked = {}
            smudge.request_update()
        else
            openMenu()
        end
    elseif game.winner ~= 0 then
        if btn == "confirm" then choose({ mode = mode }) end
    elseif btn == "confirm" then
        activate(C.index(curC, curR))
    else
        if btn == "left" then curC = (curC - 2) % 8 + 1
        elseif btn == "right" then curC = curC % 8 + 1
        elseif btn == "up" or btn == "page_back" then curR = (curR - 2) % 8 + 1
        elseif btn == "down" or btn == "page_forward" then curR = curR % 8 + 1 end
        smudge.request_update()
    end
end

function on_tap(x, y)
    if menu then
        choose(menu.tap(x, y, menuItems()))
        return
    end
    if smudge.in_rect(x, y, menuButtonX(), contentTop(), 80, 38) then
        openMenu()
        return
    end
    if game.winner ~= 0 then
        choose({ mode = mode })
        return
    end
    local B = boardRect()
    if smudge.in_rect(x, y, B.x, B.y, 8 * B.cell, 8 * B.cell) then
        curC, curR = (x - B.x) // B.cell + 1, (y - B.y) // B.cell + 1
        activate(C.index(curC, curR))
    end
end
