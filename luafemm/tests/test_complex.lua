package.path = "../?.lua;" .. package.path
require("complex")

local tests = {}

tests["new and accessors"] = function()
    local z = Complex(3, 4)
    assert_eq(re(z), 3)
    assert_eq(im(z), 4)
end

tests["addition"] = function()
    local a = Complex(1, 2)
    local b = Complex(3, 4)
    local c = a + b
    assert_eq(re(c), 4)
    assert_eq(im(c), 6)
end

tests["addition with real"] = function()
    local z = Complex(1, 2) + 5
    assert_eq(re(z), 6)
    assert_eq(im(z), 2)
    z = 5 + Complex(1, 2)
    assert_eq(re(z), 6)
    assert_eq(im(z), 2)
end

tests["subtraction"] = function()
    local z = Complex(5, 3) - Complex(2, 1)
    assert_eq(re(z), 3)
    assert_eq(im(z), 2)
end

tests["multiplication"] = function()
    local z = Complex(1, 2) * Complex(3, 4)
    assert_eq(re(z), -5)
    assert_eq(im(z), 10)
end

tests["division"] = function()
    local z = Complex(1, 2) / Complex(1, 0)
    assert_eq(re(z), 1)
    assert_eq(im(z), 2)
end

tests["negation"] = function()
    local z = -Complex(3, -4)
    assert_eq(re(z), -3)
    assert_eq(im(z), 4)
end

tests["abs"] = function()
    assert_near(abs(Complex(3, 4)), 5)
end

tests["conjugate"] = function()
    local z = conj(Complex(3, 4))
    assert_eq(re(z), 3)
    assert_eq(im(z), -4)
end

tests["power integer"] = function()
    local z = Complex(0, 1) ^ 2
    assert_near(re(z), -1)
    assert_near(im(z), 0)
end

tests["sqrt of negative"] = function()
    local z = sqrt(-4)
    assert_near(re(z), 0)
    assert_near(im(z), 2)
end

tests["sqrt of complex"] = function()
    local z = sqrt(Complex(0, 1))
    assert_near(re(z), math.sqrt(2)/2)
    assert_near(im(z), math.sqrt(2)/2)
end

tests["exp of imaginary"] = function()
    local z = exp(Complex(0, math.pi))
    assert_near(re(z), -1, 1e-10)
    assert_near(im(z), 0, 1e-10)
end

tests["complex2str pure real"] = function()
    assert_eq(complex2str(Complex(5, 0)), "5")
end

tests["complex2str pure I"] = function()
    assert_eq(complex2str(Complex(0, 1)), "I")
end

tests["complex2str -I"] = function()
    assert_eq(complex2str(Complex(0, -1)), "-I")
end

tests["complex2str pure imag"] = function()
    assert_eq(complex2str(Complex(0, 0.5)), "I*0.5")
end

tests["complex2str r+I*x"] = function()
    assert_eq(complex2str(Complex(3, 2)), "3+I*2")
end

tests["complex2str r-I*x"] = function()
    assert_eq(complex2str(Complex(3, -2)), "3-I*2")
end

tests["I global"] = function()
    assert_true(iscomplex(I))
    assert_eq(re(I), 0)
    assert_eq(im(I), 1)
end

tests["sin real passthrough"] = function()
    assert_near(sin(0), 0)
    assert_near(sin(math.pi/2), 1)
end

tests["cos complex"] = function()
    local z = cos(Complex(0, 1))
    assert_near(re(z), math.cosh(1))
    assert_near(im(z), 0, 1e-10)
end

run_tests("complex", tests)
