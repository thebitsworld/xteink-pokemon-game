-- Simulator smoke-test app (scripts/run_simulator_smoke_test.py --lua-apps).
-- Draws with a spread of the smudge.* API, round-trips a saved value, and
-- logs "LUA_SMOKE ..." markers the runner checks for. Back exits.

local w, h = 480, 800
local presses = 0

function on_init()
    w, h = smudge.get_bounds()
    local runs = (tonumber(smudge.load("runs", "0")) or 0) + 1
    smudge.save("runs", tostring(runs))
    if tonumber(smudge.load("runs", "0")) ~= runs then
        error("save/load round trip failed")
    end
    smudge.log("LUA_SMOKE init ok")
end

function on_draw()
    smudge.clear()
    local m = smudge.get_metrics()
    smudge.header("Lua Smoke Test", "v1.0.0")
    local y = m.top_padding + m.header_height + 20
    smudge.text(20, y, "Hello from Lua " .. _VERSION, "ui12", true)
    smudge.rect(20, y + 40, w - 40, 60, false, 2)
    smudge.centered_text(y + 60, string.format("Confirm presses: %d", presses), 0, false, false)
    local parts = {}
    for i = 1, 5 do parts[#parts + 1] = tostring(i * i) end
    smudge.text(20, y + 120, table.concat(parts, ", "), "ui10")
    smudge.button_hints("Back", "Press", "", "")
    smudge.log("LUA_SMOKE draw ok")
end

function on_button(btn, pressed)
    if not pressed then return end
    if btn == "back" then
        smudge.log("LUA_SMOKE exit")
        smudge.exit()
    elseif btn == "confirm" then
        presses = presses + 1
        smudge.log("LUA_SMOKE confirm " .. presses)
    end
end
