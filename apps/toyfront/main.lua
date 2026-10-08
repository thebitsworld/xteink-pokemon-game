-- Toy Front for Xteink Pokemon (Lua app): two toy armies fight over a board
-- of bases for medals, you against the device. Rules in logic.lua, the device
-- in ai.lua (loaded for its turn); the start menu (menu.lua) only while shown.
--
-- Buttons (X3/X4): the arrows move over the board and down to your toys,
-- Confirm picks a toy or plays it on a base, Back opens the menu. Touch (X4
-- Pro): tap a toy, then a base.

local T = smudge.dofile("logic.lua")

local LEVELS = { "Easy", "Normal" }
local STEP_MS = 900
local N = T.SIZE * T.SIZE

local w, h, m, touch = 480, 800, {}, false
local menu, rules = nil, false
local g, level = nil, 2
local card = 1          -- the selected toy in your hand
local cr, cc = 5, 1     -- the cursor: rows 1-4 the board, row 5 your toys
local note = nil        -- what just happened
local thinking, thinkingDrawn, stepDue = false, false, nil
local stats = {}        -- per level: { won, played }
local starter = 1       -- who moves first in the next game

local function rand(n) return math.random(1, n) end

local function finishIfOver()
    if not g.over or g.counted then return end
    g.counted = true
    local s = stats[level]
    s[2] = s[2] + 1
    if T.winner(g) == 1 then s[1] = s[1] + 1 end
    smudge.save("st" .. level, s[1] .. "," .. s[2])
    smudge.save("game", "")
end

local function startThinking()
    thinking = g ~= nil and not g.over and g.turn == 2 and not menu
    thinkingDrawn = false
end

local function newGame(lv)
    level = lv
    g = T.new(rand(#T.BOARDS), rand, starter)
    starter = 3 - starter
    smudge.save("starter", tostring(starter))
    card, cr, cc, note = 1, 5, 1, nil
end

-- Layout ---------------------------------------------------------------------

local function layout()
    local top = m.top_padding + m.header_height + 6
    local cell = math.min((w - 40) // T.SIZE, 104)
    local bx = (w - cell * T.SIZE) // 2
    local by = top + 76
    local handY = by + cell * T.SIZE + 58
    return { top = top, cell = cell, bx = bx, by = by, handY = handY, cw = 76, chh = 90 }
end

local function baseRect(L, i)
    local r, c = (i - 1) // T.SIZE, (i - 1) % T.SIZE
    local pad = L.cell // 7
    return L.bx + c * L.cell + pad, L.by + r * L.cell + pad, L.cell - 2 * pad, L.cell - 2 * pad
end

local function cardRect(L, k)
    local n = T.HAND
    local x0 = (w - n * L.cw - (n - 1) * 12) // 2
    return x0 + (k - 1) * (L.cw + 12), L.handY, L.cw, L.chh
end

-- Drawing --------------------------------------------------------------------

local RULES = {
    "Two toy armies fight over a board of bases joined by paths. Each base is worth 1 to 3 medals (the dots).",
    "Each side has twelve toys, strength 1 to 6, three in hand. Play one a turn: on an empty base in your home "
        .. "row (the bottom one) or joined to a base you hold; on your own base to make it stronger; or on an enemy "
        .. "base joined to yours to attack it.",
    "A stronger toy takes the base, keeping the difference; a weaker one wears it down; equal ones leave it empty.",
    "When both armies are spent, whoever holds more medals wins. Keep some strong toys for the end!",
}

local function drawRules()
    smudge.header("How to play", "")
    local y = m.top_padding + m.header_height + 14
    for _, para in ipairs(RULES) do
        local res = smudge.wrapped_text(para, w - 48, "ui10", "regular")
        for _, line in ipairs(res.lines) do
            smudge.text(24, y, line, "ui10", "regular", "left", true)
            y = y + smudge.line_height("ui10")
        end
        y = y + 12
    end
    if touch then smudge.button_hints("", "", "", "") else smudge.button_hints("Back", "", "", "") end
end

local function drawBoard(L)
    -- Paths between the centres of joined bases.
    for i = 1, N do
        local x, y, s = baseRect(L, i)
        local cx, cy = x + s // 2, y + s // 2
        if i % T.SIZE ~= 0 and T.joined(g, i, i + 1) then smudge.rect(cx, cy - 2, L.cell, 5, true, true) end
        if i + T.SIZE <= N and T.joined(g, i, i + T.SIZE) then smudge.rect(cx - 2, cy, 5, L.cell, true, true) end
    end
    local v = g.hands[1][card]
    for i = 1, N do
        local x, y, s = baseRect(L, i)
        local o = g.owner[i]
        smudge.rounded_rect(x, y, s, s, 10, true, o == 2)
        smudge.rounded_rect(x, y, s, s, 10, false, o == 1 and 5 or 2)
        -- Medals: dots along the top.
        local med = T.medals(g, i)
        for k = 1, med do
            local dx = x + s // 2 + (k - (med + 1) / 2) * 14
            smudge.circle(math.floor(dx), y + 12, 4, true, o ~= 2)
        end
        if o ~= 0 then
            smudge.text(x + s // 2, y + s // 2 - 10, tostring(g.strength[i]), "lexend16", "bold", "center", o ~= 2)
        elseif v and g.turn == 1 and not g.over and T.canPlay(g, 1, i) then
            smudge.circle(x + s // 2, y + s // 2 + 6, 5, false, 2)
        end
        if not touch and cr <= T.SIZE and (cr - 1) * T.SIZE + cc == i and not thinking then
            smudge.rect(x - 5, y - 5, s + 10, s + 10, false, true, 3)
        end
    end
    smudge.text(L.bx, L.by - 22, "Device's home", "ui10", "regular", "left", true)
    smudge.text(L.bx, L.by + L.cell * T.SIZE + 2, "Your home", "ui10", "regular", "left", true)
end

local function drawOver(L)
    local a, b = T.score(g, 1), T.score(g, 2)
    local wnr = T.winner(g)
    local text = (wnr == 1 and "You win! " or wnr == 2 and "The device wins. " or "A draw. ")
                 .. string.format("Medals: you %d, the device %d.", a, b)
    local res = smudge.wrapped_text(text, w - 80, "ui12", "bold")
    local lh = smudge.line_height("ui12")
    local bh = #res.lines * lh + 70
    local y = (h - bh) // 2
    smudge.rounded_rect(24, y, w - 48, bh, 10, true, false)
    smudge.rounded_rect(24, y, w - 48, bh, 10, false, 3)
    for k, line in ipairs(res.lines) do smudge.centered_text(y + 16 + (k - 1) * lh, line, "ui12", "bold", true) end
    smudge.centered_text(y + bh - 34, touch and "Tap for the menu" or "Confirm: menu", "ui10", "regular", true)
end

local function drawGame()
    local L = layout()
    smudge.header("Toy Front", T.BOARDS[g.board].name .. " - " .. LEVELS[level])
    smudge.text(16, L.top, string.format("Medals: you %d, device %d", T.score(g, 1), T.score(g, 2)), "ui12", "bold",
                "left", true)
    smudge.text(16, L.top + 26, string.format("Toys left: you %d, device %d", #g.hands[1] + #g.decks[1],
                                              #g.hands[2] + #g.decks[2]), "ui10", "regular", "left", true)
    drawBoard(L)
    local status
    if thinking then
        status = "The device is moving..."
    elseif g.over then
        status = ""
    elseif g.turn == 1 and not T.hasMove(g, 1) then
        status = "Nowhere to play: pick a toy to give up."
    else
        status = note or "Pick a toy, then a base (dots: where it can go)."
    end
    smudge.text(16, L.handY - 26, status, "ui10", "bold", "left", true)
    for k, v in ipairs(g.hands[1]) do
        local x, y, cw, ch = cardRect(L, k)
        local selected = k == card
        smudge.rounded_rect(x, y, cw, ch, 8, selected, selected and true or 2)
        smudge.text(x + cw // 2, y + ch // 2 - 14, tostring(v), "lexend16", "bold", "center", not selected)
        if not touch and cr == 5 and cc == k and not thinking then smudge.rect(x - 5, y - 5, cw + 10, ch + 10, false, true, 3) end
    end
    if g.over then drawOver(L) end
    if touch then
        smudge.button_hints("", "", "", "")
    elseif g.over then
        smudge.button_hints("Menu", "OK", "", "")
    else
        smudge.button_hints("Menu", cr == 5 and "Pick" or "Play", "<", ">")
    end
end

-- Menu -----------------------------------------------------------------------

local function menuItems()
    local items = {}
    if g and not g.over then
        items[1] = { label = "Resume", id = 0,
                     note = string.format("%s - medals %d to %d", LEVELS[level], T.score(g, 1), T.score(g, 2)) }
    end
    for i, name in ipairs(LEVELS) do
        local s = stats[i]
        items[#items + 1] = { label = "New game: " .. name, id = i, note = string.format("Won %d of %d", s[1], s[2]) }
    end
    items[#items + 1] = { label = "How to play", id = -2 }
    items[#items + 1] = { label = "Exit", id = -1 }
    return items
end

local function openMenu()
    thinking = false
    menu = smudge.dofile("menu.lua")("Toy Front", { "Toy armies fight over a board of bases;",
                                                     "hold the most medals when the toys run out." })
    smudge.request_update()
end

local function choose(item)
    if not item then return end
    local id = item == "exit" and -1 or item.id
    if id == -1 then
        smudge.exit()
    elseif id == -2 then
        rules = true
    else
        if id > 0 then newGame(id) end
        menu = nil
        collectgarbage("collect")
        startThinking()
    end
    smudge.request_update()
end

-- Turns ----------------------------------------------------------------------

local WHAT = { build = "built on", boost = "reinforced", take = "took", attack = "attacked", even = "wiped out" }

local function playerPlay(i)
    if g.turn ~= 1 or g.over or thinking then return end
    if not T.hasMove(g, 1) then
        T.discard(g, card)
        note = "You gave up a toy."
    else
        local what = T.play(g, card, i)
        if not what then
            note = "Your toys cannot reach that base."
            smudge.request_update()
            return
        end
        note = nil
    end
    card = math.min(card, math.max(1, #g.hands[1]))
    finishIfOver()
    startThinking()
    smudge.request_update()
end

local function deviceTurn()
    local AI = smudge.dofile("ai.lua")(T)
    local notes = {}
    -- The device moves until it is your turn again (you may be out of toys).
    while not g.over and g.turn == 2 do
        local k, i = AI.choose(g, level, rand)
        local v = g.hands[2][k]
        if i then
            local what = T.play(g, k, i)
            local r, c = (i - 1) // T.SIZE + 1, (i - 1) % T.SIZE + 1
            notes[#notes + 1] = string.format("%s row %d, col %d with a %d", WHAT[what], r, c, v)
        else
            T.discard(g, k)
            notes[#notes + 1] = "gave up a toy"
        end
    end
    AI = nil
    collectgarbage("collect")
    note = "Device " .. table.concat(notes, "; then ") .. "."
    card = math.min(card, math.max(1, #g.hands[1]))
    finishIfOver()
    thinking = false
    smudge.request_update()
end

-- Callbacks ------------------------------------------------------------------

function on_init()
    w, h = smudge.get_bounds()
    m = smudge.get_metrics()
    touch = smudge.has_touch()
    math.randomseed(smudge.millis() + smudge.time())
    for i = 1, #LEVELS do
        local a, b = (smudge.load("st" .. i, "0,0")):match("(%d+),(%d+)")
        stats[i] = { tonumber(a) or 0, tonumber(b) or 0 }
    end
    starter = tonumber(smudge.load("starter", "1")) or 1
    local lv, data = smudge.load("game", ""):match("^(%d)#(.+)$")
    if data then
        g = T.deserialize(data)
        if g then level = tonumber(lv) end
    end
    openMenu()
end

function on_draw()
    smudge.clear()
    if rules then
        drawRules()
    elseif menu then
        menu.draw(menuItems())
    else
        drawGame()
        if thinking and not thinkingDrawn then
            thinkingDrawn = true
            stepDue = smudge.millis() + STEP_MS
        end
    end
end

function on_update()
    if thinking and thinkingDrawn and stepDue and smudge.millis() >= stepDue then
        stepDue = nil
        deviceTurn()
    end
end

function on_exit()
    smudge.save("game", (g and not g.over) and (level .. "#" .. T.serialize(g)) or "")
end

function on_button(btn, pressed)
    if not pressed then return end
    if rules then
        rules = false
        smudge.request_update()
        return
    end
    if menu then
        choose(menu.button(btn, menuItems()))
        return
    end
    if thinking then return end
    if btn == "back" then
        openMenu()
        return
    end
    if g.over then
        if btn == "confirm" then openMenu() end
        return
    end
    local cols = cr == 5 and math.max(1, #g.hands[1]) or T.SIZE
    if btn == "confirm" then
        if cr == 5 then
            card = cc
            -- With nowhere to play, picking a toy gives it up.
            if not T.hasMove(g, 1) then playerPlay(nil) return end
            cr = T.SIZE
        else
            playerPlay((cr - 1) * T.SIZE + cc)
            return
        end
    elseif btn == "left" then
        cc = (cc - 2) % cols + 1
    elseif btn == "right" then
        cc = cc % cols + 1
    elseif btn == "up" or btn == "page_back" then
        cr = cr == 1 and 5 or cr - 1
    elseif btn == "down" or btn == "page_forward" then
        cr = cr == 5 and 1 or cr + 1
    end
    if cr == 5 then cc = math.min(cc, math.max(1, #g.hands[1])) else cc = math.min(cc, T.SIZE) end
    smudge.request_update()
end

function on_tap(x, y)
    if rules then
        rules = false
        smudge.request_update()
        return
    end
    if menu then
        choose(menu.tap(x, y, menuItems()))
        return
    end
    if thinking then return end
    if g.over then
        openMenu()
        return
    end
    local L = layout()
    for k = 1, #g.hands[1] do
        if smudge.in_rect(x, y, cardRect(L, k)) then
            card = k
            if not T.hasMove(g, 1) then playerPlay(nil) return end
            smudge.request_update()
            return
        end
    end
    for i = 1, N do
        if smudge.in_rect(x, y, baseRect(L, i)) then
            playerPlay(i)
            return
        end
    end
end
