all: spec/run testbrk testlua testruby testpython testjs libcagekeeper.a

PTMALLOC_CFLAGS=-DUSE_DL_PREFIX -DONLY_MSPACES -DMSPACES -DHAVE_MMAP=0 -DHAVE_REMAP=0 -DHAVE_MORECORE=1 -DMORECORE=sandbox_sbrk -DNO_MALLINFO=1

FFI_CFLAGS := $(shell pkg-config --cflags libffi)
FFI_LIBS:=$(shell pkg-config --libs libffi)

RUBY_CFLAGS=-I/usr/lib/ruby/1.8/i686-linux -D_FILE_OFFSET_BITS=64 -fno-strict-aliasing
RUBY_LDFLAGS=-Wl,-O1 -rdynamic -Wl,-export-dynamic
RUBY_LIBS=-lruby18 -lz -ldl -lcrypt -lm -lc

PYTHON_CFLAGS=-I/usr/include/python2.7
PYTHON_LDFLAGS=
PYTHON_LIBS=-lpython2.7 -lpthread

LUA_CFLAGS=
LUA_LDFLAGS=
LUA_LIBS=-llua

JS_CFLAGS=-I/usr/include/js/ -I/usr/local/include/js/ 
JS_LDFLAGS=
JS_LIBS=-lmozjs185

LDFLAGS=$(RUBY_LDFLAGS) $(PYTHON_LDFLAGS) $(LUA_LDFLAGS) $(JS_LDFLAGS) $(FFI_LDFLAGS) -Wl,--export-dynamic 
LIBS=$(RUBY_LIBS) $(PYTHON_LIBS) $(LUA_LIBS) $(JS_LIBS) $(FFI_LIBS) -lstdc++

CC=gcc -g -fPIC $(RUBY_CFLAGS) $(PYTHON_CFLAGS) $(LUA_CFLAGS) $(JS_CFLAGS) $(FFI_CFLAGS)
LINK=$(CC) $(LDFLAGS)
CXX=$(CC)

OBJECTS=function.o dict.o language_js.o language_py.o language_lua.o language_rb.o language_proxy.o language.o util.o settings.o seccomp.o ptmalloc/malloc.o
INCLUDES=function.h dict.h language.h

spec/run: spec/run.o $(INCLUDES) $(OBJECTS)
	$(LINK) spec/run.o $(OBJECTS) $(LIBS) -o $@

testbrk: testbrk.o
	gcc testbrk.o -o $@

testpython: testpython.o $(OBJECTS)
	$(LINK) testpython.o $(OBJECTS) $(LIBS) -o $@

testlua: testlua.o $(OBJECTS)
	$(LINK) testlua.o $(OBJECTS) $(LIBS) -o $@

testruby: testruby.o $(OBJECTS)
	$(LINK) testruby.o $(OBJECTS) $(LIBS) -o $@ $(RUBY_LDFLAGS) $(RUBY_LIBS)

testjs: testjs.o $(OBJECTS)
	$(LINK) testjs.o $(OBJECTS) $(LIBS) -o $@ $(RUBY_LDFLAGS) $(RUBY_LIBS) 

ptmalloc/malloc.o: ptmalloc/malloc.c ptmalloc/malloc-2.8.3.h
	$(CC) -c $(PTMALLOC_CFLAGS) -Iptmalloc ptmalloc/malloc.c -o $@

seccomp.o: seccomp.c
	$(CC) -c $(PTMALLOC_CFLAGS) seccomp.c -o $@

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
	$(CC) -c language_js.c

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
	rm -f *.so *.o testpython ptmalloc/*.o spec/run spec/run.o

test:
	./run_specs -a

.PHONY: all clean
