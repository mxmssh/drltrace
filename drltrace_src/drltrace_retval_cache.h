/*
 * Copyright 2020 Joe Testa <jtesta@positronsecurity.com>
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this
 * list of conditions and the following disclaimer in the documentation and/or other
 * materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _DRLTRACE_RETVAL_CACHE_H
#define _DRLTRACE_RETVAL_CACHE_H

#ifdef UNIT_TESTS

  #include <stdio.h>
  #define file_t FILE*
  #define drsys_param_type_t unsigned int

  #define DRSYS_TYPE_UNSIGNED_INT 1
  #define DRSYS_TYPE_SIGNED_INT 2
  #define DRSYS_TYPE_SIZE_T 3
  #define DRSYS_TYPE_CSTRING 4
  #define DRSYS_TYPE_CWSTRING 5

  #define MIN(x,y) (((x) > (y)) ? (y) : (x))
  #define DR_TRY_EXCEPT(_drcontext, _code, _altcode) #_code

  inline int fast_strcmp(const char *s1, size_t s1_len, const char *s2, size_t s2_len) {
  if (s1_len != s2_len)
    return -1;

  #ifdef WINDOWS
    return memcmp(s1, s2, s1_len); /* VC2013 doesn't have bcmp(), sadly. */
  #else
    return bcmp(s1, s2, s1_len);  /* Fastest option. */
  #endif
  }

#else
  #include "dr_api.h"
  #include "drltrace.h"
#endif

/* The return value cache is an array of these structs. */
struct _cached_function_call {
  unsigned int thread_id;    /* The thread ID that this call belongs to. */
  char *function_call; /* Contains the module name, function name, and arguments. */
  unsigned int function_call_len;  /* Length of the function_call string. */
  drsys_param_type_t retval_type;  /* The type of return value. */
  unsigned int retval_set;   /* Set to 1 when retval is set, otherwise 0. */
  void *retval;              /* The return value of the function. */
};
typedef struct _cached_function_call cached_function_call;


#define retval_cache_dump_all(__drcontext) retval_cache_output((__drcontext), 0, true)

void
retval_cache_output(void *drcontext, unsigned int thread_id, bool clear_all);

void
retval_cache_append(void *drcontext, unsigned int thread_id, drsys_param_type_t retval_type, const char *module_and_function_name, size_t module_and_function_name_len, const char *function_call, size_t function_call_len);

void
retval_cache_set_return_value(void *drcontext, unsigned int thread_id, const char *function_call, size_t function_call_len, void *retval);

void
retval_cache_init(file_t _out_stream, unsigned int _max_cache_size, bool grepable_output);

bool
retval_cache_free();


/* Functions only used during unit testing of the retval cache system. */
#ifdef UNIT_TESTS
unsigned int
is_cache_empty();

unsigned int
get_cache_size();
#endif

#endif /* _DRLTRACE_RETVAL_CACHE_H */
