-- Mind Dial for Xteink Pokemon (Lua app): a party game for a group around one
-- reader. Each round a card names the two ends of a scale (Cold - Hot) and
-- the reader hides a target somewhere along it. One player - the clue giver -
-- looks at the target and says a clue that sits there on the scale ("a cup of
-- tea"); the others move the pointer to where they think it is. The closer,
-- the more points: 4 for the middle of the target, 3 and 2 around it.
--
-- Buttons (X3/X4): Left/Right move the pointer a little, Up/Down a lot,
-- Confirm goes on. Touch (X4 Pro): tap the scale, then the button.

local ROUNDS = { 5, 7, 10 }
local BANDS = { { 4, 4 }, { 12, 3 }, { 20, 2 } }   -- { distance, points }

local w, h, m, touch = 480, 800, {}, false
local menu = nil
local phase = "pass"    -- pass, clue, guess, reveal, end
local rounds, round, score = 7, 0, 0
local left, right = "", ""
local target, guess, gained = 50, 50, 0
local used = {}         -- card lines already drawn this game
local best = {}         -- per rounds choice: best score

local function rand(n) return math.random(1, n) end

-- A card no one has had this game.
local function drawCard()
    local lines = {}
    for line in smudge.dofile("cards.lua"):gmatch("[^\n]+") do lines[#lines + 1] = line end
    local i
    repeat i = rand(#lines) until not used[i] or #lines <= round
    used[i] = true
    left, right = lines[i]:match("^(.-)|(.-)$")
end

local function points(d)
    for _, b in ipairs(BANDS) do
        if d <= b[1] then return b[2] end
    end
    return 0
end

local function nextRound()
    round = round + 1
    drawCard()
    collectgarbage("collect")
    target = 6 + rand(89) -- clear of the ends, so the whole target shows
    guess = 50
    phase = "pass"
end

local function newGame(n)
    rounds, round, score, used = n, 0, 0, {}
    menu = nil
    collectgarbage("collect")
    nextRound()
end

local function menuItems()
    local items = {}
    if round > 0 and phase ~= "end" then
        items[1] = { label = "Resume", rounds = 0, note = string.format("Round %d of %d", round, rounds) }
    end
    for i, n in ipairs(ROUNDS) do
        local b = best[i]
        items[#items + 1] = { label = n .. " rounds", rounds = n,
                              note = b > 0 and string.format("Best %d of %d", b, n * 4) or "Not played yet" }
    end
    items[#items + 1] = { label = "Exit", rounds = -1 }
    return items
end

local function openMenu()
    menu = smudge.dofile("menu.lua")("Mind Dial", { "A clue giver hints where a hidden target sits",
                                                     "on a scale; the group guesses. 3+ players." })
    smudge.request_update()
end

-- Drawing --------------------------------------------------------------------

local function bar()
    local x0, x1 = 32, w - 32
    local y = m.top_padding + m.header_height + 190
    return x0, x1, y, 80
end

local function posX(v)
    local x0, x1 = bar()
    return x0 + (x1 - x0) * v // 100
end

local function drawScale(showTarget, showGuess)
    local x0, x1, y, bh = bar()
    -- The two ends, over the scale.
    local lw = (x1 - x0) // 2 - 8
    for side = 1, 2 do
        local text = side == 1 and left or right
        local res = smudge.wrapped_text(text, lw, "ui12", "bold")
        for k, line in ipairs(res.lines) do
            local ly = y - 20 - (#res.lines - k + 1) * smudge.line_height("ui12")
            if side == 1 then
                smudge.text(x0, ly, line, "ui12", "bold", "left", true)
            else
                smudge.text(x1, ly, line, "ui12", "bold", "right", true)
            end
        end
    end
    smudge.text(x0, y - 18, "<", "ui12", "bold", "left", true)
    smudge.text(x1, y - 18, ">", "ui12", "bold", "right", true)
    if showTarget then
        -- The target: 2, 3 and 4 point bands, the 4 in the middle.
        for k = #BANDS, 1, -1 do
            local d = BANDS[k][1]
            local a, b = posX(math.max(0, target - d)), posX(math.min(100, target + d))
            if k == 1 then
                smudge.rect(a, y + 2, b - a, bh - 4, true, true)
            else
                smudge.rect(a, y + 2, b - a, bh - 4, true, false)
                smudge.rect_dither(a, y + 2, b - a, bh - 4, k == 2)
            end
        end
        for k = 1, #BANDS do
            local d = BANDS[k][1]
            local a, b = posX(math.max(0, target - d)), posX(math.min(100, target + d))
            local label = tostring(BANDS[k][2])
            if k == 1 then
                smudge.text((a + b) // 2, y + bh + 6, label, "ui12", "bold", "center", true)
            else
                smudge.text(a + 10, y + bh + 6, label, "ui10", "regular", "center", true)
                smudge.text(b - 10, y + bh + 6, label, "ui10", "regular", "center", true)
            end
        end
    end
    smudge.rect(x0, y, x1 - x0, bh, false, true, 3)
    if showGuess then
        local gx = posX(guess)
        smudge.rect(gx - 4, y - 10, 9, bh + 20, true, false)
        smudge.rect(gx - 2, y - 8, 5, bh + 16, true, true)
        smudge.triangle(gx - 12, y - 26, gx + 12, y - 26, gx, y - 8, true, true)
    end
end

local function buttonRect() return 32, h - m.button_hints_height - 8 - 60, w - 64, 56 end

local function drawButton(label)
    local x, y, bw, bh = buttonRect()
    smudge.rounded_rect(x, y, bw, bh, 10, not touch, not touch and true or 3)
    smudge.text(x + bw // 2, y + 14, label, "ui12", "bold", "center", touch)
end

local function paragraph(text, y)
    local res = smudge.wrapped_text(text, w - 64, "ui12", "regular")
    for k, line in ipairs(res.lines) do
        smudge.centered_text(y + (k - 1) * smudge.line_height("ui12"), line, "ui12", "regular", true)
    end
end

function on_draw()
    smudge.clear()
    if menu then
        menu.draw(menuItems())
        return
    end
    smudge.header("Mind Dial", string.format("Round %d of %d  -  %d points", round, rounds, score))
    local top = m.top_padding + m.header_height
    local label
    if phase == "pass" then
        smudge.centered_text(top + 120, "Pass the reader to the clue giver.", "ui12", "bold", true)
        paragraph("Everyone else: look away until the target is hidden.", top + 170)
        label = "I'm the clue giver"
    elseif phase == "clue" then
        drawScale(true, false)
        paragraph("Think of something that sits at the target on this scale, say it out loud, then hide the target.",
                  top + 340)
        label = "Hide the target"
    elseif phase == "guess" then
        drawScale(false, true)
        paragraph(touch and "Tap the scale where the clue sits, then lock it in."
                      or "Move the pointer where the clue sits (Left/Right a little, Up/Down a lot), then lock it in.",
                  top + 340)
        label = "Lock it in"
    elseif phase == "reveal" then
        drawScale(true, true)
        smudge.centered_text(top + 360, gained > 0 and string.format("+%d points!", gained) or "No points this time.",
                             "ui12", "bold", true)
        label = round < rounds and "Next round" or "Final score"
    else
        local max = rounds * 4
        smudge.centered_text(top + 120, string.format("%d of %d points", score, max), "ui12", "bold", true)
        local f = score / max
        local verdict = f >= 0.75 and "You are on the same wavelength!" or f >= 0.5 and "Well tuned in."
                        or f >= 0.25 and "Some static on the line." or "Were you even listening?"
        paragraph(verdict, top + 170)
        label = "Back to the menu"
    end
    drawButton(label)
    if touch then
        smudge.button_hints("", "", "", "")
    elseif phase == "guess" then
        smudge.button_hints("Menu", "Lock in", "<", ">")
    else
        smudge.button_hints("Menu", "OK", "", "")
    end
end

-- Input ----------------------------------------------------------------------

local function advance()
    if phase == "pass" then
        phase = "clue"
    elseif phase == "clue" then
        phase = "guess"
    elseif phase == "guess" then
        gained = points(math.abs(guess - target))
        score = score + gained
        phase = "reveal"
    elseif phase == "reveal" then
        if round < rounds then
            nextRound()
        else
            phase = "end"
            for i, n in ipairs(ROUNDS) do
                if n == rounds and score > best[i] then
                    best[i] = score
                    smudge.save("best" .. i, tostring(score))
                end
            end
        end
    else
        openMenu()
        return
    end
    smudge.request_update()
end

local function choose(item)
    if not item then return end
    if item == "exit" or item.rounds < 0 then
        smudge.exit()
    elseif item.rounds == 0 then
        menu = nil
        collectgarbage("collect")
        smudge.request_update()
    else
        newGame(item.rounds)
    end
end

function on_init()
    w, h = smudge.get_bounds()
    m = smudge.get_metrics()
    touch = smudge.has_touch()
    math.randomseed(smudge.millis() + smudge.time())
    for i = 1, #ROUNDS do best[i] = tonumber(smudge.load("best" .. i, "0")) or 0 end
    openMenu()
end

function on_button(btn, pressed)
    if not pressed then return end
    if menu then
        choose(menu.button(btn, menuItems()))
        return
    end
    if btn == "back" then
        openMenu()
    elseif btn == "confirm" then
        advance()
    elseif phase == "guess" then
        local step = (btn == "left" or btn == "right") and 2 or 10
        if btn == "left" or btn == "up" or btn == "page_back" then guess = math.max(0, guess - step) end
        if btn == "right" or btn == "down" or btn == "page_forward" then guess = math.min(100, guess + step) end
        smudge.request_update()
    end
end

function on_tap(x, y)
    if menu then
        choose(menu.tap(x, y, menuItems()))
        return
    end
    if smudge.in_rect(x, y, buttonRect()) then
        advance()
        return
    end
    local x0, x1, by, bh = bar()
    if phase == "guess" and y >= by - 40 and y <= by + bh + 20 then
        guess = math.max(0, math.min(100, (x - x0) * 100 // (x1 - x0)))
        smudge.request_update()
    end
end
