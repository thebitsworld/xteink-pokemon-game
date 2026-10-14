-- Input on the Co Ganh board screen. Usage:
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

local function flipped() return S.vsDevice() and S.human == C.BLACK end

-- Confirm or a tap on point p.
local function activate(p)
    if not humanTurn() then return end
    local g = S.game
    S.note, S.deviceMoved = nil, false
    if S.picked then
        if p == S.picked then
            S.picked = nil
            smudge.request_update()
            return
        end
        local mv = S.picked + p * 32
        for _, m in ipairs(S.legal) do
            if m == mv then
                S.queue(mv)
                return
            end
        end
    end
    -- Pick (or switch to) one of your pieces that can move.
    if g.b[p] == g.side then
        for _, m in ipairs(S.legal) do
            if m % 32 == p then
                S.picked = p
                smudge.request_update()
                return
            end
        end
        S.note = "That piece cannot move"
    end
    S.picked = nil
    smudge.request_update()
end

-- The cursor's next point in direction dx, dy as seen on the screen (`flip`:
-- the board is drawn turned round). Left and right run on into the next or
-- previous row; up and down wrap round the column.
local function step(p, dx, dy, flip)
    if flip then dx, dy = -dx, -dy end
    local r, c = C.rc(p)
    if dx ~= 0 then
        local i = (r * 5 + c + dx) % 25
        return i + 1
    end
    r = (r + dy) % 5
    return r * 5 + c + 1
end

function V.button(btn)
    if btn == "back" then
        if S.picked then
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
            S.cur = step(S.cur, dx, dy, flipped())
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
    for p = 1, 25 do
        local r, c = C.rc(p)
        if B.flip then r, c = 4 - r, 4 - c end
        local qx, qy = B.x + c * B.c, B.y + r * B.c
        local d = (qx - x) * (qx - x) + (qy - y) * (qy - y)
        if not bd or d < bd then best, bd = p, d end
    end
    local r = B.c * 48 // 100
    if bd and bd <= r * r then
        S.cur = best
        activate(best)
    elseif S.picked then
        -- A tap off the points puts the piece down.
        S.picked = nil
        smudge.request_update()
    end
end

V.step = step
return V
end
