-- Start menu shared by the games: a title, a line or two of description and a
-- column of buttons, each with an optional note under it. A game loads it only
-- while the menu is on screen and drops it during play, so the memory goes to
-- the game (the X3 gives a Lua app about 75 KB).
--
--   local menu = smudge.dofile("menu.lua")(title, lines)
--   menu.draw(items)          -- items: { { label = , note = }, ... }
--   menu.button(btn, items)   -- returns the chosen item, "exit", or nil
--   menu.tap(x, y, items)     -- returns the chosen item or nil

return function(title, lines)
    local M = { index = 1 }
    local w, h = smudge.get_bounds()
    local m = smudge.get_metrics()
    local touch = smudge.has_touch()
    local top = m.top_padding + m.header_height + 8
    local first = top + 16 + #lines * 20

    local function rect(i) return 24, first + (i - 1) * 74, w - 48, 64 end

    function M.draw(items)
        smudge.header(title, "")
        for i, line in ipairs(lines) do
            smudge.centered_text(top + (i - 1) * 20, line, "ui10", "regular", true)
        end
        if M.index > #items then M.index = #items end
        for i, item in ipairs(items) do
            local x, y, bw, bh = rect(i)
            local selected = i == M.index and not touch
            smudge.rounded_rect(x, y, bw, bh, 8, selected, selected and true or 2)
            local ty = item.note and y + 8 or y + (bh - smudge.line_height("ui12")) // 2
            smudge.text(x + bw // 2, ty, item.label, "ui12", "bold", "center", not selected)
            if item.note then smudge.text(x + bw // 2, y + 36, item.note, "ui10", "regular", "center", not selected) end
        end
        if touch then smudge.button_hints("", "", "", "") else smudge.button_hints("Exit", "Select", "Up", "Down") end
    end

    function M.button(btn, items)
        if btn == "confirm" then return items[M.index] end
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
        for i, item in ipairs(items) do
            local rx, ry, rw, rh = rect(i)
            if smudge.in_rect(x, y, rx, ry, rw, rh) then return item end
        end
        return nil
    end

    return M
end
