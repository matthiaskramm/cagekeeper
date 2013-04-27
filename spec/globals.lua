function assert(b)
    if not b then
        error("assertion failed")
    end
end

function test()
    assert(type(global_int) == "number")
    assert(type(global_array) == "table")
    assert(type(global_boolean) == "boolean")
    assert(type(global_float) == "number")
    assert(type(global_string) == "string")
    return "ok"
end
