#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include "util.h"

char* dbg(const char*format, ...)
{
    va_list arglist;
    va_start(arglist, format);
    vfprintf(stdout, format, arglist);
    printf("\n");
    va_end(arglist);
}

void*memdup(const void*ptr, size_t size)
{
    void*new_ptr = malloc(size);
    if(!new_ptr) 
        return NULL;
    memcpy(new_ptr, ptr, size);
    return new_ptr;
}

char*escape_string(const char *input) 
{
    static const char before[] = "\a\b\f\n\r\t\v\\\"\'";
    static const char after[]  = "abfnrtv\\\"\'";

    int l = strlen(input);
    char*output = malloc((l+1)*2);
    char*o = output;
    const char*p;
    for(p=input; *p; p++) {
        char*pos=strchr(before, *p);
        if(pos) {
            *o++ = '\\';
            *o++ = after[pos-before];
        } else {
            *o++ = *p;
        }
    }
    *o = 0;
    return output;
}

char* allocprintf(const char*format, ...)
{
    va_list arglist1;
    va_start(arglist1, format);
    char dummy;
    int l = vsnprintf(&dummy, 1, format, arglist1);
    va_end(arglist1);

    va_list arglist2;
    va_start(arglist2, format);
    char*buf = malloc(l+1);
    vsnprintf(buf, l+1, format, arglist2);
    va_end(arglist2);
    return buf;
}

#if defined(CYGWIN)
static char path_seperator = '/';
#elif defined(WIN32)
static char path_seperator = '\\';
#else
static char path_seperator = '/';
#endif

char* concat_paths(const char*base, const char*add)
{

    int l1 = strlen(base);
    int l2 = strlen(add);
    int pos = 0;
    char*n = 0;
    while(l1 && base[l1-1] == path_seperator)
	l1--;
    while(pos < l2 && add[pos] == path_seperator)
	pos++;

    n = (char*)malloc(l1 + (l2-pos) + 2);
    memcpy(n,base,l1);
    n[l1]=path_seperator;
    strcpy(&n[l1+1],&add[pos]);
    return n;
}

void mkdir_p(const char*_path)
{
    char*path = strdup(_path);
    char*p = path;
    while(*p == path_seperator) {
        p++;
    }
    while(*p) {
        while(*p && *p != path_seperator) {
            p++;
        }
        if(!*p)
            break;
        *p = 0;
        mkdir(path, 0700);
        *p = path_seperator;
        p++;
    }
    mkdir(path, 0700);
}

char*read_file(const char*filename)
{
    FILE*fi = fopen(filename, "rb");
    if(!fi)
        return NULL;
    fseek(fi, 0, SEEK_END);
    int len = ftell(fi);
    fseek(fi, 0, SEEK_SET);
    char*script = malloc(len+1);
    fread(script, len, 1, fi);
    script[len]=0;
    fclose(fi);
    return script;
}

bool read_with_timeout(int fd, void* data, int len, struct timeval* timeout)
{
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(fd, &readfds);

    int pos = 0;
    while(pos<len) {
        int ret;
        while(1) {
            ret = select(fd+1, &readfds, NULL, NULL, timeout);
            if(ret<0) {
                if(errno == EINTR || errno == EAGAIN)
                    continue;
                return false;
            }
            if(ret>=0)
                break;
        }
        if(!FD_ISSET(fd, &readfds)) {
            // timeout
            return false;
        }

        ret = read(fd, data+pos, len-pos);
        if(ret<0) {
            if(errno == EINTR || errno == EAGAIN)
                continue;
            // read error
            return false;
        }
        if(ret==0) {
            // EOF
            return false;
        }
        pos += ret;
    }
    return true;
}
