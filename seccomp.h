#ifndef __seccomp_h__
#define __seccomp_h__

/* logging function, writes directly to fd 1 using a system call */
void stdout_printf(const char*format, ...);

void seccomp_lockdown();
#endif
