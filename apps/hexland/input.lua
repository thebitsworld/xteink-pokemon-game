-- Hexland: your buttons and taps on the board screen. Usage:
--   local screen = smudge.dofile("input.lua")(L, S); screen.button(btn)
-- S.ui (ui.lua) says what can be done: the buttons, a spot on the island to
-- pick, or a pop-up. Asking the device players about a trade is left to
-- main.lua (S.req), which loads them once this screen is dropped.

return function(L, S)
L = smudge.dofile("rules.lua")(L)
local I = {}

local function done()
    S.pop, S.place = nil, nil
    S.changed()
end

local function placeAt(spot)
    local g, U = S.game, S.ui
    S.cur = spot
    local ph = g.phase
    if ph == "setupS" then
        L.setupSettle(g, spot)
    elseif ph == "setupR" then
        L.setupRoad(g, spot)
    elseif ph == "roads" then
        L.freeRoad(g, spot)
    elseif ph == "robber" then
        L.moveRobber(g, spot)
    else
        L.build(g, g.human, U.what, spot)
    end
    done()
end

local function press(i)
    local g, U = S.game, S.ui
    if not U.on[i] then return end
    if i == 1 then
        L.roll(g, math.random(1, 6), math.random(1, 6))
        S.bar = 2
    elseif i == 5 then
        L.endTurn(g)
        S.bar = 1
    else
        S.pop = ({ "build", "trade", "dev" })[i - 1]
    end
    S.changed()
end

local function choose(it)
    local g, U = S.game, S.ui
    if not it or it.off then return end
    local w, id, me, tr = U.what, it.id, g.human, S.tr
    if id == 0 and not U.forced then return done() end
    S.pop = nil
    if w == "over" then
        if id == 1 then S.newGame(g.level) else S.show("menu") end
        return
    elseif w == "discard" then
        L.discardOne(g, me, id)
    elseif w == "steal" then
        L.steal(g, id)
    elseif w == "offer" then
        L.answer(g, id == 1)
    elseif w == "build" then
        if id == L.CARD then
            local c = L.buyCard(g, me)
            L.log(g, "You drew: " .. L.DEV[c])
        else
            S.place = id
        end
    elseif w == "trade" then
        S.pop = id == 1 and "bgive" or "ogive"
    elseif w == "bgive" then
        tr.give, S.pop = id, "bget"
    elseif w == "bget" then
        L.bankTrade(g, me, tr.give, id)
    elseif w == "ogive" then
        tr.give, tr.gn, S.pop = id // 10, id % 10, "oget"
    elseif w == "oget" then
        tr.get, S.pop = id, "owho"
    elseif w == "owho" then
        S.req = { give = tr.give, gn = tr.gn, get = tr.get, who = id }
    elseif w == "dev" then
        if id == 3 then
            S.pop = "plenty1"
        elseif id == 4 then
            S.pop = "mono"
        else
            L.play(g, me, id)
        end
    elseif w == "plenty1" then
        tr.a, S.pop = id, "plenty2"
    elseif w == "plenty2" then
        L.play(g, me, 3, tr.a, id)
    elseif w == "mono" then
        L.play(g, me, 4, id)
    end
    S.changed()
end

-- The spot nearest to the cursor's in a row above (d < 0) or below.
local function rowStep(U, d)
    local at = U.what == L.HEX and S.hxy or (U.what == L.ROAD and S.exy or S.vxy)
    local cx, cy = at(U.spots[U.idx])
    local best, bi = nil, nil
    for i, s in ipairs(U.spots) do
        local x, y = at(s)
        local dy = (y - cy) * d
        if dy > 4 then
            local cost = dy * 3 + math.abs(x - cx)
            if not best or cost < best then best, bi = cost, i end
        end
    end
    return bi
end

local function move(d, rows)
    local U = S.ui
    if U.kind == "bar" then
        for _ = 1, 5 do
            S.bar = (S.bar - 1 + d) % 5 + 1
            if U.on[S.bar] then break end
        end
    elseif U.kind == "place" then
        local n = #U.spots
        if n == 0 then return end
        U.idx = (rows and rowStep(U, d)) or (U.idx - 1 + d) % n + 1
        S.cur = U.spots[U.idx]
    elseif U.kind == "pop" then
        local n = #U.items
        for _ = 1, n do
            U.idx = (U.idx - 1 + d) % n + 1
            if not U.items[U.idx].off then break end
        end
    end
    smudge.request_update()
end

local function back()
    local g, U = S.game, S.ui
    if not U.forced and (U.kind == "pop" or U.kind == "place") then
        done()
    elseif U.kind == "place" and g.phase == "roads" then
        g.free, g.phase = 0, "main"
        done()
    else
        S.show("menu")
    end
end

function I.button(btn)
    local U = S.ui
    if btn == "back" then
        back()
    elseif btn == "confirm" then
        if U.kind == "bar" then
            press(S.bar)
        elseif U.kind == "place" and U.spots[U.idx] then
            placeAt(U.spots[U.idx])
        elseif U.kind == "pop" then
            choose(U.items[U.idx])
        end
    elseif btn == "left" or btn == "right" then
        move(btn == "left" and -1 or 1, false)
    elseif btn == "up" or btn == "page_back" then
        move(-1, U.kind == "place")
    elseif btn == "down" or btn == "page_forward" then
        move(1, U.kind == "place")
    end
end

function I.tap(x, y)
    local U = S.ui
    if y < S.lay.bTop and x > S.w - 90 then return S.show("menu") end
    if U.kind == "pop" then
        local px, py, pw, ph, ry, rh = S.popRect(#U.items, U.sub)
        for i, it in ipairs(U.items) do
            if smudge.in_rect(x, y, px + 8, ry + (i - 1) * rh, pw - 16, rh - 4) then return choose(it) end
        end
        if not U.forced and not smudge.in_rect(x, y, px, py, pw, ph) then done() end
        return
    end
    if U.kind == "place" then
        local at = U.what == L.HEX and S.hxy or (U.what == L.ROAD and S.exy or S.vxy)
        local best, bs = (U.what == L.HEX and 40 or 26) ^ 2, nil
        for _, s in ipairs(U.spots) do
            local sx, sy = at(s)
            local d = (sx - x) ^ 2 + (sy - y) ^ 2
            if d < best then best, bs = d, s end
        end
        if bs then return placeAt(bs) end
        if not U.forced then done() end
    elseif U.kind == "bar" then
        for i = 1, 5 do
            if U.on[i] and smudge.in_rect(x, y, S.barRect(i)) then
                S.bar = i
                return press(i)
            end
        end
    end
end

return I
end
