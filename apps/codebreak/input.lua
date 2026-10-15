-- Input on the Code Breaker board screen. Usage:
--   local screen = smudge.dofile("input.lua")(L, S)
-- with L the rules (logic.lua) and S the app's state (main.lua). The drawing
-- is draw.lua, which leaves the board's geometry in S.B.

return function(L, S)
local V = {}
local B = S.B

local function g() return S.game end

local function gameOver()
    local g = S.game
    local s = S.stat(S.statKey(g.mode, g.n, g.k, g.rep))
    s[1] = s[1] + 1
    if g.result == "won" then
        local c = L.count(g)
        s[2], s[3] = s[2] + 1, s[3] + c
        if s[4] == 0 or c < s[4] then s[4] = c end
    end
    smudge.save("st_" .. S.statKey(g.mode, g.n, g.k, g.rep), table.concat(s, ","))
end

-- After a guess or a score: the record when the game is over, the saved
-- game, and the device's turn.
local function moved()
    if S.game.result then gameOver() end
    S.saveGame()
    S.showBoard()
end

-- The row as a code, or nil (with a note) when a position is empty.
local function rowCode()
    local g, t = S.game, {}
    for i = 1, g.n do
        if S.row[i] == 0 then
            S.cur = i
            S.note = "Fill every position first"
            return nil
        end
        t[i] = string.char(48 + S.row[i])
    end
    return table.concat(t)
end

-- Submit: your guess, or the secret code the device will break.
local function submitRow()
    local g = S.game
    local code = rowCode()
    if not code then
        smudge.request_update()
        return
    end
    if g.mode == "you" then
        L.play(g, code)
        S.note = nil
        S.clearRow()
    else
        if not L.valid(g, code) then
            S.note = "No repeated symbols in this game"
            smudge.request_update()
            return
        end
        g.secret, S.note = code, nil
        S.clearRow()
    end
    moved()
end

-- Your score of the device's guess (or the app's, with auto scoring).
local function submitScore(b, w)
    local g = S.game
    if not L.possible(g.n, b, w) then
        S.note = b + w > g.n and "More pegs than positions" or
                     ((g.n - 1) .. " black and 1 white cannot happen")
        smudge.request_update()
        return
    end
    L.add(g, g.pending, b, w)
    g.pending, S.note = nil, nil
    S.sb, S.sw, S.field = 0, 0, 1
    moved()
end


local function setSymbol(s)
    local n = g().n
    if S.cur > n then
        S.cur = 1
        for i = n, 1, -1 do
            if S.row[i] == 0 then S.cur = i end
        end
    end
    S.row[S.cur] = s
    -- On to the next empty position, or to Submit.
    local nxt = n + 1
    for i = n, 1, -1 do
        if S.row[i] == 0 then nxt = i end
    end
    S.cur = nxt
end

local function scoreNow()
    local game = g()
    if game.auto then
        submitScore(L.score(game.pending, game.secret))
    else
        submitScore(S.sb, S.sw)
    end
end

function V.button(btn)
    local game, ph = g(), S.phase()
    local n = game.n
    if btn == "back" then
        S.openMenu()
        return
    end
    local up = btn == "up" or btn == "page_back"
    local down = btn == "down" or btn == "page_forward"
    if ph == "over" then
        if btn == "confirm" then S.newGame(game.mode, game.preset) end
    elseif ph == "edit" or ph == "secret" then
        if btn == "confirm" then
            submitRow()
            return
        elseif btn == "left" then
            S.cur = (S.cur - 2) % (n + 1) + 1
        elseif btn == "right" then
            S.cur = S.cur % (n + 1) + 1
        elseif (up or down) and S.cur <= n then
            local s = S.row[S.cur]
            if up then s = s % game.k + 1 else s = (s - 2) % game.k + 1 end
            S.row[S.cur] = s
            S.note = nil
        end
        smudge.request_update()
    elseif ph == "score" then
        if btn == "confirm" then
            scoreNow()
            return
        elseif btn == "left" then
            S.field = (S.field - 2) % 3 + 1
        elseif btn == "right" then
            S.field = S.field % 3 + 1
        elseif (up or down) and S.field < 3 then
            local d = up and 1 or -1
            if S.field == 1 then S.sb = (S.sb + d) % (n + 1) else S.sw = (S.sw + d) % (n + 1) end
        end
        smudge.request_update()
    elseif ph == "auto" then
        if btn == "confirm" then scoreNow() end
    end
end

local function inBtn(x, y) return smudge.in_rect(x, y, B.btnX, B.btnY, B.btnW, B.btnH) end

function V.tap(x, y)
    local game, ph = g(), S.phase()
    if smudge.in_rect(x, y, S.w - 92, B.statusY, 80, 36) then
        S.openMenu()
        return
    end
    if ph == "over" then
        if inBtn(x, y) then S.newGame(game.mode, game.preset) end
        return
    end
    if ph == "edit" or ph == "secret" then
        if inBtn(x, y) then
            submitRow()
            return
        end
        local rowY = ph == "secret" and B.codeY or (B.rowsY + L.count(game) * B.rowH)
        if y >= rowY and y < rowY + B.rowH and x >= B.cellX and x < B.cellX + game.n * B.cw then
            S.cur = (x - B.cellX) // B.cw + 1
            smudge.request_update()
            return
        end
        if y >= B.palY and y < B.palY + B.palH and x >= B.palX and x < B.palX + game.k * B.pw then
            setSymbol((x - B.palX) // B.pw + 1)
            S.note = nil
            smudge.request_update()
        end
    elseif ph == "score" then
        if inBtn(x, y) then
            scoreNow()
            return
        end
        if x >= B.sx and x < B.sx + (game.n + 1) * B.sw then
            local v = (x - B.sx) // B.sw
            if y >= B.sy1 and y < B.sy1 + B.sh then
                S.sb = v
            elseif y >= B.sy2 and y < B.sy2 + B.sh then
                S.sw = v
            end
            smudge.request_update()
        end
    elseif ph == "auto" then
        if inBtn(x, y) then scoreNow() end
    end
end

return V
end
