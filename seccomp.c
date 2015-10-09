#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <memory.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/prctl.h>
#include <sys/signal.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <asm/unistd_32.h>
#define __USE_GNU
#include <dlfcn.h>

#include <linux/filter.h>
#include <linux/audit.h>
#include "seccomp_defs.h"

#include "settings.h"
#include "util.h"

#define CATCH_SIGNALS
//#define HIJACK_SYSCALLS
//#define LOG_SYSCALLS
//#define LOG_SYSCALL_RETURN_VALUES

#define MEM_PAD 65536

#define SAVE_REGS \
	"push %%rbp\n" \
	"push %%rax\n" \
	"push %%rbx\n" \
	"push %%rcx\n" \
	"push %%rdx\n" \
	"push %%rsi\n" \
	"push %%rdi\n"

#define RESTORE_REGS \
	"pop %%rdi\n" \
	"pop %%rsi\n" \
	"pop %%rdx\n" \
	"pop %%rcx\n" \
	"pop %%rbx\n" \
	"pop %%rax\n" \
	"pop %%rbp\n"

#ifdef HIJACK_SYSCALLS
static long my_write(long handle, void*data, long length) {
    if(length < 0)
        length = strlen(data);

    long ret;
    asm(
        SAVE_REGS
        "mov %1, %%rdx\n" // len
        "mov %2, %%rcx\n" // message
        "mov %3, %%rbx\n" // fd
        "mov %4, %%rax\n" // sys_write
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

static void direct_exit(long code)
{
    long ret;
    asm(
        "push %%rbx\n"
        "mov %1, %%rbx\n" // code
        "mov %2, %%rax\n" // sys_exit
        "int $0x080\n"
        "pop %%rbx\n"
        "mov %%rax, %0\n"
        : "=r" (ret)
        : "m" (code),
          "i" (__NR_exit));
}


void stdout_printf(const char*format, ...)
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

static void _syscall_log(long rdi, long rsi, long rdx, long rcx, long rbx, long rax) 
{
    if(rax == __NR_gettimeofday)
        return;

    stdout_printf("syscall rax=%d rbx=%d rcx=%d rdx=%d rsi=%d rdi=%d\n", 
            rax, rbx, rcx, rdx, rsi, rdi);
    if(rax == __NR_open) {
        stdout_printf("\topen: %s\n", (char*)rbx);
    }
}

static void _syscall_log_return_value(long rdi, long rsi, long rdx, long rcx, long rbx, long rax, long ebp, long call) 
{
    if(call == __NR_gettimeofday)
        return;

    stdout_printf("syscall return: rax=%d rax=%08x rbx=%d rcx=%d rdx=%d rsi=%d rdi=%d\n", 
            rax, rax, rbx, rcx, rdx, rsi, rdi);
}

static bool refused[512];
static void _syscall_refuse(long rdi, long rsi, long rdx, long rcx, long rbx, long rax) 
{
    if(rax>=512 || rax<0) {
        log_err("[sandbox] Illegal/Unknown syscall %d", rax);
        return;
    }
    if(!refused[rax]) {
        log_warn("[sandbox] Refusing system call %d", rax);
        refused[rax] = true;
    }
    if(rax == __NR_open) {
        stdout_printf("refusing open: %s\n", (char*)rbx);
    }
}

static void (*syscall_log)() = _syscall_log;
static int *errno_location;

void do_syscall();

static void _dummy() {
asm(
"do_syscall:\n"
        //"movl (%%ebp), %%ebp\n" // ignore the gcc prologue
        //
#ifdef LOG_SYSCALLS
        SAVE_REGS
        "call _syscall_log\n"
        RESTORE_REGS
        "push %%rax\n"
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
#ifdef LOG_SYSCALLS
        "add $4, %%esp\n"
#endif
        "ret\n"
        : 
	: "m" (syscall_log),
	  "m" (errno_location)
);}

static void*old_syscall_handler = (void*)0x12345678;

static void hijack_linux_gate(void) {
    // all your system calls are belong to us!
    asm("mov %%gs:0x10, %%rax\n"
        "mov %%rax, %0\n"

        "mov $do_syscall, %%rax\n"
        "mov %%rax, %%gs:0x10\n"

        : "=m" (old_syscall_handler)
        : "r" (&do_syscall)
        : "rax");
};
#endif

#ifdef CATCH_SIGNALS
static void handle_signal(int signal, siginfo_t*siginfo, void*ucontext)
{
    void*here;
#ifdef HIJACK_SYSCALLS
    my_write(1, "signal\n", 7);
#else
    printf("signal\n");
#endif
    Dl_info info;
#ifdef HIJACK_SYSCALLS
    stdout_printf("signal %d, memory access to addr: %p\n", signal);
#endif
    void**stack_top = &here;
    int i = 0;

#ifdef HIJACK_SYSCALLS
    for(i=0;i<256;i++) {
        if(dladdr(*stack_top, &info)) {
            const char* symbol_name = info.dli_sname;
            if(!symbol_name)
                symbol_name = "?";
            stdout_printf("[%3d] %08x %16s:%s\n", i, *stack_top, info.dli_fname, symbol_name);
        }
        stack_top++;
    }
#endif

#ifdef HIJACK_SYSCALLS
    direct_exit(signal);
#else
    _exit(signal);
#endif
}
static struct sigaction sig;
#endif

void seccomp_lockdown()
{
    setenv("MALLOC_CHECK_", "0", 1);

#ifdef CATCH_SIGNALS
    sig.sa_sigaction = handle_signal;
    sig.sa_flags = SA_SIGINFO;
    sigaction(11, &sig, NULL);
    sigaction(6, &sig, NULL);
#endif

    struct rlimit rlimit;
    rlimit.rlim_cur = rlimit.rlim_max = config_maxmem + MEM_PAD;
    setrlimit(RLIMIT_DATA, &rlimit);

#ifdef HIJACK_SYSCALLS
    errno_location = __errno_location();
    hijack_linux_gate();
#endif

    /* offset k=0: syscall number
              k=4: architecture 
     */
    struct sock_filter seccomp_filter[] = {
        {code: BPF_LD+BPF_W+BPF_ABS,  jt: 0, jf: 0, k: 4},
        {code: BPF_JMP+BPF_JEQ+BPF_K, jt: 1, jf: 0, k: AUDIT_ARCH_I386},
        {code: BPF_RET+BPF_K,         jt: 0, jf: 0, k: SECCOMP_RET_KILL},

#define ALLOW_ANYARGS(syscall_nr) \
        {code: BPF_LD+BPF_W+BPF_ABS,  jt: 0, jf: 0, k: 0}, \
        {code: BPF_JMP+BPF_JEQ+BPF_K, jt: 0, jf: 1, k: syscall_nr}, \
        {code: BPF_RET+BPF_K,         jt: 0, jf: 0, k: SECCOMP_RET_ALLOW}

        ALLOW_ANYARGS(__NR_gettimeofday),
        ALLOW_ANYARGS(__NR_time),
        ALLOW_ANYARGS(__NR_read),
        ALLOW_ANYARGS(__NR_readv),
        ALLOW_ANYARGS(__NR_write),
        ALLOW_ANYARGS(__NR_writev),
        ALLOW_ANYARGS(__NR_brk),
        ALLOW_ANYARGS(__NR_mmap2),
        ALLOW_ANYARGS(__NR_munmap),
        ALLOW_ANYARGS(__NR_futex),
        ALLOW_ANYARGS(__NR_sigprocmask),
        ALLOW_ANYARGS(__NR_exit),

        {code: BPF_RET+BPF_K,         jt: 0, jf: 0, k: SECCOMP_RET_ERRNO | 1},
    };
    struct sock_fprog seccomp_prog = {
        len: sizeof(seccomp_filter) / sizeof(seccomp_filter[0]),
        filter: seccomp_filter,
    };

    int ret = !prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0)
           && !prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &seccomp_prog, 0, 0);

    if(!ret) {
        fprintf(stderr, "could not enter secure computation mode\n");
        perror("prctl");
        _exit(1);
    }
}

