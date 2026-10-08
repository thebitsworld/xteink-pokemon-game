-- Cult Ledger for Xteink Pokemon (Lua app): a solo card game in the style
-- of Underhand, with cards written for it. Rules in logic.lua, the event
-- cards in cards.lua; the start menu (menu.lua) is loaded only while shown.
--
-- Buttons (X3/X4): Up/Down move between the cards (and the button under
-- them), Left/Right between a card's two choices, Confirm picks, Back opens
-- the menu. Touch (X4 Pro): tap a choice.

local C = smudge.dofile("logic.lua")
local CARDS = C.lines(smudge.dofile("cards.lua"))

local w, h, m, touch = 480, 800, {}, false
local menu, rules = nil, false
local g = nil
local sel = 1           -- 1..6: card (sel+1)//2, choice 2-sel%2; 7: the button under the cards
local stats = {}        -- per level: { wins, played, best turns }

local function rand(n) return math.random(1, n) end

local function save()
    smudge.save("game", (g and not g.over) and C.serialize(g) or "")
end

local function finishIfOver()
    if not g.over or g.counted then return end
    g.counted = true
    local s = stats[g.level]
    s[2] = s[2] + 1
    if g.over == "won" then
        s[1] = s[1] + 1
        if s[3] == 0 or g.turn < s[3] then s[3] = g.turn end
    end
    smudge.save("st" .. g.level, table.concat(s, ","))
    smudge.save("game", "")
end

-- Layout ---------------------------------------------------------------------

local function layout()
    local top = m.top_padding + m.header_height + 6
    local L = { top = top, res = top, info = top + 56, cards = top + 112 }
    L.button = h - m.button_hints_height - 8 - 52
    L.ch = (L.button - 10 - L.cards) // 3
    return L
end

local function choiceRect(L, k, choice)
    local y = L.cards + (k - 1) * L.ch
    local bw = (w - 32 - 8) // 2
    return 16 + (choice - 1) * (bw + 8), y + 30, bw, L.ch - 38
end

local function bottomAction()
    if not g or g.over then return nil end
    if C.canSummon(g) then return "summon" end
    if not C.canChoose(g) then return "lie" end
    return nil
end

-- Drawing --------------------------------------------------------------------

local RULES = {
    "You run a small cult. Each turn three events come up: pick one, and one of its two choices, paying what "
        .. "it costs. Choices you cannot pay for are greyed out.",
    "Every 4th turn your members feast: one food for every two of them, or some walk out. Every few turns the "
        .. "town gets more suspicious (+1 heat). At 5 heat the police raid you; with no members left the cult is "
        .. "over; and the bishop's inspector arrives on the last turn.",
    "From turn 6, once you have the relics, members and prisoners the summoning needs, summon your god to win.",
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

local function resourceText(k)
    local need = C.LEVELS[g.level].need[k]
    local v = g.res[k]
    if k == "s" then return string.format("Heat %d/%d", v, C.HEAT_MAX) end
    if need then return string.format("%s %d/%d", C.NAMES[k], v, need) end
    return string.format("%s %d", C.NAMES[k], v)
end

local function drawOver(L)
    local text = g.over == "won" and string.format("Your god walks the earth! Summoned on turn %d.", g.turn)
                 or g.over == "raid" and "The police raid the cellar. The cult is finished."
                 or g.over == "empty" and "The last member has left. The cult is finished."
                 or "The bishop's inspector has arrived. Too late!"
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
    local lv = C.LEVELS[g.level]
    smudge.header("Cult Ledger", string.format("%s - turn %d of %d", lv.name, g.turn, lv.deadline))
    for i, k in ipairs(C.KEYS) do
        local col, row = (i - 1) % 3, (i - 1) // 3
        smudge.text(16 + col * ((w - 32) // 3), L.res + row * 24, resourceText(k), "ui10",
                    (k == "s" and g.res.s >= C.HEAT_MAX - 1) and "bold" or "regular", "left", true)
    end
    local info = string.format("Feast in %d, gossip in %d", C.turnsUntil(g, C.FEAST_EVERY), C.turnsUntil(g, lv.gossip))
    if g.turn < C.SUMMON_FROM then info = info .. string.format(", summon from turn %d", C.SUMMON_FROM) end
    smudge.text(16, L.info, info, "ui10", "bold", "left", true)
    if g.log then
        local res = smudge.wrapped_text(g.log, w - 32, "ui10", "regular")
        if res.lines[1] then smudge.text(16, L.info + 22, res.lines[1], "ui10", "regular", "left", true) end
    end
    for k = 1, 3 do
        local card = C.card(g, g.hand[k])
        local y = L.cards + (k - 1) * L.ch
        smudge.text(16, y + 4, card[1], "ui12", "bold", "left", true)
        for choice = 1, 2 do
            local x, cy, bw, bh = choiceRect(L, k, choice)
            local ok = C.affordable(g, g.hand[k], choice)
            local selected = not touch and sel == (k - 1) * 2 + choice
            -- What cannot be paid for: a thin frame and plain text.
            smudge.rounded_rect(x, cy, bw, bh, 8, selected, selected and true or (ok and 2 or 1))
            local opt = card[choice + 1]
            smudge.text(x + bw // 2, cy + 6, opt[1], "ui10", ok and "bold" or "regular", "center", not selected)
            local res = smudge.wrapped_text(C.describe(opt[2]), bw - 12, "ui10", "regular")
            for j = 1, math.min(2, #res.lines) do
                smudge.text(x + bw // 2, cy + 6 + j * 20, res.lines[j], "ui10", "regular", "center", not selected)
            end
        end
    end
    local action = bottomAction()
    if action then
        local selected = not touch and sel == 7
        smudge.rounded_rect(16, L.button, w - 32, 48, 10, true, selected and true or false)
        if not selected then smudge.rounded_rect(16, L.button, w - 32, 48, 10, false, 3) end
        local label = action == "summon" and "Summon your god!" or "Nothing you can pay for: lie low (+1 heat)"
        smudge.text(w // 2, L.button + 12, label, "ui12", "bold", "center", not selected)
    end
    if g.over then drawOver(L) end
    if touch then
        smudge.button_hints("", "", "", "")
    elseif g.over then
        smudge.button_hints("Menu", "OK", "", "")
    else
        smudge.button_hints("Menu", "Choose", "<", ">")
    end
end

-- Menu -----------------------------------------------------------------------

local function menuItems()
    local items = {}
    if g and not g.over then
        items[1] = { label = "Resume", id = 0, note = string.format("%s, turn %d", C.LEVELS[g.level].name, g.turn) }
    end
    for i, lv in ipairs(C.LEVELS) do
        local s = stats[i]
        local note = s[2] == 0 and "Not played yet"
                     or string.format("Won %d of %d%s", s[1], s[2], s[3] > 0 and (", best turn " .. s[3]) or "")
        items[#items + 1] = { label = "New game: " .. lv.name, id = i, note = note }
    end
    items[#items + 1] = { label = "How to play", id = -2 }
    items[#items + 1] = { label = "Exit", id = -1 }
    return items
end

local function openMenu()
    menu = smudge.dofile("menu.lua")("Cult Ledger", { "Gather relics, members and an offering,",
                                                       "dodge the police - and summon a god." })
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
        if id > 0 then g = C.new(id, CARDS, rand) end
        menu = nil
        sel = 1
        collectgarbage("collect")
    end
    smudge.request_update()
end

-- Callbacks ------------------------------------------------------------------

function on_init()
    w, h = smudge.get_bounds()
    m = smudge.get_metrics()
    touch = smudge.has_touch()
    math.randomseed(smudge.millis() + smudge.time())
    for i = 1, #C.LEVELS do
        local a, b, c = (smudge.load("st" .. i, "0,0,0")):match("(%d+),(%d+),(%d+)")
        stats[i] = { tonumber(a) or 0, tonumber(b) or 0, tonumber(c) or 0 }
    end
    g = C.deserialize(smudge.load("game", ""), CARDS)
    openMenu()
end

function on_draw()
    smudge.clear()
    if rules then drawRules() elseif menu then menu.draw(menuItems()) else drawGame() end
end

function on_exit() save() end

local function act(k, choice)
    local ok
    if k == 7 then
        local action = bottomAction()
        if action == "summon" then ok = C.summon(g) elseif action == "lie" then ok = C.choose(g, 0) end
    else
        ok = C.choose(g, k, choice)
    end
    if ok then
        finishIfOver()
        if not bottomAction() and sel == 7 then sel = 1 end
    end
    smudge.request_update()
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
    if btn == "back" then
        openMenu()
        return
    end
    if g.over then
        if btn == "confirm" then openMenu() end
        return
    end
    local hasButton = bottomAction() ~= nil
    if btn == "confirm" then
        if sel == 7 then act(7) else act((sel + 1) // 2, 2 - sel % 2) end
        return
    elseif btn == "left" and sel <= 6 then
        sel = sel % 2 == 0 and sel - 1 or sel + 1
    elseif btn == "right" and sel <= 6 then
        sel = sel % 2 == 1 and sel + 1 or sel - 1
    elseif btn == "down" or btn == "page_forward" then
        if sel >= 5 and sel <= 6 then sel = hasButton and 7 or sel - 4
        elseif sel == 7 then sel = 1
        else sel = sel + 2 end
    elseif btn == "up" or btn == "page_back" then
        if sel == 7 then sel = 5
        elseif sel <= 2 then sel = hasButton and 7 or sel + 4
        else sel = sel - 2 end
    end
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
    if g.over then
        openMenu()
        return
    end
    local L = layout()
    if bottomAction() and smudge.in_rect(x, y, 16, L.button, w - 32, 48) then
        act(7)
        return
    end
    for k = 1, 3 do
        for choice = 1, 2 do
            if smudge.in_rect(x, y, choiceRect(L, k, choice)) then
                act(k, choice)
                return
            end
        end
    end
end
