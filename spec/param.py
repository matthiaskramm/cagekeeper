class Count:
    ok = 0

def call_noargs(*args, **kwargs):
    assert(len(args)+len(kwargs) == 0)
    Count.ok += 1

def call_array(a):
    assert(type(a) == list)
    Count.ok += 1

def call_boolean(b):
    assert(type(b) == bool)
    Count.ok += 1

def call_float(f):
    assert(type(f) == float)
    Count.ok += 1

def call_int(i):
    assert(type(i) == int)
    Count.ok += 1

def call_string(s):
    assert(type(s) == str or type(s) == unicode)
    Count.ok += 1

def call_int_and_float_and_string(i,f,s):
    assert(type(i) == int)
    assert(type(f) == float)
    assert(type(s) == str or type(s) == unicode)
    Count.ok += 1

def call_boolean_and_array(b,a):
    assert(type(b) == bool)
    assert(type(a) == list)
    Count.ok += 1

def test():
    assert(Count.ok == 8)
    return "ok"

