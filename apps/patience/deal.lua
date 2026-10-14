-- Patience: dealing a new game, loaded only for that. Usage:
--   local g = smudge.dofile("deal.lua")(P).freeCell(deal)   (or .spider(suits, rand))
-- with P the rules (logic.lua).

return function(P)
local char = string.char
local D = {}

-- FreeCell deal number `deal` (1..32000): Microsoft's numbering, so deal 1
-- here is deal 1 there. Its random generator fits in 31 bits; the multiply may
-- wrap on 32-bit Lua, which the mask makes harmless.
function D.freeCell(deal)
    local deck, seed = {}, deal
    for i = 1, 52 do deck[i] = 52 - i end -- 51 .. 0; value v = rank*4 + suit (CDHS)
    for i = 1, 51 do
        seed = (seed * 214013 + 2531011) & 0x7FFFFFFF
        local j = 52 - (seed >> 16) % (53 - i)
        deck[i], deck[j] = deck[j], deck[i]
    end
    local cols = { "", "", "", "", "", "", "", "" }
    for i = 1, 52 do
        local v = deck[i]
        local k = (i - 1) % 8 + 1
        cols[k] = cols[k] .. char((v % 4) * 13 + v // 4 + 1 + 64)
    end
    local g = { kind = P.FREECELL, deal = deal, cols = cols, cells = { 0, 0, 0, 0 }, found = { 0, 0, 0, 0 },
                log = "", moves = 0 }
    -- Aces and twos already on top go straight up; not an undoable move.
    P.autoFoundation(g)
    g.log, g.moves = "", 0
    return g
end

-- Spider with 1, 2 or 4 suits: 104 cards, 54 dealt in ten columns (the first
-- four get six), only the top card of each face up; 50 left in the stock.
function D.spider(suits, rand)
    local SUITS = suits == 1 and { 3 } or suits == 2 and { 3, 2 } or { 3, 2, 0, 1 }
    local deck, n = {}, 0
    for k = 1, 8 do
        local s = SUITS[(k - 1) % #SUITS + 1]
        for r = 1, 13 do n = n + 1 deck[n] = s * 13 + r end
    end
    for i = n, 2, -1 do
        local j = rand(i)
        deck[i], deck[j] = deck[j], deck[i]
    end
    local cols, down = {}, {}
    local k = 0
    for c = 1, 10 do
        local len = c <= 4 and 6 or 5
        local t = {}
        for i = 1, len do k = k + 1 t[i] = char(deck[k] + 64) end
        cols[c], down[c] = table.concat(t), len - 1
    end
    local t = {}
    for i = k + 1, n do t[#t + 1] = char(deck[i] + 64) end
    return { kind = P.SPIDER, suits = suits, cols = cols, down = down, stock = table.concat(t), done = "",
             log = "", moves = 0 }
end

return D
end
