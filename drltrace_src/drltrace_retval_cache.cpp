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

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include "drltrace_retval_cache.h"


#ifdef UNIT_TESTS /* Re-define a few things when doing unit tests. */

  #define RETVAL_CACHE_SIZE 8
  #define dr_fprintf fprintf

#else /* Production values. */

  #include "drltrace_utils.h"

  /* There are 128K entries in the cache.  Each entry is 16 bytes, so the cache takes
   * up 2MB memory at minimum (not counting the function strings stored). */
  #define RETVAL_CACHE_SIZE (128 * 1024)

#endif

/* The output stream for dr_fprintf() calls. */
file_t out_stream;

/* True when the user wants grepable output (see '-grepable' option in the frontend). */
bool grepable_output = false;

/* The array of cached_function_call structs which make up the return value cache. */
static cached_function_call *retval_cache = NULL;

/* The current size of the cache. */
static unsigned int cache_size = 0;

/* The user-defined maximum size of the cache.  When non-zero, if this size is reached,
 * all entries are immediately dumped out to prevent memory exhaustion; caching then
 * continues normally. */
static unsigned int max_cache_size = 0;

/* If at any point the cache was dumped out (due to max_cache_size being reached, or
 * the cache limit being reached), this is set to 1 so that subsequent mismatched
 * return values do not trigger error messages. */
static unsigned int cache_dump_triggered = 0;


/* Outputs and clears as many entries with return values from the cache matching
 * the thread_id as possible (which may be zero).  If 'clear_all' is set, then all
 * entries are outputted & cleared. */
void
retval_cache_output(void *drcontext, unsigned int thread_id, bool clear_all) {

  /* If the caller wants to dump the cache, set this flag so that later return value
   * calls don't print error messages when the entry can't be found. */
  unsigned int i = 0;
  int first_slot = -1;
  if (clear_all)
    cache_dump_triggered = 1;
  else {

    /* Find the first index matching this thread ID. */
    for (; (i < cache_size); i++)
      if (retval_cache[i].thread_id == thread_id) {

	/* If the first matching index doesn't have its return value set, there's
         * nothing to do. */
	if (!retval_cache[i].retval_set)
	  return;
	else
	  break;
      }

    /* If no matching thread ID was found after reaching the end, there's nothing to
     * do. */
    if (i == cache_size)
      return;
  }

  first_slot = i;
  for (; (i < cache_size); i++) {
    if ((thread_id == retval_cache[i].thread_id) || clear_all) {

      /* Print the function and return value. */
      dr_fprintf(out_stream, "%s", retval_cache[i].function_call);
      dr_fprintf(out_stream, grepable_output ? " = " : "\n    ret: ");
      unsigned int retval_set = retval_cache[i].retval_set;
      void *retval = retval_cache[i].retval;

      if (retval_set) {

        switch (retval_cache[i].retval_type) {
        /* Unfortunately, the type for VOID and VOID * are lumped together.  So we'll
         * just have to skip this case and let the default case print it in hex. */
        /*
        case DRSYS_TYPE_VOID:
          dr_fprintf(out_stream, "<void>\n");
          break;
        */
        case DRSYS_TYPE_SIGNED_INT:
          dr_fprintf(out_stream, "%d\n", retval);
          break;
        case DRSYS_TYPE_UNSIGNED_INT:
          dr_fprintf(out_stream, "%u\n", retval);
          break;
        case DRSYS_TYPE_SIZE_T:
          dr_fprintf(out_stream, "%zu\n", (size_t)retval);
          break;
        case DRSYS_TYPE_CSTRING:
          if (retval == NULL)
            dr_fprintf(out_stream, "<NULL>\n");
          else if (drcontext == NULL)
            dr_fprintf(out_stream, "0x%" PRIxPTR "\n", (uintptr_t)retval);
          else {
            DR_TRY_EXCEPT(drcontext, {
              dr_fprintf(out_stream, "0x%" PRIxPTR ":\"%s\"\n", \
                         (uintptr_t)retval, (char *)retval);
            }, {
              dr_fprintf(out_stream, "<invalid memory>");
            });
          }
          break;
        case DRSYS_TYPE_CWSTRING:
          if (retval == NULL)
            dr_fprintf(out_stream, "<NULL>\n");
          else if (drcontext == NULL)
            dr_fprintf(out_stream, "0x%" PRIxPTR "\n", (uintptr_t)retval);
          else {
            DR_TRY_EXCEPT(drcontext, {
              dr_fprintf(out_stream, "0x%" PRIxPTR ":\"%S\"\n", \
                         (uintptr_t)retval, (char *)retval);
            }, {
              dr_fprintf(out_stream, "<invalid memory>");
            });
          }
          break;
        default: /* Print hex value. */
          dr_fprintf(out_stream, "0x%" PRIxPTR "\n", (uintptr_t)retval);
          break;
        }
      } else
        dr_fprintf(out_stream, grepable_output ? "?\n" : "?");

      free(retval_cache[i].function_call);
    }
  }

  if (clear_all)
    cache_size = 0;
  else {
    int free_slot = first_slot;

    /* There may be remaining entries in the cache belonging to other thread IDs.
     * De-fragment the entries to eliminate gaps in the array. */
    for (i = first_slot + 1; i < cache_size; i++) {
      if (thread_id != retval_cache[i].thread_id) {
        memcpy(&(retval_cache[free_slot]), &(retval_cache[i]), sizeof(cached_function_call));
        free_slot++;
      }
    }

    cache_size = free_slot;
  }

  return;
}

/* Append a function call to the return value cache. */
void
retval_cache_append(void *drcontext, \
                    unsigned int thread_id, \
                    drsys_param_type_t retval_type, \
                    const char *module_and_function_name, \
                    size_t module_and_function_name_len, \
                    const char *function_call, \
                    size_t function_call_len) {

  /* If the cache size hits the user-defined limit, immediately dump it all as-is into
   * the log, then we'll continue. */
  if ((max_cache_size > 0) && (cache_size >= max_cache_size))
    retval_cache_dump_all(drcontext);

#ifdef UNIX
  /* The post-function callback for these functions are never called.  So instead of
   * letting them clog up the cache, we'll just dump them out immediately. */
  if ((fast_strcmp(module_and_function_name, module_and_function_name_len, \
           "libc.so.6!__libc_start_main", 27) == 0) || \
      (fast_strcmp(module_and_function_name, module_and_function_name_len, \
           "libc.so.6!exit", 14) == 0)) {

    dr_fprintf(out_stream, "%s", function_call);
    if (grepable_output)
      dr_fprintf(out_stream, " = ??\n");
    else
      dr_fprintf(out_stream, "\n    ret: ??\n");

    return;
  }
#endif

  retval_cache[cache_size].thread_id = thread_id;
  retval_cache[cache_size].function_call = strdup(function_call);
  retval_cache[cache_size].function_call_len = function_call_len;
  retval_cache[cache_size].retval_type = retval_type;
  retval_cache[cache_size].retval_set = 0;
  cache_size++;

  /* If we reached the end of the cache, dump it all out then continue normally. */
  if (cache_size == RETVAL_CACHE_SIZE)
    retval_cache_dump_all(drcontext);
}

/* Set the return value of an entry in the cache. */
void
retval_cache_set_return_value(void *drcontext, \
                              unsigned int thread_id, \
                              const char *module_name_and_function, \
                              size_t module_name_and_function_len, \
                              void *retval) {
  bool found_entry = false;
  unsigned int min_len;
  int i = cache_size - 1;

  for (; (i >= 0) && (found_entry == false); i--) {
    /* If the return value is already set on this entry, or it does not belong to
     * the same thread ID, skip it. */
    if (retval_cache[i].retval_set || (retval_cache[i].thread_id != thread_id))
      continue;

    /* Only compare the shortest prefix of the two strings. */
    min_len = MIN(module_name_and_function_len, retval_cache[i].function_call_len);
    if (fast_strcmp(module_name_and_function, min_len, \
          retval_cache[i].function_call, min_len) == 0) {

      retval_cache[i].retval = retval;
      retval_cache[i].retval_set = 1;
      found_entry = true;
    }
  }

  /* If we found and set the entry, then check if its time to output the call stack. */
  if (found_entry)
    retval_cache_output(drcontext, thread_id, false);

  /* If the entry was not found, print an error.  However, if the cache was dumped
   * previously, this might be a valid call to an entry that was cleared. */
  else if (!found_entry && !cache_dump_triggered)
    dr_fprintf(out_stream, "ERROR: failed to find cache entry for [%s] (return value: 0x%" PRIx64 ")\n", module_name_and_function, retval);

  return;
}

/* Initialize the return value caching system.  The 'retval_cache_free()' function
 * must be called to clean up when done. */
void
retval_cache_init(file_t _out_stream, unsigned int _max_cache_size, \
                  bool _grepable_output) {

  out_stream = _out_stream;
  max_cache_size = _max_cache_size;
  grepable_output = _grepable_output;

  retval_cache = (cached_function_call *)calloc(RETVAL_CACHE_SIZE, \
      sizeof(cached_function_call));
  if (retval_cache == NULL) {
    fprintf(stderr, "Failed to create retval_cache array.\n");
    exit(-1);
  }
}

/* Frees and cleans up the return value caching system.  The caller must call
 * 'retval_cache_dump_all()' before this function to prevent memory leackage.  Returns
 * true if the cache was empty. */
bool
retval_cache_free() {
  bool ret = true;

  if (retval_cache == NULL)
    return false;

  if (cache_size != 0) {
    ret = false;
    fprintf(stderr, "WARNING: freeing return value cache even though it is not " \
        "empty!: %u\n", cache_size);
  }

  free(retval_cache);  retval_cache = NULL;
  return ret;
}

/* Functions only used during unit testing. */
#ifdef UNIT_TESTS
unsigned int
is_cache_empty() {
  if (cache_size == 0)
    return 1;
  else
    return 0;
}

unsigned int
get_cache_size() {
  return cache_size;
}
#endif
