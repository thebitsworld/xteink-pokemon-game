-- The Dungeon Map rules page, loaded only while it is on screen:
--   local rules = smudge.dofile("rules.lua")()
--   rules.draw()

return function()
local R = {}
local TEXT = {
    "Wall in the dungeon map so that:",
    "- each number at the edge is how many walls its row or column holds;",
    "- every dead end (an open cell with only one open neighbour) holds a monster, and every monster stands in a dead end;",
    "- every chest lies in a 3x3 treasure room of open cells with exactly one way out;",
    "- outside treasure rooms the passages are one cell wide: no 2x2 square is all open;",
    "- all the open cells are joined up.",
    "Each map has exactly one answer. Mark cells you know are open with a dot to keep track.",
}

function R.draw()
    local w, h = smudge.get_bounds()
    local m = smudge.get_metrics()
    smudge.header("How to play", "")
    local y = m.top_padding + m.header_height + 12
    local lh = smudge.line_height("ui10")
    for i, para in ipairs(TEXT) do
        local res = smudge.wrapped_text(para, w - 48, "ui10", i == 1 and "bold" or "regular")
        for _, line in ipairs(res.lines) do
            smudge.text(24, y, line, "ui10", i == 1 and "bold" or "regular", "left", true)
            y = y + lh
        end
        y = y + lh // 2
    end
    if smudge.has_touch() then
        smudge.centered_text(h - m.button_hints_height - 40, "Tap to go back", "ui10", "regular", true)
        smudge.button_hints("", "", "", "")
    else
        smudge.button_hints("Back", "", "", "")
    end
end

return R
end
