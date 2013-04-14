/* dict.h
   Hash tables

   Copyright (c) 2010-2013 Matthias Kramm <kramm@quiss.org> 
 
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

#ifndef __dict_h__
#define __dict_h__

#include <stdio.h>
#include <stdbool.h>

typedef bool (*equals_func)(const void*o1, const void*o2);
typedef unsigned int (*hash_func)(const void*o);
typedef void* (*dup_func)(const void*o);
typedef void (*free_func)(void*o);

typedef struct _hashtype_t {
    equals_func equals;
    hash_func hash;
    dup_func dup;
    free_func free;
} hashtype_t;

extern hashtype_t charptr_type;
extern hashtype_t stringstruct_type;
extern hashtype_t ptr_type;
extern hashtype_t int_type;

#define PTR_TO_INT(p) (((char*)(p))-((char*)NULL))
#define INT_TO_PTR(i) (((char*)NULL)+(int)(i))

typedef struct _dictentry {
    void*key;
    unsigned int hash;
    void*data;
    struct _dictentry*next;
} dictentry_t;

typedef struct _dict {
    dictentry_t**slots;
    hashtype_t*key_type;
    int hashsize;
    int num;
} dict_t;

unsigned int crc32_add_byte(unsigned int checksum, unsigned char b);
unsigned int crc32_add_string(unsigned int checksum, const char*s);
unsigned int crc32_add_bytes(unsigned int checksum, const void*_s, int len);
unsigned int hash_block(const void*data, int len);

dict_t*dict_new(hashtype_t*type);
void dict_init(dict_t*dict, int size);
void dict_init2(dict_t*dict, hashtype_t*type, int size);
dictentry_t*dict_put(dict_t*h, const void*key, void* data);
dictentry_t*dict_put_int(dict_t*h, const void*key, int value);
int dict_count(dict_t*h);
void dict_dump(dict_t*h, FILE*fi, const char*prefix);
dictentry_t* dict_get_slot(dict_t*h, const void*key);
char dict_contains(dict_t*h, const void*s);

void* dict_lookup(dict_t*h, const void*s);
int dict_lookup_int(dict_t*h, const void*s);

char dict_del(dict_t*h, const void*s);
char dict_del2(dict_t*h, const void*key, void*data);
dict_t*dict_clone(dict_t*);

void dict_foreach_keyvalue(dict_t*h, void (*runFunction)(void*data, const void*key, void*val), void*data);
void dict_foreach_value(dict_t*h, void (*runFunction)(void*));
void dict_free_all(dict_t*h, char free_keys, void (*free_data_function)(void*));
void dict_clear(dict_t*h);
void dict_destroy_shallow(dict_t*dict);
void dict_destroy(dict_t*dict);
#define DICT_ITERATE_DATA(d,t,v) \
    int v##_i;dictentry_t*v##_e;t v;\
    for(v##_i=0;v##_i<(d)->hashsize;v##_i++) \
        for(v##_e=(d)->slots[v##_i]; v##_e && ((v=(t)v##_e->data)||1); v##_e=v##_e->next)
#define DICT_ITERATE_KEY(d,t,v)  \
    int v##_i;dictentry_t*v##_e;t v;\
    for(v##_i=0;v##_i<(d)->hashsize;v##_i++) \
        for(v##_e=(d)->slots[v##_i];v##_e && ((v=(t)v##_e->key)||1);v##_e=v##_e->next)
#define DICT_ITERATE_ITEMS(d,t1,v1,t2,v2) \
    int v1##_i;dictentry_t*v1##_e;t1 v1;t2 v2; \
    for(v1##_i=0;v1##_i<(d)->hashsize;v1##_i++) \
        for(v1##_e=(d)->slots[v1##_i]; v1##_e && (((v1=(t1)v1##_e->key)||1)&&((v2=(t2)v1##_e->data)||1)); v1##_e=v1##_e->next)

#endif
