-- Logic tests for apps/patience/logic.lua, deal.lua, save.lua and tools.lua (run by ctest: LuaAppLogic_patience).
local P = dofile(APPS .. "/patience/logic.lua")
local D = dofile(APPS .. "/patience/deal.lua")(P)
local Save = dofile(APPS .. "/patience/save.lua")(P)
local T = dofile(APPS .. "/patience/tools.lua")(P)
P.undo, P.bestTarget, P.fit, P.deal = T.undo, T.bestTarget, T.fit, T.deal
P.newFreeCell, P.newSpider, P.serialize, P.deserialize = D.freeCell, D.spider, Save.serialize, Save.deserialize

-- math.random, seeded: Lua here has 32-bit integers, so a hand-rolled LCG overflows.
math.randomseed(77)
local function rand(n) return math.random(1, n) end

local SUIT = { C = 0, D = 1, H = 2, S = 3 }
local RANK = { A = 1, T = 10, J = 11, Q = 12, K = 13 }
-- "JD" -> card number.
local function card(name)
    local r = RANK[name:sub(1, 1)] or tonumber(name:sub(1, 1))
    return SUIT[name:sub(2, 2)] * 13 + r
end
-- "JD 2D" -> pile string (bottom first).
local function pile(names)
    local t = {}
    for n in names:gmatch("%S+") do t[#t + 1] = P.enc(card(n)) end
    return table.concat(t)
end

local function copy(g) return P.deserialize(P.serialize(g)) end
local function same(a, b) return P.serialize(a) == P.serialize(b) end

-- Every card of the game, counted (foundations by their ranks, Spider's
-- completed runs as 13 cards each).
local function census(g)
    local count = {}
    local function add(c) count[c] = (count[c] or 0) + 1 end
    for _, s in ipairs(g.cols) do for i = 1, #s do add(P.at(s, i)) end end
    if g.kind == P.FREECELL then
        for i = 1, 4 do if g.cells[i] ~= 0 then add(g.cells[i]) end end
        for s = 1, 4 do for r = 1, g.found[s] do add((s - 1) * 13 + r) end end
    else
        for i = 1, #g.stock do add(P.at(g.stock, i)) end
        for i = 1, #g.done do
            local s = g.done:byte(i) - 48
            for r = 1, 13 do add(s * 13 + r) end
        end
    end
    return count
end

-- An empty FreeCell table to set positions up by hand.
local function fcEmpty()
    return { kind = P.FREECELL, deal = 0, cols = { "", "", "", "", "", "", "", "" }, cells = { 0, 0, 0, 0 },
             found = { 0, 0, 0, 0 }, log = "", moves = 0 }
end
local function spEmpty()
    return { kind = P.SPIDER, suits = 4, cols = { "", "", "", "", "", "", "", "", "", "" },
             down = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, stock = "", done = "", log = "", moves = 0 }
end

-- FreeCell deals: Microsoft's numbering, every card once ------------------------------
do
    local g = P.newFreeCell(1)
    -- Deal 1's first row, as every FreeCell program shows it.
    local row = {}
    for c = 1, 8 do row[c] = P.at(g.cols[c], 1) end
    local want = { "JD", "2D", "9H", "JC", "5D", "7H", "7C", "5H" }
    for c = 1, 8 do assert(row[c] == card(want[c]), "deal 1 column " .. c) end
    for _, deal in ipairs({ 1, 2, 617, 11982, 32000 }) do
        local d = P.newFreeCell(deal)
        local count, total = census(d), 0
        for c = 1, 52 do assert(count[c] == 1, "deal " .. deal .. " card " .. c) total = total + 1 end
        for c = 1, 8 do assert(#d.cols[c] <= 7) end
        assert(d.log == "" and d.moves == 0)
    end
    -- Deal 617 (the other usual example) starts 7D AD 5C 3S.
    local d = P.newFreeCell(617)
    for c, n in ipairs({ "7D", "AD", "5C", "3S" }) do assert(P.at(d.cols[c], 1) == card(n), "deal 617 column " .. c) end
end

-- Spider deals: 104 cards, 54 on the table, 10 face up, 50 in the stock ---------------
for _, suits in ipairs({ 1, 2, 4 }) do
    local g = P.newSpider(suits, rand)
    local count, n = census(g), 0
    for c, k in pairs(count) do
        n = n + k
        assert(k == 8 // suits, "each card " .. (8 // suits) .. " times")
    end
    assert(n == 104 and #g.stock == 50)
    for c = 1, 10 do
        assert(#g.cols[c] == (c <= 4 and 6 or 5) and g.down[c] == #g.cols[c] - 1)
    end
    local kinds = 0
    for _ in pairs(count) do kinds = kinds + 1 end
    assert(kinds == 13 * suits)
end

-- FreeCell move rules -----------------------------------------------------------------
do
    local g = fcEmpty()
    g.cols[1] = pile("KS QH JC")
    g.cols[2] = pile("QD")
    g.cols[3] = pile("8S 7D 6C 5H")
    g.cols[4] = pile("TC 9C")
    g.cols[5] = pile("AH")
    for c = 6, 8 do g.cols[c] = pile("KC") end
    assert(P.runLength(g, 1) == 3 and P.runLength(g, 3) == 4 and P.runLength(g, 4) == 1)
    -- Alternate colours, one rank lower.
    assert(P.canMove(g, 1, 2, 1), "JC on QD")
    assert(not P.canMove(g, 1, 2, 2), "QH JC on QD: QH is not one lower")
    assert(P.canMove(g, 4, 2, 1) == false, "9C on QD: wrong rank")
    g.cols[2] = pile("TD")
    assert(P.canMove(g, 4, 2, 1), "9C on TD")
    g.cols[2] = pile("TS")
    assert(not P.canMove(g, 4, 2, 1), "9C on TS: same colour")
    -- Cells: one card, empty cell only.
    assert(P.canMove(g, 1, P.CELL, 1) and not P.canMove(g, 1, P.CELL, 2))
    g.cells[1] = card("2C")
    assert(not P.canMove(g, 1, P.CELL, 1) and P.canMove(g, 1, P.CELL + 1, 1))
    -- Foundations: from the ace up, by suit.
    assert(P.canMove(g, 5, P.FOUND + SUIT.H, 1) and not P.canMove(g, 5, P.FOUND + SUIT.S, 1))
    assert(not P.canMove(g, P.CELL, P.FOUND + SUIT.C, 1), "2C before the ace")
    -- fit(): onto a card only the part of the run that fits.
    g.cols[2] = pile("7H")
    local k, d = P.fit(g, 3, 2, 4)
    assert(k == 2 and d == 2, "6C 5H onto 7H")
    k, d = P.fit(g, 5, P.FOUND, 1)
    assert(k == 1 and d == P.FOUND + SUIT.H, "the foundation of the card's suit")
end

-- Supermoves: (free cells + 1) * 2 ^ empty cascades, the target not counted ------------
do
    local g = fcEmpty()
    g.cols[1] = pile("KS QH JC TD 9S 8H 7C 6D 5S 4H 3C")
    g.cols[2] = pile("4D")
    g.cols[3] = pile("KH")
    g.cols[4] = pile("KD")
    g.cols[5] = pile("KC")
    g.cols[6] = pile("QS")
    -- Columns 7 and 8 empty, four free cells: (4 + 1) * 4 = 20, to an empty one 10.
    assert(P.maxMove(g, false) == 20 and P.maxMove(g, true) == 10)
    g.cells = { card("2S"), card("2H"), card("2D"), 0 }
    assert(P.maxMove(g, false) == 8 and P.maxMove(g, true) == 4)
    g.cols[7] = pile("KS")
    assert(P.maxMove(g, false) == 4 and P.maxMove(g, true) == 2)
    -- Moving 5 cards onto JC needs 5: refused with 4 allowed, fine with a cell freed.
    g.cols[6] = pile("QD JS")
    g.cols[1] = pile("KS QH TD 9S 8H 7C 6D")
    g.cols[6] = pile("JC")
    assert(not P.canMove(g, 1, 6, 5), "five cards, four allowed")
    g.cells[3] = 0
    assert(P.maxMove(g, false) == 6 and P.canMove(g, 1, 6, 5))
    -- To an empty cascade: 3 cards allowed (2 cells + 1, the other empty one is the target).
    g.cols[7] = ""
    g.cols[8] = pile("KH")
    assert(P.maxMove(g, true) == 3)
    assert(P.canMove(g, 1, 7, 3) and not P.canMove(g, 1, 7, 4))
    local k = P.fit(g, 1, 7, 5)
    assert(k == 3, "fit() trims a pick to what an empty cascade takes")
end

-- Safe cards go up by themselves; undo takes them back with the move -----------------
do
    local g = fcEmpty()
    g.found = { 1, 1, 1, 1 }
    g.cols[1] = pile("KS 2H")
    g.cols[2] = pile("2C 3D")
    g.cols[3] = pile("QS")
    g.cells[1] = card("2D")
    local before = copy(g)
    -- Moving 3D to a cell uncovers 2C: 2C, 2D and 2H go up; 3D stays (the ace of spades is
    -- still alone, so a black 2 might want it).
    assert(P.move(g, 2, P.CELL + 1, 1))
    assert(g.found[SUIT.C + 1] == 2 and g.found[SUIT.D + 1] == 2 and g.found[SUIT.H + 1] == 2)
    assert(g.cells[2] == card("3D") and g.cells[1] == 0)
    assert(g.moves == 1)
    assert(P.undo(g))
    assert(same(g, before), "undo restores the position exactly")
    -- A 5H is not safe while the black foundations are at 3.
    local h = fcEmpty()
    h.found = { 3, 4, 4, 3 }
    h.cols[1] = pile("KS 5H")
    h.cols[2] = pile("KC 6C")
    assert(P.move(h, 2, P.CELL, 1))
    assert(h.found[SUIT.H + 1] == 4, "5H stays: 4C and 4S are not up yet")
    h.found[1], h.found[4] = 4, 4
    assert(P.autoFoundation(h) >= 1 and h.found[SUIT.H + 1] == 5)
end

-- Spider move rules ---------------------------------------------------------------------
do
    local g = spEmpty()
    g.cols[1] = pile("5C 9S 8S 7S")
    g.down[1] = 1
    g.cols[2] = pile("8H")
    g.cols[3] = pile("9D 8D 7S")
    g.cols[4] = pile("KS")
    for c = 5, 10 do g.cols[c] = pile("QC") end
    assert(P.runLength(g, 1) == 3, "face-down cards never count")
    assert(P.runLength(g, 3) == 1, "a run is one suit")
    -- Any suit one rank higher, or an empty column; only same-suit runs move.
    assert(P.canMove(g, 3, 2, 1), "7S on 8H")
    assert(not P.canMove(g, 3, 2, 2), "7S 8D is not a run")
    assert(not P.canMove(g, 1, 2, 3), "9S needs a 10")
    g.cols[6] = pile("TH")
    assert(P.canMove(g, 1, 6, 3), "9S 8S 7S on TH")
    -- Turning up: moving the whole run shows the face-down 5C.
    local before = copy(g)
    assert(P.move(g, 1, 6, 3))
    assert(g.down[1] == 0 and g.cols[1] == pile("5C"))
    assert(P.undo(g) and same(g, before), "undo turns the card back down")
    -- Empty column takes anything.
    g.cols[5] = ""
    assert(P.canMove(g, 4, 5, 1) and P.canMove(g, 1, 5, 2))
end

-- Spider: a complete K..A run goes off; dealing is refused with an empty column --------
do
    local g = spEmpty()
    g.cols[1] = pile("3C KH QH JH TH 9H 8H 7H 6H 5H 4H 3H 2H")
    g.down[1] = 1
    g.cols[2] = pile("AH")
    for c = 3, 10 do g.cols[c] = pile("KC") end
    g.stock = pile("AS 2S 3S 4S 5S 6S 7S 8S 9S TS")
    local before = copy(g)
    assert(P.move(g, 2, 1, 1))
    assert(g.done == "2" and g.cols[1] == pile("3C") and g.down[1] == 0, "run off, 3C turned up")
    assert(not P.won(g))
    -- Column 2 is now empty: no deal.
    local ok, why = P.deal(g)
    assert(not ok and why:find("column"))
    g.cols[2] = pile("4D")
    assert(P.deal(g))
    assert(g.stock == "" and #g.cols[1] == 2 and P.top(g.cols[1]) == card("TS") and P.top(g.cols[10]) == card("AS"))
    local ok2 = P.deal(g)
    assert(not ok2, "the stock is empty")
    -- Undo the deal, then the move with its run: back exactly.
    assert(P.undo(g))
    g.cols[2] = ""
    assert(P.undo(g) and same(g, before), "undo puts the run back with its card face down")
    -- A deal that completes a run takes it off too.
    local h = spEmpty()
    h.cols[1] = pile("KS QS JS TS 9S 8S 7S 6S 5S 4S 3S 2S")
    for c = 2, 10 do h.cols[c] = pile("KD") end
    h.stock = pile("2D 2D 2D 2D 2D 2D 2D 2D 2D AS")
    assert(P.deal(h) and h.done == "3" and h.cols[1] == "")
    assert(P.undo(h) and #h.stock == 10 and h.done == "" and #h.cols[1] == 12)
    -- Eight runs off: won.
    h.done = "33333333"
    assert(P.won(h))
end

-- Undo is exact across a long random game, for both games ------------------------------
local function randomPlay(g, steps)
    local snaps = { P.serialize(g) }
    local fc = g.kind == P.FREECELL
    for _ = 1, steps do
        -- Every legal move, one picked at random (Spider deals now and then).
        local moves = {}
        for src = 1, fc and 12 or 10 do
            local run = src <= #g.cols and P.runLength(g, src) or 1
            for dst = 1, fc and 16 or 10 do
                for n = 1, run do
                    if P.canMove(g, src, dst, n) then moves[#moves + 1] = { src, dst, n } end
                end
            end
        end
        local moved = false
        if not fc and (#moves == 0 or rand(10) == 1) then moved = P.deal(g) end
        if not moved and #moves > 0 then
            local mv = moves[rand(#moves)]
            moved = P.move(g, mv[1], mv[2], mv[3])
            assert(moved)
        end
        if not moved then break end
        snaps[#snaps + 1] = P.serialize(g)
        local count, total = census(g), 0
        for _, k in pairs(count) do total = total + k end
        assert(total == (g.kind == P.FREECELL and 52 or 104), "cards conserved")
    end
    -- Saving keeps the log: undo after a reload.
    local back = P.deserialize(P.serialize(g))
    assert(same(back, g))
    for i = #snaps - 1, 1, -1 do
        assert(P.undo(back))
        -- The saved position kept its log; compare without it.
        local want = P.deserialize(snaps[i])
        back.moves = want.moves
        assert(same(back, want), "undo step " .. i)
    end
    assert(not P.undo(back) or back.log == "")
    return #snaps
end
for seed = 1, 8 do assert(randomPlay(P.newFreeCell(seed * 1000 + 7), 150) > 10) end
for _, suits in ipairs({ 1, 2, 4 }) do for _ = 1, 3 do assert(randomPlay(P.newSpider(suits, rand), 200) > 30) end end

-- Saving round trip; broken text is refused --------------------------------------------
do
    local g = P.newSpider(2, rand)
    assert(same(P.deserialize(P.serialize(g)), g))
    local f = P.newFreeCell(5)
    assert(same(P.deserialize(P.serialize(f)), f))
    assert(P.deserialize("") == nil and P.deserialize("F|1|2|x") == nil and P.deserialize("S|2|0|||") == nil)
end

-- Sending a card to its best place ------------------------------------------------------
do
    local g = fcEmpty()
    g.found = { 0, 0, 2, 0 }
    g.cols[1] = pile("KS 3H")
    g.cols[2] = pile("5C")
    g.cols[3] = pile("QS 4D")
    for c = 4, 8 do g.cols[c] = pile("KC") end
    local k, d = P.bestTarget(g, 1, 1)
    assert(k == 1 and d == P.FOUND + SUIT.H, "3H to its foundation")
    k, d = P.bestTarget(g, 3, 1)
    assert(k == 1 and d == 2, "4D onto 5C")
    k, d = P.bestTarget(g, 2, 1)
    assert(k == 1 and d == P.CELL, "5C has nowhere but a cell")
end

-- A simple solver wins FreeCell deal 3 (depth-first with safe auto-moves; it needs
-- about 150 positions there, while deal 1 takes it some 50000) --------------
do
    local seen, nodes = {}, 0
    local function key(g)
        local cols = {}
        for i = 1, 8 do cols[i] = g.cols[i] end
        table.sort(cols)
        local cells = {}
        for i = 1, 4 do cells[i] = g.cells[i] end
        table.sort(cells)
        return table.concat(cols, ",") .. table.concat(cells, ".")
    end
    local function candidates(g)
        local out = {}
        -- Foundations, then onto cards, then empty columns, then cells.
        for src = 1, 12 do
            local c = src <= 8 and P.top(g.cols[src]) or g.cells[src - 8]
            if c and c ~= 0 and P.canMove(g, src, P.foundationFor(c), 1) then out[#out + 1] = { src, P.foundationFor(c), 1 } end
        end
        for src = 1, 12 do
            local run = src <= 8 and P.runLength(g, src) or 1
            for dst = 1, 8 do
                if g.cols[dst] ~= "" then
                    local k = P.fit(g, src, dst, run)
                    if k then out[#out + 1] = { src, dst, k } end
                end
            end
        end
        for src = 1, 12 do
            local run = src <= 8 and P.runLength(g, src) or 1
            if src > 8 or #g.cols[src] > run then
                for dst = 1, 8 do
                    if g.cols[dst] == "" then
                        local k = P.fit(g, src, dst, run)
                        if k then out[#out + 1] = { src, dst, k } end
                        break
                    end
                end
            end
        end
        for src = 1, 8 do
            if g.cols[src] ~= "" then
                for i = 1, 4 do
                    if g.cells[i] == 0 then out[#out + 1] = { src, P.CELL + i - 1, 1 } break end
                end
            end
        end
        return out
    end
    local function solve(g, depth)
        if P.won(g) then return true end
        if depth > 200 or nodes > 5000 then return false end
        local k = key(g)
        if seen[k] then return false end
        seen[k] = true
        nodes = nodes + 1
        for _, mv in ipairs(candidates(g)) do
            if P.move(g, mv[1], mv[2], mv[3]) then
                if solve(g, depth + 1) then return true end
                P.undo(g)
            end
        end
        return false
    end
    local g = P.newFreeCell(3)
    assert(solve(g, 0), "deal 3 solved")
    assert(P.won(g))
    -- Undoing the whole game gives deal 3 again.
    while P.undo(g) do end
    assert(same(g, P.newFreeCell(3)))
end

print("patience: all tests passed")
