ok = 0

function assert(b)
    if not b then
        error("assertion failed")
    end
end

function call_noargs(...)
    assert((...) == nil)
    ok = ok + 1
end

function call_array(a)
    assert(type(a) == "table")
    ok = ok + 1
end

function call_boolean(b)
    assert(type(b) == "boolean")
    ok = ok + 1
end

function call_float(f)
    assert(type(f) == "number")
    ok = ok + 1
end

function call_int(i)
    assert(type(i) == "number")
    ok = ok + 1
end

function call_string(s)
    assert(type(s) == "string")
    ok = ok + 1
end

function call_int_and_float_and_string(i,f,s)
    assert(type(i) == "number")
    assert(type(f) == "number")
    assert(type(s) == "string")
    ok = ok + 1
end

function call_boolean_and_array(b,a)
    assert(type(b) == "boolean")
    assert(type(a) == "table")
    ok = ok + 1
end

function test()
    assert(ok == 8)
    return "ok"
end
