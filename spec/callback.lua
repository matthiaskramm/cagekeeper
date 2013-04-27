function assert(b)
    if not b then
        error("assertion failed")
    end
end

function compare_tables(t1, t2)
    if #t1 ~= #t2 then
        print(#t1 .. " " .. #t2)
        return false 
    end
    for i = 1,#t1 do
        if t1[i] ~= t2[i] then
            return false 
        end
    end
    return true
end

function test()

    assert(add2(3,4) == 7)
    assert(add3(3,4,-5) == 2)

    assert(fadd2(3.0,4.0) == 7.0)
    assert(fadd3(3.0,4.0,-5.0) == 2.0)

    assert(concat_strings("foo", "bar") == "foobar")

    a1 = {1,2,3}
    a2 = {4,5,6}
    a12 = {1,2,3,4,5,6}
    assert(compare_tables(concat_arrays(a1, a2), a12))

    assert(negate(true) == false)
    assert(type(negate(true)) == "boolean")

    return "ok"
end
