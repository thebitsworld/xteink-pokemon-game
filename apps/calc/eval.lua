-- Calculator arithmetic: evaluating an expression typed on the keypad. No
-- drawing, so it can be tested on a computer (test/lua_apps/calc_test.lua).
--
--   local E = smudge.dofile("eval.lua")
--   E.evaluate("2+3*(4-1)")  -> 11        (nil, "Error" on a mistake)
--   E.format(11)             -> "11"
--
-- Numbers on the reader are 32-bit floats, good for about 7 significant
-- digits, so results are shown to 7.

local E = {}

-- Recursive descent over the characters of the expression:
--   expr   := term { ("+" | "-") term }
--   term   := factor { ("*" | "/") factor }
--   factor := "-" factor | number | "(" expr ")"
local src, pos

local function peek() return src:sub(pos, pos) end

local expr

local function factor()
    local c = peek()
    if c == "-" then
        pos = pos + 1
        return -factor()
    elseif c == "(" then
        pos = pos + 1
        local v = expr()
        if peek() ~= ")" then error("bracket", 0) end
        pos = pos + 1
        return v
    end
    local s, e = src:find("^%d*%.?%d*", pos)
    if not s or e < s or src:sub(s, e) == "." then error("number", 0) end
    pos = e + 1
    -- As a float: the reader's 32-bit integers would wrap round on big products.
    return tonumber(src:sub(s, e)) * 1.0
end

local function term()
    local v = factor()
    while true do
        local c = peek()
        if c == "*" then
            pos = pos + 1
            v = v * factor()
        elseif c == "/" then
            pos = pos + 1
            local d = factor()
            if d == 0 then error("zero", 0) end
            v = v / d
        else
            return v
        end
    end
end

function expr()
    local v = term()
    while true do
        local c = peek()
        if c == "+" then
            pos = pos + 1
            v = v + term()
        elseif c == "-" then
            pos = pos + 1
            v = v - term()
        else
            return v
        end
    end
end

-- The value of expression s, or nil and a short reason.
function E.evaluate(s)
    src, pos = s, 1
    local ok, v = pcall(expr)
    if not ok then return nil, "Error" end
    if pos <= #s then return nil, "Error" end
    if v ~= v or v == math.huge or v == -math.huge then return nil, "Error" end
    return v
end

-- A number as the display shows it: whole numbers plainly, others to 7
-- significant digits without trailing zeros.
function E.format(v)
    if v == math.floor(v) and math.abs(v) < 1e7 then return string.format("%d", math.floor(v)) end
    local s = string.format("%.7g", v)
    if s:find("e") then return s end
    if s:find("%.") then s = s:gsub("0+$", ""):gsub("%.$", "") end
    return s
end

return E
