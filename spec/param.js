ok = 0

function assert(b) {
    if(!b) {
        throw "Assertion failed";
    }
}

function call_noargs() {
    assert(arguments.length == 0);
    ok += 1
}

function call_array(a) {
    assert(typeof(a) == "object")
    ok += 1
}

function call_boolean(b) {
    assert(typeof(b) == "boolean")
    ok += 1
}

function call_float(f) {
    assert(typeof(f) == "number")
    ok += 1
}

function call_int(i) {
    assert(typeof(i) == "number")
    ok += 1
}

function call_string(s) {
    assert(typeof(s) == "string")
    ok += 1
}

function call_int_and_float_and_string(i,f,s) {
    assert(typeof(i) == "number")
    assert(typeof(f) == "number")
    assert(typeof(s) == "string")
    ok += 1
}

function call_boolean_and_array(b,a) {
    assert(typeof(b) == "boolean")
    assert(typeof(a) == "array")
    ok += 1
}

function test() {
    assert(Count.ok == 8)
    return "ok"
}

