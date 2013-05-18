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
#define HIJACK_SYSCALLS
//#define LOG_SYSCALLS
//#define LOG_SYSCALL_RETURN_VALUES
//#define REFUSE_UNSUPPORTED

#define MEM_PAD 65536

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

static void _syscall_log(int edi, int esi, int edx, int ecx, int ebx, int eax) 
{
    if(eax == __NR_gettimeofday)
        return;

    stdout_printf("syscall eax=%d ebx=%d ecx=%d edx=%d esi=%d edi=%d\n", 
            eax, ebx, ecx, edx, esi, edi);
    if(eax == __NR_open) {
        stdout_printf("\topen: %s\n", (char*)ebx);
    }
}

static void _syscall_log_return_value(int edi, int esi, int edx, int ecx, int ebx, int eax, int ebp, int call) 
{
    if(call == __NR_gettimeofday)
        return;

    stdout_printf("syscall return: eax=%d eax=%08x ebx=%d ecx=%d edx=%d esi=%d edi=%d\n", 
            eax, eax, ebx, ecx, edx, esi, edi);
}

static bool refused[512];
static void _syscall_refuse(int edi, int esi, int edx, int ecx, int ebx, int eax) 
{
    if(eax>=512 || eax<0) {
        log_err("[sandbox] Illegal/Unknown syscall %d", eax);
        return;
    }
    if(!refused[eax]) {
        log_warn("[sandbox] Refusing system call %d", eax);
        refused[eax] = true;
    }
    if(eax == __NR_open) {
        stdout_printf("refusing open: %s\n", (char*)ebx);
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
        "cmp $78, %%eax\n"  // gettimeofday
        "je forward\n"
        "cmp $13, %%eax\n"  // time
        "je forward\n"
        "cmp $146, %%eax\n "// writev
        "je forward\n"
        
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
        SAVE_REGS
        "call _syscall_refuse\n"
        RESTORE_REGS

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

#ifdef LOG_SYSCALLS
        "push %%eax\n"
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
    asm("mov %%gs:0x10, %%eax\n"
        "mov %%eax, %0\n"

        "mov $do_syscall, %%eax\n"
        "mov %%eax, %%gs:0x10\n"

        : "=m" (old_syscall_handler)
        : "r" (&do_syscall)
        : "eax");
};
#endif

#ifdef CATCH_SIGNALS
static void handle_signal(int signal, siginfo_t*siginfo, void*ucontext)
{
    void*here;
    my_write(1, "signal\n", 7);
    Dl_info info; 
    stdout_printf("signal %d, memory access to addr: %p\n", signal);
    void**stack_top = &here;
    int i = 0;

    for(i=0;i<256;i++) {
        if(dladdr(*stack_top, &info)) {
            const char* symbol_name = info.dli_sname;
            if(!symbol_name)
                symbol_name = "?";
            stdout_printf("[%3d] %08x %16s:%s\n", i, *stack_top, info.dli_fname, symbol_name);
        }
        stack_top++;
    }

    direct_exit(signal);
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
        ALLOW_ANYARGS(__NR_write),
        ALLOW_ANYARGS(__NR_writev),
        ALLOW_ANYARGS(__NR_brk),
        ALLOW_ANYARGS(__NR_mmap2),
        ALLOW_ANYARGS(__NR_munmap),
        ALLOW_ANYARGS(__NR_exit),
        ALLOW_ANYARGS(__NR_read),

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

