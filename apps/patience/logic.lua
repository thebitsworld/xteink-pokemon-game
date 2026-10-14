-- Patience rules: FreeCell and Spider. No drawing, so it can be tested on a
-- computer (test/lua_apps/patience_test.lua). Dealing a game (deal.lua),
-- saving (save.lua), undo, the Spider stock, fitting picked cards to a place
-- and "send it" (tools.lua) are loaded only when needed.
--
-- A card is a number 1..52: suit = (card - 1) // 13 (0 clubs, 1 diamonds,
-- 2 hearts, 3 spades), rank = (card - 1) % 13 + 1 (1 ace .. 13 king). A pile
-- is a string, bottom card first, one character per card (card + 64), so a
-- position costs a few hundred bytes and copying a pile is cheap.
--
-- Places ("locations"): FreeCell 1..8 cascades, 9..12 free cells, 13..16 the
-- foundations (13 + suit). Spider 1..10 columns, 11 the stock, 12 the
-- completed runs.
--
-- Undo is a log of every change, four characters each (48 + source,
-- 48 + destination, 48 + count, 48 + flags), so it can be unlimited: a long
-- game is a couple of KB. Flag 1: the move turned up a Spider card at its
-- source; flag 2: done by the game itself after the move before it (a safe
-- card sent to a foundation, a complete Spider run taken off), undone with it.

local P = { FREECELL = 1, SPIDER = 2 }

local CELL, FOUND, STOCK, DONE = 9, 13, 11, 12
P.CELL, P.FOUND, P.STOCK, P.DONE = CELL, FOUND, STOCK, DONE
local FLIP, CHAIN = 1, 2

local char, byte, sub = string.char, string.byte, string.sub

function P.suit(c) return (c - 1) // 13 end
function P.rank(c) return (c - 1) % 13 + 1 end
function P.red(c) local s = (c - 1) // 13 return s == 1 or s == 2 end
function P.enc(c) return char(c + 64) end
-- Card i of a pile string (negative i counts from the top).
function P.at(s, i) return byte(s, i) - 64 end
function P.top(s) local n = #s if n == 0 then return nil end return byte(s, n) - 64 end

-- Runs and limits -----------------------------------------------------------------

-- How many cards on top of column `col` form a run that can move together:
-- FreeCell alternating colours, Spider one suit, each one rank lower.
function P.runLength(g, col)
    local s = g.cols[col]
    local n = #s
    if n == 0 then return 0 end
    local low = n - (g.down and g.down[col] or 0) -- face-up cards
    local len, c = 1, byte(s, n) - 64
    local spider = g.kind == P.SPIDER
    while len < low do
        local b = byte(s, n - len) - 64
        if (b - 1) % 13 ~= (c - 1) % 13 + 1 then break end
        if spider then
            if (b - 1) // 13 ~= (c - 1) // 13 then break end
        elseif P.red(b) == P.red(c) then
            break
        end
        len, c = len + 1, b
    end
    return len
end

function P.freeCells(g)
    local n = 0
    for i = 1, 4 do if g.cells[i] == 0 then n = n + 1 end end
    return n
end

function P.emptyColumns(g)
    local n = 0
    for i = 1, #g.cols do if g.cols[i] == "" then n = n + 1 end end
    return n
end

-- FreeCell: the most cards that can move at once, (free cells + 1) * 2 ^
-- (empty cascades), not counting the destination if it is empty.
function P.maxMove(g, toEmpty)
    local e = P.emptyColumns(g) - (toEmpty and 1 or 0)
    if e < 0 then e = 0 end
    return (P.freeCells(g) + 1) * (1 << e)
end

-- Legal moves ------------------------------------------------------------------

local function isCascade(g, loc) return loc >= 1 and loc <= #g.cols end

-- The bottom card of the `n` cards that would leave `src`, or nil.
function P.moving(g, src, n)
    if isCascade(g, src) then
        local s = g.cols[src]
        if n < 1 or n > P.runLength(g, src) then return nil end
        return byte(s, #s - n + 1) - 64
    end
    if g.kind == P.FREECELL and src >= CELL and src < CELL + 4 then
        local c = g.cells[src - CELL + 1]
        if n == 1 and c ~= 0 then return c end
    end
    return nil
end

-- Whether `n` cards may go from `src` to `dst`; the reason when not.
function P.canMove(g, src, dst, n)
    if src == dst then return false, "Same place" end
    local c = P.moving(g, src, n)
    if not c then return false, "Those cards can't move" end
    if isCascade(g, dst) then
        local s = g.cols[dst]
        if g.kind == P.FREECELL then
            local limit = P.maxMove(g, s == "")
            if n > limit then return false, "Only " .. limit .. " cards can move now" end
        end
        if s == "" then return true end
        local t = byte(s, #s) - 64
        if (t - 1) % 13 ~= (c - 1) % 13 + 1 then return false, "Can't go there" end
        if g.kind == P.FREECELL and P.red(t) == P.red(c) then return false, "Colours must alternate" end
        return true
    end
    if g.kind ~= P.FREECELL then return false, "Can't go there" end
    if dst >= CELL and dst < CELL + 4 then
        if n ~= 1 then return false, "One card per free cell" end
        if g.cells[dst - CELL + 1] ~= 0 then return false, "That cell is taken" end
        return true
    end
    if dst >= FOUND and dst < FOUND + 4 then
        if n ~= 1 then return false, "One card at a time" end
        if dst - FOUND ~= (c - 1) // 13 then return false, "Not its suit" end
        if g.found[(c - 1) // 13 + 1] ~= (c - 1) % 13 then return false, "Not the next card for its suit" end
        return true
    end
    return false, "Can't go there"
end

-- Where a card goes on the foundations: the location of its suit.
function P.foundationFor(c) return FOUND + (c - 1) // 13 end

-- Doing ------------------------------------------------------------------------------
-- (Undoing, dealing from the Spider stock and fitting picked cards to a place
-- are in tools.lua.)

local function record(g, src, dst, n, flags)
    g.log = g.log .. char(48 + src, 48 + dst, 48 + n, 48 + flags)
end

function P.take(g, loc, n)
    if isCascade(g, loc) then
        local s = g.cols[loc]
        g.cols[loc] = sub(s, 1, #s - n)
        return sub(s, #s - n + 1)
    end
    if loc >= FOUND then -- FreeCell foundation
        local i = loc - FOUND + 1
        local r = g.found[i]
        g.found[i] = r - 1
        return char((i - 1) * 13 + r + 64)
    end
    local i = loc - CELL + 1
    local c = g.cells[i]
    g.cells[i] = 0
    return char(c + 64)
end

function P.put(g, loc, cards)
    if isCascade(g, loc) then
        g.cols[loc] = g.cols[loc] .. cards
    elseif loc >= FOUND then
        local i = loc - FOUND + 1
        g.found[i] = g.found[i] + 1
    else
        g.cells[loc - CELL + 1] = byte(cards) - 64
    end
end

-- Spider: turns up the new top card of `col` if it is face down.
local function turnUp(g, col)
    if g.down[col] > 0 and #g.cols[col] == g.down[col] then
        g.down[col] = g.down[col] - 1
        return FLIP
    end
    return 0
end

-- Spider: takes a complete king-to-ace run of one suit off `col`.
function P.removeRun(g, col)
    local s = g.cols[col]
    if #s - g.down[col] < 13 or byte(s, #s) - 64 ~= (byte(s, #s) - 65) // 13 * 13 + 1 then return false end
    if P.runLength(g, col) < 13 then return false end
    g.done = g.done .. char(48 + (byte(s, #s) - 65) // 13)
    g.cols[col] = sub(s, 1, #s - 13)
    record(g, col, DONE, 13, CHAIN + turnUp(g, col))
    return true
end

-- Moves without checking; `chained` marks a move the game made by itself.
local function doMove(g, src, dst, n, chained)
    P.put(g, dst, P.take(g, src, n))
    local flags = chained and CHAIN or 0
    if g.kind == P.SPIDER then flags = flags + turnUp(g, src) end
    record(g, src, dst, n, flags)
end

-- FreeCell: a card is safe on its foundation when both foundations of the
-- other colour already hold its rank - 1 (nothing could still go on it), or
-- it is an ace or a two. Sends every such card from cascades and cells.
function P.autoFoundation(g)
    local sent, again = 0, true
    while again do
        again = false
        for loc = 1, 12 do
            local c
            if loc <= 8 then c = P.top(g.cols[loc]) else c = g.cells[loc - 8] end
            if c and c ~= 0 then
                local r, s = (c - 1) % 13 + 1, (c - 1) // 13
                local f = g.found
                local safe = f[s + 1] == r - 1
                if safe and r > 2 then
                    if s == 1 or s == 2 then safe = f[1] >= r - 1 and f[4] >= r - 1
                    else safe = f[2] >= r - 1 and f[3] >= r - 1 end
                end
                if safe then
                    doMove(g, loc, FOUND + s, 1, true)
                    sent, again = sent + 1, true
                end
            end
        end
    end
    return sent
end

-- A player's move: checked, then the game's own follow-ups. Returns true, or
-- false and the reason.
function P.move(g, src, dst, n)
    local ok, why = P.canMove(g, src, dst, n)
    if not ok then return false, why end
    doMove(g, src, dst, n, false)
    g.moves = g.moves + 1
    if g.kind == P.FREECELL then
        P.autoFoundation(g)
    else
        P.removeRun(g, dst)
    end
    return true
end

function P.canUndo(g) return #g.log > 0 end

function P.won(g)
    if g.kind == P.SPIDER then return #g.done == 8 end
    local f = g.found
    return f[1] == 13 and f[2] == 13 and f[3] == 13 and f[4] == 13
end

return P
