-- Patience: the rules needed only now and then - undo, dealing from the Spider
-- stock, fitting picked cards to a place and "send it" - loaded for that. Usage:
--   local T = smudge.dofile("tools.lua")(P)   -- T.undo(g), T.deal(g), T.fit(...), T.bestTarget(...)
-- with P the rules (logic.lua); the undo log is described there.

return function(P)
local char, byte, sub = string.char, string.byte, string.sub
local STOCK, DONE, FLIP, CHAIN = P.STOCK, P.DONE, 1, 2
local T = {}

-- Takes back the last player's move with everything the game did after it.
function T.undo(g)
    local log = g.log
    if log == "" then return false end
    repeat
        local n = #log
        local src, dst, cnt, flags = byte(log, n - 3) - 48, byte(log, n - 2) - 48, byte(log, n - 1) - 48,
                                     byte(log, n) - 48
        log = sub(log, 1, n - 4)
        if g.kind == P.SPIDER and flags & FLIP ~= 0 then g.down[src] = g.down[src] + 1 end
        if src == STOCK and g.kind == P.SPIDER then
            local back = {}
            for c = 1, 10 do
                local s = g.cols[c]
                back[11 - c] = sub(s, #s)
                g.cols[c] = sub(s, 1, #s - 1)
            end
            g.stock = g.stock .. table.concat(back)
        elseif dst == DONE and g.kind == P.SPIDER then
            local suit = byte(g.done, #g.done) - 48
            g.done = sub(g.done, 1, #g.done - 1)
            local t = {}
            for r = 13, 1, -1 do t[#t + 1] = char(suit * 13 + r + 64) end
            g.cols[src] = g.cols[src] .. table.concat(t)
        else
            P.put(g, src, P.take(g, dst, cnt))
        end
    until flags & CHAIN == 0 or log == ""
    g.log = log
    g.moves = math.max(0, g.moves - 1)
    return true
end

-- Where `n` cards picked at `src` would best go, for "send it": the
-- foundation, then a card they fit on (Spider: one of their suit first), then
-- an empty column, then a free cell. Returns count, destination or nil.
function T.bestTarget(g, src, n)
    if g.kind == P.FREECELL and n == 1 then
        local c = P.moving(g, src, 1)
        if c and P.canMove(g, src, P.foundationFor(c), 1) then return 1, P.foundationFor(c) end
    end
    local c = P.moving(g, src, n)
    if not c then return nil end
    local best, bestScore = nil, 0
    for d = 1, #g.cols do
        if d ~= src and P.canMove(g, src, d, n) then
            local t = P.top(g.cols[d])
            local score = t and 2 or 1
            if t and g.kind == P.SPIDER and (t - 1) // 13 == (c - 1) // 13 then score = 3 end
            -- Moving a whole column onto an empty one gains nothing.
            if not t and src <= #g.cols and #g.cols[src] == n then score = 0 end
            if score > bestScore then best, bestScore = d, score end
        end
    end
    if best then return n, best end
    if g.kind == P.FREECELL and n == 1 and src <= 8 then
        for i = 1, 4 do if g.cells[i] == 0 then return 1, P.CELL + i - 1 end end
    end
    return nil
end

-- With `n` cards picked at `src` and `dst` chosen, how many of them go: onto
-- a card only the run that fits it, onto an empty cascade as many as allowed,
-- into a cell or a foundation the top card. Returns count and destination
-- (a foundation drop goes to the card's own suit), or nil and a reason.
function T.fit(g, src, dst, n)
    if dst >= P.FOUND and dst < P.FOUND + 4 and g.kind == P.FREECELL then
        local c = P.moving(g, src, 1)
        if not c then return nil, "Those cards can't move" end
        dst = P.foundationFor(c)
        local ok, why = P.canMove(g, src, dst, 1)
        if ok then return 1, dst end
        return nil, why
    end
    if dst > #g.cols then
        local ok, why = P.canMove(g, src, dst, 1)
        if ok then return 1, dst end
        return nil, why
    end
    if g.cols[dst] == "" then
        local k = n
        if g.kind == P.FREECELL then k = math.min(n, P.maxMove(g, true)) end
        local ok, why = P.canMove(g, src, dst, k)
        if ok then return k, dst end
        return nil, why
    end
    local why = "Can't go there"
    for k = n, 1, -1 do
        local ok, w = P.canMove(g, src, dst, k)
        if ok then return k, dst end
        if k == n then why = w end
    end
    return nil, why
end

-- Spider: deals one card on every column. The classic rule: not while a
-- column is empty.
function T.canDeal(g)
    if g.kind ~= P.SPIDER then return false end
    if g.stock == "" then return false, "The stock is empty" end
    if P.emptyColumns(g) > 0 then return false, "Fill every column before dealing" end
    return true
end

function T.deal(g)
    local ok, why = T.canDeal(g)
    if not ok then return false, why end
    local s = g.stock
    for c = 1, 10 do
        g.cols[c] = g.cols[c] .. sub(s, #s - c + 1, #s - c + 1)
    end
    g.stock = sub(s, 1, #s - 10)
    g.log = g.log .. char(48 + STOCK, 48, 58, 48)
    g.moves = g.moves + 1
    for c = 1, 10 do P.removeRun(g, c) end
    return true
end

return T
end
