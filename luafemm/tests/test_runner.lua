local pass, fail, total = 0, 0, 0

local function run_tests(name, tests)
    print("--- " .. name .. " ---")
    for tname, tfunc in pairs(tests) do
        total = total + 1
        local ok, err = pcall(tfunc)
        if ok then
            pass = pass + 1
            print("  PASS: " .. tname)
        else
            fail = fail + 1
            print("  FAIL: " .. tname .. " — " .. tostring(err))
        end
    end
end

local function assert_eq(a, b, msg)
    if a ~= b then
        error((msg or "") .. " expected " .. tostring(b) .. ", got " .. tostring(a), 2)
    end
end

local function assert_near(a, b, tol, msg)
    tol = tol or 1e-10
    if math.abs(a - b) > tol then
        error((msg or "") .. " expected ~" .. tostring(b) .. ", got " .. tostring(a), 2)
    end
end

local function assert_true(v, msg)
    if not v then error(msg or "assertion failed", 2) end
end

_G.run_tests = run_tests
_G.assert_eq = assert_eq
_G.assert_near = assert_near
_G.assert_true = assert_true

-- Run all test files passed as arguments
local files = {...}
if #files == 0 then
    files = {"test_complex", "test_compat4"}
end

for _, f in ipairs(files) do
    require(f)
end

print("")
print(string.format("Results: %d/%d passed, %d failed", pass, total, fail))
if fail > 0 then os.exit(1) end
