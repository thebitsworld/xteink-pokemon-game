-- Makes a new puzzle (the one S.gen asks for) and shows it, or the options
-- with a note when that fails. Loaded only for that moment. Usage:
--   smudge.dofile("make.lua")(S, rand).generate()
-- S: main.lua's state; rand(n): 1..n.

return function(S, rand)
local M = {}
local GEN_MS = 6000 -- a generator's time; past it, Slitherlink and Light Up keep more clues

-- Makes the puzzle S.gen asks for and shows it (or the options with a note).
function M.generate()
    local k, lv = S.gen[1], S.gen[2]
    S.gen = nil
    if S.kind ~= k then S.savePuzzle() end -- the other kind keeps its puzzle
    S.drop(true)
    local info = S.KINDS[k]
    local _, size, arg = S.levelOf(k, lv)
    local K, P
    if info.loop then
        -- Slitherlink: a loop from one module, then its clues thinned by the
        -- generator, never both in memory; again while deduction fails.
        -- Kakuro the same way: a filled layout, then its digits changed
        -- until reasoning alone solves it; again when that gets stuck.
        local start, T, clues = smudge.millis(), nil, nil
        repeat
            local loop = smudge.dofile(info.loop)()
            T = T or loop.topology(size)
            local sol, all = loop.make(size, rand)
            loop = nil
            collectgarbage("collect")
            local G = smudge.dofile(info.gen)()
            clues = G.reduce(size, T, all, rand, smudge.millis, start + GEN_MS, arg)
            G, all = nil, nil
            collectgarbage("collect")
            if clues then
                K = smudge.dofile(info.logic)
                P = K.new(size, clues, sol)
            end
        until P or smudge.millis() - start > GEN_MS
    elseif info.gen then
        -- The rules are loaded only once the puzzle is made.
        local G = smudge.dofile(info.gen)()
        collectgarbage("collect")
        local a, b = G.generate(size, rand, smudge.millis, GEN_MS, arg)
        G = nil
        collectgarbage("collect")
        K = smudge.dofile(info.logic)
        if a then P = K.new(size, a, b) end
    else
        K = smudge.dofile(info.logic)
        P = K.new(size, rand)
    end
    if not P then
        K = nil
        collectgarbage("collect")
        S.optKind, S.failed = k, "Could not make a puzzle - try again."
        S.show("options")
        return
    end
    S.kind, S.level, S.K, S.P = k, lv, K, P
    S.resetCursor()
    S.savePuzzle()
    S.show("play")
end

return M
end
