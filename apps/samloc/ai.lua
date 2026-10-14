-- Sam Loc computer players. Usage:
--   local AI = smudge.dofile("ai.lua")(T)
--   local cards = AI.choose(g, seat, level, rand)   -- nil means pass
--   local yes = AI.declare(g, seat, level)         -- declare Sam?
-- with T the rules (logic.lua). Loaded for one move and dropped after.
--
-- Rule based: the hand is split into groups it means to play together (four
-- of a kind kept as a bomb, straights, triples, pairs and singles; the split
-- with fewer groups wins), and the 2s and bombs are held back for control.
-- Leading dumps the lowest group; answering plays the cheapest group that
-- beats the table and spends a 2 only on a high card or when someone is
-- close to going out. A Sam declarer is always blocked when possible. Level
-- 1 (Normal) chops and breaks up its groups only now and then. Level 2
-- (Hard) always chops a 2, breaks a group when it must, keeps its aces early
-- on, plays the highest single at anyone on one card and takes the lead with
-- its control cards before going out. Sam is declared only with a hand of
-- groups nobody can beat (Hard takes a little more risk).

return function(T)
local A = {}

local SINGLE, PAIR, TRIPLE, QUAD, STRAIGHT = T.SINGLE, T.PAIR, T.TRIPLE, T.QUAD, T.STRAIGHT
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
    return br
end

-- The longest run (start, length) of ranks 1..12 with at least one card in
-- `cnt` (exactly one when `single`), or nil when shorter than 3.
local function longestRun(cnt, single)
    local bestS, bestL, s = nil, 0, nil
    for r = 1, 13 do
        local n = cnt[r]
        if r <= 12 and (n == 1 or n > 1 and not single) then
            s = s or r
            if r - s + 1 > bestL then bestS, bestL = s, r - s + 1 end
        else
            s = nil
        end
    end
    if bestL >= 3 then return bestS, bestL end
    return nil
end

local function isControl(k, t) return k == QUAD or t == TWO end

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
    for r = 1, 13 do
        if cnt[r] == 4 then add(sp, take(br, used, cnt, r, 1, 4), QUAD) end
    end
    while true do
        local s, l = longestRun(cnt, not straightsFirst)
        if not s then break end
        add(sp, take(br, used, cnt, s, l, 1), STRAIGHT)
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
            local k, t = sp.kind[i], rank(cards[#cards])
            if not isControl(k, t) then
                score = score + 10
                if k == SINGLE then score = score + (13 - t) // 4 end
            end
        end
        if not bestScore or score < bestScore then best, bestScore = sp, score end
        sp = nil
    end
    return best
end

-- Every play from `hand` that beats the table (kind k, size l, top rank t):
-- the lowest cards of each rank or run, and four of a kind on a 2.
function A.beatingPlays(hand, k, l, t)
    local br = byRank(hand)
    local out = {}
    if k <= QUAD then
        for r = t + 1, 13 do
            local L = br[r]
            if #L >= k then
                local cards = {}
                for i = 1, k do cards[i] = L[i] end
                out[#out + 1] = cards
            end
        end
        if k == SINGLE and t == TWO then
            for r = 1, 12 do
                if #br[r] == 4 then out[#out + 1] = br[r] end
            end
        end
    else
        for s = math.max(1, t - l + 2), 12 - l + 1 do
            local cards = {}
            for r = s, s + l - 1 do
                if #br[r] == 0 then cards = nil break end
                cards[#cards + 1] = br[r][1]
            end
            if cards then out[#out + 1] = cards end
        end
    end
    return out
end

-- The fewest cards any other player holds.
local function danger(g, seat)
    local d = 99
    for s = 1, 4 do
        if s ~= seat and #g.hands[s] < d then d = #g.hands[s] end
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
    local plain, control = 0, 0
    for i, cards in ipairs(sp) do
        if isControl(sp.kind[i], rank(cards[#cards])) then control = control + 1 else plain = plain + 1 end
    end
    local useControl = plain == 0
    -- Hard: one plain group left behind control cards - take the round with
    -- the control first, then go out with the last group.
    if level >= 2 and plain == 1 and control > 0 then useControl = true end
    -- A Sam declarer plays its strongest groups first.
    if g.sam == seat then useControl = control > 0 end
    local d = level >= 2 and danger(g, seat) or 99
    local best, bestScore = nil, nil
    for i, cards in ipairs(sp) do
        local k, t = sp.kind[i], rank(cards[#cards])
        if isControl(k, t) == useControl then
            local score = rank(cards[1]) * 10 - #cards
            -- Someone about to go out: do not lead what they can follow.
            if d == 1 and k == SINGLE then score = 200 - t * 12 end
            if d == 2 and k == PAIR then score = score + 100 end
            if useControl or g.sam == seat then score = -t * 10 - #cards end
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
        if n > 0 and n < #cards then cost = cost + (isControl(sp.kind[i], rank(cards[#cards])) and 5 or 1) end
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
    -- Going out with this play.
    for _, p in ipairs(plays) do
        if #p == #hand then return p end
    end
    local blockSam = g.sam and g.sam ~= seat
    -- Hard (or a Sam to stop): someone holds one card and a single is on the
    -- table - stop it with the highest single.
    if (hard and d == 1 or blockSam) and g.ck == SINGLE then
        local hi = plays[1]
        for _, p in ipairs(plays) do
            if #p == 1 and p[1] > hi[1] then hi = p end
        end
        if #hi == 1 or blockSam then return hi end
    end
    local best, bestScore = nil, nil
    for _, p in ipairs(plays) do
        local t = rank(p[#p])
        local cost = breakCost(p, sp)
        local score = cost * 40 + t * 4
        local ok = true
        if #p ~= g.cl then
            -- Four of a kind on a 2.
            ok = hard or rand(2) == 1
            score = score + 30
        elseif t == TWO then
            local high = g.ct >= (hard and 11 or 10)
            ok = high or d <= 3 or #hand <= 4 or (not hard and rand(3) == 1)
            if cost > 0 then ok = ok and (d <= 2 or #hand <= 3) end
            score = score + 60
        elseif cost > 0 then
            if hard then
                ok = cost <= 2 or d <= 2
            else
                ok = cost == 1 and rand(3) == 1
            end
        elseif hard and t == T.ACE and g.ct <= 6 and #hand >= 7 and d > 4 then
            ok = false -- keep the aces for later
        end
        if (ok or blockSam) and (not bestScore or score < bestScore) then best, bestScore = p, score end
    end
    return best
end

function A.choose(g, seat, level, rand)
    local hand = g.hands[seat]
    local sp = A.groups(hand)
    local c
    if g.cur then c = answer(g, seat, level, rand, sp) else c = lead(g, seat, level, sp) end
    -- Báo 1: a single must be the highest card.
    if c and #c == 1 and #hand > 1 and T.mustTop(g, seat) then c = { hand[#hand] } end
    return c
end

-- How safe a group (kind k, size n, top rank t) is to lead in a Sam: 2
-- when nothing (or only a rare four of a kind) beats it, 1 when little can.
local function safety(k, n, t)
    if t == TWO or k == QUAD or k == STRAIGHT and t == T.ACE then return 2 end
    if k == TRIPLE then return t >= 11 and 2 or (t >= 8 and 1 or 0) end
    if k == STRAIGHT then return (t >= 10 and n >= 4 or t >= 8 and n >= 6) and 1 or 0 end
    if k == PAIR then return t == T.ACE and 1 or 0 end
    return k == SINGLE and t == T.ACE and 1 or 0
end

-- Sam only with every group safe; Hard also risks one fairly safe group.
function A.declare(g, seat, level)
    local sp = A.groups(g.hands[seat])
    local risky = 0
    for i, cards in ipairs(sp) do
        local s = safety(sp.kind[i], #cards, rank(cards[#cards]))
        if s == 0 then return false end
        if s == 1 then risky = risky + 1 end
    end
    return risky <= level
end

return A
end
