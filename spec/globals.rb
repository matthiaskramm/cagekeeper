def assert(b)
    raise if not b
end

def test()
    assert(global_int.is_a? Integer)
    assert(global_array.is_a? Array)
    assert(global_boolean.is_a?(TrueClass) || global_boolean.is_a?(FalseClass))
    assert(global_float.is_a? Float)
    assert(global_string.is_a? String)
    return "ok"
end
