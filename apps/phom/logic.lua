-- Phom (Phỏm, tá lả) rules: the cards, the melds and the best way to split
-- a hand into melds. Dealing and moves are in game.lua, the computer
-- players in ai.lua. No drawing, so it can be tested on a computer
-- (test/lua_apps/phom_test.lua).
--
-- A card is 0..51: rank = card // 4 + 1 (1 = the ace .. 13 = the king),
-- suit = card % 4 (0 spades, 1 clubs, 2 diamonds, 3 hearts); a card's
-- points are its rank (A 1, J 11, Q 12, K 13). A meld (phỏm) is three or
-- four cards of one rank, or three or more cards of one suit in a row (the
-- ace is low only: no Q K A, no K A 2). Cards a player has eaten must each
-- end up in a meld, and never two of them in the same meld.

local T = {}

local function has(list, c)
    for _, x in ipairs(list) do
        if x == c then return true end
    end
    return false
end
T.has = has

-- The split search. Melds are bit masks over the hand's positions (a hand
-- is at most 10 cards, so they fit any integer). State in locals of the
-- file, not closures, to keep the search cheap.
local hand_, n_, emask_ = nil, 0, 0
local cm, cmin, csz, nc = {}, {}, {}, 0
local stack, best = {}, {}
local bestN, bestL, bestC, byCount_ = 0, nil, 0, false
local pos = {}

local function addCand(m, lo, sz)
    local e = m & emask_
    if e & (e - 1) ~= 0 then return end -- two eaten cards in one meld
    nc = nc + 1
    cm[nc], cmin[nc], csz[nc] = m, lo, sz
end

local function dfs(i, used, left, cnt, d)
    if bestL and not byCount_ and left > bestL then return end
    while i <= n_ and used & (1 << (i - 1)) ~= 0 do i = i + 1 end
    if i > n_ then
        local better
        if not bestL then
            better = true
        elseif byCount_ then
            better = cnt > bestC or (cnt == bestC and left < bestL)
        else
            better = left < bestL or (left == bestL and cnt > bestC)
        end
        if better then
            bestL, bestC, bestN = left, cnt, d
            for k = 1, d do best[k] = stack[k] end
        end
        return
    end
    for k = 1, nc do
        local m = cm[k]
        if cmin[k] == i and m & used == 0 then
            stack[d + 1] = m
            dfs(i + 1, used | m, left, cnt + csz[k], d + 1)
        end
    end
    if emask_ & (1 << (i - 1)) == 0 then dfs(i + 1, used, left + hand_[i] // 4 + 1, cnt, d) end
end

-- The best split of `hand` into melds, with the cards in `eaten` each in a
-- different meld. Fewest points left over (then most cards in melds); with
-- `byCount` most cards in melds (then fewest points). Returns nil when no
-- split keeps the eaten cards in melds, else: points left over, the melds
-- (lists of cards), the cards left over, the number of cards in melds.
function T.split(hand, eaten, byCount)
    hand_, n_, emask_, nc = hand, #hand, 0, 0
    for k in pairs(pos) do pos[k] = nil end
    for i, c in ipairs(hand) do
        pos[c] = i
        if eaten and has(eaten, c) then emask_ = emask_ | (1 << (i - 1)) end
    end
    -- Three or four of a rank.
    for r = 0, 12 do
        local idx, k = {}, 0
        for s = 0, 3 do
            local i = pos[r * 4 + s]
            if i then k = k + 1 idx[k] = i end
        end
        if k >= 3 then
            local all, lo = 0, 99
            for j = 1, k do
                all = all | (1 << (idx[j] - 1))
                if idx[j] < lo then lo = idx[j] end
            end
            if k == 4 then
                addCand(all, lo, 4)
                for j = 1, 4 do
                    local m, l2 = all & ~(1 << (idx[j] - 1)), 99
                    for q = 1, 4 do
                        if q ~= j and idx[q] < l2 then l2 = idx[q] end
                    end
                    addCand(m, l2, 3)
                end
            else
                addCand(all, lo, 3)
            end
        end
    end
    -- Runs in a suit, from each card up.
    for i = 1, n_ do
        local x, m, lo, len = hand[i], 1 << (i - 1), i, 1
        while pos[x + 4] do
            x = x + 4
            local j = pos[x]
            m, len = m | (1 << (j - 1)), len + 1
            if j < lo then lo = j end
            if len >= 3 then addCand(m, lo, len) end
        end
    end
    bestL, bestC, bestN, byCount_ = nil, 0, 0, byCount and true or false
    dfs(1, 0, 0, 0, 0)
    hand_ = nil
    if not bestL then return nil end
    local melds, used = {}, 0
    for k = 1, bestN do
        local meld, m = {}, best[k]
        used = used | m
        for i = 1, n_ do
            if m & (1 << (i - 1)) ~= 0 then meld[#meld + 1] = hand[i] end
        end
        table.sort(meld)
        melds[k] = meld
    end
    local rest = {}
    for i = 1, n_ do
        if used & (1 << (i - 1)) == 0 then rest[#rest + 1] = hand[i] end
    end
    table.sort(rest)
    return bestL, melds, rest, bestC
end

-- `hand` with `c` added, or without it.
local function plus(hand, c)
    local h = {}
    for i, x in ipairs(hand) do h[i] = x end
    h[#h + 1] = c
    return h
end
local function minus(hand, c)
    local h = {}
    for _, x in ipairs(hand) do
        if x ~= c then h[#h + 1] = x end
    end
    return h
end
T.plus, T.minus = plus, minus

-- Whether a player holding `hand` (with `eaten` among it) may eat `c`: it
-- must make a meld with the hand, apart from the cards eaten before.
function T.canEat(hand, eaten, c)
    return T.split(plus(hand, c), plus(eaten, c)) ~= nil
end

-- Whether `c` may be thrown from `hand`: not an eaten card, and the eaten
-- cards can still be kept in melds without it.
function T.canDiscard(hand, eaten, c)
    if not has(hand, c) or has(eaten, c) then return false end
    return T.split(minus(hand, c), eaten) ~= nil
end

-- Whether a hand of 10 is ù: at least 9 of its cards in melds.
function T.isU(hand, eaten)
    if #hand < 10 then return false end
    local left, _, _, cnt = T.split(hand, eaten, true)
    return left ~= nil and cnt >= #hand - 1
end

-- Puts `hand` in the order to show it: its melds, then the other cards by
-- rank (with `byCount` the split with most cards in melds, for an ù).
-- Returns the sizes of the melds.
function T.arrange(hand, eaten, byCount)
    local left, melds, rest = T.split(hand, eaten, byCount)
    if not left then
        table.sort(hand)
        return {}
    end
    local k, sizes = 0, {}
    for i, meld in ipairs(melds) do
        sizes[i] = #meld
        for _, c in ipairs(meld) do
            k = k + 1
            hand[k] = c
        end
    end
    for _, c in ipairs(rest) do
        k = k + 1
        hand[k] = c
    end
    return sizes
end

return T
