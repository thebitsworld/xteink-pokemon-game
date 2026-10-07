-- The Whodunit grid page. A staircase: categories 1..n-1 across the top,
-- n..2 down the side, and a block wherever the column's category comes
-- before the row's. Loaded by view.lua: smudge.dofile("grid.lua")(W, S, U).

return function(W, S, U)
local P = { CONFIRM = "Mark", PREV = "<", NEXT = ">" }

local function initial(c, name)
    if c == W.SUSPECT then return name:match("(%a)%a*$"):upper() end
    return name:sub(1, 1):upper()
end

local function geometry()
    local case = S.case
    local n, k = case.categories, case.items
    local labelW = 0
    for r = 2, n do
        for i = 1, k do
            local tw = smudge.text_width(case.names[r][i], "ui10", "regular")
            if tw > labelW then labelW = tw end
        end
    end
    labelW = math.min(labelW + 8, 118)
    local cells = (n - 1) * k
    local headerH = 44
    local gap = 22 -- room for the category name over each later block of rows
    local cell = math.min((S.w - 16 - labelW) // cells, (U.bodyBottom() - U.bodyTop() - headerH - 40 - (n - 2) * gap) // cells,
                          40)
    local x0 = (S.w - labelW - cells * cell) // 2 + labelW
    return { n = n, k = k, cell = cell, x0 = x0, y0 = U.bodyTop() + headerH, labelW = labelW, cells = cells, gap = gap }
end

-- The top of a grid row (1-based).
local function rowY(G, R) return G.y0 + (R - 1) * G.cell + ((R - 1) // G.k) * G.gap end

-- Category and item of a grid row or column (1-based).
local function rowAt(G, R) return G.n - (R - 1) // G.k, (R - 1) % G.k + 1 end
local function colAt(G, C) return (C - 1) // G.k + 1, (C - 1) % G.k + 1 end
local function valid(G, R, C)
    local rc = rowAt(G, R)
    local cc = colAt(G, C)
    return cc < rc
end

function P.draw()
    local case, grid = S.case, S.grid
    local G = geometry()
    local s = G.cell
    -- Column headers: category names over their groups, item initials.
    for cg = 1, G.n - 1 do
        local x = G.x0 + (cg - 1) * G.k * s
        smudge.text(x + G.k * s // 2, U.bodyTop(), W.CATEGORY_NAMES[cg], "ui10", "bold", "center", true)
        for i = 1, G.k do
            smudge.text(x + (i - 1) * s + s // 2, U.bodyTop() + 22, initial(cg, case.names[cg][i]), "ui10", "bold",
                        "center", true)
        end
    end
    for R = 1, G.cells do
        local rc, ri = rowAt(G, R)
        local y = rowY(G, R)
        local name = case.names[rc][ri]
        while smudge.text_width(name, "ui10", "regular") > G.labelW - 6 and #name > 3 do name = name:sub(1, -2) end
        smudge.text(G.x0 - 6, y + (s - 20) // 2, name, "ui10", "regular", "right", true)
        for C = 1, G.cells do
            if valid(G, R, C) then
                local cc, ci = colAt(G, C)
                local x = G.x0 + (C - 1) * s
                smudge.rect(x, y, s + 1, s + 1, false, true, 1)
                local v = W.mark(grid, rc, ri, cc, ci)
                if v == W.YES then
                    smudge.circle(x + s // 2, y + s // 2, s // 3, true, true)
                elseif v == W.NO then
                    local q = s // 4
                    smudge.line(x + q, y + q, x + s - q, y + s - q, 2, true)
                    smudge.line(x + s - q, y + q, x + q, y + s - q, 2, true)
                end
            end
        end
    end
    -- Heavier lines around each block.
    for rg = 1, G.n - 1 do
        for cg = 1, G.n - rg do
            local x, y = G.x0 + (cg - 1) * G.k * s, rowY(G, (rg - 1) * G.k + 1)
            smudge.rect(x - 1, y - 1, G.k * s + 3, G.k * s + 3, false, true, 2)
        end
    end
    -- Category names down the side.
    for rg = 1, G.n - 1 do
        local rc = G.n - rg + 1
        smudge.text(16, rowY(G, (rg - 1) * G.k + 1) - 20, W.CATEGORY_NAMES[rc], "ui10", "bold", "left", true)
    end
    if S.focus == "content" and not S.touch then
        local x, y = G.x0 + (S.gc - 1) * s, rowY(G, S.gr)
        smudge.rect(x - 2, y - 2, s + 5, s + 5, false, false, 2)
        smudge.rect(x, y, s + 1, s + 1, false, true, 3)
    end
    -- The pair under the cursor, in words.
    if not S.touch or S.lastCell then
        local rc, ri = rowAt(G, S.gr)
        local cc, ci = colAt(G, S.gc)
        smudge.centered_text(rowY(G, G.cells) + s + 10, case.names[rc][ri] .. "  /  " .. case.names[cc][ci], "ui10", "bold",
                             true)
    end
end

-- Cycles a grid cell: unknown, no, yes. A yes crosses out the rest of its
-- row and column in that block.
local function cycle(R, C)
    local G = geometry()
    if not valid(G, R, C) then return end
    local rc, ri = rowAt(G, R)
    local cc, ci = colAt(G, C)
    local grid = S.grid
    local kk = W.key(rc, ri, cc, ci)
    local v = (grid[kk] or 0)
    v = (v == W.UNKNOWN) and W.NO or ((v == W.NO) and W.YES or W.UNKNOWN)
    grid[kk] = (v ~= 0) and v or nil
    if v == W.YES then
        for j = 1, G.k do
            if j ~= ci and (grid[W.key(rc, ri, cc, j)] or 0) == 0 then grid[W.key(rc, ri, cc, j)] = W.NO end
            if j ~= ri and (grid[W.key(rc, j, cc, ci)] or 0) == 0 then grid[W.key(rc, j, cc, ci)] = W.NO end
        end
    end
    smudge.request_update()
end

local function move(dr, dc)
    local G = geometry()
    local r, c = S.gr, S.gc
    for _ = 1, G.cells do
        r, c = r + dr, c + dc
        if r < 1 then S.focus = "tabs" return end
        if r > G.cells or c < 1 or c > G.cells then return end
        if valid(G, r, c) then
            S.gr, S.gc = r, c
            return
        end
    end
end

function P.button(btn)
    if btn == "left" then move(0, -1)
    elseif btn == "right" then move(0, 1)
    elseif btn == "up" or btn == "page_back" then move(-1, 0)
    elseif btn == "down" or btn == "page_forward" then move(1, 0)
    elseif btn == "confirm" then cycle(S.gr, S.gc) return end
    smudge.request_update()
end

function P.tap(x, y)
    local G = geometry()
    local C = (x - G.x0) // G.cell + 1
    local R = 0
    for r = 1, G.cells do
        if y >= rowY(G, r) and y < rowY(G, r) + G.cell then R = r end
    end
    if R >= 1 and R <= G.cells and C >= 1 and C <= G.cells and valid(G, R, C) then
        S.gr, S.gc, S.lastCell = R, C, true
        cycle(R, C)
    end
end

return P
end
