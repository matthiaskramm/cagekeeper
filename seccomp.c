#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <memory.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/prctl.h>
#include <sys/signal.h>
#include <asm/unistd_32.h>
#define __USE_GNU
#include <dlfcn.h>

#define HIJACK_SYSCALLS
#define LOG_SYSCALLS
#define REFUSE_UNSUPPORTED

#ifdef HIJACK_SYSCALLS
static ssize_t my_write(int handle, void*data, int length) {
    if(length < 0)
        length = strlen(data);

    int ret;
    asm(
        "mov %1, %%edx\n" // len
        "mov %2, %%ecx\n" // message
        "mov %3, %%ebx\n" // fd
        "mov %4, %%eax\n" // sys_write
        "int $0x080\n"
        : "=ra" (ret)
        : "m" (length),
          "m" (data),
          "m" (handle),
          "i" (__NR_write)
        );
    return ret;
}

char* dbg(const char*format, ...)
{
    static char buffer[256];
    va_list arglist;
    va_start(arglist, format);
    int length = vsnprintf(buffer, sizeof(buffer), format, arglist);
    va_end(arglist);
    my_write(1, buffer, length);
    if(length && buffer[length-1] != '\n')
        my_write(1, "\n", 1);
}

static void _syscall_log(int edi, int esi, int edx, int ecx, int ebx, int eax) {
    dbg("syscall eax=%d ebx=%d ecx=%d edx=%d esi=%d edi=%d\n", 
            eax, ebx, ecx, edx, esi, edi);
    if(eax == __NR_open) {
        dbg("\topen: %s\n", (char*)ebx);
    }
}

static void (*syscall_log)() = _syscall_log;
static int *errno_location;

void do_syscall();

void _dummy(void) {
    asm(
"do_syscall:\n"
        //"movl (%%ebp), %%ebp\n" // ignore the gcc prologue
#ifdef LOG_SYSCALLS
        /* log system call */    
	"pushl %%ebp\n"
	"pushl %%eax\n"
	"pushl %%ebx\n"
	"pushl %%ecx\n"
	"pushl %%edx\n"
	"pushl %%esi\n"
	"pushl %%edi\n"
        "call _syscall_log\n"
	"popl %%edi\n"
	"popl %%esi\n"
	"popl %%edx\n"
	"popl %%ecx\n"
	"popl %%ebx\n"
	"popl %%eax\n"
	"popl %%ebp\n"
#endif

#ifdef REFUSE_UNSUPPORTED
        /* consult blacklist */
        "cmp $197, %%eax\n" // fstat64
        "je refuse\n"
        "cmp $192, %%eax\n" // mmap2
        "je refuse\n"
        "cmp $175, %%eax\n" // rt_sigprocmask
        "je refuse\n"
        "cmp $270, %%eax\n" // tgkill
        "je refuse\n"
        "cmp $174, %%eax\n" // rt_sigaction
        "je refuse\n"
        "cmp $5, %%eax\n " // open
        "je refuse\n"
        "jmp forward\n"
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

static void handle_signal(int signal, siginfo_t*siginfo, void*ucontext)
{
    Dl_info info; 
    dbg("signal %d, at %p\n", signal, info.dli_saddr);
    void*here;
    void**stack_top = &here;
    int i = 0;
    for(i=0;i<256;i++) {
        if(dladdr(*stack_top, &info) && info.dli_saddr && info.dli_sname) {
            dbg("%010p %16s:%s\n", info.dli_saddr, info.dli_fname, info.dli_sname);
        }
        stack_top++;
    }

    _exit(signal);
}

static struct sigaction sig;

void seccomp_lockdown()
{
    sig.sa_sigaction = handle_signal;
    sig.sa_flags = SA_SIGINFO;
    sigaction(11, &sig, NULL);
    sigaction(6, &sig, NULL);

#ifdef HIJACK_SYSCALLS
    errno_location = __errno_location();
    hijack_linux_gate();
#endif

    int ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_STRICT, 0, 0, 0);
    if(ret) {
        fprintf(stderr, "could not enter secure computation mode\n");
        perror("prctl");
        _exit(1);
    }
}

