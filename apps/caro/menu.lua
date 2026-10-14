-- Start menu shared by the games: a title, a line or two of description and a
-- column of buttons, each with an optional note under it. A game loads it only
-- while the menu is on screen and drops it during play, so the memory goes to
-- the game (the X3 gives a Lua app about 75 KB).
--
--   local menu = smudge.dofile("menu.lua")(title, lines)
--   menu.draw(items)          -- items: { { label = , note = }, ... }
--   menu.button(btn, items)   -- returns the chosen item, "exit", or nil
--   menu.tap(x, y, items)     -- returns the chosen item or nil
--
-- When the app has a guide (guide.md, on firmware with smudge.open_guide) the
-- menu adds a "How to play" button above Exit and opens the guide itself.

return function(title, lines)
    local M = { index = 1 }
    local w, h = smudge.get_bounds()
    local m = smudge.get_metrics()
    local touch = smudge.has_touch()
    local top = m.top_padding + m.header_height + 8
    local first = top + 16 + #lines * 20
    local HOW = { label = "How to play" }
    local guide = smudge.has_guide and smudge.has_guide()

    -- The game's items with "How to play" before a last "Exit" (or at the end).
    local function withGuide(items)
        if not guide then return items end
        local out = {}
        for i, item in ipairs(items) do
            if i == #items and item.label == "Exit" then out[#out + 1] = HOW end
            out[#out + 1] = item
        end
        if #out == #items then out[#out + 1] = HOW end
        return out
    end

    local function picked(item)
        if item == HOW then
            smudge.open_guide()
            return nil
        end
        return item
    end

    -- Buttons 64 px tall, 10 apart; shorter when that many would not fit.
    local function rect(i, n)
        local step = math.min(74, (h - m.button_hints_height - 8 - first) // n)
        return 24, first + (i - 1) * step, w - 48, step - 10
    end

    function M.draw(items)
        items = withGuide(items)
        smudge.header(title, "")
        for i, line in ipairs(lines) do
            smudge.centered_text(top + (i - 1) * 20, line, "ui10", "regular", true)
        end
        if M.index > #items then M.index = #items end
        for i, item in ipairs(items) do
            local x, y, bw, bh = rect(i, #items)
            local selected = i == M.index and not touch
            smudge.rounded_rect(x, y, bw, bh, 8, selected, selected and true or 2)
            local ty = item.note and y + math.max(1, (bh - 56) // 2 + 8) or y + (bh - smudge.line_height("ui12")) // 2
            smudge.text(x + bw // 2, ty, item.label, "ui12", "bold", "center", not selected)
            if item.note then smudge.text(x + bw // 2, ty + 28, item.note, "ui10", "regular", "center", not selected) end
        end
        if touch then smudge.button_hints("", "", "", "") else smudge.button_hints("Exit", "Select", "Up", "Down") end
    end

    function M.button(btn, items)
        items = withGuide(items)
        if btn == "confirm" then return picked(items[M.index]) end
        if btn == "back" then return "exit" end
        if btn == "up" or btn == "left" or btn == "page_back" then
            M.index = (M.index - 2) % #items + 1
        elseif btn == "down" or btn == "right" or btn == "page_forward" then
            M.index = M.index % #items + 1
        end
        smudge.request_update()
        return nil
    end

    function M.tap(x, y, items)
        items = withGuide(items)
        for i, item in ipairs(items) do
            local rx, ry, rw, rh = rect(i, #items)
            if smudge.in_rect(x, y, rx, ry, rw, rh) then return picked(item) end
        end
        return nil
    end

    return M
end
