-- The Whodunit accusation page. Loaded by view.lua:
-- smudge.dofile("accuse.lua")(W, S, U).

return function(W, S, U)
local P = { CONFIRM = "Accuse", PREV = "<", NEXT = ">" }

local function rows() return S.case.categories + 1 end
local function rect(r) return 24, U.bodyTop() + 30 + (r - 1) * 64, S.w - 48, 54 end

function P.draw()
    local case = S.case
    smudge.centered_text(U.bodyTop(), "Who did it, with what, and where?", "ui10", "regular", true)
    for r = 1, rows() do
        local x, y, bw, bh = rect(r)
        local focused = S.focus == "content" and S.arow == r and not S.touch
        if r <= case.categories then
            smudge.rounded_rect(x, y, bw, bh, 8, false, focused and 4 or 2)
            smudge.text(x + 12, y + 4, W.CATEGORY_NAMES[r]:sub(1, -2), "ui10", "regular", "left", true)
            smudge.text(x + bw // 2, y + 24, case.names[r][S.accused[r]], "ui12", "bold", "center", true)
            smudge.text(x + 14, y + 22, "<", "ui12", "bold", "left", true)
            smudge.text(x + bw - 14, y + 22, ">", "ui12", "bold", "right", true)
        else
            smudge.rounded_rect(x, y + 10, bw, bh, 8, true, true)
            smudge.text(x + bw // 2, y + 25, "Accuse", "ui12", "bold", "center", false)
            if focused then smudge.rect(x - 4, y + 6, bw + 8, bh + 8, false, true, 3) end
        end
    end
end

function P.button(btn)
    local n = rows()
    if btn == "up" or btn == "page_back" then
        if S.arow == 1 then S.focus = "tabs" else S.arow = S.arow - 1 end
    elseif btn == "down" or btn == "page_forward" then
        S.arow = math.min(n, S.arow + 1)
    elseif (btn == "left" or btn == "right") and S.arow < n then
        local step = (btn == "left") and -1 or 1
        S.accused[S.arow] = (S.accused[S.arow] - 1 + step) % S.case.items + 1
    elseif btn == "confirm" then
        if S.arow == n then S.accuse() return end
        S.accused[S.arow] = S.accused[S.arow] % S.case.items + 1
    end
    smudge.request_update()
end

function P.tap(x, y)
    for r = 1, rows() do
        local rx, ry, rw, rh = rect(r)
        if smudge.in_rect(x, y, rx, ry, rw, rh + 10) then
            if r == rows() then
                S.accuse()
                return
            end
            local step = (x < rx + rw // 2) and -1 or 1
            S.accused[r] = (S.accused[r] - 1 + step) % S.case.items + 1
            smudge.request_update()
            return
        end
    end
end

return P
end
