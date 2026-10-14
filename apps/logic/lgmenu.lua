-- Logic Puzzles menus on top of the shared menu.lua: the start menu (S.want
-- "main"), a puzzle's options ("options": its saved puzzle and its sizes) and
-- the menu during play ("pause"). Usage:
--   local screen = smudge.dofile("lgmenu.lua")(S)    -- S: main.lua's state

return function(S)
local M = {}
local menu
local want = S.want

local function statsNote(k, lv)
    local s = S.stats[k][lv]
    if s[1] == 0 then return "Not solved yet" end
    return string.format("Solved %d  Best %s", s[1], S.formatTime(s[2]))
end

-- A kind's puzzle in progress: { level, ms } or nil.
local function inProgress(k)
    if S.kind == k and S.P and not S.P.solved then return { S.level, S.elapsed() } end
    return S.saved[k]
end

local function items()
    local out = {}
    if want == "main" then
        if S.P and not S.P.solved then
            out[1] = { label = "Resume", act = "resume",
                       note = S.KINDS[S.kind].name .. "  " .. S.levelText(S.kind, S.level) }
        end
        for k, info in ipairs(S.KINDS) do
            local solved = 0
            for lv = 1, #info.levels do solved = solved + S.stats[k][lv][1] end
            local note = info.blurb
            if inProgress(k) then
                note = "Puzzle in progress"
            elseif solved > 0 then
                note = "Solved " .. solved
            end
            out[#out + 1] = { label = info.name, act = "kind", kind = k, note = note }
        end
        out[#out + 1] = { label = "Exit", act = "exit" }
    elseif want == "options" then
        local k = S.optKind
        local cur = inProgress(k)
        if cur then
            out[1] = { label = "Continue", act = "continue",
                       note = S.levelText(k, cur[1]) .. "  " .. S.formatTime(cur[2]) }
        end
        for lv, L in ipairs(S.KINDS[k].levels) do
            out[#out + 1] = { label = string.format("%s  %dx%d", L.name, L.size, L.size), act = "new", level = lv,
                              note = statsNote(k, lv) }
        end
        out[#out + 1] = { label = "Back", act = "back" }
    else
        local solved = S.P.solved
        if not solved then
            out[1] = { label = "Resume", act = "play" }
            if S.K.mistakes then out[#out + 1] = { label = "Check for mistakes", act = "check" } end
            out[#out + 1] = { label = "Restart", act = "restart", note = "Clear the board, keep the clock" }
        end
        out[#out + 1] = { label = "New puzzle", act = "new", level = S.level, note = S.levelText(S.kind, S.level) }
        out[#out + 1] = { label = "Other puzzles", act = "main" }
        out[#out + 1] = { label = "Exit", act = "exit" }
    end
    return out
end

if want == "main" then
    menu = smudge.dofile("menu.lua")("Logic Puzzles", { "Pen-and-paper puzzles made on the reader,", "each with a single solution." })
elseif want == "options" then
    local info = S.KINDS[S.optKind]
    menu = smudge.dofile("menu.lua")(info.name, { S.failed or info.blurb })
else
    menu = smudge.dofile("menu.lua")(S.KINDS[S.kind].name, { "Paused - the clock is stopped." })
end

local function choose(item)
    if not item then return end
    local act = item == "exit" and (want == "main" and "exit" or want == "options" and "back" or "play") or item.act
    if act == "exit" then
        smudge.exit()
    elseif act == "resume" or act == "play" then
        S.show("play")
    elseif act == "kind" then
        S.optKind, S.failed = item.kind, nil
        S.show("options")
    elseif act == "back" or act == "main" then
        S.failed = nil
        if act == "main" then S.savePuzzle() end
        S.show("main")
    elseif act == "continue" then
        S.continue(S.optKind)
    elseif act == "new" then
        S.newPuzzle(want == "options" and S.optKind or S.kind, item.level)
    elseif act == "check" then
        local n = S.K.mistakes(S.P)
        S.show("play")
        S.note = n == 0 and "No mistakes so far." or (n == 1 and "1 mistake on the board." or (n .. " mistakes on the board."))
    elseif act == "restart" then
        S.K.restart(S.P)
        S.show("play")
        S.cr, S.cc, S.sel = 0, 0, nil
        S.note = "Restarted."
    end
end

function M.draw() menu.draw(items()) end
function M.button(btn) choose(menu.button(btn, items())) end
function M.tap(x, y) choose(menu.tap(x, y, items())) end

return M
end
