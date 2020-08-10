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

#ifndef _DRLTRACE_UTILS_H
#define _DRLTRACE_UTILS_H

#ifdef WINDOWS
#include <string.h>
#else
#include <strings.h>
#endif

#include "dr_api.h"

/* XXX: some data types were taken from drsyscall.h and utils.h (DrMemory) */

void print_prefix_to_console(void);

/* A faster(?) version of strcmp(), since strcmp() does extra string
 * comparison we don't need (we just need an equality test).  Returns
 * 0 when strings are equal, otherwise returns non-zero. */
inline int
fast_strcmp(const char *s1, size_t s1_len, const char *s2, size_t s2_len) {
  if (s1_len != s2_len)
    return -1;

#ifdef WINDOWS
  return memcmp(s1, s2, s1_len); /* VC2013 doesn't have bcmp(), sadly. */
#else
  return bcmp(s1, s2, s1_len);  /* Fastest option. */
#endif
}

#define MIN(x,y) (((x) > (y)) ? (y) : (x))

#ifdef DEBUG
# define IF_DEBUG(x) x
# define IF_DEBUG_ELSE(x,y) x
# define _IF_DEBUG(x) , x
#else
# define IF_DEBUG(x)
# define IF_DEBUG_ELSE(x,y) y
# define _IF_DEBUG(x)
#endif

#ifdef UNIX
# define DIRSEP '/'
# define ALT_DIRSEP '/'
# define NL "\n"
#else
/* We can pick which we want for usability: the auto-launch of notepad
* converts to backslash regardless (i#1123).  Backslash works in all
* Windows apps, while forward works in most and in cygwin (though
* still not first-class there as it has a drive letter) but not in
* File Open dialogs or older notepad Save As (or as executable path
* when launching).  We could consider making this a runtime option
* or auto-picked if in cygwin but for now we're going with backslash.
*/
# define DIRSEP '\\'
# define ALT_DIRSEP '/'
# define NL "\r\n"
#endif

#define INVALID_THREAD_ID 0

#define TEST(mask, var) (((mask) & (var)) != 0)

#ifdef WINDOWS
# define IF_WINDOWS(x) x
# define IF_WINDOWS_(x) x,
# define _IF_WINDOWS(x) , x
# define IF_WINDOWS_ELSE(x,y) x
# define IF_UNIX(x)
# define IF_UNIX_ELSE(x,y) y
# define IF_LINUX(x)
# define IF_LINUX_ELSE(x,y) y
# define IF_UNIX_(x)
#else
# define IF_WINDOWS(x)
# define IF_WINDOWS_(x)
# define _IF_WINDOWS(x)
# define IF_WINDOWS_ELSE(x,y) y
# define IF_UNIX(x) x
# define IF_UNIX_ELSE(x,y) x
# define IF_UNIX_(x) x,
# ifdef LINUX
#  define IF_LINUX(x) x
#  define IF_LINUX_ELSE(x,y) x
#  define IF_LINUX_(x) x,
# else
#  define IF_LINUX(x)
#  define IF_LINUX_ELSE(x,y) y
#  define IF_LINUX_(x)
# endif
#endif

/* for notifying user
* XXX: should add messagebox, controlled by option
*/
enum {
	PREFIX_STYLE_DEAULT,
	PREFIX_STYLE_NONE,
	PREFIX_STYLE_BLANK,
};

/* globals that affect NOTIFY* and *LOG* macros */
extern bool op_print_stderr;
extern uint op_verbose_level;
extern file_t f_global;
extern int reported_disk_error;
extern uint op_prefix_style;
extern uint op_ignore_asserts;

#if defined(WIN32) && defined(USE_DRSYMS)
# define IN_CMD (dr_using_console())
# define USE_MSGBOX (op_print_stderr && IN_CMD)
#else
/* For non-USE_DRSYMS Windows we just don't support cmd: unfortunately this
* includes cygwin in cmd.  With PR 561181 we'll get cygwin into USE_DRSYMS.
*/
# define USE_MSGBOX (false)
#endif

# define PREFIX_DEFAULT_MAIN_THREAD "~~drltrace~~ "
#define PREFIX_DRLTRACE                "~~drltrace~~ "

/* For printing to a buffer.
* Usage: have a size_t variable "sofar" that counts the chars used so far.
* We take in "len" to avoid repeated locals, which some compilers won't
* combine (grrr: xref some PR).
* If we had i#168 dr_vsnprintf this wouldn't have to be a macro.
*/
#define BUFPRINT_NO_ASSERT(buf, bufsz, sofar, len, ...) do { \
	len = dr_snprintf((buf)+(sofar), (bufsz)-(sofar), __VA_ARGS__); \
	sofar += (len == -1 ? ((bufsz)-(sofar)) : (len < 0 ? 0 : len)); \
	/* be paranoid: though usually many calls in a row and could delay until end */ \
	(buf)[(bufsz)-1] = '\0';                                 \
} while (0)

/* dr_fprintf() now prints to the console after dr_enable_console_printing() */
#define PRINT_CONSOLE(...) dr_fprintf(STDERR, __VA_ARGS__)

#define REPORT_DISK_ERROR() do { \
	/* this implements a DO_ONCE with multiple instantiations */ \
	int report_count = dr_atomic_add32_return_sum(&reported_disk_error, 1); \
    if (report_count == 1) {\
        if (op_print_stderr) {\
	        print_prefix_to_console(); \
	        PRINT_CONSOLE("WARNING: Unable to write to the disk.  "\
	                      "Ensure that you have enough space and permissions.\n"); \
        } \
        if (USE_MSGBOX) {\
	        IF_WINDOWS(dr_messagebox("Unable to write to the disk.  "\
				"Ensure that you have enough space and permissions.\n")); \
        } \
    } \
} while (0)

/* we require a ,fmt arg but C99 requires one+ argument which we just strip */
#define ELOGF(level, f, ...) do {   \
    if (op_verbose_level >= (level) && (f) != INVALID_FILE) {\
        if (dr_fprintf(f, __VA_ARGS__) < 0) \
	        REPORT_DISK_ERROR(); \
    } \
} while (0)

#define LOGFILE(pt) ((pt) == NULL ? f_global : (pt)->f)

#define ELOGPT(level, pt, ...) \
	ELOGF(level, LOGFILE(pt), __VA_ARGS__)

#define ELOG(level, ...) do {   \
    if (op_verbose_level >= (level)) { /* avoid unnec PT_GET */ \
	    ELOGPT(level, PT_LOOKUP(), __VA_ARGS__); \
    } \
} while (0)

#define NOTIFY_COND(cond, f, ...) do { \
	ELOGF(0, f, __VA_ARGS__); \
    if ((cond) && op_print_stderr) {\
	    print_prefix_to_console(); \
	    PRINT_CONSOLE(__VA_ARGS__); \
    }\
} while (0)

/* XXX: VNOTIFY prints in console only for debug build, however we have
* some important user notifications that would be good to keep visible
* in release build as well.
*/
#define VNOTIFY(level, ...) do {          \
	NOTIFY_COND(op_verbose.get_value() >= level, f_global, __VA_ARGS__); \
} while (0)

#define OPTION_MAX_LENGTH MAXIMUM_PATH

#define BUFFER_SIZE_BYTES(buf)      sizeof(buf)
#define BUFFER_SIZE_ELEMENTS(buf)   (BUFFER_SIZE_BYTES(buf) / sizeof((buf)[0]))
#define BUFFER_LAST_ELEMENT(buf)    (buf)[BUFFER_SIZE_ELEMENTS(buf) - 1]
#define NULL_TERMINATE_BUFFER(buf)  BUFFER_LAST_ELEMENT(buf) = 0

#if !defined(MACOS) && !defined(ANDROID) && !defined(NOLINK_STRCASESTR)
const char *
strcasestr(const char *text, const char *pattern);
#endif


#define NOTIFY_ERROR(...) do { \
	IF_WINDOWS({ if (USE_MSGBOX) dr_messagebox(__VA_ARGS__); })   \
} while (0)

#ifdef DEBUG
# define ASSERT(x, msg) do { \
    if (!(x)) {\
	    NOTIFY_ERROR("ASSERT FAILURE (thread " TIDFMT "): %s:%d: %s (%s)" NL, \
	                 (dr_get_current_drcontext() == NULL ? 0 : \
	                  dr_get_thread_id(dr_get_current_drcontext())), \
	                  __FILE__, __LINE__, #x, msg); \
        if (!op_ignore_asserts) dr_abort(); \
    } \
} while (0)
#else
# define ASSERT(x, msg) /* nothing */
# define ASSERT_NOT_TESTED(msg) /* nothing */
#endif

#endif /* _DRLTRACE_UTILS_H */
