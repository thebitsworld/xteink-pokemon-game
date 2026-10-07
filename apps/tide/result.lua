-- The Tide & Paper round result screen, shown between rounds in place of the
-- table (so the two are never in memory together). Usage:
--   local screen = smudge.dofile("result.lua")(T, S)

return function(T, S)
local R = {}

local function lines(g)
    local r, out = g.result, {}
    if r.kind == "mermaids" then
        out[1] = (r.player == 1 and "You have" or "The device has") .. " four mermaids!"
    elseif r.kind == "empty" then
        out[1] = "The deck ran out: no points this round."
    else
        local who = r.player == 1 and "You" or "The device"
        out[1] = who .. (r.kind == "stop" and " said Stop." or " bet Last chance.")
        if r.kind == "last" then
            out[#out + 1] = r.gain[r.player] > r.bonus[r.player] and "The bet came off." or "The bet failed."
        end
        out[#out + 1] = string.format("Cards: you %d, device %d", r.points[1], r.points[2])
        out[#out + 1] = string.format("Mark bonus: you %d, device %d", r.bonus[1], r.bonus[2])
        out[#out + 1] = string.format("This round: you +%d, device +%d", r.gain[1], r.gain[2])
    end
    return out
end

function R.draw()
    local g = S.game
    smudge.header("Tide & Paper", S.LEVELS[S.level])
    local y = S.m.top_padding + S.m.header_height + 40
    local title = g.phase == "over" and (g.winner == 1 and "You win the game!" or "The device wins the game.")
                  or ("Round " .. g.round .. " over")
    smudge.centered_text(y, title, "ui12", "bold", true)
    y = y + 50
    for _, line in ipairs(lines(g)) do
        smudge.centered_text(y, line, "ui12", "regular", true)
        y = y + 32
    end
    y = y + 20
    smudge.centered_text(y, string.format("Score: you %d, device %d (to %d)", g.scores[1], g.scores[2], T.TARGET),
                         "ui12", "bold", true)
    local next = g.phase == "over" and "a new game" or "the next round"
    smudge.centered_text(y + 60, (S.touch and "Tap for " or "Confirm: ") .. next, "ui10", "regular", true)
    if S.touch then smudge.button_hints("", "", "", "") else smudge.button_hints("Menu", "Next", "", "") end
end

function R.button(btn)
    if btn == "back" then
        S.show("menu")
    elseif btn == "confirm" then
        S.continue()
    end
end

function R.tap() S.continue() end

return R
end
