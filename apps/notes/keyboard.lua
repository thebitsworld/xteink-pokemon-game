-- An on-screen keyboard, loaded only while typing:
--   local kb = smudge.dofile("keyboard.lua")(title, text, maxLength)
--   kb.draw(); local r = kb.button(btn) / kb.tap(x, y)
-- button/tap return nil while typing, the text on Done, or false on cancel.
--
-- Buttons (X3/X4): the arrows move between keys, Confirm types one, Back
-- cancels. Touch (X4 Pro): tap the keys.

return function(title, text, maxLength)
local K = {}
local w, h = smudge.get_bounds()
local m = smudge.get_metrics()
local touch = smudge.has_touch()

local LETTERS = { "qwertyuiop", "asdfghjkl", "zxcvbnm<" }
local SYMBOLS = { "1234567890", "-/:;()&@'", ".,?!#%+=<" }
local BOTTOM = { "shift", "mode", " ", "done" }

local symbols, upper = false, text == ""
local kr, kc = 1, 1

local function rows() return symbols and SYMBOLS or LETTERS end

local function layout()
    local bottom = h - m.button_hints_height - 8
    local kh = 56
    local top = bottom - 4 * kh - 3 * 6
    local kw = (w - 32 - 9 * 4) // 10
    return { top = top, kw = kw, kh = kh, field = m.top_padding + m.header_height + 40 }
end

-- The keys of row r: { label, value, x, y, w, h }.
local function keys(L, r)
    local out = {}
    local y = L.top + (r - 1) * (L.kh + 6)
    if r <= 3 then
        local s = rows()[r]
        local x = (w - #s * L.kw - (#s - 1) * 4) // 2
        for i = 1, #s do
            local ch = s:sub(i, i)
            local label = ch == "<" and "Del" or ((upper and not symbols) and ch:upper() or ch)
            out[i] = { label, ch == "<" and "del" or label, x, y, L.kw, L.kh }
            x = x + L.kw + 4
        end
    else
        local widths = { 2, 2, 4, 2 }
        local unit = (w - 32 - 3 * 4) // 10
        local x = 16
        for i, key in ipairs(BOTTOM) do
            local kw = widths[i] * unit + (widths[i] - 1) * 0
            local label = key == "shift" and (upper and "ABC" or "abc") or key == "mode" and (symbols and "abc" or "123")
                          or key == " " and "space" or "Done"
            if key == "shift" and symbols then label = "" end
            out[i] = { label, key, x, y, kw, L.kh }
            x = x + kw + 4
        end
    end
    return out
end

function K.draw()
    smudge.header(title, "")
    local L = layout()
    -- The text so far, its end marked by a bar.
    smudge.rounded_rect(16, L.field, w - 32, 56, 8, false, 2)
    local shown = text
    while smudge.text_width(shown .. "|", "ui12", "regular") > w - 56 and #shown > 0 do shown = shown:sub(2) end
    smudge.text(28, L.field + 14, shown .. "|", "ui12", "regular", "left", true)
    smudge.text(w - 20, L.field + 64, string.format("%d / %d", #text, maxLength), "ui10", "regular", "right", true)
    for r = 1, 4 do
        for c, k in ipairs(keys(L, r)) do
            local selected = not touch and r == kr and c == kc
            smudge.rounded_rect(k[3], k[4], k[5], k[6], 6, selected, selected and true or 1)
            local font = smudge.text_width(k[1], "ui12", "bold") > k[5] - 4 and "ui10" or "ui12"
            smudge.text(k[3] + k[5] // 2, k[4] + (k[6] - smudge.line_height(font)) // 2, k[1], font, "bold", "center",
                        not selected)
        end
    end
    if touch then smudge.button_hints("", "", "", "") else smudge.button_hints("Cancel", "Type", "<", ">") end
end

-- Presses key value v; returns the text when it is Done.
local function press(v)
    if v == "done" then
        local t = text:match("^%s*(.-)%s*$")
        return t
    elseif v == "del" then
        text = text:sub(1, -2)
    elseif v == "shift" then
        if not symbols then upper = not upper end
    elseif v == "mode" then
        symbols = not symbols
        kr, kc = 1, 1
    elseif v ~= "" and #text < maxLength then
        text = text .. v
        if upper and not symbols then upper = false end
    end
    smudge.request_update()
    return nil
end

function K.button(btn)
    if btn == "back" then return false end
    local L = layout()
    if btn == "confirm" then
        local k = keys(L, kr)[kc]
        return k and press(k[2])
    end
    if btn == "up" or btn == "page_back" then kr = (kr - 2) % 4 + 1
    elseif btn == "down" or btn == "page_forward" then kr = kr % 4 + 1
    elseif btn == "left" then kc = kc - 1
    elseif btn == "right" then kc = kc + 1 end
    local n = #keys(L, kr)
    kc = (kc - 1) % n + 1
    smudge.request_update()
    return nil
end

function K.tap(x, y)
    local L = layout()
    for r = 1, 4 do
        for _, k in ipairs(keys(L, r)) do
            if smudge.in_rect(x, y, k[3], k[4], k[5], k[6]) then return press(k[2]) end
        end
    end
    return nil
end

return K
end
