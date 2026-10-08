-- Calculator for Xteink Pokemon (Lua app): big keys, the sum shown as you
-- type it with its result underneath, and the sums you already did kept for
-- next time. The arithmetic lives in eval.lua.
--
-- Buttons (X3/X4): the arrows move between keys, Confirm presses one, Back
-- deletes the last character (or exits when there is nothing to delete).
-- Touch (X4 Pro): tap a key; tap an earlier sum to use its result.

local E = smudge.dofile("eval.lua")

local KEYS = {
    { "C", "(", ")", "/" },
    { "7", "8", "9", "*" },
    { "4", "5", "6", "-" },
    { "1", "2", "3", "+" },
    { "0", ".", "<", "=" },
}
-- How the keys are labelled (the expression itself keeps * and /).
local LABELS = { ["*"] = "x", ["/"] = "÷", ["<"] = "Del" }
local MAX_HISTORY = 30
local MAX_LENGTH = 40

local w, h, m, touch = 480, 800, {}, false
local expr = ""         -- what has been typed
local justEvaluated = false
local history = {}      -- { "2+2=4", ... }, newest last
local kr, kc = 5, 4     -- the selected key

-- The display writes x for *; division stays "/", since a small bold ÷ is
-- easily read as +.
local function shown(s) return (s:gsub("%*", "x")) end

local function saveHistory() smudge.save("hist", table.concat(history, "\n")) end

local function evaluate()
    if expr == "" then return end
    local v, err = E.evaluate(expr)
    local result = v and E.format(v) or err
    history[#history + 1] = shown(expr) .. " = " .. result
    while #history > MAX_HISTORY do table.remove(history, 1) end
    saveHistory()
    expr = v and result or ""
    justEvaluated = v ~= nil
end

local function press(key)
    if key == "C" then
        expr, justEvaluated = "", false
    elseif key == "<" then
        expr, justEvaluated = expr:sub(1, -2), false
    elseif key == "=" then
        evaluate()
    else
        -- After a result, a digit starts afresh and an operator carries on.
        if justEvaluated and key:match("[%d%.%(]") then expr = "" end
        justEvaluated = false
        if #expr < MAX_LENGTH then expr = expr .. key end
    end
    smudge.request_update()
end

-- Layout ---------------------------------------------------------------------

local function layout()
    local top = m.top_padding + m.header_height + 8
    local bottom = h - m.button_hints_height - 8
    local kw = (w - 32 - 3 * 8) // 4
    local kh = math.min(72, (bottom - top - 230) // 5)
    local keysTop = bottom - 5 * kh - 4 * 8
    return { top = top, kw = kw, kh = kh, keysTop = keysTop, display = keysTop - 96 }
end

local function keyRect(L, r, c) return 16 + (c - 1) * (L.kw + 8), L.keysTop + (r - 1) * (L.kh + 8), L.kw, L.kh end

-- The history lines that fit above the display, newest at the bottom.
local function historyLines(L)
    local lh = smudge.line_height("ui10") + 6
    local count = math.max(0, (L.display - 8 - L.top) // lh)
    local first = math.max(1, #history - count + 1)
    return first, lh
end

-- Drawing --------------------------------------------------------------------

function on_draw()
    smudge.clear()
    smudge.header("Calculator", "")
    local L = layout()
    local first, lh = historyLines(L)
    local y = L.display - 8 - (#history - first + 1) * lh
    for i = first, #history do
        smudge.text(w - 20, y, history[i], "ui10", "regular", "right", true)
        y = y + lh
    end
    -- The display: what is typed, and its value so far.
    smudge.rounded_rect(16, L.display, w - 32, 88, 8, false, 2)
    local text = expr == "" and "0" or shown(expr)
    while smudge.text_width(text, "ui12", "bold") > w - 56 and #text > 1 do text = text:sub(2) end
    smudge.text(w - 28, L.display + 12, text, "ui12", "bold", "right", true)
    if expr ~= "" and not justEvaluated then
        local v = E.evaluate(expr)
        if v then smudge.text(w - 28, L.display + 50, "= " .. E.format(v), "ui10", "regular", "right", true) end
    end
    for r = 1, 5 do
        for c = 1, 4 do
            local key = KEYS[r][c]
            local x, ky, kw, kh = keyRect(L, r, c)
            local selected = not touch and r == kr and c == kc
            local op = not key:match("[%d%.]")
            smudge.rounded_rect(x, ky, kw, kh, 10, selected, selected and true or (op and 3 or 1))
            smudge.text(x + kw // 2, ky + (kh - smudge.line_height("ui12")) // 2, LABELS[key] or key, "ui12", "bold",
                        "center", not selected)
        end
    end
    if touch then
        smudge.button_hints("", "", "", "")
    else
        smudge.button_hints(expr == "" and "Exit" or "Delete", "Press", "<", ">")
    end
end

-- Input ----------------------------------------------------------------------

function on_init()
    w, h = smudge.get_bounds()
    m = smudge.get_metrics()
    touch = smudge.has_touch()
    for line in (smudge.load("hist", "") .. "\n"):gmatch("([^\n]*)\n") do
        if line ~= "" then history[#history + 1] = line end
    end
end

function on_button(btn, pressed)
    if not pressed then return end
    if btn == "back" then
        if expr == "" then smudge.exit() else press("<") end
        return
    end
    if btn == "confirm" then
        press(KEYS[kr][kc])
        return
    end
    if btn == "left" then kc = (kc - 2) % 4 + 1
    elseif btn == "right" then kc = kc % 4 + 1
    elseif btn == "up" or btn == "page_back" then kr = (kr - 2) % 5 + 1
    elseif btn == "down" or btn == "page_forward" then kr = kr % 5 + 1 end
    smudge.request_update()
end

function on_tap(x, y)
    local L = layout()
    for r = 1, 5 do
        for c = 1, 4 do
            if smudge.in_rect(x, y, keyRect(L, r, c)) then
                press(KEYS[r][c])
                return
            end
        end
    end
    -- An earlier sum: carry on from its result.
    local first, lh = historyLines(L)
    local top = L.display - 8 - (#history - first + 1) * lh
    if y >= top and y < L.display - 8 then
        local i = first + (y - top) // lh
        local result = history[i] and history[i]:match("= (%-?[%d%.e%+%-]+)$")
        if result then
            if justEvaluated or expr == "" then expr = result else expr = expr .. result end
            justEvaluated = false
            smudge.request_update()
        end
    end
end
