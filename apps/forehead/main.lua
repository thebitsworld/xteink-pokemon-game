-- Forehead for Xteink Pokemon (Lua app): a party game. One player holds the
-- reader to their forehead, screen facing out; the others give clues to the
-- word on it without saying it. Got it - next word; stuck - pass. As many as
-- you can before the time runs out. Words live in words.lua.
--
-- Buttons (X3/X4): Right or Confirm - got it, Left - pass, Back - stop.
-- Touch (X4 Pro): tap the right half for got it, the left half to pass.

local TIMES = { 60, 90, 120 }

local w, h, m, touch = 480, 800, {}, false
local phase = "pick"    -- pick, ready, play, done
local names = {}        -- category names
local best = {}         -- per category: best score
local timeIdx = 1
local cat = 1
local words = {}        -- this round's words, shuffled
local results = {}      -- { word, got } in the order shown
local index = 0
local score = 0
local endsAt = 0
local lastSecond = -1
local sel = 1           -- the selected button on the pick screen (buttons only)

local function rand(n) return math.random(1, n) end

local function loadWords(c)
    local list = smudge.dofile("words.lua")[c][2]
    words = {}
    for word in list:gmatch("[^|]+") do words[#words + 1] = word end
    collectgarbage("collect")
    for i = #words, 2, -1 do
        local j = rand(i)
        words[i], words[j] = words[j], words[i]
    end
end

local function startRound()
    loadWords(cat)
    results, index, score = {}, 1, 0
    endsAt = smudge.millis() + TIMES[timeIdx] * 1000
    lastSecond = -1
    phase = "play"
end

local function finish()
    phase = "done"
    if score > best[cat] then
        best[cat] = score
        smudge.save("best" .. cat, tostring(score))
    end
    words = {}
    collectgarbage("collect")
end

local function answer(got)
    results[#results + 1] = { words[index], got }
    if got then score = score + 1 end
    index = index + 1
    if index > #words then finish() end
    smudge.request_update()
end

-- The pick screen: categories in two columns, then the time and Exit.
local function pickButtons()
    local top = m.top_padding + m.header_height + 50
    local bw, bh = (w - 32 - 10) // 2, 64
    local out = {}
    for i, name in ipairs(names) do
        local col, row = (i - 1) % 2, (i - 1) // 2
        out[i] = { name, 16 + col * (bw + 10), top + row * (bh + 10), bw, bh, note = best[i] > 0 and ("Best " .. best[i]) }
    end
    local y = top + ((#names + 1) // 2) * (bh + 10) + 10
    out[#out + 1] = { "Time: " .. TIMES[timeIdx] .. " s", 16, y, w - 32, 56 }
    out[#out + 1] = { "Exit", 16, y + 66, w - 32, 56 }
    return out
end

local function choosePick(k)
    local n = #names
    if k <= n then
        cat = k
        phase = "ready"
    elseif k == n + 1 then
        timeIdx = timeIdx % #TIMES + 1
        smudge.save("time", tostring(timeIdx))
    else
        smudge.exit()
    end
    smudge.request_update()
end

-- Drawing --------------------------------------------------------------------

local function bigButton(label, y)
    smudge.rounded_rect(32, y, w - 64, 56, 10, not touch, not touch and true or 3)
    smudge.text(w // 2, y + 14, label, "ui12", "bold", "center", touch)
end

local function paragraph(text, y, font)
    font = font or "ui12"
    local res = smudge.wrapped_text(text, w - 64, font, "regular")
    for k, line in ipairs(res.lines) do
        smudge.centered_text(y + (k - 1) * smudge.line_height(font), line, font, "regular", true)
    end
    return y + #res.lines * smudge.line_height(font)
end

function on_draw()
    smudge.clear()
    local top = m.top_padding + m.header_height
    if phase == "pick" then
        smudge.header("Forehead", "")
        smudge.centered_text(top + 14, "Choose the words to guess:", "ui10", "regular", true)
        for k, b in ipairs(pickButtons()) do
            local selected = not touch and k == sel
            smudge.rounded_rect(b[2], b[3], b[4], b[5], 8, selected, selected and true or 2)
            local ty = b.note and b[3] + 8 or b[3] + (b[5] - smudge.line_height("ui12")) // 2
            smudge.text(b[2] + b[4] // 2, ty, b[1], "ui12", "bold", "center", not selected)
            if b.note then smudge.text(b[2] + b[4] // 2, b[3] + 36, b.note, "ui10", "regular", "center", not selected) end
        end
        if touch then smudge.button_hints("", "", "", "") else smudge.button_hints("Exit", "Select", "Up", "Down") end
    elseif phase == "ready" then
        smudge.header("Forehead", names[cat])
        local y = paragraph("Hold the reader to your forehead with the screen facing the others.", top + 60)
        y = paragraph("They describe the word without saying it.", y + 20)
        y = paragraph(touch and "Tap the right half when you get it, the left half to pass."
                          or "Press Right when you get it, Left to pass.", y + 20)
        paragraph(TIMES[timeIdx] .. " seconds. Ready?", y + 20)
        bigButton("Start", h - m.button_hints_height - 80)
        if touch then smudge.button_hints("", "", "", "") else smudge.button_hints("Back", "Start", "", "") end
    elseif phase == "play" then
        local left = math.max(0, (endsAt - smudge.millis() + 999) // 1000)
        smudge.text(20, top - 30, left .. " s", "lexend16", "bold", "left", true)
        smudge.text(w - 20, top - 30, "Score " .. score, "lexend16", "bold", "right", true)
        -- The word, as large as it fits.
        local word = words[index]
        local font = smudge.text_width(word, "lexend16", "bold") <= w - 40 and "lexend16" or "ui12"
        local res = smudge.wrapped_text(word, w - 40, font, "bold")
        local lh = smudge.line_height(font)
        local y = (h - #res.lines * lh) // 2 - 40
        for k, line in ipairs(res.lines) do smudge.centered_text(y + (k - 1) * lh, line, font, "bold", true) end
        smudge.centered_text(y + #res.lines * lh + 20, names[cat], "ui10", "regular", true)
        -- Pass and Got it, left and right.
        local by = h - m.button_hints_height - 90
        smudge.rounded_rect(16, by, w // 2 - 24, 70, 10, false, 3)
        smudge.text(w // 4, by + 22, "Pass", "ui12", "bold", "center", true)
        smudge.rounded_rect(w // 2 + 8, by, w // 2 - 24, 70, 10, true, true)
        smudge.text(w * 3 // 4, by + 22, "Got it", "ui12", "bold", "center", false)
        if touch then smudge.button_hints("", "", "", "") else smudge.button_hints("Stop", "Got it", "Pass", "Got it") end
    else
        smudge.header("Forehead", names[cat])
        smudge.centered_text(top + 16, string.format("%d got - best %d", score, best[cat]), "ui12", "bold", true)
        -- The words, in two columns: ticked ones got, plain ones passed.
        local lh = smudge.line_height("ui10") + 4
        local perCol = (h - m.button_hints_height - 100 - (top + 56)) // lh
        for k, r in ipairs(results) do
            if k > perCol * 2 then break end
            local col, row = (k - 1) // perCol, (k - 1) % perCol
            local x, y = 24 + col * (w // 2), top + 56 + row * lh
            smudge.text(x, y, (r[2] and "+ " or "- ") .. r[1], "ui10", r[2] and "bold" or "regular", "left", true)
        end
        bigButton("Play again", h - m.button_hints_height - 80)
        if touch then smudge.button_hints("", "", "", "") else smudge.button_hints("Menu", "Again", "", "") end
    end
end

-- Time -----------------------------------------------------------------------

function on_update()
    if phase ~= "play" then return end
    local leftMs = endsAt - smudge.millis()
    if leftMs <= 0 then
        finish()
        smudge.request_update()
        return
    end
    local s = (leftMs + 999) // 1000
    if s ~= lastSecond then
        lastSecond = s
        smudge.request_update()
    end
end

-- Input ----------------------------------------------------------------------

function on_init()
    w, h = smudge.get_bounds()
    m = smudge.get_metrics()
    touch = smudge.has_touch()
    math.randomseed(smudge.millis() + smudge.time())
    for i, c in ipairs(smudge.dofile("words.lua")) do
        names[i] = c[1]
        best[i] = tonumber(smudge.load("best" .. i, "0")) or 0
    end
    collectgarbage("collect")
    timeIdx = tonumber(smudge.load("time", "1")) or 1
    if not TIMES[timeIdx] then timeIdx = 1 end
end

function on_button(btn, pressed)
    if not pressed then return end
    if phase == "pick" then
        local n = #pickButtons()
        if btn == "back" then smudge.exit() return end
        if btn == "confirm" then choosePick(sel) return end
        if btn == "up" or btn == "page_back" then sel = (sel - 2) % n + 1
        elseif btn == "down" or btn == "page_forward" then sel = sel % n + 1
        elseif btn == "left" then sel = math.max(1, sel - 1)
        elseif btn == "right" then sel = math.min(n, sel + 1) end
        smudge.request_update()
    elseif phase == "ready" then
        if btn == "confirm" then startRound() elseif btn == "back" then phase = "pick" end
        smudge.request_update()
    elseif phase == "play" then
        if btn == "right" or btn == "confirm" then
            answer(true)
        elseif btn == "left" then
            answer(false)
        elseif btn == "back" then
            finish()
            smudge.request_update()
        end
    else
        if btn == "confirm" then phase = "ready" elseif btn == "back" then phase = "pick" end
        smudge.request_update()
    end
end

function on_tap(x, y)
    if phase == "pick" then
        for k, b in ipairs(pickButtons()) do
            if smudge.in_rect(x, y, b[2], b[3], b[4], b[5]) then
                choosePick(k)
                return
            end
        end
    elseif phase == "ready" then
        startRound()
        smudge.request_update()
    elseif phase == "play" then
        answer(x >= w // 2)
    else
        if y >= h - m.button_hints_height - 80 then phase = "ready" else phase = "pick" end
        smudge.request_update()
    end
end
