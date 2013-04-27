$ok = 0

def assert(b)
    raise if not b
end

def call_noargs(*args)
    assert(args.size == 0)
    $ok += 1
end

def call_array(a):
    assert(a.is_a? Array)
    $ok += 1
end

def call_boolean(b):
    assert(b.is_a? TrueClass or b.is_a? FalseClass)
    $ok += 1
end

def call_float(f):
    assert(f.is_a? Float)
    $ok += 1
end

def call_int(i):
    assert(i.is_a? Integer)
    $ok += 1
end

def call_string(s):
    assert(s.is_a? String)
    $ok += 1
end

def call_int_and_float_and_string(i,f,s):
    assert(i.is_a? Integer)
    assert(f.is_a? Float)
    assert(s.is_a? String)
    $ok += 1
end

def call_boolean_and_array(b,a):
    assert(b.is_a? TrueClass or b.is_a? FalseClass)
    assert(a.is_a? Array)
    $ok += 1
end

def test():
    assert($ok == 8)
    return "ok"
end

