function assert(b) {
    if(!b) {
        throw "assertion failed";
    }
}

function test() {
    assert(typeof(global_int) == "number");
    assert(typeof(global_array) == "object");
    assert(typeof(global_boolean) == "boolean");
    assert(typeof(global_float) == "number");
    assert(typeof(global_int) == "number");
    assert(typeof(global_string) == "string");
    return "ok";
}
