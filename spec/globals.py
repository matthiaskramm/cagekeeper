def test():
    assert(type(global_int) == int)
    assert(type(global_array) == list)
    assert(type(global_boolean) == bool)
    assert(type(global_float) == float)
    assert(type(global_string) in [str,unicode])
    return "ok"
