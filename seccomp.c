#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <memory.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/prctl.h>
#include <sys/signal.h>
#include <sys/mman.h>
#include <asm/unistd_32.h>
#define __USE_GNU
#include <dlfcn.h>

#include "settings.h"
#include "util.h"
#include "ptmalloc/malloc-2.8.3.h"

#define CATCH_SIGNALS
#define HIJACK_SYSCALLS
//#define LOG_SYSCALLS
//#define LOG_SYSCALL_RETURN_VALUES
#define REFUSE_UNSUPPORTED

#define SAVE_REGS \
	"pushl %%ebp\n" \
	"pushl %%eax\n" \
	"pushl %%ebx\n" \
	"pushl %%ecx\n" \
	"pushl %%edx\n" \
	"pushl %%esi\n" \
	"pushl %%edi\n"

#define RESTORE_REGS \
	"popl %%edi\n" \
	"popl %%esi\n" \
	"popl %%edx\n" \
	"popl %%ecx\n" \
	"popl %%ebx\n" \
	"popl %%eax\n" \
	"popl %%ebp\n"

#ifdef HIJACK_SYSCALLS
static ssize_t my_write(int handle, void*data, int length) {
    if(length < 0)
        length = strlen(data);

    int ret;
    asm(
        SAVE_REGS
        "mov %1, %%edx\n" // len
        "mov %2, %%ecx\n" // message
        "mov %3, %%ebx\n" // fd
        "mov %4, %%eax\n" // sys_write
        "int $0x080\n"
        RESTORE_REGS
        : "=ra" (ret)
        : "m" (length),
          "m" (data),
          "m" (handle),
          "i" (__NR_write)
        );
    return ret;
}

static int direct_brk(int addr)
{
    int ret;
    asm(
        "push %%ebx\n"
        "mov %1, %%ebx\n" // addr
        "mov %2, %%eax\n" // syscall nr
        "int $0x080\n"
        "pop %%ebx\n"
        "mov %%eax, %0\n"
        : "=r" (ret)
        : "m" (addr),
          "i" (__NR_brk));
    return ret;
}

static void direct_exit(int code)
{
    int ret;
    asm(
        "push %%ebx\n"
        "mov %1, %%ebx\n" // code
        "mov %2, %%eax\n" // sys_exit
        "int $0x080\n"
        "pop %%ebx\n"
        "mov %%eax, %0\n"
        : "=r" (ret)
        : "m" (code),
          "i" (__NR_exit));
}


char* dbg_write(const char*format, ...)
{
    static char buffer[256];
    va_list arglist;
    va_start(arglist, format);
    int length = vsnprintf(buffer, sizeof(buffer), format, arglist);
    va_end(arglist);

#if 0
    printf("%s", buffer);
#else
    if(length<0 || length>=sizeof(buffer)) {
        my_write(1, "vsnprintf failed\n", 17);
        return;
    }
    my_write(1, buffer, length);
    if(length>0 && buffer[length-1] != '\n')
        my_write(1, "\n", 1);
#endif
}

static int32_t current_brk = 0;
static int32_t max_brk = 0;

static void*msp_ptr = NULL;
static size_t msp_size = 0;
static mspace msp;

static void init_memory(size_t size)
{
    msp_ptr  = mmap(NULL, size, PROT_READ|PROT_WRITE|PROT_EXEC, MAP_PRIVATE|MAP_ANON, -1, 0);
    if(!msp_ptr) {
        dbg_write("OUT OF MEMORY\n");
        _exit(7);
    }
    msp_size = size;
    msp = create_mspace_with_base(msp_ptr, msp_size, 0);
    dbg("mem space: %p - %p\n", msp_ptr, msp_ptr+msp_size);
}

static int dealloc_memory(int addr)
{
    dbg("deallocating memory: %08x\n", addr);
    mspace_free(msp, (void*)addr);
    return 0;
}

static void oom()
{
    dbg_write("OUT OF MEMORY\n");
    _exit(7);
}

static int alloc_memory(int size)
{
    if(size == 1<<20) {
        /* spidermonkey does "trial and error" allocations until
           a piece of memory is aligned. See js/src/jsgcchunk.cpp. 
           To prevent that, we proactively aligned memory.
         */
        void*ptr = mspace_memalign(msp, size, size);
        if(!ptr) {
            dbg_write("out of memory while allocating %d aligned bytes\n", size);
            return -1;
        }
        memset(ptr, 0, size);
        dbg("allocating %d bytes of aligned memory: %08x\n", size, ptr);
        return ptr;
    }

    void*ptr = mspace_calloc(msp, size, 1);
    if(!ptr) {
        dbg_write("out of memory while allocating %d bytes\n", size);
        return -1;
    }
    dbg("allocating %d bytes of memory: %08x\n", size, ptr);
    return (int)ptr;
}

static void _syscall_log(int edi, int esi, int edx, int ecx, int ebx, int eax) {
    dbg_write("syscall eax=%d ebx=%d ecx=%d edx=%d esi=%d edi=%d\n", 
            eax, ebx, ecx, edx, esi, edi);
    if(eax == __NR_open) {
        dbg_write("\topen: %s\n", (char*)ebx);
    }
}

static void _syscall_log_return_value(int edi, int esi, int edx, int ecx, int ebx, int eax) {
    dbg_write("syscall return: eax=%d eax=%08x ebx=%d ecx=%d edx=%d esi=%d edi=%d\n", 
            eax, eax, ebx, ecx, edx, esi, edi);
}

static void (*syscall_log)() = _syscall_log;
static int *errno_location;

void do_syscall();

#ifdef NATIVE_WRITE
asm(
".section .data\n"
"sc_msg1: .string \"syscall \"\n"
"sc_msg2: .string \"\\n\"\n"
"digit: .string \".\"\n"
"number: .string \"4294967296\"\n"
"number_end: .string \"\\n\"\n"
);
#endif

void _dummy(void) {
    asm(

"do_syscall:\n"
        //"movl (%%ebp), %%ebp\n" // ignore the gcc prologue
        //
#ifdef LOG_SYSCALLS
        SAVE_REGS

#ifdef NATIVE_WRITE
	"pushl %%eax\n"
        "mov $8, %%edx\n" // len
        "mov $sc_msg1, %%ecx\n" // message
        "mov $1, %%ebx\n" // fd
        "mov $4, %%eax\n" // sys_write
        "int $0x080\n"
	"popl %%eax\n"

	"mov $number_end, %%esi\n"
	"xor %%edi, %%edi\n"
        "jmp convert\n"
"convert_loop:\n"
	"dec %%esi\n"
"convert:\n"
	"xor %%edx, %%edx\n"
	"mov $10, %%ecx\n"
        "div %%ecx, %%eax\n"
        "add $0x30, %%dl\n"
        "mov %%dl, (%%esi)\n"
	"inc %%edi\n"
        "test %%eax, %%eax\n"
        "jnz convert_loop\n"
        "mov %%edi, %%edx\n" // len
        "mov %%esi, %%ecx\n" // message
        "mov $1, %%ebx\n" // fd
        "mov $4, %%eax\n" // sys_write
        "int $0x080\n"

        "mov $1, %%edx\n" // len
        "mov $sc_msg2, %%ecx\n" // message
        "mov $1, %%ebx\n" // fd
        "mov $4, %%eax\n" // sys_write
        "int $0x080\n"
#else
        "call _syscall_log\n"
#endif
        RESTORE_REGS
#endif

#ifdef REFUSE_UNSUPPORTED
        /* consult blacklist */
        "cmp $197, %%eax\n" // fstat64
        "je refuse\n"
        "cmp $192, %%eax\n" // mmap2
        "je fake_mmap2\n"
        "cmp $91, %%eax\n"  // munmap
        "je fake_munmap\n"
        "cmp $270, %%eax\n" // tgkill
        "je refuse\n"
        "cmp $174, %%eax\n" // rt_sigaction
        "je refuse\n"
        "cmp $5, %%eax\n "  // open
        "je refuse\n"
        "cmp $172, %%eax\n" // prctl
        "je forward\n"
        "cmp $1, %%eax\n"   // exit
        "je forward\n"
        "cmp $3, %%eax\n"   // read
        "je forward\n"
        "cmp $4, %%eax\n"   // write
        "je forward\n"
        "cmp $119, %%eax\n" // sigreturn
        "je forward\n"
        "cmp $45, %%eax\n"  // brk
        "je fake_brk\n"
        
        "cmp $175, %%eax\n" // rt_sigprocmask
        "je refuse\n"
        "cmp $126, %%eax\n" // sigprocmask
        "je refuse\n"
        "jmp refuse\n"

"fake_munmap:\n"
	"pushl %%ebx\n"
	"pushl %%ecx\n"
	"pushl %%edx\n"
        " pushl %%ebx\n"
        // notice: we ignore the size argument
        " call dealloc_memory\n"
	" add $4, %%esp\n"
	"popl %%edx\n"
	"popl %%ecx\n"
        "popl %%ebx\n"
        "jmp exit\n"

"fake_mmap2:\n"
        "test %%ebx, %%ebx\n" // ebx: address
        "jnz refuse\n"
	"pushl %%ebx\n"
	"pushl %%ecx\n"
	"pushl %%edx\n"
        " pushl %%ecx\n"
        " call alloc_memory\n"
	" add $4, %%esp\n"
	"popl %%edx\n"
	"popl %%ecx\n"
        "popl %%ebx\n"
        "jmp exit\n"
        
"fake_brk:\n"
        "push %%ebx\n"
        "cmp current_brk, %%ebx\n"
        "jle fake_brk_done\n"
        "cmp max_brk, %%ebx\n"
        "jle fake_brk_below_max\n"
        "mov max_brk, %%ebx\n"
"fake_brk_below_max:\n"
        "mov %%ebx, current_brk\n"
"fake_brk_done:\n"
        "mov current_brk, %%eax\n"
        "pop %%ebx\n"
        "jmp exit\n"

"refuse:\n"

#ifdef SET_ERRNO
        "push %%ebx\n"
        "mov $12, %%eax\n" // ENOMEM
        "mov %1, %%ebx\n"  // errno_location
        "mov %%eax, (%%ebx)\n"
        "pop %%ebx\n"
#endif
        "mov $-1, %%eax\n" // return value
        "jmp exit\n"

"forward:\n"
#endif

        /* make the syscall */
        "int $0x080\n"

"exit:\n"

#ifdef LOG_SYSCALLS
#ifdef LOG_SYSCALL_RETURN_VALUES
        SAVE_REGS
        "call _syscall_log_return_value\n"
        RESTORE_REGS
#endif
#endif

        "ret\n"
        : 
	: "m" (syscall_log),
	  "m" (errno_location)
        );
}

static void*old_syscall_handler = (void*)0x12345678;

static void hijack_linux_gate(void) {
    // all your system calls are belong to us!
    asm("mov %%gs:0x10, %%eax\n"
        "mov %%eax, %0\n"

        "mov $do_syscall, %%eax\n"
        "mov %%eax, %%gs:0x10\n"

        : "=m" (old_syscall_handler)
        : "r" (&do_syscall)
        : "eax");
};
#endif

#ifndef SECCOMP_MODE_STRICT
#define SECCOMP_MODE_STRICT 1
#endif

#ifdef CATCH_SIGNALS
static void handle_signal(int signal, siginfo_t*siginfo, void*ucontext)
{
    my_write(1, "signal\n", 7);
    Dl_info info; 
    dbg_write("signal %d, memory access to addr: %p\n", signal);
    void*here;
    void**stack_top = &here;
    int i = 0;

    for(i=0;i<256;i++) {
        if(dladdr(*stack_top, &info) && info.dli_saddr && info.dli_sname) {
            dbg_write("%010p %16s:%s\n", info.dli_saddr, info.dli_fname, info.dli_sname);
        }
        stack_top++;
    }

    direct_exit(signal);
}
static struct sigaction sig;
#endif

void*sandbox_sbrk(intptr_t len)
{
    dbg_write("Out of memory (not allowing sbrk)\n");
    return NULL;
}

void seccomp_lockdown()
{
#ifdef CATCH_SIGNALS
    sig.sa_sigaction = handle_signal;
    sig.sa_flags = SA_SIGINFO;
    sigaction(11, &sig, NULL);
    sigaction(6, &sig, NULL);
#endif

    current_brk = direct_brk(0);
    max_brk = current_brk + (config_maxmem * 2);
    int ret = direct_brk(max_brk);
    if(ret < max_brk) {
        fprintf(stderr, "Could not expand process data segment (brk)\n");
        _exit(1);
    }

    init_memory(config_maxmem * 2);

    dbg("extra process space: brk: %p - %p\n", (void*)current_brk, (void*)max_brk);

#ifdef HIJACK_SYSCALLS
    errno_location = __errno_location();
    hijack_linux_gate();
#endif

    ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_STRICT, 0, 0, 0);
    if(ret) {
        fprintf(stderr, "could not enter secure computation mode\n");
        perror("prctl");
        _exit(1);
    }
}

