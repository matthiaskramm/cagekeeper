all: spec/run testlua testruby testpython testjs libcagekeeper.a

PTMALLOC_CFLAGS=-DUSE_DL_PREFIX -DONLY_MSPACES -DMSPACES -DHAVE_MMAP=0 -DHAVE_REMAP=0 -DHAVE_MORECORE=1 -DMORECORE=sandbox_sbrk -DNO_MALLINFO=1
LIBFFI_CFLAGS := $(shell pkg-config --cflags libffi)
PYTHON_CFLAGS=-I/usr/include/python2.7

RUBY_CFLAGS=-I/usr/lib/ruby/1.8/i686-linux -D_FILE_OFFSET_BITS=64 -fno-strict-aliasing
RUBY_LDFLAGS=-Wl,-O1 -rdynamic -Wl,-export-dynamic
RUBY_LIBS=-lruby18 -lz -ldl -lcrypt -lm -lc

PYTHON_LIBS=-lpython2.7 -lpthread
LUA_LIBS=-llua
JS_LIBS=-lmozjs185
FFI_LIBS:=$(shell pkg-config --libs libffi)

MEMWRAP=-Wl,--wrap=malloc \
	-Wl,--wrap=free \
	-Wl,--wrap=calloc \
	-Wl,--wrap=realloc \
	-Wl,--wrap=memalign \
	-Wl,--wrap=strdup \
	-Wl,--wrap=valloc

CC=gcc -g -fPIC $(RUBY_CFLAGS) $(RUBY_LDFLAGS) -Wl,--export-dynamic $(MEMWRAP) $(LIBFFI_CFLAGS)
CXX=$(CC)

LIBS=$(JS_LIBS) $(LUA_LIBS) $(PYTHON_LIBS) $(FFI_LIBS) $(RUBY_LIBS)

OBJECTS=function.o dict.o language_js.o language_py.o language_lua.o language_rb.o language_proxy.o language.o util.o seccomp.o mem.o ptmalloc/malloc.o

spec/run: spec/run.o $(OBJECTS)
	$(CC) spec/run.o $(OBJECTS) $(LIBS) -o $@

testpython: testpython.o $(OBJECTS)
	$(CC) testpython.o $(OBJECTS) $(LIBS) -o $@

testlua: testlua.o $(OBJECTS)
	$(CC) testlua.o $(OBJECTS) $(LIBS) -o $@

testruby: testruby.o $(OBJECTS)
	$(CC) testruby.o $(OBJECTS) $(LIBS) -o $@ $(RUBY_LDFLAGS) $(RUBY_LIBS) 

testjs: testjs.o $(OBJECTS)
	$(CC) testjs.o $(OBJECTS) $(LIBS) -o $@ $(RUBY_LDFLAGS) $(RUBY_LIBS) 

ptmalloc/malloc.o: ptmalloc/malloc.c ptmalloc/malloc-2.8.3.h
	$(CC) -c $(PTMALLOC_CFLAGS) -Iptmalloc ptmalloc/malloc.c -o $@
ptmalloc/ptmalloc3.o: ptmalloc/ptmalloc3.c ptmalloc/malloc-2.8.3.h
	$(CC) -c $(PTMALLOC_CFLAGS) -Iptmalloc ptmalloc/ptmalloc3.c -o $@

mem.o: mem.c
	$(CC) -c $(PTMALLOC_CFLAGS) mem.c -o $@

seccomp.o: seccomp.c
	$(CC) -c $(PTMALLOC_CFLAGS) seccomp.c -o $@

dict.o: dict.c language.h
	$(CC) -c dict.c

util.o: util.c util.h
	$(CC) -c util.c

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
	rm -f *.so *.o testpython ptmalloc/*.o

test:
	./run_specs -a

.PHONY: all clean
