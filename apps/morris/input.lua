-- Input on the Nine Men's Morris board screen. Usage:
--   local screen = smudge.dofile("input.lua")(C, S)
-- with C the rules (logic.lua) and S the app's shared state (main.lua). The
-- drawing is draw.lua, which leaves the board's geometry in S.B. A chosen move
-- goes to S.queue: main.lua plays it once this module is out of memory.

return function(C, S)
local V = {}

local function humanTurn()
    local g = S.game
    return not g.result and not S.thinking and (not S.vsDevice() or g.side == S.human)
end

-- The first legal move from `from` to `to` and whether it takes a piece.
local function moveTo(from, to)
    for _, mv in ipairs(S.legal) do
        if mv % 32 == from and (mv // 32) % 32 == to then return mv, mv >= 1024 end
    end
    return nil
end

-- Waits for the piece a mill takes, with the cursor on the first one.
local function askRemoval(from, to)
    S.pending = to
    for _, mv in ipairs(S.legal) do
        if mv % 32 == from and (mv // 32) % 32 == to then
            S.cur = mv // 1024
            break
        end
    end
end

-- Confirm or a tap on point p.
local function activate(p)
    if not humanTurn() then return end
    local g = S.game
    S.note, S.deviceMoved = nil, false
    if S.pending then
        local from = S.picked or 0
        if p == S.pending then
            S.pending = nil -- the piece moved back: choose again
            smudge.request_update()
            return
        end
        for _, mv in ipairs(S.legal) do
            if mv % 32 == from and (mv // 32) % 32 == S.pending and mv // 1024 == p then
                S.queue(mv)
                return
            end
        end
        return
    end
    if C.phase(g, g.side) == 1 then
        local mv, mill = moveTo(0, p)
        if mv then
            if mill then askRemoval(0, p) else
                S.queue(mv)
                return
            end
        end
        smudge.request_update()
        return
    end
    if S.picked then
        local mv, mill = moveTo(S.picked, p)
        if mv then
            if mill then
                askRemoval(S.picked, p)
                smudge.request_update()
            else
                S.queue(mv)
            end
            return
        end
    end
    -- Pick (or switch to) one of your pieces that can move.
    if g.b[p] == g.side then
        for _, mv in ipairs(S.legal) do
            if mv % 32 == p then
                S.picked = p
                smudge.request_update()
                return
            end
        end
    end
    if S.picked then S.picked = nil end
    smudge.request_update()
end

-- The cursor's next point in direction dx, dy: the nearest one that way,
-- keeping to the line; past the edge it wraps to the far side.
local function step(p, dx, dy)
    local best, cost = p, nil
    local x0, y0 = C.xy(p)
    for q = 1, 24 do
        if q ~= p then
            local ax, ay = C.xy(q)
            ax, ay = ax - x0, ay - y0
            local along = ax * dx + ay * dy
            local perp = dx ~= 0 and math.abs(ay) or math.abs(ax)
            if along > 0 then
                local k = along + perp * 3
                if not cost or k < cost then best, cost = q, k end
            end
        end
    end
    if cost then return best end
    for q = 1, 24 do
        local ax, ay = C.xy(q)
        ax, ay = ax - x0, ay - y0
        local along = ax * dx + ay * dy
        local perp = dx ~= 0 and math.abs(ay) or math.abs(ax)
        local k = along + perp * 20
        if not cost or k < cost then best, cost = q, k end
    end
    return best
end

function V.button(btn)
    if btn == "back" then
        if S.pending then
            S.pending = nil
            smudge.request_update()
        elseif S.picked then
            S.picked = nil
            smudge.request_update()
        else
            S.show("menu")
        end
    elseif S.game.result then
        if btn == "confirm" then
            S.newGame(S.mode)
            smudge.request_update()
        end
    elseif btn == "confirm" then
        activate(S.cur)
    else
        local dx, dy = 0, 0
        if btn == "left" then dx = -1 elseif btn == "right" then dx = 1
        elseif btn == "up" or btn == "page_back" then dy = -1 elseif btn == "down" or btn == "page_forward" then dy = 1 end
        if dx ~= 0 or dy ~= 0 then
            S.cur = step(S.cur, dx, dy)
            smudge.request_update()
        end
    end
end

function V.tap(x, y)
    if smudge.in_rect(x, y, S.w - 96, S.m.top_padding + S.m.header_height + 6, 80, 34) then
        S.show("menu")
        return
    end
    if S.game.result then
        S.newGame(S.mode)
        smudge.request_update()
        return
    end
    local B = S.B
    if not B then return end
    local best, bd = nil, nil
    for p = 1, 24 do
        local qx, qy = C.xy(p)
        qx, qy = B.x + qx * B.c, B.y + qy * B.c
        local d = (qx - x) * (qx - x) + (qy - y) * (qy - y)
        if not bd or d < bd then best, bd = p, d end
    end
    local r = B.c * 55 // 100
    if bd and bd <= r * r then
        S.cur = best
        activate(best)
    elseif S.pending or S.picked then
        -- A tap off the points cancels the piece in hand.
        S.pending, S.picked = nil, nil
        smudge.request_update()
    end
end

V.step = step
return V
end
