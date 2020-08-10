/* ***************************************************************************
 * Copyright (c) 2013-2017 Google, Inc.  All rights reserved.
 * ***************************************************************************/

/*
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * * Neither the name of Google, Inc. nor the names of its contributors may be
 *   used to endorse or promote products derived from this software without
 *   specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL GOOGLE, INC. OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 */

/* Library Tracing Tool: drltrace
 *
 * Records calls to exported library routines.
 *
 * The runtime options for this client are specified in drltrace_options.h,
 * see DROPTION_SCOPE_CLIENT options.
 */
#include <fstream>
#include <vector>
#include <inttypes.h>
#include "drltrace.h"
#include "drltrace_utils.h"
#include "drltrace_retval_cache.h"

/* Where to write the trace */
static file_t outf;

/* Avoid exe exports, as on Linux many apps have a ton of global symbols. */
static app_pc exe_start;

static inline generic_func_t
cast_to_func(void *p)
{
    return (generic_func_t) p;
}

struct _wblist {
  char *func_name;
  size_t func_name_len;
  unsigned int is_wildcard;  /* Set to 1 when this is a wildcard, otherwise 0. */
};
typedef struct _wblist wb_list; /* Stands for white/black list. */

/* Arrays to hold functions in the whitelist/blacklist.  Used instead
 * of vectors due to speed requirements. */
static wb_list *filter_function_whitelist = NULL;
static unsigned int filter_function_whitelist_len = 0;

static wb_list *filter_function_blacklist = NULL;
static unsigned int filter_function_blacklist_len = 0;

/* Vectors to hold modules in the whitelist/blacklist. */
static std::vector<std::string> filter_module_whitelist;
static std::vector<std::string> filter_module_blacklist;


/****************************************************************************
 * Arguments printing
 */

static void
get_simple_value(char *out, size_t out_size, drltrace_arg_t *arg, bool leading_zeroes)
{
    bool pointer = !TEST(DRSYS_PARAM_INLINED, arg->mode);
    char temp[256];
    snprintf(temp, sizeof(temp) - 1, pointer ? PFX : (leading_zeroes ? PFX : PIFX), arg->value);
    strncat(out, temp, out_size - 1);
    if (pointer && ((arg->pre && TEST(DRSYS_PARAM_IN, arg->mode)) ||
                    (!arg->pre && TEST(DRSYS_PARAM_OUT, arg->mode)))) {
        ptr_uint_t deref = 0;
        ASSERT(arg->size <= sizeof(deref), "too-big simple type");
        /* We assume little-endian */
        if (dr_safe_read((void *)arg->value, arg->size, &deref, NULL)) {
          snprintf(temp, sizeof(temp) - 1, (leading_zeroes ? " => " PFX : " => " PIFX), deref);
          strncat(out, temp, out_size - 1);
        }
    }
}

static void
get_string(char *out, size_t out_size, void *drcontext, void *pointer_str, bool is_wide)
{
    if (pointer_str == NULL)
        strncat(out, "<null>", out_size - 1);
    else {
        DR_TRY_EXCEPT(drcontext, {
            char temp[256];
            if (is_wide)
              snprintf(temp, sizeof(temp) - 1, "0x%"PRIxPTR":\"%S\"", (uintptr_t)pointer_str, (wchar_t *)pointer_str);
            else
              snprintf(temp, sizeof(temp) - 1, "0x%"PRIxPTR":\"%s\"", (uintptr_t)pointer_str, (char *)pointer_str);

            strncat(out, temp, out_size - 1);
        }, {
            strncat(out, "<invalid memory>", out_size - 1);
        });
    }
}

static void
get_arg(char *out, size_t out_size, void *drcontext, drltrace_arg_t *arg)
{
    if (arg->pre && (TEST(DRSYS_PARAM_OUT, arg->mode) && !TEST(DRSYS_PARAM_IN, arg->mode)))
        return;

    char temp[384];
    snprintf(temp, sizeof(temp) - 1, "%s%d: ", (op_grepable.get_value() ? " {" : "\n    arg "), arg->ordinal);
    strncat(out, temp, out_size - 1);

    switch (arg->type) {
    case DRSYS_TYPE_VOID:         get_simple_value(out, out_size, arg, true); break;
    case DRSYS_TYPE_POINTER:      get_simple_value(out, out_size, arg, true); break;
    case DRSYS_TYPE_BOOL:         get_simple_value(out, out_size, arg, false); break;
    case DRSYS_TYPE_INT:          get_simple_value(out, out_size, arg, false); break;
    case DRSYS_TYPE_SIGNED_INT:   get_simple_value(out, out_size, arg, false); break;
    case DRSYS_TYPE_UNSIGNED_INT: get_simple_value(out, out_size, arg, false); break;
    case DRSYS_TYPE_HANDLE:       get_simple_value(out, out_size, arg, false); break;
    case DRSYS_TYPE_NTSTATUS:     get_simple_value(out, out_size, arg, false); break;
    case DRSYS_TYPE_ATOM:         get_simple_value(out, out_size, arg, false); break;
#ifdef WINDOWS
    case DRSYS_TYPE_LCID:         get_simple_value(out, out_size, arg, false); break;
    case DRSYS_TYPE_LPARAM:       get_simple_value(out, out_size, arg, false); break;
    case DRSYS_TYPE_SIZE_T:       get_simple_value(out, out_size, arg, false); break;
    case DRSYS_TYPE_HMODULE:      get_simple_value(out, out_size, arg, false); break;
#endif
    case DRSYS_TYPE_CSTRING:
        get_string(out, out_size, drcontext, (void *)arg->value, false);
        break;
    case DRSYS_TYPE_CWSTRING:
        get_string(out, out_size, drcontext, (void *)arg->value, true);
        break;
    default: {
        if (arg->value == 0)
          strncat(out, "<null>", out_size - 1);
        else {
          snprintf(temp, sizeof(temp), PFX, arg->value);
          strncat(out, temp, out_size - 1);
        }
    }
    }

    snprintf(temp, sizeof(temp) - 1, " (%s%s%stype=%s%s, size=" PIFX ")",
              (arg->arg_name == NULL) ? "" : "name=",
              (arg->arg_name == NULL) ? "" : arg->arg_name,
              (arg->arg_name == NULL) ? "" : ", ",
              (arg->type_name == NULL) ? "\"\"" : arg->type_name,
              (arg->type_name == NULL ||
              TESTANY(DRSYS_PARAM_INLINED|DRSYS_PARAM_RETVAL, arg->mode)) ? "" : "*",
              arg->size);
    strncat(out, temp, out_size - 1);

    if (op_grepable.get_value())
      strncat(out, "}", out_size - 1);
}

static bool
drlib_iter_arg_cb(char *out, size_t out_size, drltrace_arg_t *arg, void *wrapcxt)
{
    if (arg->ordinal == -1)
        return true;
    if (arg->ordinal >= op_max_args.get_value())
        return false; /* limit number of arguments to be printed */

    arg->value = (ptr_uint_t)drwrap_get_arg(wrapcxt, arg->ordinal);

    get_arg(out, out_size, drwrap_get_drcontext(wrapcxt), arg);
    return true; /* keep going */
}

static void
get_args_unknown_call(char *out, size_t out_size, app_pc func, void *wrapcxt)
{
    uint i;
    void *drcontext = drwrap_get_drcontext(wrapcxt);
    const char *prefix = "\n    arg ";
    const char *suffix = "";
    if (op_grepable.get_value()) {
      prefix = " {";
      suffix = "}";
    }
    DR_TRY_EXCEPT(drcontext, {
        for (i = 0; i < op_unknown_args.get_value(); i++) {
          char temp[256];
          snprintf(temp, sizeof(temp) - 1, "%s%d: " PFX, prefix, i, (long unsigned int)drwrap_get_arg(wrapcxt, i));
          strncat(out, temp, out_size - 1);
          if (*suffix != '\0')
            strncat(out, suffix, out_size - 1);
        }
    }, {
        strncat(out, "<invalid memory>", out_size - 1);
        /* Just keep going */
    });
    /* all args have been sucessfully printed */
    if (op_print_ret_addr.get_value())
      strncat(out, "\n   ", out_size - 1);
}

static bool
get_libcall_args(char *out, size_t out_size, drsys_param_type_t *retval_type, std::vector<drltrace_arg_t*> *args_vec, void *wrapcxt)
{
    if (args_vec == NULL || args_vec->size() <= 0)
        return false;

    std::vector<drltrace_arg_t*>::iterator it;
    for (it = args_vec->begin(); it != args_vec->end(); ++it) {
      if (it == args_vec->begin())
        *retval_type = (*it)->type;
      else if (!drlib_iter_arg_cb(out, out_size, *it, wrapcxt))
        break;
    }
    return true;
}

static void
get_symbolic_args(char *out, size_t out_size, drsys_param_type_t *retval_type, const char *name, void *wrapcxt, app_pc func)
{
    std::vector<drltrace_arg_t *> *args_vec;

    if (op_max_args.get_value() == 0)
        return;

	if (op_use_config.get_value()) {
		/* looking for libcall in libcalls hashtable */
		args_vec = libcalls_search(name);
                if (get_libcall_args(out, out_size, retval_type, args_vec, wrapcxt)) {
                  if (op_print_ret_addr.get_value()) {
                    strncat(out, "\n   ", out_size - 1);
                  }
                  return; /* we found libcall and sucessfully printed all arguments */
		}
	}
    /* use standard type-blind scheme */
    if (op_unknown_args.get_value() > 0)
        get_args_unknown_call(out, out_size, func, wrapcxt);
}

/* Puts "module_name!function_name" into the module_and_function_name output buffer.
 * Returns the number of characters written. */
inline int
get_module_and_function_name(char *module_and_function_name, \
                             size_t module_and_function_name_len, \
                             const char *function_name, \
                             void *wrapcxt) {
  int ret;
  const char *modname = NULL;
  /* XXX: it may be better to heap-allocate the "module!func" string and
   * pass in, to avoid this lookup.
   */
  module_data_t *mod = dr_lookup_module(drwrap_get_func(wrapcxt));
  if (mod != NULL)
    modname = dr_module_preferred_name(mod);
  if (modname == NULL)
    modname = "";

  ret = snprintf(module_and_function_name, \
        module_and_function_name_len - 1, "%s%s%s", \
        modname == NULL ? "" : modname, modname == NULL ? "" : "!", function_name);

  if (mod != NULL)
    dr_free_module_data(mod);

  return ret;
}

/* Places the thread ID tag into the buffer, then returns its length. */
inline unsigned int
get_thread_id_tag(char *out, size_t out_size, void *drcontext) {
  thread_id_t tid = INVALID_THREAD_ID;
  if (drcontext != NULL)
    tid = dr_get_thread_id(drcontext);
  if (tid != INVALID_THREAD_ID)
    return (unsigned int)snprintf(out, out_size - 1, "~~%d~~ ", tid);
  else {
    strncpy(out, "~~Dr.L~~ ", out_size - 1);
    return 9;
  }
}

/****************************************************************************
 * Library entry wrapping
 */

static void
lib_entry(void *wrapcxt, INOUT void **user_data)
{
    const char *function_name = (const char *) *user_data;

    skip_unstable_functions(function_name);

    const char *modname = NULL;
    app_pc func = drwrap_get_func(wrapcxt);
    void *drcontext = drwrap_get_drcontext(wrapcxt);
    thread_id_t tid = dr_get_thread_id(drcontext);

    if (op_only_from_app.get_value()) {
        /* For just this option, the modxfer approach might be better */
        app_pc retaddr =  NULL;
        DR_TRY_EXCEPT(drcontext, {
            retaddr = drwrap_get_retaddr(wrapcxt);
        }, { /* EXCEPT */
            retaddr = NULL;
        });
        if (retaddr != NULL) {
            module_data_t *mod = dr_lookup_module(retaddr);
            if (mod != NULL) {
                bool from_exe = (mod->start == exe_start);
                dr_free_module_data(mod);
                if (!from_exe)
                    return;
            }
        } else {
            /* Nearly all of these cases should be things like KiUserCallbackDispatcher
             * or other abnormal transitions.
             * If the user really wants to see everything they can not pass
             * -only_from_app.
             */
            return;
        }
    }

    /* Build the module & function string, then compare to the white/black
     * list. */
    char module_and_function_name[256];
    size_t module_and_function_name_len = \
        get_module_and_function_name(module_and_function_name, \
        sizeof(module_and_function_name), function_name, wrapcxt);

    /* Check if this module & function is in the whitelist. */
    bool allowed = false;
    bool tested = false;  /* True only if any white/blacklist testing below is done. */
    for (unsigned int i = 0; (allowed == false) && (i < filter_function_whitelist_len); i++) {
      tested = true;

      /* If the whitelist entry contains a wildcard, then compare only the shortest
       * part of either string. */
      unsigned int len_compare;
      if (filter_function_whitelist[i].is_wildcard)
        len_compare = MIN(module_and_function_name_len, \
          filter_function_whitelist[i].func_name_len);
      else
        len_compare = module_and_function_name_len;

      if (fast_strcmp(module_and_function_name, len_compare, \
          filter_function_whitelist[i].func_name, \
          filter_function_whitelist[i].func_name_len) == 0) {
        allowed = true;
      }
    }

    /* Check the blacklist if it was specified instead of a whitelist. */
    if (!allowed && filter_function_blacklist_len > 0) {
      allowed = true;
      for (unsigned int i = 0; allowed && (i < filter_function_blacklist_len); i++) {
        tested = true;

	/* If the blacklist entry contains a wildcard, then compare only the shortest
	 * part of either string. */
        unsigned int len_compare;
        if (filter_function_blacklist[i].is_wildcard)
          len_compare = MIN(module_and_function_name_len, \
            filter_function_blacklist[i].func_name_len);
        else
          len_compare = module_and_function_name_len;

        if (fast_strcmp(module_and_function_name, len_compare, \
            filter_function_blacklist[i].func_name, \
            filter_function_blacklist[i].func_name_len) == 0) {
          allowed = false;
        }
      }
    }

    /* If whitelist/blacklist testing was performed, and it was determined
     * this function is not to be logged... */
    if (tested && !allowed)
      return;

    /* Buffer to hold function call and arguments for caching. */
    char out[1024];
    out[ sizeof(out) - 1 ] = '\0'; /* Ensure it remains null-terminated. */

    unsigned int thread_id_tag_len = get_thread_id_tag(out, sizeof(out) - 1, drcontext);
    strncat(out, module_and_function_name, sizeof(out) - 1);

    /* XXX: We employ two schemes of arguments printing.  We are looking for prototypes
     * in config file specified by user to get symbolic representation of arguments
     * for known library calls. For the rest of library calls.  If there is no info
     * we employ type-blindprinting and use -num_unknown_args to get a count of arguments
     * to print.
     */
    drsys_param_type_t retval_arg = DRSYS_TYPE_UNKNOWN;
    get_symbolic_args(out, sizeof(out) - 1, &retval_arg, function_name, wrapcxt, func);

    uint mod_id;
    app_pc mod_start, ret_addr;
    drcovlib_status_t res;
    if (op_print_ret_addr.get_value()) {
      ret_addr = drwrap_get_retaddr(wrapcxt);
      res = drmodtrack_lookup(drcontext, ret_addr, &mod_id, &mod_start);
      if (res == DRCOVLIB_SUCCESS) {
        char temp[128];
        snprintf(temp, sizeof(temp) - 1,
                 op_print_ret_addr.get_value() ?
                 " and return to module id:%d, offset:" PIFX : "",
                 mod_id, ret_addr - mod_start);

        strncat(out, temp, sizeof(out) - 1);
      }
    }

    /* If return value caching is disabled, just print the function out now. */
    if (op_no_retval.get_value()) {
      dr_fprintf(outf, "%s", out);
      dr_fprintf(outf, "\n");

    /* Otherwise, cache this function call until the return value is obtained later. */
    } else {
      retval_cache_append(drcontext, (unsigned int)tid, retval_arg, \
          module_and_function_name, module_and_function_name_len, out, strlen(out));
    }
}

/****************************************************************************
 * Library exit wrapping
 */

static void
lib_exit(void *wrapcxt, void *user_data)
{
  const char *function_name = (const char *)user_data;
  void *drcontext = NULL;
  unsigned int tid = 0;
  void *retval = NULL;

  skip_unstable_functions(function_name);

  if (wrapcxt != NULL) {
    drcontext = drwrap_get_drcontext(wrapcxt);
	retval = drwrap_get_retval(wrapcxt);
	if (drcontext != NULL)
	  tid = (unsigned int)dr_get_thread_id(drcontext);
  }

  char module_and_function_name[256];

  unsigned int thread_id_tag_len = get_thread_id_tag(module_and_function_name, sizeof(module_and_function_name), drcontext);

  /*size_t module_and_function_name_len =*/ get_module_and_function_name(module_and_function_name + thread_id_tag_len, sizeof(module_and_function_name) - thread_id_tag_len, function_name, wrapcxt);

  /* Set the return value in the cache for this function call. */
  retval_cache_set_return_value(drcontext, tid, module_and_function_name, strlen(module_and_function_name), retval);
}

static void
iterate_exports(const module_data_t *info, bool add)
{
    dr_symbol_export_iterator_t *exp_iter =
        dr_symbol_export_iterator_start(info->handle);
    while (dr_symbol_export_iterator_hasnext(exp_iter)) {
        dr_symbol_export_t *sym = dr_symbol_export_iterator_next(exp_iter);
        app_pc func = NULL;
        if (sym->is_code)
            func = sym->addr;
#ifdef LINUX
        else if (sym->is_indirect_code) {
            /* Invoke the export to get the real entry: */
            app_pc (*indir)(void) = (app_pc (*)(void)) cast_to_func(sym->addr);
            void *drcontext = dr_get_current_drcontext();
            DR_TRY_EXCEPT(drcontext, {
                func = (*indir)();
            }, { /* EXCEPT */
                func = NULL;
            });
            VNOTIFY(2, "export %s indirected from " PFX " to " PFX NL,
                   sym->name, sym->addr, func);
        }
#endif
        if (op_ignore_underscore.get_value() && strstr(sym->name, "_") == sym->name)
            func = NULL;
        if (func != NULL) {
            if (add) {
                IF_DEBUG(bool ok =)
                    drwrap_wrap_ex(func, lib_entry, op_no_retval.get_value() ? NULL : lib_exit, (void *) sym->name, 0);
                ASSERT(ok, "wrap request failed");
                VNOTIFY(2, "wrapping export %s!%s @" PFX NL,
                       dr_module_preferred_name(info), sym->name, func);
            } else {
                IF_DEBUG(bool ok =)
                    drwrap_unwrap(func, lib_entry, op_no_retval.get_value() ? NULL : lib_exit);
                ASSERT(ok, "unwrap request failed");
            }
        }
    }
    dr_symbol_export_iterator_stop(exp_iter);
}

static bool
library_matches_filter(const module_data_t *info)
{
  if (!filter_module_whitelist.empty()) {
    const char *libname = dr_module_preferred_name(info);
    if (libname == NULL)
      return false;

    for (std::vector<std::string>::const_iterator iter = \
        filter_module_whitelist.begin(); iter != filter_module_whitelist.end(); \
        iter++) {

      if (strcmp(libname, iter->c_str()) == 0)
	return true;
    }

    return false;
  } else if (!filter_module_blacklist.empty()) {
    const char *libname = dr_module_preferred_name(info);
    if (libname == NULL)
      return true;

    for (std::vector<std::string>::const_iterator iter = \
        filter_module_blacklist.begin(); iter != filter_module_blacklist.end(); \
        iter++) {
      if (strcmp(libname, iter->c_str()) == 0)
	return false;
    }

    return true;
  } else
    return true;
}

static void
event_module_load(void *drcontext, const module_data_t *info, bool loaded)
{
    if (info->start != exe_start && library_matches_filter(info))
        iterate_exports(info, true/*add*/);
}

static void
event_module_unload(void *drcontext, const module_data_t *info)
{
    if (info->start != exe_start && library_matches_filter(info))
        iterate_exports(info, false/*remove*/);
}

/****************************************************************************
 * Init and exit
 */

static void
open_log_file(void)
{
    char buf[MAXIMUM_PATH];
    if (op_logdir.get_value().compare("-") == 0)
        outf = STDERR;
    else {
        outf = drx_open_unique_appid_file(op_logdir.get_value().c_str(),
                                          dr_get_process_id(),
                                          "drltrace", "log",
#ifndef WINDOWS
                                          DR_FILE_CLOSE_ON_FORK |
#endif
                                          DR_FILE_ALLOW_LARGE,
                                          buf, BUFFER_SIZE_ELEMENTS(buf));
        ASSERT(outf != INVALID_FILE, "failed to open log file");
        VNOTIFY(0, "drltrace log file is %s" NL, buf);

    }
}

/* Frees a wblist array. */
static void
free_wblist_array(wb_list **wbl, unsigned int wb_list_len) {
  if ((wbl == NULL) || (*wbl == NULL) || (wb_list_len == 0))
    return;

  /* Loop through all entries and free the function name, since it was
   * created with strdup().  Set it (and the length) to zero for good
   * measure. */
  for (unsigned int i = 0; i < wb_list_len; i++) {
    wb_list *l = *wbl;
    free(l[i].func_name);  l[i].func_name = NULL;
    l[i].func_name_len = 0;
  }

  /* Now free the array itself. */
  free(*wbl);  *wbl = NULL;
}

/* Adds a module name to the module filter. */
void
add_module_filter(std::vector<std::string> &module_wbl, const char *module_name) {

  /* If the module name is already in the filter, ignore. */
  for (std::vector<std::string>::const_iterator iter = module_wbl.begin(); iter != module_wbl.end(); iter++) {
    if (strcmp(module_name, iter->c_str()) == 0)
      return;
  }

  module_wbl.push_back(module_name);
}

/* Convert a vector to an array of wb_list structs.  This may be faster
 * to process than a vector when handling function call-backs. */
static void
parse_filter(std::vector<std::string> &v_in, bool is_whitelist, std::vector<std::string> &module_wbl, wb_list **func_wbl, unsigned int *func_wbl_len) {

  /* First look for entries that don't have a '!'; these are module names that
   * need to be filtered at the module-level. */
  for (std::vector<std::string>::const_iterator iter = v_in.begin(); \
       iter != v_in.end(); iter++) {
    if (iter->find('!') == std::string::npos)
      add_module_filter(module_wbl, iter->c_str());
  }

  /* Allocate the array of wb_list structs. */
  unsigned int v_in_len = v_in.size();
  *func_wbl = (wb_list *)calloc(v_in_len, sizeof(wb_list));
  if (*func_wbl == NULL) {
    fprintf(stderr, "Failed to allocate whitelist/blacklist array.\n");
    exit(-1);
  }

  /* For every entry in the vector, strdup() the function name and add
   * it to the array. */
  unsigned j = 0;
  for (std::vector<std::string>::const_iterator iter = v_in.begin(); \
       (iter != v_in.end()) && (j < v_in_len); iter++, j++) {

    unsigned int is_wildcard = 0;
    char *s = strdup(iter->c_str());
    if (s == NULL) {
      fprintf(stderr, "Failed to allocate whitelist/blacklist array.\n");
      exit(-1);
    }

    /* If the function name ends with a '*', then it is a wildcard prefix.  Set the
     * wildcard flag and cut off the trailing '*'. */
    if (s[strlen(s) - 1] == '*') {
      s[strlen(s) - 1] = '\0';
      is_wildcard = 1;

    /* If a module name was provided without a corresponding function, then this is a
     * wildcard module.  These, too, must be added to the function-level filter in
     * order for whitelisted functions to be allowed through. */
    } else if (strchr(s, '!') == NULL)
      is_wildcard = 1;

    /* If we're parsing the whitelist filter, ensure that the module for this function
     * is in the module whitelist as well, otherwise the function-level filtering will
     * never trigger. */
    if (is_whitelist) {
      char *module_name = strdup(s);
      char *bang_pos = strchr(module_name, '!');
      if (bang_pos != NULL) {
	*bang_pos = '\0';
	add_module_filter(module_wbl, module_name);
      }
      free(module_name);  module_name = NULL;
    }

    wb_list *l = *func_wbl;
    l[j].func_name = s;
    l[j].is_wildcard = is_wildcard;

    /* We store the function name length too, so we don't repeatedly
     * call strlen() on an unchanging string in the critical region. */
    l[j].func_name_len = strlen(s);
  }

  *func_wbl_len = v_in_len;
}

/* Parse the whitelist/blacklist entries in the filter file. */
static void
parse_filter_file(void)
{
  if (op_filter_file.get_value().empty())
    return;

  std::vector<std::string> temp_whitelist;
  std::vector<std::string> temp_blacklist;

  std::ifstream filter_file(op_filter_file.get_value().c_str());

  /* Loop through every line in the filter file. */
  std::string line;
  bool mode_whitelist = false, mode_blacklist = false;
  while (std::getline(filter_file, line)) {

    /* Skip empty lines and comments. */
    if (line.empty() || (line.find("#") == 0))
      continue;

    /* When we find a whitelist header, add subsequent lines to the whitelist.
    /* Otherwise, when we find a blacklist header, add subsequent lines to the
    /* blacklist. */
    if (line == std::string("[whitelist]")) {
      mode_whitelist = true;
      mode_blacklist = false;
      continue;
    } else if (line == std::string("[blacklist]")) {
      mode_whitelist = false;
      mode_blacklist = true;
      continue;
    }

    if (mode_whitelist)
      temp_whitelist.push_back(line);
    else if (mode_blacklist)
      temp_blacklist.push_back(line);
  }

  /* If both a whitelist and a blacklist was specified, the blacklist is
   * ignored. */
  if (!temp_whitelist.empty() && !temp_blacklist.empty())
    temp_blacklist.clear();

  /* Convert the vectors to an array of wb_list structs.  This is
   * possibly faster to process in the critical region later. */
  if (!temp_whitelist.empty())
    parse_filter(temp_whitelist, true, filter_module_whitelist, &filter_function_whitelist, &filter_function_whitelist_len);
  else if (!temp_blacklist.empty())
    parse_filter(temp_blacklist, false, filter_module_blacklist, &filter_function_blacklist, &filter_function_blacklist_len);

  filter_file.close();
}

#ifndef WINDOWS
static void
event_fork(void *drcontext)
{
    /* The old file was closed by DR b/c we passed DR_FILE_CLOSE_ON_FORK */
    open_log_file();
}
#endif

static void
event_exit(void)
{
    if (op_use_config.get_value())
        libcalls_hashtable_delete();

    /* Flush any remaining entries in the return value cache. */
    if (!op_no_retval.get_value())
      retval_cache_dump_all(NULL);

    if (outf != STDERR) {
        if (op_print_ret_addr.get_value())
            drmodtrack_dump(outf);
        dr_close_file(outf);
    }

    free_wblist_array(&filter_function_whitelist, filter_function_whitelist_len);
    free_wblist_array(&filter_function_blacklist, filter_function_blacklist_len);

    if (!op_no_retval.get_value())
      retval_cache_free();

    drx_exit();
    drwrap_exit();
    drmgr_exit();
    if (op_print_ret_addr.get_value())
        drmodtrack_exit();
}

DR_EXPORT void
dr_client_main(client_id_t id, int argc, const char *argv[])
{
    module_data_t *exe;
    IF_DEBUG(bool ok;)

    dr_set_client_name("Dr. LTrace", "???");

    if (!droption_parser_t::parse_argv(DROPTION_SCOPE_CLIENT, argc, argv,
                                       NULL, NULL))
        ASSERT(false, "unable to parse options specified for drltracelib");

    IF_DEBUG(ok = )
        drmgr_init();
    ASSERT(ok, "drmgr failed to initialize");
    IF_DEBUG(ok = )
        drwrap_init();
    ASSERT(ok, "drwrap failed to initialize");
    IF_DEBUG(ok = )
        drx_init();
    ASSERT(ok, "drx failed to initialize");
    if (op_print_ret_addr.get_value()) {
        IF_DEBUG(ok = )
            drmodtrack_init();
        ASSERT(ok == DRCOVLIB_SUCCESS, "drmodtrack failed to initialize");
    }

    exe = dr_get_main_module();
    if (exe != NULL)
        exe_start = exe->start;
    dr_free_module_data(exe);

    /* No-frills is safe b/c we're the only module doing wrapping, and
     * we're only wrapping at module load and unwrapping at unload.
     * Fast cleancalls is safe b/c we're only wrapping func entry and
     * we don't care about the app context.
     */
    drwrap_set_global_flags((drwrap_global_flags_t)
                            (DRWRAP_NO_FRILLS | DRWRAP_FAST_CLEANCALLS));

    dr_register_exit_event(event_exit);
#ifdef UNIX
    dr_register_fork_init_event(event_fork);
#endif
    drmgr_register_module_load_event(event_module_load);
    drmgr_register_module_unload_event(event_module_unload);

#ifdef WINDOWS
    dr_enable_console_printing();
#endif
    if (op_max_args.get_value() > 0)
        parse_config();

    open_log_file();
    parse_filter_file();

    /* Initialize the return value cache system, unless it was disabled by the user. */
    if (!op_no_retval.get_value())
      retval_cache_init(outf, op_retval_max_cache.get_value(), op_grepable.get_value());
}
