-- Phom start-up, run once by main.lua's on_init and dropped: the layout,
-- the records and points, and a saved hand. Usage:
--   smudge.dofile("boot.lua")(S, rulesOf)
-- with S the app's state and rulesOf(file) loading a rules module.

return function(S, rulesOf)
    S.w, S.h = smudge.get_bounds()
    S.m = smudge.get_metrics()
    S.touch = smudge.has_touch()
    local L, m, h, w = S.L, S.m, S.h, S.w
    local top = m.top_padding + m.header_height + 6
    L.top = top
    L.cw, L.ch, L.lift = 56, 84, 20
    L.mw, L.mh, L.mstep = 34, 48, 24 -- small cards
    L.handY = h - m.button_hints_height - 20 - L.ch
    L.btnW, L.btnH = 136, 50
    L.btnY = L.handY - L.lift - 12 - L.btnH
    L.btnX = (w - 3 * L.btnW - 24) // 2
    L.pw, L.ph = (w - 24) // 2, 86 -- a seat's panel
    L.seatY = top + 38
    L.tableY = L.seatY + 2 * L.ph + 22

    math.randomseed(smudge.millis() + smudge.time())
    for i = 1, #S.LEVELS do
        local a, b = (smudge.load("st" .. i, "0,0")):match("(%d+),(%d+)")
        S.stats[i] = { tonumber(a) or 0, tonumber(b) or 0 }
    end
    local p1, p2, p3, p4, n = smudge.load("pts", ""):match("^(%d+),(%d+),(%d+),(%d+),(%d+)$")
    if n then S.points, S.handsPlayed = { tonumber(p1), tonumber(p2), tonumber(p3), tonumber(p4) }, tonumber(n) end
    S.leader = tonumber(smudge.load("lead", "0")) or 0
    local lv, data = smudge.load("game", ""):match("^(%d)#(.+)$")
    if data then
        S.game = rulesOf("save.lua").deserialize(data)
        S.level = math.max(1, math.min(#S.LEVELS, tonumber(lv) or 1))
        collectgarbage("collect")
        if S.game then S.rules().prepare(S.game) end
    end
end
