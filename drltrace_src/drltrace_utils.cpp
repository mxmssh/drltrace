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