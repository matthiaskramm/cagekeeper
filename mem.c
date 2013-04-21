#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <memory.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include "ptmalloc/malloc-2.8.3.h"

#ifdef DEBUG_MEMORY
char* dbg(const char*format, ...)
{
    va_list arglist;
    va_start(arglist, format);
    vfprintf(stdout, format, arglist);
    printf("\n");
    va_end(arglist);
}
#else
#define dbg(f,...) do {} while(0)
#endif

extern void* __libc_malloc(size_t size);
extern void __libc_free(void*ptr);
extern void *__libc_calloc(size_t nmemb, size_t size);
extern void *__libc_realloc(void *ptr, size_t size);
extern void *__libc_memalign(size_t size, size_t align);
extern void *__libc_valloc(size_t size);

static bool lockdown = false;
static mspace msp;

void init_mem_wrapper(size_t size)
{
    lockdown = true;
    void*mem =  __libc_malloc(size);
    msp = create_mspace_with_base(mem, size, 0);
}

void *__wrap_malloc(size_t size)
{
    if(lockdown) {
        return mspace_malloc(msp, size);
    } else {
        dbg("malloc(%d)\n", size);
        void*ptr = __libc_malloc(size);
        if(!ptr) {
            write(2, "Out of memory\n", 14);
            _exit(5);
        }
        return ptr;
    }
}

void __wrap_free(void*ptr)
{
    if(lockdown) {
        return mspace_free(msp, ptr);
    } else {
        dbg("free()\n");
        __libc_free(ptr);
    }
}

void *__wrap_calloc(size_t nmemb, size_t size)
{
    if(lockdown) {
        return mspace_calloc(msp, nmemb, size);
    } else {
        dbg("calloc(%d)\n", size);
        void*ptr = __libc_calloc(nmemb, size);
        if(!ptr) {
            write(2, "Out of memory\n", 14);
            _exit(5);
        }
        return ptr;
    }
}

void *__wrap_realloc(void *_ptr, size_t size)
{
    if(lockdown) {
        return mspace_realloc(msp, _ptr, size);
    } else {
        dbg("realloc(%d)\n", size);
        void*ptr = __libc_realloc(_ptr, size);
        if(!ptr) {
            write(2, "Out of memory\n", 14);
            _exit(5);
        }
        return ptr;
    }
}

void *__wrap_memalign(size_t size, size_t align)
{
    if(lockdown) {
        return mspace_memalign(msp, size, align);
    } else {
        dbg("memalign(%d, align)\n", size, align);
        void*ptr = __libc_memalign(size, align);
        if(!ptr) {
            write(2, "Out of memory\n", 14);
            _exit(5);
        }
        return ptr;
    }
}

void*sandbox_sbrk(intptr_t len)
{
    if(lockdown) {
        write(2, "Out of memory\n", 14);
        errno = ENOMEM;
        _exit(5);
        return NULL;
    } else {
        //return sbrk(size);
        write(2, "Out of memory\n", 14);
        errno = ENOMEM;
        _exit(5);
        return NULL;
    }
}

_IO_size_t _IO_fwrite (const void *buf, _IO_size_t size, _IO_size_t count, _IO_FILE *fp);
size_t __wrap_fwrite(const void*data, size_t size, size_t num, FILE*fi)
{
    if(lockdown) {
    } else {
        return _IO_fwrite(data, size, num, fi);
    }
}
