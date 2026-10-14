-- Patience table, the input half (draw.lua draws it). Usage:
--   local screen = smudge.dofile("input.lua")(P, S)   -- screen.button(btn), screen.tap(x, y)
-- with P the rules (logic.lua) and S the app's state (main.lua).

return function(P, S)
local M = {}
local w, touch = S.w, S.touch
local L = smudge.dofile("layout.lua")(P, S)
local layout, colX, cardY = L.update, L.colX, L.cardY
local stockRect, touchButtons = L.stockRect, L.touchButtons
local FOUNDATIONS = L.FOUNDATIONS

-- Moves ------------------------------------------------------------------------------

local function moved(ok, why)
    S.note = (not ok) and why or nil
    S.picked = nil
    S.count = nil
    local g = S.game
    if ok and P.won(g) then
        local i = S.kindIndex(g)
        local st = S.stats[i]
        st[2], st[3] = st[2] + 1, st[3] + 1
        if st[3] > st[4] then st[4] = st[3] end
        S.saveStats(i)
        S.save()
    end
    smudge.request_update()
end

-- Picks `n` cards at `loc`, if anything there can move.
local function pick(loc, n)
    local g = S.game
    S.note = nil
    if g.kind == P.SPIDER and loc == P.STOCK then
        moved(smudge.dofile("tools.lua")(P).deal(g))
        return
    end
    if loc == FOUNDATIONS then
        S.note = "Cards on the foundations stay there"
    elseif loc <= L.n then
        local run = P.runLength(g, loc)
        if run == 0 then
            S.note = "Nothing to pick up"
        else
            S.picked, S.pickN = loc, math.max(1, math.min(n, run))
        end
    elseif g.cells[loc - P.CELL + 1] ~= 0 then
        S.picked, S.pickN = loc, 1
    else
        S.note = "That cell is empty"
    end
    smudge.request_update()
end

-- Puts the picked cards on `loc`; on their own place, sends them where they fit.
local function drop(loc)
    local g = S.game
    local src, n = S.picked, S.pickN
    if loc == src then
        local k, dst = smudge.dofile("tools.lua")(P).bestTarget(g, src, n)
        if k then moved(P.move(g, src, dst, k)) else moved(false, "No place for that") end
        return
    end
    if g.kind == P.SPIDER and loc == P.STOCK then
        moved(false, "Cards can't go on the stock")
        return
    end
    local k, dst = smudge.dofile("tools.lua")(P).fit(g, src, loc == FOUNDATIONS and P.FOUND or loc, n)
    if not k then
        moved(false, dst)
        return
    end
    moved(P.move(g, src, dst, k))
end

local function places() return S.game.kind == P.FREECELL and 13 or 11 end

function M.button(btn)
    local g = S.game
    if P.won(g) then
        if btn == "confirm" or btn == "back" then S.show("menu") end
        return
    end
    if btn == "back" then
        if S.picked then
            S.picked, S.note = nil, nil
            smudge.request_update()
        else
            S.show("menu")
        end
        return
    end
    layout()
    if btn == "confirm" then
        if S.picked then drop(S.cur) else pick(S.cur, L.count()) end
        return
    end
    if btn == "left" or btn == "right" then
        S.cur = (S.cur - 1 + (btn == "left" and -1 or 1)) % places() + 1
        if not S.picked then S.count = nil end
    elseif btn == "up" or btn == "page_back" or btn == "down" or btn == "page_forward" then
        local more = btn == "up" or btn == "page_back"
        if S.picked then
            if S.picked <= L.n then
                S.pickN = math.max(1, math.min(P.runLength(g, S.picked), S.pickN + (more and 1 or -1)))
            end
        elseif S.cur <= L.n then
            S.count = math.max(1, math.min(P.runLength(g, S.cur), L.count() + (more and 1 or -1)))
        end
    end
    smudge.request_update()
end

-- What a tap hits: a place and, in a column, how many cards from the top.
local function hit(x, y)
    local g = S.game
    if y >= L.topY and y < L.topY + L.topH then
        if g.kind == P.SPIDER then
            local sx, sy, sw, sh = stockRect()
            if smudge.in_rect(x, y, sx, sy, sw, sh) then return P.STOCK end
            return nil
        end
        for c = 1, 8 do
            if x >= colX(c) and x < colX(c) + L.cw + L.gap then return c <= 4 and P.CELL + c - 1 or FOUNDATIONS end
        end
        return nil
    end
    if y < L.tabY then return nil end
    for c = 1, L.n do
        if x >= colX(c) - L.gap // 2 and x < colX(c) + L.cw + L.gap - L.gap // 2 then
            local n = #g.cols[c]
            local depth = n
            for i = 1, n do
                if y >= cardY(c, i) then depth = i end
            end
            return c, n - depth + 1
        end
    end
    return nil
end

function M.tap(x, y)
    local g = S.game
    layout()
    local tb = touchButtons()
    local u, mb = tb.undo, tb.menu
    if smudge.in_rect(x, y, mb[1], mb[2], mb[3], mb[4]) then
        S.show("menu")
        return
    end
    if P.won(g) then
        S.show("menu")
        return
    end
    if smudge.in_rect(x, y, u[1], u[2], u[3], u[4]) then
        S.undo()
        return
    end
    local loc, k = hit(x, y)
    if not loc then
        if S.picked then
            S.picked, S.note = nil, nil
            smudge.request_update()
        end
        return
    end
    S.cur = loc
    if not S.picked then
        -- A card under a break in the run: the run on top is picked instead.
        local run = loc <= L.n and P.runLength(g, loc) or 1
        pick(loc, k or 1)
        if S.picked and k and k > run then
            S.note = "Only the top " .. run .. (run == 1 and " card moves" or " cards move")
        end
        return
    end
    -- A tap on the picked cards sends them; on other cards of the same column picks again.
    if loc == S.picked and loc <= L.n and k ~= S.pickN and k <= P.runLength(g, loc) then
        S.pickN = k
        smudge.request_update()
        return
    end
    drop(loc)
end

return M
end
