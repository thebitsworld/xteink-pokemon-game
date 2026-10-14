-- Sudoku game for CrossSmudge (Lua)
-- Faithful reproduction of SudokuActivity.h with procedural generation and difficulty picker

local b = {} -- board (81 integers)
local s = {} -- solution (81 integers)
local init_b = {} -- givens (81 bools)
local hint_b = {} -- hints (81 bools)

for i = 1, 81 do
    b[i] = 0
    s[i] = 0
    init_b[i] = false
    hint_b[i] = false
end

local row = 1 -- 1..9, 10 = New Game, 11 = Hint
local col = 1 -- 1..9
local solved = false
local diff = 1 -- 1: Easy, 2: Med, 3: Hard
local picker = false
local pick_diff = 1
local diff_names = {"EASY", "MEDIUM", "HARD"}
local w, h = 480, 800
-- Number pad under the grid: keys 1-9 and 10 = erase. With buttons, Confirm
-- on a cell moves the cursor onto the pad (pad_sel); touch taps it directly.
local pad_sel = nil
local PAD_COLS = 5

-- Any full grid that follows the rules wins, not only the stored solution:
-- the generator does not guarantee a unique one. Givens cannot be edited.
local function check_win()
    for i = 1, 81 do
        if b[i] == 0 then return false end
    end
    for u = 0, 8 do
        local mr, mc, mb = 0, 0, 0
        local br, bc = (u // 3) * 3, (u % 3) * 3
        for k = 0, 8 do
            mr = mr | (1 << b[u * 9 + k + 1])
            mc = mc | (1 << b[k * 9 + u + 1])
            mb = mb | (1 << b[(br + k // 3) * 9 + bc + k % 3 + 1])
        end
        if mr ~= 1022 or mc ~= 1022 or mb ~= 1022 then return false end
    end
    solved = true
    return true
end

local function apply_hint()
    for i = 1, 81 do
        if not init_b[i] and not hint_b[i] then
            if b[i] == 0 or b[i] ~= s[i] then
                b[i] = s[i]
                hint_b[i] = true
                check_win()
                return
            end
        end
    end
end

local function new_game(d)
    diff = d
    pick_diff = d
    solved = false
    row = 1
    col = 1
    picker = false

    local gen = smudge.dofile("generator.lua")
    if gen then
        gen(b, s, init_b, diff)
    end
    for i = 1, 81 do hint_b[i] = false end
    collectgarbage()
end

local function save_state()
    if solved or picker or b[1] == 0 then
        smudge.save("state", "")
        return
    end
    local t = {diff, row, col}
    for i = 1, 81 do
        t[#t + 1] = b[i]
        t[#t + 1] = s[i]
        t[#t + 1] = init_b[i] and 1 or 0
        t[#t + 1] = hint_b[i] and 1 or 0
    end
    smudge.save("state", table.concat(t, ","))
end

local function load_state()
    local str = smudge.load("state", "")
    if #str < 100 then return false end
    local vals = {}
    for v in str:gmatch("%d+") do
        vals[#vals + 1] = tonumber(v)
    end
    if #vals < 327 then return false end
    diff = vals[1] or 1
    pick_diff = diff
    row = vals[2] or 1
    col = vals[3] or 1
    local idx = 4
    for i = 1, 81 do
        b[i] = vals[idx] or 0
        s[i] = vals[idx + 1] or 0
        init_b[i] = (vals[idx + 2] == 1)
        hint_b[i] = (vals[idx + 3] == 1)
        idx = idx + 4
    end
    solved = false
    return true
end

-- Grid, number pad and action buttons, from the top down. The grid shrinks on
-- short screens so the pad always fits.
local function layout()
    local m = smudge.get_metrics()
    local topY = m.top_padding + m.header_height
    local padKey = 50
    local reserve = 2 * padKey + 8 + 36 + 36 + m.button_hints_height
    local cSize = math.min(math.floor((w - 16) / 9), math.floor((h - topY - reserve) / 9))
    local gSize = cSize * 9
    local gx = math.floor((w - gSize) / 2)
    local gy = topY + 4
    local padY = gy + gSize + 10
    local keyW = math.floor((gSize - (PAD_COLS - 1) * 6) / PAD_COLS)
    return {cSize = cSize, gSize = gSize, gx = gx, gy = gy, padY = padY, keyW = keyW, keyH = padKey,
            by = padY + 2 * padKey + 6 + 10}
end

local function pad_key_rect(L, k)
    local r = math.floor((k - 1) / PAD_COLS)
    local c = (k - 1) % PAD_COLS
    return L.gx + c * (L.keyW + 6), L.padY + r * (L.keyH + 6), L.keyW, L.keyH
end

local function editable(idx)
    return not init_b[idx] and not hint_b[idx]
end

-- Puts digit k (10 = erase) in the selected cell.
local function enter_digit(k)
    if row > 9 or solved or picker then return end
    local idx = (row - 1) * 9 + col
    if not editable(idx) then return end
    b[idx] = (k == 10) and 0 or k
    check_win()
end

-- How many times digit d is on the board (a full digit is greyed on the pad).
local function digit_count(d)
    local n = 0
    for i = 1, 81 do
        if b[i] == d then n = n + 1 end
    end
    return n
end

function on_init()
    w, h = smudge.get_bounds()
    math.randomseed(os.time())
    if not load_state() then
        picker = true
    end
end

function on_draw()
    smudge.clear()
    -- Header
    smudge.header("Sudoku", solved and "SOLVED!" or diff_names[diff])

    local L = layout()
    local cSize, gSize, gx, gy = L.cSize, L.gSize, L.gx, L.gy

    -- Cells
    for r = 1, 9 do
        for c = 1, 9 do
            local idx = (r - 1) * 9 + c
            local x = gx + (c - 1) * cSize
            local y = gy + (r - 1) * cSize
            local sel = (r == row and c == col and not solved and not picker)
            if sel then
                smudge.rect_dither(x + 1, y + 1, cSize - 2, cSize - 2, false)
                smudge.rect(x + 1, y + 1, cSize - 2, cSize - 2, false)
            else
                smudge.rect(x, y, cSize, cSize, false)
            end
            local val = b[idx]
            if val > 0 then
                smudge.text(x + math.floor(cSize / 2), y + math.floor(cSize / 2) - 14,
                            tostring(val), 0, init_b[idx], "center", true)
            end
        end
    end

    -- 3x3 Block outlines
    for i = 0, 3 do
        local ly = gy + i * 3 * cSize
        local lx = gx + i * 3 * cSize
        smudge.thick_line(gx, ly, gx + gSize, ly, 3)
        smudge.thick_line(lx, gy, lx, gy + gSize, 3)
    end

    -- Number pad: 1-9 and erase
    local selIdx = (row <= 9) and ((row - 1) * 9 + col) or nil
    local padLive = selIdx and editable(selIdx) and not solved and not picker
    for k = 1, 10 do
        local x, y, kw, kh = pad_key_rect(L, k)
        local focused = (pad_sel == k)
        if focused then
            smudge.rounded_rect(x, y, kw, kh, 6, true, true)
        else
            smudge.rounded_rect(x, y, kw, kh, 6, false, padLive and 2 or 1)
        end
        local full = (k <= 9 and digit_count(k) >= 9)
        if full and not focused then smudge.rect_dither(x + 3, y + 3, kw - 6, kh - 6, false) end
        local label = (k == 10) and "X" or tostring(k)
        smudge.text(x + math.floor(kw / 2), y + math.floor(kh / 2) - 14, label, 0, true, "center", not focused)
    end

    -- Action buttons: New Game (row 10), Hint (row 11)
    local by = L.by
    local bw = 140
    local bh = 36
    local ngx = gx + 20
    local hx = gx + gSize - bw - 20

    local ngSel = (row == 10 and not picker)
    smudge.rect(ngx, by, bw, bh, ngSel)
    smudge.text(ngx + math.floor(bw / 2), by + 10, "New Game", 10, true, "center", not ngSel)

    local hSel = (row == 11 and not picker)
    smudge.rect(hx, by, bw, bh, hSel)
    smudge.text(hx + math.floor(bw / 2), by + 10, "Hint", 10, true, "center", not hSel)

    -- Difficulty Modal Dialog (Faithful reproduction of SudokuActivity.h)
    if picker then
        local mw = w - 40
        local mh = 110
        local mx = math.floor((w - mw) / 2)
        local my = math.floor((h - mh) / 2) - 20

        smudge.rect(mx, my, mw, mh, true, true)
        smudge.rect(mx + 2, my + 2, mw - 4, mh - 4, false, 1, false)
        smudge.centered_text(my + 18, "SELECT DIFFICULTY", 0, false, false)

        local secW = math.floor(mw / 3)
        local lbls = {"Easy", "Medium", "Hard"}
        for i = 1, 3 do
            local txt = (i == pick_diff) and ("[ " .. lbls[i] .. " ]") or lbls[i]
            local tx = mx + (i - 1) * secW + math.floor(secW / 2)
            smudge.text(tx, my + 60, txt, 0, false, "center", false)
        end
    end

    -- Button hints
    if pad_sel then
        smudge.button_hints("Cancel", "Enter", "<", ">")
    elseif picker then
        smudge.button_hints("Cancel", "Select", "<-", "->")
    elseif solved then
        smudge.button_hints("Back", "New Game", "", "")
    else
        smudge.button_hints("Back", "Select", "Up", "Down")
    end
end

function on_button(btn, pressed)
    if not pressed then return end

    if picker then
        if btn == "confirm" then
            new_game(pick_diff)
        elseif btn == "back" then
            if b[1] ~= 0 or s[1] ~= 0 then
                picker = false
            else
                smudge.exit()
            end
        elseif btn == "left" or btn == "up" or btn == "page_back" then
            pick_diff = (pick_diff == 1) and 3 or (pick_diff - 1)
        elseif btn == "right" or btn == "down" or btn == "page_forward" then
            pick_diff = (pick_diff % 3) + 1
        end
        return
    end

    if pad_sel then
        if btn == "confirm" then
            enter_digit(pad_sel)
            pad_sel = nil
        elseif btn == "back" then
            pad_sel = nil
        elseif btn == "left" or btn == "up" or btn == "page_back" then
            pad_sel = (pad_sel == 1) and 10 or (pad_sel - 1)
        elseif btn == "right" or btn == "down" or btn == "page_forward" then
            pad_sel = (pad_sel % 10) + 1
        end
        return
    end

    if btn == "back" then
        save_state()
        smudge.exit()
    elseif btn == "confirm" then
        if solved or row == 10 then
            picker = true
        elseif row == 11 then
            apply_hint()
        else
            local idx = (row - 1) * 9 + col
            if editable(idx) then
                -- Onto the pad, at the cell's digit (or 1 when it is empty).
                pad_sel = (b[idx] > 0) and b[idx] or 1
            end
        end
    elseif btn == "left" then
        -- Up
        if row >= 10 then row = 9; col = 5
        elseif row == 1 then row = 10
        else row = row - 1 end
    elseif btn == "right" then
        -- Down
        if row >= 10 then row = 1; col = 5
        elseif row == 9 then row = 10
        else row = row + 1 end
    elseif btn == "up" or btn == "page_back" then
        -- Left
        if row == 11 then row = 10
        elseif row == 10 then row = 11
        else col = (col == 1) and 9 or (col - 1) end
    elseif btn == "down" or btn == "page_forward" then
        -- Right
        if row == 10 then row = 11
        elseif row == 11 then row = 10
        else col = (col == 9) and 1 or (col + 1) end
    end
end

function on_tap(x, y)
    local m = smudge.get_metrics()
    if picker then
        if y > h - m.button_hints_height then
            if x < w / 4 then
                if b[1] ~= 0 or s[1] ~= 0 then picker = false else smudge.exit() end
            elseif x < w / 2 then
                new_game(pick_diff)
            elseif x < 3 * w / 4 then
                pick_diff = (pick_diff == 1) and 3 or (pick_diff - 1)
            else
                pick_diff = (pick_diff % 3) + 1
            end
            return
        end
        local mw = w - 40
        local mh = 110
        local mx = math.floor((w - mw) / 2)
        local my = math.floor((h - mh) / 2) - 20
        if x >= mx and x <= mx + mw and y >= my and y <= my + mh then
            local secW = math.floor(mw / 3)
            local tapped = math.floor((x - mx) / secW) + 1
            if tapped >= 1 and tapped <= 3 then new_game(tapped) end
        end
        return
    end

    if y > h - m.button_hints_height then
        if x < w / 4 then
            save_state()
            smudge.exit()
        elseif x < w / 2 then
            if solved or row == 10 then picker = true
            elseif row == 11 then apply_hint()
            end
        elseif x < 3 * w / 4 then
            if row >= 10 then row = 9; col = 5
            elseif row == 1 then row = 10
            else row = row - 1 end
        else
            if row >= 10 then row = 1; col = 5
            elseif row == 9 then row = 10
            else row = row + 1 end
        end
        return
    end

    local L = layout()
    local cSize, gSize, gx, gy = L.cSize, L.gSize, L.gx, L.gy
    pad_sel = nil

    if x >= gx and x < gx + gSize and y >= gy and y < gy + gSize then
        local c = math.floor((x - gx) / cSize) + 1
        local r = math.floor((y - gy) / cSize) + 1
        if r >= 1 and r <= 9 and c >= 1 and c <= 9 then
            row = r
            col = c
        end
        return
    end

    for k = 1, 10 do
        local kx, ky, kw, kh = pad_key_rect(L, k)
        if smudge.in_rect(x, y, kx, ky, kw, kh) then
            enter_digit(k)
            return
        end
    end

    local by = L.by
    local bw = 140
    local bh = 36
    local ngx = gx + 20
    local hx = gx + gSize - bw - 20

    if y >= by and y <= by + bh then
        if x >= ngx and x <= ngx + bw then picker = true
        elseif x >= hx and x <= hx + bw then apply_hint() end
    end
end
