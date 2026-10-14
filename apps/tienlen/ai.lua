-- Tien Len computer players. Usage:
--   local AI = smudge.dofile("ai.lua")(T)
--   local cards = AI.choose(g, seat, level, rand)   -- nil means pass
-- with T the rules (logic.lua). Loaded for one move and dropped after.
--
-- Rule based: the hand is split into groups it means to play together (four
-- of a kind and runs of pairs kept as bombs, straights, triples, pairs and
-- singles; the split with fewer groups wins), and the 2s and bombs are held
-- back for control. Leading dumps the lowest group; answering plays the
-- cheapest group that beats the table and spends a 2 only on an ace (Hard)
-- or a king, or when someone is close to going out. Level 1 (Normal) chops
-- and breaks up its groups only now and then and does not play around a
-- player about to go out. Level 2 (Hard) always chops a 2, breaks a group
-- when it must, keeps its aces early on, blocks a player holding one card
-- and takes the lead with its control cards before going out.

return function(T)
local A = {}

local SINGLE, PAIR, PAIRS, QUAD = T.SINGLE, T.PAIR, T.PAIRS, T.QUAD
local TWO = T.TWO

local function rank(c) return c // 4 + 1 end

-- Cards by rank: byRank[r] is a sorted list.
local function byRank(hand)
    local br = {}
    for r = 1, 13 do br[r] = {} end
    for _, c in ipairs(hand) do
        local l = br[rank(c)]
        l[#l + 1] = c
    end
    for r = 1, 13 do table.sort(br[r]) end
    return br
end

-- The longest run (start, length) of ranks 1..12 with at least `need`
-- cards in `cnt` (exactly one when `need` is 0), or nil when shorter than 3.
local function longestRun(cnt, need)
    local bestS, bestL, s = nil, 0, nil
    for r = 1, 13 do
        local n = cnt[r]
        if r <= 12 and (need == 0 and n == 1 or need > 0 and n >= need) then
            s = s or r
            if r - s + 1 > bestL then bestS, bestL = s, r - s + 1 end
        else
            s = nil
        end
    end
    if bestL >= 3 then return bestS, bestL end
    return nil
end

local function isControl(k, t) return k == QUAD or k == PAIRS or rank(t) == TWO end

-- Adds a group: its cards (ascending, so the last is the top), its kind.
local function add(sp, cards, k)
    local n = #sp + 1
    sp[n], sp.kind[n] = cards, k
end

-- Takes `per` cards of each rank s..s+l-1 (the lowest not yet used).
local function take(br, used, cnt, s, l, per)
    local cards = {}
    for r = s, s + l - 1 do
        for _ = 1, per do
            used[r], cnt[r] = used[r] + 1, cnt[r] - 1
            cards[#cards + 1] = br[r][used[r]]
        end
    end
    return cards
end

-- Splits the hand (by rank: br) into groups: sp[i] the cards of group i,
-- sp.kind[i] its kind. With `straightsFirst` straights are taken before
-- pairs and triples; otherwise only from ranks held once.
local function split(br, straightsFirst)
    local used, cnt = {}, {}
    for r = 1, 13 do
        used[r], cnt[r] = 0, #br[r]
    end
    local sp = { kind = {} }
    for r = 1, 12 do
        if cnt[r] == 4 then add(sp, take(br, used, cnt, r, 1, 4), QUAD) end
    end
    while true do
        local s, l = longestRun(cnt, 2)
        if not s then break end
        add(sp, take(br, used, cnt, s, l, 2), PAIRS)
    end
    while true do
        local s, l = longestRun(cnt, straightsFirst and 1 or 0)
        if not s then break end
        add(sp, take(br, used, cnt, s, l, 1), T.STRAIGHT)
    end
    -- What is left: triples, pairs, singles, by rank.
    for r = 1, 13 do
        if cnt[r] > 0 then add(sp, take(br, used, cnt, r, 1, cnt[r]), cnt[r]) end
    end
    return sp
end

-- The better split: fewer groups that are not control, then fewer low singles.
function A.groups(hand)
    local br = byRank(hand)
    local best, bestScore = nil, nil
    for pass = 1, 2 do
        local sp = split(br, pass == 1)
        local score = 0
        for i, cards in ipairs(sp) do
            local k, t = sp.kind[i], cards[#cards]
            if not isControl(k, t) then
                score = score + 10
                if k == SINGLE then score = score + (13 - rank(t)) // 4 end
            end
        end
        if not bestScore or score < bestScore then best, bestScore = sp, score end
        sp = nil
    end
    return best
end

-- Every play from `hand` that beats the table (kind k, size l, top t): the
-- lowest cards of each way to make it, and the chops.
function A.beatingPlays(hand, k, l, t)
    local br = byRank(hand)
    local out = {}
    local tr = rank(t)
    if k == SINGLE then
        for _, c in ipairs(hand) do
            if c > t then out[#out + 1] = { c } end
        end
    elseif k <= QUAD then
        for r = tr, 13 do
            local L = br[r]
            if #L >= k then
                local cards = {}
                for i = 1, k - 1 do cards[i] = L[i] end
                for j = k, #L do
                    if L[j] > t then
                        cards[k] = L[j]
                        out[#out + 1] = cards
                        break
                    end
                end
            end
        end
    else
        local per = k == PAIRS and 2 or 1
        local span = l
        for s = 1, 12 - span + 1 do
            local topR = s + span - 1
            if topR >= tr then
                local cards, ok = {}, true
                for r = s, topR do
                    local L = br[r]
                    if #L < per then ok = false break end
                    if r < topR then
                        for i = 1, per do cards[#cards + 1] = L[i] end
                    else
                        for i = 1, per - 1 do cards[#cards + 1] = L[i] end
                        local found = false
                        for j = per, #L do
                            if L[j] > t then
                                cards[#cards + 1] = L[j]
                                found = true
                                break
                            end
                        end
                        ok = found
                    end
                end
                if ok then out[#out + 1] = cards end
            end
        end
    end
    -- Chops: four of a kind and runs of pairs of other kinds or sizes.
    if (k == SINGLE or k == PAIR) and tr == TWO or k == PAIRS or k == QUAD then
        for r = 1, 12 do
            if #br[r] == 4 and k ~= QUAD then
                local q = br[r]
                if T.beats(QUAD, 4, q[4], k, l, t) then out[#out + 1] = { q[1], q[2], q[3], q[4] } end
            end
        end
        for s = 1, 10 do
            local cards = {}
            for r = s, 12 do
                if #br[r] < 2 then break end
                cards[#cards + 1] = br[r][1]
                cards[#cards + 1] = br[r][2]
                local n = r - s + 1
                if n >= 3 and (k ~= PAIRS or n ~= l) and T.beats(PAIRS, n, cards[#cards], k, l, t) then
                    local copy = {}
                    for i, c in ipairs(cards) do copy[i] = c end
                    out[#out + 1] = copy
                end
            end
        end
    end
    return out
end

-- The fewest cards any other player still in holds (99 if none).
local function danger(g, seat)
    local d = 99
    for s = 1, 4 do
        if s ~= seat and not g.out[s] and #g.hands[s] < d then d = #g.hands[s] end
    end
    return d
end

local function contains(cards, x)
    for _, c in ipairs(cards) do
        if c == x then return true end
    end
    return false
end

-- Leading: which group to play.
local function lead(g, seat, level, sp)
    local hand = g.hands[seat]
    if T.classify(hand) then return T.sorted(hand) end
    if g.need3 then
        for _, cards in ipairs(sp) do
            if contains(cards, T.THREE_SPADES) then return cards end
        end
        return { T.THREE_SPADES }
    end
    local plain, control = 0, 0
    for i, cards in ipairs(sp) do
        if isControl(sp.kind[i], cards[#cards]) then control = control + 1 else plain = plain + 1 end
    end
    local useControl = plain == 0
    -- Hard: one plain group left behind control cards - take the round with
    -- the control first, then go out with the last group.
    if level >= 2 and plain == 1 and control > 0 then useControl = true end
    local d = level >= 2 and danger(g, seat) or 99
    local best, bestScore = nil, nil
    for i, cards in ipairs(sp) do
        local k, t = sp.kind[i], cards[#cards]
        if isControl(k, t) == useControl then
            local score = rank(cards[1]) * 10 - #cards
            -- Someone about to go out: do not lead what they can follow;
            -- singles only: the highest one.
            if d == 1 and k == SINGLE then score = 200 - rank(t) * 12 end
            if d == 2 and k == PAIR then score = score + 100 end
            if useControl then score = t end
            if not bestScore or score < bestScore then best, bestScore = cards, score end
        end
    end
    return best
end

-- How many groups a play breaks (a group partly used), weighted.
local function breakCost(p, sp)
    local cost = 0
    for i, cards in ipairs(sp) do
        local n = 0
        for _, c in ipairs(p) do
            if contains(cards, c) then n = n + 1 end
        end
        if n > 0 and n < #cards then cost = cost + (isControl(sp.kind[i], cards[#cards]) and 5 or 1) end
    end
    return cost
end

-- Answering the table: cards to play, or nil to pass.
local function answer(g, seat, level, rand, sp)
    local hand = g.hands[seat]
    local plays = A.beatingPlays(hand, g.ck, g.cl, g.ct)
    if #plays == 0 then return nil end
    local d = danger(g, seat)
    local hard = level >= 2
    local size = #g.cur
    -- Going out with this play.
    for _, p in ipairs(plays) do
        if #p == #hand then return p end
    end
    -- Hard: someone holds one card and a single is on the table - stop it
    -- with the highest single.
    if hard and d == 1 and g.ck == SINGLE then
        local hi = plays[1]
        for _, p in ipairs(plays) do
            if #p == 1 and p[1] > hi[1] then hi = p end
        end
        return hi
    end
    local best, bestScore = nil, nil
    for _, p in ipairs(plays) do
        -- Every play here is listed lowest card first; a chop (four of a
        -- kind or a run of pairs on something else) has another size.
        local t = p[#p]
        local cost = breakCost(p, sp)
        local score = cost * 40 + rank(t) * 4 + t % 4
        local ok = true
        if #p ~= size then
            -- A bomb: worth it on 2s and other bombs.
            ok = hard or rand(2) == 1
            score = score + 30
        elseif rank(t) == TWO then
            local high = rank(g.ct) >= (hard and 12 or 11)
            ok = high or d <= 3 or #hand <= 5 or (not hard and rand(3) == 1)
            if cost > 0 then ok = ok and (d <= 2 or #hand <= 4) end
            score = score + 60
        elseif cost > 0 then
            if hard then
                ok = cost <= 2
            else
                ok = cost == 1 and rand(3) == 1
            end
        elseif hard and rank(t) >= 12 and rank(g.ct) <= 6 and #hand >= 9 and d > 5 then
            ok = false -- keep the aces for later
        end
        if ok and (not bestScore or score < bestScore) then best, bestScore = p, score end
    end
    return best
end

function A.choose(g, seat, level, rand)
    local sp = A.groups(g.hands[seat])
    if not g.cur then return lead(g, seat, level, sp) end
    return answer(g, seat, level, rand, sp)
end

return A
end
