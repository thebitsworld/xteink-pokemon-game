-- Tests for apps/calc/eval.lua (run by ctest: LuaAppLogic_calc).
local E = dofile(APPS .. "/calc/eval.lua")

local function is(s, want)
    local v, err = E.evaluate(s)
    local got = v and E.format(v) or err
    assert(got == want, string.format("%s gave %s, not %s", s, tostring(got), want))
end

is("2+3", "5")
is("2+3*4", "14")
is("(2+3)*4", "20")
is("10/4", "2.5")
is("1/3", "0.3333333")
is("-5+2", "-3")
is("2*-3", "-6")
is("--4", "4")
is("7-2-1", "4")
is("8/2/2", "2")
is("0.1+0.2", "0.3")
is(".5*4", "2")
is("3.", "3")
is("1/0", "Error")
is("2+", "Error")
is("(1+2", "Error")
is("1+2)", "Error")
is(".", "Error")
is("", "Error")
is("99999*99999", "9.9998e+09")
is("123456*10", "1234560")

assert(E.format(2.5) == "2.5" and E.format(-0.125) == "-0.125" and E.format(100) == "100")

print("calc ok")
