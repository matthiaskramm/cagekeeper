About:
------

This is a secure sandbox for (so far) Python and Javascript programs.

It's aimed at online programming challenges, homework assignments etc.

Features:

* shielding of guest programs from the rest of the operating system: the processes run in a strict seccomp jail (prctl(SECCOMP, MODE\_STRICT)), and hence can't do any system calls other than read/write (to pre-opened pipes) and exit.
* memory limit: cagekeeper does its own memory allocation, so you can limit the amount of memory guest processes are able to consume.
* time limits: You can abort function calls to guest programs after a time limit has elapsed.
* unified interface: All language interpreters are initialized in the same way.
* unified data types: You can pass strings, integers, floats, nested arrays and booleans back and fro from all guest languages using a common API.
* callback functions: Guest programs can invoke callback functions defined in your application e.g. to query for state, deliver results or write debug output. Function calls are serialized through pre-opened pipes to communicate with the sandbox the guest programs run in.
* multiple simultaneous programs: You can have any number of guests running in a single application.

