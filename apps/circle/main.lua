-- Inner Circle for Xteink Pokemon (Lua app): a party game for 4 to 8 around
-- one reader. Passed round, the reader tells each player their role in
-- secret: one Guide, who knows the secret word and answers only yes or no;
-- one Insider, who knows it too and steers the questions without being
-- caught; the rest, who don't know it. Find the word before the time runs
-- out, then spend as long again working out who the Insider was. Words live
-- in words.lua.
--
-- Buttons (X3/X4): Confirm goes on, Up/Down choose in the menu, Back goes to
-- the menu. Touch (X4 Pro): tap the button.

local TIMES = { 3, 4, 5 }   -- minutes to find the word

local w, h, m, touch = 480, 800, {}, false
local menu, rules = nil, false
local players, timeIdx = 5, 3
local phase = nil       -- pass, role, guide, ask, talk, vote, reveal, lost
local word = ""
local guide, insider = 1, 2
local current = 1       -- whose role is being shown
local endsAt, usedMs, lastSecond = 0, 0, -1

local function rand(n) return math.random(1, n) end

local function deal()
    local list = smudge.dofile("words.lua")
    local words = {}
    for wd in list:gmatch("[^|]+") do words[#words + 1] = wd end
    word = words[rand(#words)]
    words, list = nil, nil
    collectgarbage("collect")
    guide = rand(players)
    repeat insider = rand(players) until insider ~= guide
    current, phase = 1, "pass"
end

local function startTimer(ms)
    endsAt, lastSecond = smudge.millis() + ms, -1
end

local function leftMs() return math.max(0, endsAt - smudge.millis()) end

local function clock(ms)
    local s = (ms + 999) // 1000
    return string.format("%d:%02d", s // 60, s % 60)
end

-- Menu -----------------------------------------------------------------------

local function menuItems()
    local items = {}
    if phase and phase ~= "reveal" and phase ~= "lost" then items[1] = { label = "Resume", id = "resume" } end
    items[#items + 1] = { label = "Players: " .. players, id = "players", note = "Select to change" }
    items[#items + 1] = { label = "Time to find the word: " .. TIMES[timeIdx] .. " min", id = "time",
                          note = "Select to change" }
    items[#items + 1] = { label = "Deal the roles", id = "deal" }
    items[#items + 1] = { label = "How to play", id = "rules" }
    items[#items + 1] = { label = "Exit", id = "exit" }
    return items
end

local function openMenu()
    menu = smudge.dofile("menu.lua")("Inner Circle", { "Find the secret word with yes/no questions,",
                                                        "then unmask the Insider who knew it all along." })
    smudge.request_update()
end

local function choose(item)
    if not item then return end
    local id = item == "exit" and "exit" or item.id
    if id == "exit" then
        smudge.exit()
    elseif id == "players" then
        players = players % 8 + 1
        if players < 4 then players = 4 end
        smudge.save("players", tostring(players))
    elseif id == "time" then
        timeIdx = timeIdx % #TIMES + 1
        smudge.save("time", tostring(timeIdx))
    elseif id == "rules" then
        rules = true
    else
        menu = nil
        collectgarbage("collect")
        if id == "deal" then deal() end
    end
    smudge.request_update()
end

-- Drawing --------------------------------------------------------------------

local function buttonRect() return 32, h - m.button_hints_height - 8 - 60, w - 64, 56 end

local function paragraph(text, y, font, style)
    font, style = font or "ui12", style or "regular"
    local res = smudge.wrapped_text(text, w - 64, font, style)
    for k, line in ipairs(res.lines) do
        smudge.centered_text(y + (k - 1) * smudge.line_height(font), line, font, style, true)
    end
    return y + #res.lines * smudge.line_height(font) + 16
end

local RULES = {
    "Pass the reader round: it shows each player their role in secret.",
    "The Guide knows the secret word and tells everyone they are the Guide. They answer questions only with "
        .. "yes, no or I don't know.",
    "The Insider knows the word too, but keeps quiet about it, steering the questions so the word is found in "
        .. "time - without being found out.",
    "Everyone else asks yes/no questions. If nobody says the word before the time runs out, everyone loses.",
    "Once the word is found, there is as long again to talk it over; then all point at once at who they think "
        .. "the Insider was. Catch them and the group wins - miss and the Insider wins.",
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
        y = y + 10
    end
    if touch then smudge.button_hints("", "", "", "") else smudge.button_hints("Back", "", "", "") end
end

function on_draw()
    smudge.clear()
    if rules then
        drawRules()
        return
    end
    if menu then
        menu.draw(menuItems())
        return
    end
    local top = m.top_padding + m.header_height
    smudge.header("Inner Circle", players .. " players")
    local y = top + 80
    local label
    if phase == "pass" then
        y = paragraph("Player " .. current, y, "lexend16", "bold")
        paragraph("Take the reader. Everyone else, look away!", y)
        label = "Show my role"
    elseif phase == "role" then
        y = paragraph("Player " .. current .. ", you are", y - 30)
        if current == guide then
            y = paragraph("the Guide", y, "lexend16", "bold")
            y = paragraph("The secret word is", y + 10)
            y = paragraph(word, y, "lexend16", "bold")
            paragraph("Tell everyone you are the Guide. Answer questions only with yes, no or I don't know.", y + 10)
        elseif current == insider then
            y = paragraph("the Insider", y, "lexend16", "bold")
            y = paragraph("The secret word is", y + 10)
            y = paragraph(word, y, "lexend16", "bold")
            paragraph("Keep it secret! Steer the questions so the word is found - without being caught.", y + 10)
        else
            y = paragraph("one of the circle", y, "lexend16", "bold")
            paragraph("You don't know the word. Ask the Guide yes/no questions to find it - and watch for the "
                      .. "Insider, who knows it already.", y + 20)
        end
        label = current < players and ("Hide it, pass to player " .. (current + 1)) or "Hide it"
    elseif phase == "guide" then
        y = paragraph("Player " .. guide .. " is the Guide.", y, "lexend16", "bold")
        y = paragraph("Ask them yes/no questions to find the secret word. One of you is the Insider and knows it "
                      .. "already...", y + 10)
        paragraph(TIMES[timeIdx] .. " minutes. Ready?", y + 10)
        label = "Start the clock"
    elseif phase == "ask" then
        y = paragraph(clock(leftMs()), y, "lexend16", "bold")
        paragraph("Ask the Guide yes/no questions. Say the word as soon as you think you know it.", y + 10)
        label = "The word is found!"
    elseif phase == "talk" then
        y = paragraph(clock(leftMs()), y, "lexend16", "bold")
        y = paragraph("The word was " .. word .. ".", y + 10, "ui12", "bold")
        paragraph("Who knew it all along? Talk it over - then everyone points at the Insider at once.", y + 10)
        label = "Time to point"
    elseif phase == "vote" then
        y = paragraph("On three, everyone points at who they think the Insider is.", y)
        paragraph("Count who has the most fingers pointing at them.", y + 10)
        label = "Reveal the Insider"
    elseif phase == "reveal" then
        y = paragraph("The Insider was", y)
        y = paragraph("Player " .. insider, y, "lexend16", "bold")
        y = paragraph("Did most of you point at them? Then the circle wins. If not, the Insider wins.", y + 10)
        paragraph("The word was " .. word .. ".", y + 10)
        label = "Play again"
    else
        y = paragraph("Time's up!", y, "lexend16", "bold")
        y = paragraph("Nobody found the word - everyone loses, the Insider too.", y + 10)
        paragraph("The word was " .. word .. ". The Insider was player " .. insider .. ".", y + 10)
        label = "Play again"
    end
    local bx, by, bw, bh = buttonRect()
    smudge.rounded_rect(bx, by, bw, bh, 10, not touch, not touch and true or 3)
    local font = smudge.text_width(label, "ui12", "bold") > bw - 16 and "ui10" or "ui12"
    smudge.text(bx + bw // 2, by + 14, label, font, "bold", "center", touch)
    if touch then smudge.button_hints("", "", "", "") else smudge.button_hints("Menu", "OK", "", "") end
end

-- Time and input -------------------------------------------------------------

local function advance()
    if phase == "pass" then
        phase = "role"
    elseif phase == "role" then
        if current < players then
            current, phase = current + 1, "pass"
        else
            phase = "guide"
        end
    elseif phase == "guide" then
        phase = "ask"
        startTimer(TIMES[timeIdx] * 60000)
    elseif phase == "ask" then
        usedMs = TIMES[timeIdx] * 60000 - leftMs()
        phase = "talk"
        startTimer(usedMs)
    elseif phase == "talk" then
        phase = "vote"
    elseif phase == "vote" then
        phase = "reveal"
    else
        deal()
    end
    smudge.request_update()
end

function on_update()
    if phase ~= "ask" and phase ~= "talk" then return end
    local left = leftMs()
    if left == 0 then
        phase = phase == "ask" and "lost" or "vote"
        smudge.request_update()
        return
    end
    local s = (left + 999) // 1000
    if s ~= lastSecond then
        lastSecond = s
        smudge.request_update()
    end
end

function on_init()
    w, h = smudge.get_bounds()
    m = smudge.get_metrics()
    touch = smudge.has_touch()
    math.randomseed(smudge.millis() + smudge.time())
    players = math.max(4, math.min(8, tonumber(smudge.load("players", "5")) or 5))
    timeIdx = tonumber(smudge.load("time", "3")) or 3
    if not TIMES[timeIdx] then timeIdx = 3 end
    openMenu()
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
    elseif btn == "confirm" then
        advance()
    end
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
    if smudge.in_rect(x, y, buttonRect()) then advance() end
end
