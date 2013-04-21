count = 0

def test(*args, **kwargs):
    global count
    count += 1
    if(len(args)+len(kwargs) == 0):
        return "ok"

