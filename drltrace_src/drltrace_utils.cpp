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

#include "drltrace_utils.h"

static thread_id_t primary_thread = INVALID_THREAD_ID;

bool op_print_stderr = true;
uint op_verbose_level;
uint op_prefix_style;
file_t f_global = INVALID_FILE;
int reported_disk_error;
uint op_ignore_asserts = false;

void
print_prefix_to_buffer(char *buf, size_t bufsz, size_t *sofar)
{
    void *drcontext = dr_get_current_drcontext();
    ssize_t len;
    if (op_prefix_style == PREFIX_STYLE_NONE) {
        BUFPRINT_NO_ASSERT(buf, bufsz, *sofar, len, "");
        return;
    } else if (op_prefix_style == PREFIX_STYLE_BLANK) {
        BUFPRINT_NO_ASSERT(buf, bufsz, *sofar, len, "%s", PREFIX_DRLTRACE);
        return;
    } else if (drcontext != NULL) {
        thread_id_t tid = dr_get_thread_id(drcontext);
        if (primary_thread != INVALID_THREAD_ID/*initialized?*/ &&
            tid != primary_thread) {
            /* no-assert since used for errors, etc. in fragile contexts */
            BUFPRINT_NO_ASSERT(buf, bufsz, *sofar, len, "~~%d~~ ", tid);
            return;
        }
    }
    /* no-assert since used for errors, etc. in fragile contexts */
    BUFPRINT_NO_ASSERT(buf, bufsz, *sofar, len, "%s", PREFIX_DEFAULT_MAIN_THREAD);
}

void
print_prefix_to_console(void)
{
    char buf[16];
    size_t sofar = 0;
    print_prefix_to_buffer(buf, BUFFER_SIZE_ELEMENTS(buf), &sofar);
    PRINT_CONSOLE("%s", buf);
}

/* Not available in ntdll CRT so we supply our own.
* It is available on Mac and Android, and we want to avoid it for libs that do not
* want a libc dependence.
*/
#if !defined(MACOS) && !defined(ANDROID) && !defined(NOLINK_STRCASESTR)
const char *
strcasestr(const char *text, const char *pattern)
{
	const char *cur_text, *cur_pattern, *root;
	cur_text = text;
	root = text;
	cur_pattern = pattern;
	while (true) {
		if (*cur_pattern == '\0')
			return root;
		if (*cur_text == '\0')
			return NULL;
		/* XXX DRi#943: toupper is better, for int18n, and we need to call
		* islower() first to be safe for all tolower() implementations.
		* Even better would be switching to our own locale-independent case
		* folding.
		*/
		if ((char)tolower(*cur_text) == (char)tolower(*cur_pattern)) {
			cur_text++;
			cur_pattern++;
		}
		else {
			root++;
			cur_text = root;
			cur_pattern = pattern;
		}
	}
}
#endif
