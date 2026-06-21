package.path = "../?.lua;" .. package.path
require("compat4")

local tests = {}

tests["getn"] = function()
    assert_eq(getn({10, 20, 30}), 3)
end

tests["tinsert"] = function()
    local t = {1, 2, 3}
    tinsert(t, 4)
    assert_eq(t[4], 4)
end

tests["tremove"] = function()
    local t = {1, 2, 3}
    tremove(t, 2)
    assert_eq(t[2], 3)
    assert_eq(getn(t), 2)
end

tests["strfind"] = function()
    local s, e = strfind("hello world", "world")
    assert_eq(s, 7)
    assert_eq(e, 11)
end

tests["strsub"] = function()
    assert_eq(strsub("hello", 2, 4), "ell")
end

tests["strlen"] = function()
    assert_eq(strlen("hello"), 5)
end

tests["strupper strlower"] = function()
    assert_eq(strupper("hello"), "HELLO")
    assert_eq(strlower("HELLO"), "hello")
end

tests["format"] = function()
    assert_eq(format("%d", 42), "42")
    assert_eq(format("%.2f", 3.14159), "3.14")
end

tests["gsub"] = function()
    assert_eq(gsub("hello", "l", "L"), "heLLo")
end

tests["mod"] = function()
    assert_eq(mod(10, 3), 1)
    assert_near(mod(5.5, 2), 1.5)
end

tests["foreachi"] = function()
    local sum = 0
    foreachi({10, 20, 30}, function(i, v) sum = sum + v end)
    assert_eq(sum, 60)
end

tests["foreach"] = function()
    local keys = {}
    foreach({a=1, b=2}, function(k, v) keys[k] = v end)
    assert_eq(keys.a, 1)
    assert_eq(keys.b, 2)
end

run_tests("compat4", tests)
