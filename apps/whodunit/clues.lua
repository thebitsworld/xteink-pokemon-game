-- The Whodunit clues page: the clues and the case file, a page at a time.
-- Loaded by view.lua: smudge.dofile("clues.lua")(W, S, U).

return function(W, S, U)
local P = { CONFIRM = "", PREV = "Up", NEXT = "Down" }

-- The wrapped text, as two flat arrays: the lines, and for each its indent
-- times two, plus one if bold.
local texts, styles = nil, nil

local function build()
    texts, styles = {}, {}
    local width = S.w - 40
    local function add(text, bold, indent)
        indent = indent or 0
        local res = smudge.wrapped_text(text, width - indent, "ui10", bold and "bold" or "regular")
        for k, line in ipairs(res.lines) do
            texts[#texts + 1] = line
            styles[#styles + 1] = ((k > 1) and indent or 0) * 2 + (bold and 1 or 0)
        end
    end
    local case = S.case
    for i, c in ipairs(case.clues) do add(i .. ". " .. c.text, false, 22) end
    add(case.murderClue, true)
    texts[#texts + 1], styles[#styles + 1] = "", 0
    add("Case file", true)
    for c = 1, case.categories do
        add(W.CATEGORY_NAMES[c], true)
        for i = 1, case.items do
            local d = case.dossier[c][i]
            add("  " .. case.names[c][i] .. ((d ~= "") and (": " .. d) or ""), false, 16)
        end
    end
end

local function perPage()
    return (U.bodyBottom() - U.bodyTop() - (S.touch and 56 or 10)) // smudge.line_height("ui10")
end

function P.draw()
    if not texts then build() end
    local lh = smudge.line_height("ui10")
    local per = perPage()
    local y = U.bodyTop()
    for k = S.scroll + 1, math.min(#texts, S.scroll + per) do
        local st = styles[k]
        smudge.text(20 + st // 2, y, texts[k], "ui10", (st % 2 == 1) and "bold" or "regular", "left", true)
        y = y + lh
    end
    local pages = math.max(1, (#texts + per - 1) // per)
    local label = string.format("Page %d of %d", S.scroll // per + 1, pages)
    if S.touch then
        local b = U.bodyBottom() - 44
        smudge.rounded_rect(16, b, 100, 40, 6, false, 2)
        smudge.text(66, b + 9, "Prev", "ui12", "bold", "center", true)
        smudge.rounded_rect(S.w - 116, b, 100, 40, 6, false, 2)
        smudge.text(S.w - 66, b + 9, "Next", "ui12", "bold", "center", true)
        smudge.centered_text(b + 10, label, "ui10", "regular", true)
    elseif pages > 1 then
        smudge.text(S.w - 16, U.bodyBottom() - 20, label, "ui10", "regular", "right", true)
    end
end

local function scroll(dir)
    if not texts then build() end
    local per = perPage()
    if dir < 0 and S.scroll == 0 then
        S.focus = "tabs"
        return
    end
    S.scroll = math.max(0, math.min(S.scroll + dir * per, math.max(0, #texts - 1) // per * per))
end

function P.button(btn)
    if btn == "up" or btn == "page_back" or btn == "left" then scroll(-1)
    elseif btn == "down" or btn == "page_forward" or btn == "right" or btn == "confirm" then scroll(1) end
    smudge.request_update()
end

function P.tap(x, y)
    scroll((x > S.w // 2) and 1 or -1)
    smudge.request_update()
end

return P
end
