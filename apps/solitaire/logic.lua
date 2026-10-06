-- Klondike solitaire rules. No drawing, so it can be tested on a computer
-- (test/lua_apps/solitaire_test.lua).
--
-- A card is a number 1..52: suit = (card - 1) // 13 (0 clubs, 1 diamonds,
-- 2 hearts, 3 spades), rank = (card - 1) % 13 + 1 (1 ace .. 13 king).
-- Every pile lists its cards bottom to top. A tableau column also records how
-- many of its bottom cards are still face down.

local K = {}

function K.suit(card) return (card - 1) // 13 end
function K.rank(card) return (card - 1) % 13 + 1 end
function K.isRed(card) local s = K.suit(card); return s == 1 or s == 2 end
K.RANK_NAMES = { "A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K" }

-- `rand(n)` returns 1..n. drawCount is 1 or 3.
function K.new(rand, drawCount)
    local deck = {}
    for i = 1, 52 do deck[i] = i end
    for i = 52, 2, -1 do
        local j = rand(i)
        deck[i], deck[j] = deck[j], deck[i]
    end
    local g = { stock = {}, waste = {}, foundations = { {}, {}, {}, {} }, tableau = {},
                drawCount = drawCount or 1, moves = 0, history = {} }
    local n = 52
    for col = 1, 7 do
        local pile = { cards = {}, down = col - 1 }
        for _ = 1, col do
            pile.cards[#pile.cards + 1] = deck[n]
            n = n - 1
        end
        g.tableau[col] = pile
    end
    for i = n, 1, -1 do g.stock[#g.stock + 1] = deck[i] end
    return g
end

-- Undo ----------------------------------------------------------------------
-- Each undo step is the position as a compact string (K.serialize(), a few
-- hundred bytes), not a copy of every pile table: tables would cost ~3 KB a
-- step against the 75 KB Lua heap on the X3.

local MAX_UNDO = 20

local function remember(g)
    g.history[#g.history + 1] = K.serialize(g)
    if #g.history > MAX_UNDO then table.remove(g.history, 1) end
end

function K.canUndo(g) return #g.history > 0 end

function K.undo(g)
    local s = table.remove(g.history)
    if not s then return false end
    local back = K.deserialize(s)
    if not back then return false end
    g.stock, g.waste, g.foundations, g.tableau, g.moves = back.stock, back.waste, back.foundations, back.tableau, back.moves
    return true
end

-- Moves ---------------------------------------------------------------------

-- Turns over cards from the stock, or turns the waste back over when the
-- stock is empty. Returns false when both are empty.
function K.draw(g)
    if #g.stock == 0 then
        if #g.waste == 0 then return false end
        remember(g)
        for i = #g.waste, 1, -1 do g.stock[#g.stock + 1] = g.waste[i] end
        g.waste = {}
        g.moves = g.moves + 1
        return true
    end
    remember(g)
    for _ = 1, g.drawCount do
        if #g.stock == 0 then break end
        g.waste[#g.waste + 1] = table.remove(g.stock)
    end
    g.moves = g.moves + 1
    return true
end

-- A source is { kind = "waste" } | { kind = "foundation", index = 1..4 } |
-- { kind = "tableau", col = 1..7, index = position of the first moved card }.
-- Returns the list of cards it would move (bottom first), or nil.
function K.sourceCards(g, src)
    if src.kind == "waste" then
        if #g.waste == 0 then return nil end
        return { g.waste[#g.waste] }
    elseif src.kind == "foundation" then
        local f = g.foundations[src.index]
        if #f == 0 then return nil end
        return { f[#f] }
    elseif src.kind == "tableau" then
        local pile = g.tableau[src.col]
        if src.index <= pile.down or src.index > #pile.cards then return nil end
        local out = {}
        for i = src.index, #pile.cards do out[#out + 1] = pile.cards[i] end
        return out
    end
    return nil
end

function K.canPlaceOnTableau(g, cards, col)
    local pile = g.tableau[col]
    local first = cards[1]
    if #pile.cards == 0 then return K.rank(first) == 13 end
    local top = pile.cards[#pile.cards]
    return K.isRed(top) ~= K.isRed(first) and K.rank(top) == K.rank(first) + 1
end

function K.canPlaceOnFoundation(g, cards, index)
    if #cards ~= 1 then return false end
    local card = cards[1]
    local f = g.foundations[index]
    if #f == 0 then return K.rank(card) == 1 end
    local top = f[#f]
    return K.suit(top) == K.suit(card) and K.rank(card) == K.rank(top) + 1
end

local function removeFromSource(g, src, count)
    if src.kind == "waste" then
        table.remove(g.waste)
    elseif src.kind == "foundation" then
        table.remove(g.foundations[src.index])
    else
        local pile = g.tableau[src.col]
        for _ = 1, count do table.remove(pile.cards) end
        -- Turn over the new top card.
        if #pile.cards == 0 then
            pile.down = 0
        elseif pile.down >= #pile.cards then
            pile.down = #pile.cards - 1
        end
    end
end

-- dst = { kind = "tableau", col } | { kind = "foundation", index }.
function K.move(g, src, dst)
    local cards = K.sourceCards(g, src)
    if not cards then return false end
    -- Dropping a card back where it came from is not a move.
    if src.kind == "foundation" and dst.kind == "foundation" and src.index == dst.index then return false end
    if src.kind == "tableau" and dst.kind == "tableau" and src.col == dst.col then return false end
    if dst.kind == "tableau" then
        if not K.canPlaceOnTableau(g, cards, dst.col) then return false end
        remember(g)
        removeFromSource(g, src, #cards)
        local pile = g.tableau[dst.col]
        for _, c in ipairs(cards) do pile.cards[#pile.cards + 1] = c end
    elseif dst.kind == "foundation" then
        if not K.canPlaceOnFoundation(g, cards, dst.index) then return false end
        remember(g)
        removeFromSource(g, src, 1)
        local f = g.foundations[dst.index]
        f[#f + 1] = cards[1]
    else
        return false
    end
    g.moves = g.moves + 1
    return true
end

-- Sends the source's top card to whichever foundation takes it.
function K.moveToFoundation(g, src)
    local cards = K.sourceCards(g, src)
    if not cards or #cards ~= 1 then return false end
    for i = 1, 4 do
        if K.canPlaceOnFoundation(g, cards, i) then return K.move(g, src, { kind = "foundation", index = i }) end
    end
    return false
end

-- Any legal destination for the source, foundations first; nil if none.
function K.findDestination(g, src)
    local cards = K.sourceCards(g, src)
    if not cards then return nil end
    if #cards == 1 then
        for i = 1, 4 do
            if K.canPlaceOnFoundation(g, cards, i) then return { kind = "foundation", index = i } end
        end
    end
    for col = 1, 7 do
        if not (src.kind == "tableau" and src.col == col) and K.canPlaceOnTableau(g, cards, col) then
            -- Moving a king from one empty-bottomed column to another is pointless.
            if not (src.kind == "tableau" and src.index == 1 and #g.tableau[col].cards == 0) then
                return { kind = "tableau", col = col }
            end
        end
    end
    return nil
end

function K.isWon(g)
    for i = 1, 4 do if #g.foundations[i] ~= 13 then return false end end
    return true
end

-- Every card is face up and the stock is gone: the rest plays itself.
function K.canAutoFinish(g)
    if #g.stock > 0 or #g.waste > 0 then return false end
    for col = 1, 7 do if g.tableau[col].down > 0 then return false end end
    return not K.isWon(g)
end

-- One step of auto-finish: the lowest card that fits a foundation.
function K.autoFinishStep(g)
    local bestCol, bestRank = nil, 99
    for col = 1, 7 do
        local pile = g.tableau[col]
        if #pile.cards > 0 then
            local r = K.rank(pile.cards[#pile.cards])
            if r < bestRank then
                local src = { kind = "tableau", col = col, index = #pile.cards }
                if K.findDestination(g, src) and K.findDestination(g, src).kind == "foundation" then
                    bestCol, bestRank = col, r
                end
            end
        end
    end
    if not bestCol then return false end
    return K.moveToFoundation(g, { kind = "tableau", col = bestCol, index = #g.tableau[bestCol].cards })
end

-- Save/restore as one string (no history) ------------------------------------

local function listToString(list) return table.concat(list, ".") end
local function stringToList(s)
    local out = {}
    for n in s:gmatch("%d+") do out[#out + 1] = tonumber(n) end
    return out
end

function K.serialize(g)
    local parts = { tostring(g.drawCount), tostring(g.moves), listToString(g.stock), listToString(g.waste) }
    for i = 1, 4 do parts[#parts + 1] = listToString(g.foundations[i]) end
    for i = 1, 7 do parts[#parts + 1] = g.tableau[i].down .. ":" .. listToString(g.tableau[i].cards) end
    return table.concat(parts, "|")
end

function K.deserialize(s)
    local fields = {}
    for f in (s .. "|"):gmatch("([^|]*)|") do fields[#fields + 1] = f end
    if #fields ~= 15 then return nil end
    local g = { drawCount = tonumber(fields[1]) or 1, moves = tonumber(fields[2]) or 0,
                stock = stringToList(fields[3]), waste = stringToList(fields[4]),
                foundations = {}, tableau = {}, history = {} }
    for i = 1, 4 do g.foundations[i] = stringToList(fields[4 + i]) end
    local seen, total = {}, 0
    for i = 1, 7 do
        local down, cards = fields[8 + i]:match("^(%d+):(.*)$")
        if not down then return nil end
        g.tableau[i] = { down = tonumber(down), cards = stringToList(cards) }
    end
    -- Every card exactly once, or the save is not trusted.
    local function count(list)
        for _, c in ipairs(list) do
            if c < 1 or c > 52 or seen[c] then return false end
            seen[c] = true
            total = total + 1
        end
        return true
    end
    if not (count(g.stock) and count(g.waste)) then return nil end
    for i = 1, 4 do if not count(g.foundations[i]) then return nil end end
    for i = 1, 7 do if not count(g.tableau[i].cards) then return nil end end
    if total ~= 52 then return nil end
    return g
end

return K
