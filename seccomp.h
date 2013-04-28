#ifndef __seccomp_h__
#define __seccomp_h__

/* logging function, writes directly to fd 1 using a system call */
char* dbg_write(const char*format, ...);

void seccomp_lockdown();
#endif
