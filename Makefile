all: spec/run testbrk testlua testruby testpython testjs libcagekeeper.a

LIBFFI_CFLAGS := $(shell pkg-config --cflags libffi)
PYTHON_CFLAGS=-I/usr/include/python2.7

RUBY_CFLAGS=-I/usr/lib/ruby/1.8/i686-linux -D_FILE_OFFSET_BITS=64 -fno-strict-aliasing
RUBY_LDFLAGS=-Wl,-O1 -rdynamic -Wl,-export-dynamic
RUBY_LIBS=-lruby18 -lz -ldl -lcrypt -lm -lc

PYTHON_LIBS=-lpython2.7 -lpthread
LUA_LIBS=-llua
JS_LIBS=-lmozjs185
FFI_LIBS:=$(shell pkg-config --libs libffi)

CC=gcc -g -fPIC $(RUBY_CFLAGS) $(RUBY_LDFLAGS) -Wl,--export-dynamic $(LIBFFI_CFLAGS)
CXX=$(CC)

LIBS=$(JS_LIBS) $(LUA_LIBS) $(PYTHON_LIBS) $(FFI_LIBS) $(RUBY_LIBS)

OBJECTS=function.o dict.o language_js.o language_py.o language_lua.o language_rb.o language_proxy.o language.o util.o settings.o seccomp.o
INCLUDES=function.h dict.h language.h

spec/run: spec/run.o $(INCLUDES) $(OBJECTS)
	$(CC) spec/run.o $(OBJECTS) $(LIBS) -o $@

testbrk: testbrk.o
	gcc testbrk.o -o $@

testpython: testpython.o $(OBJECTS)
	$(CC) testpython.o $(OBJECTS) $(LIBS) -o $@

testlua: testlua.o $(OBJECTS)
	$(CC) testlua.o $(OBJECTS) $(LIBS) -o $@

testruby: testruby.o $(OBJECTS)
	$(CC) testruby.o $(OBJECTS) $(LIBS) -o $@ $(RUBY_LDFLAGS) $(RUBY_LIBS)

testjs: testjs.o $(OBJECTS)
	$(CC) testjs.o $(OBJECTS) $(LIBS) -o $@ $(RUBY_LDFLAGS) $(RUBY_LIBS) 

seccomp.o: seccomp.c
	$(CC) -c seccomp.c -o $@

dict.o: dict.c language.h
	$(CC) -c dict.c

util.o: util.c util.h
	$(CC) -c util.c

settings.o: settings.c settings.h
	$(CC) -c settings.c

function.o: function.c function.h
	$(CC) -c function.c

language.o: language.c language.h
	$(CC) -c language.c

language_proxy.o: language_proxy.c language.h
	$(CC) -c language_proxy.c

language_js.o: language_js.c language.h
	$(CC) -c -I/usr/include/js/ -I/usr/local/include/js/ language_js.c

language_py.o: language_py.c language.h
	$(CC) $(PYTHON_CFLAGS) -c language_py.c

language_rb.o: language_rb.c language.h
	$(CC) -c -I /usr/lib/ruby/1.8/i686-linux language_rb.c

language_lua.o: language_lua.c language.h
	$(CC) -c language_lua.c

libcagekeeper.a: $(OBJECTS)
	ar cru $@ $(OBJECTS)
	ranlib $@

clean:
	rm -f *.so *.o testpython spec/run spec/run.o

test:
	./run_specs -a

.PHONY: all clean
