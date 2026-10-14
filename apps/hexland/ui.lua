-- Hexland: what you can do now, worked out after each change. Usage:
--   S.ui = smudge.dofile("ui.lua")(L, S)
-- Returns { kind = , prompt = , ... } with kind
--   "wait"  - a device player is playing (or the game is over: a pop-up)
--   "bar"   - the buttons; on[1..5]: Roll, Build, Trade, Cards, End enabled
--   "place" - what (L.ROAD, L.SETTLE, L.CITY or L.HEX), spots (top to
--             bottom), idx (the cursor), forced (Back does not cancel)
--   "pop"   - what, title, items { label, id, note, off }, idx, forced
-- The phases that must be answered (set-up, discards, the robber, an offer)
-- come first, then a pop-up or build you opened (S.pop, S.place).

return function(L, S)
L = smudge.dofile("rules.lua")(L)
local g = S.game
local me = g.human
local pl = g.pl[me]
local U = { kind = "bar" }
local CANCEL = { label = "Cancel", id = 0 }

local function place(what, prompt, forced)
    U.kind, U.what, U.prompt, U.forced = "place", what, prompt, forced
    local spots = L.spots(g, me, what)
    local key = {}
    for _, s in ipairs(spots) do
        local x, y
        if what == L.HEX then x, y = S.hxy(s) elseif what == L.ROAD then x, y = S.exy(s) else x, y = S.vxy(s) end
        key[s] = y * 1000 + x
    end
    table.sort(spots, function(a, b) return key[a] < key[b] end)
    U.spots, U.idx = spots, 1
    for i, s in ipairs(spots) do
        if s == S.cur then U.idx = i end
    end
end

local function pop(what, title, items, forced, prompt)
    U.kind, U.what, U.title, U.items, U.forced = "pop", what, title, items, forced
    U.prompt = prompt or title
    U.idx = 1
    for i, it in ipairs(items) do
        if not it.off then
            U.idx = i
            break
        end
    end
end

-- One item per resource; f(r) returns label, note, off (nil label: none).
local function resources(f, cancel)
    local items = {}
    for r = 1, 5 do
        local label, note, off = f(r)
        if label then items[#items + 1] = { label = label, id = r, note = note, off = off } end
    end
    if cancel then items[#items + 1] = CANCEL end
    return items
end

local function have(r) return pl.r[r] end
local ph = g.phase

if ph == "over" then
    local w = g.winner
    pop("over", (w == me and "You win" or (L.NAMES[w] .. " wins")) .. " with " .. L.vp(g, w, true) .. " points!",
        { { label = "New game", id = 1, note = "Same level and board" }, { label = "Menu", id = 2 } }, true)
    local n = L.place(g, me)
    U.sub = "You " .. (w == me and "came first" or ("placed " .. ({ "1st", "2nd", "3rd", "4th" })[n]))
elseif L.actor(g) ~= me then
    U.kind = "wait"
    U.prompt = L.NAMES[L.actor(g)] .. (L.setup(g) and " is placing..." or " is playing...")
elseif ph == "setupS" then
    place(L.SETTLE, g.sstep > 4 and "Your second settlement" or "Place your first settlement", true)
elseif ph == "setupR" then
    place(L.ROAD, "Place a road next to it", true)
elseif ph == "roads" then
    place(L.ROAD, "Road Building: free road " .. (3 - g.free) .. " of 2", true)
elseif ph == "robber" then
    place(L.HEX, "Move the robber to a hex", true)
elseif ph == "discard" then
    pop("discard", "Discard " .. g.need[me] .. " more", resources(function(r)
        if have(r) > 0 then return L.RES[r], "you have " .. have(r) end
    end), true, "Rolled 7: discard half your cards")
elseif ph == "steal" then
    local items = {}
    for i = 1, #g.vict do
        local p = g.vict:byte(i) - 48
        items[#items + 1] = { label = L.NAMES[p], id = p, note = L.count(g, p) .. " cards" }
    end
    pop("steal", "Take a card from", items, true)
elseif ph == "offer" then
    local o = g.offer
    pop("offer", L.NAMES[o[1]] .. " offers " .. o[3] .. " " .. L.RES[o[2]] .. " for your " .. o[5] .. " " .. L.RES[o[4]],
        { { label = "Accept", id = 1, off = have(o[4]) < o[5], note = "you have " .. have(o[4]) },
          { label = "Decline", id = 2 } }, true, "A trade offer")
elseif S.place then
    local names = { "road", "settlement", "city" }
    place(S.place, "Place your " .. names[S.place] .. " (Back: cancel)", false)
elseif S.pop == "build" then
    local items = {}
    local names = { "Road", "Settlement", "City" }
    for k = 1, 3 do
        local why
        if L.left(g, me, k) <= 0 then
            why = "none left"
        elseif not L.afford(g, me, k) then
            why = "need " .. L.short(L.COST[k])
        elseif #L.spots(g, me, k) == 0 then
            why = "no place for it"
        end
        items[k] = { label = names[k], id = k, note = why or L.short(L.COST[k]), off = why ~= nil }
    end
    local why = (#g.deck == 0 and "none left") or (not L.afford(g, me, L.CARD) and "need Wl Gr Or") or nil
    items[4] = { label = "Development card", id = L.CARD, note = why or ("Wl Gr Or, " .. #g.deck .. " left"),
                 off = why ~= nil }
    items[5] = CANCEL
    pop("build", "Build", items)
elseif S.pop == "trade" then
    pop("trade", "Trade", { { label = "With the bank", id = 1, note = "4:1, or your ports' rates" },
                            { label = "Offer to players", id = 2, note = "1 or 2 of yours for 1" }, CANCEL })
elseif S.pop == "bgive" then
    pop("bgive", "Give to the bank", resources(function(r)
        local n = L.rate(g, me, r)
        return "Give " .. n .. " " .. L.RES[r], "you have " .. have(r), have(r) < n
    end, true))
elseif S.pop == "bget" then
    pop("bget", "Give " .. L.rate(g, me, S.tr.give) .. " " .. L.RES[S.tr.give] .. " for", resources(function(r)
        if r ~= S.tr.give then return "1 " .. L.RES[r], g.bank[r] == 0 and "bank has none" or nil, g.bank[r] == 0 end
    end, true))
elseif S.pop == "ogive" then
    local items = {}
    for r = 1, 5 do
        for n = 1, 2 do
            if have(r) >= n then items[#items + 1] = { label = "Give " .. n .. " " .. L.RES[r], id = r * 10 + n } end
        end
    end
    items[#items + 1] = CANCEL
    pop("ogive", "Your offer: you give", items)
elseif S.pop == "oget" then
    pop("oget", "Give " .. S.tr.gn .. " " .. L.RES[S.tr.give] .. " for", resources(function(r)
        if r ~= S.tr.give then return "1 " .. L.RES[r] end
    end, true))
elseif S.pop == "owho" then
    local items = { { label = "All players", id = 0 } }
    for p = 2, 4 do items[#items + 1] = { label = L.NAMES[p], id = p, note = L.count(g, p) .. " cards" } end
    items[#items + 1] = CANCEL
    pop("owho", "Offer " .. S.tr.gn .. " " .. L.SH[S.tr.give] .. " for 1 " .. L.SH[S.tr.get] .. " to", items)
elseif S.pop == "dev" then
    local items = {}
    local HELP = { "move the robber, take a card", "two free roads", "take any 2 from the bank",
                   "take all of one resource" }
    for c = 1, 4 do
        if pl.d[c] > 0 then
            local ok = L.canPlay(g, me, c)
            local note = HELP[c]
            if not ok then
                note = (g.played and "one card a turn") or (pl.d[c] == pl.nd[c] and "bought this turn") or
                    (ph == "roll" and "after rolling") or "can't now"
            end
            items[#items + 1] = { label = L.DEV[c] .. (pl.d[c] > 1 and (" x" .. pl.d[c]) or ""), id = c, note = note,
                                  off = not ok }
        end
    end
    items[#items + 1] = CANCEL
    pop("dev", "Play a development card", items)
elseif S.pop == "plenty1" or S.pop == "plenty2" then
    pop(S.pop, "Year of Plenty: " .. (S.pop == "plenty1" and "first" or "second") .. " resource", resources(function(r)
        return L.RES[r], "bank " .. g.bank[r], g.bank[r] == 0
    end, S.pop == "plenty1"))
elseif S.pop == "mono" then
    pop("mono", "Monopoly: take all of", resources(function(r) return L.RES[r] end, true))
else
    U.on = { ph == "roll", ph == "main", ph == "main", false, ph == "main" }
    for c = 1, 4 do
        if L.canPlay(g, me, c) then U.on[4] = true end
    end
    U.prompt = ph == "roll" and "Your turn: roll the dice" or "Build, trade or end your turn"
    if S.bar < 1 or S.bar > 5 or not U.on[S.bar] then
        S.bar = ph == "roll" and 1 or 2
    end
end

return U
end
