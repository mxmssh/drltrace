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

#include "dr_api.h"
#include "drltrace_options.h"
#include "drmgr.h"
#include "drwrap.h"
#include "drx.h"
#include "drcovlib.h"
#include "drltrace_utils.h"
#include <string.h>
#include <vector>

typedef enum {
	DRSYS_PARAM_IN = 0x01,  /**< Input parameter. */
	DRSYS_PARAM_OUT = 0x02,  /**< Output parameter. */
	/**
	* May be IN or OUT.  Used only in pre-syscall to indicate the
	* size of an entire data structure, when only some fields are
	* actually read or writen.  Those fields will be presented as
	* separate IN or OUT arguments which will of course overlap this
	* one.
	*/
	DRSYS_PARAM_BOUNDS = 0x04,
	/**
	* Not used for memory iteration, only for type iteration, where
	* the type of the return value is indicated if it is other than a
	* status or error code.
	*/
	DRSYS_PARAM_RETVAL = 0x08,
	/**
	* If this flag is not set, the parameter is passed as a pointer to
	* the specified type.  If this flag is set, the parameter's value
	* is passed in.
	*/
	DRSYS_PARAM_INLINED = 0x10,
} drsys_param_mode_t;

/* Keep this in synch with param_type_names[] */
/**
* Indicates the data type of a parameter.
* For the non-memarg iterators, a pointer type is implied whenever the
* mode is DRSYS_PARAM_OUT.  Thus, a system call parameter of type DRSYS_TYPE_INT
* and mode DRSYS_PARAM_OUT can be assumed to be a pointer to an int.
*/
typedef enum {
	DRSYS_TYPE_INVALID,     /**< This type field is not used for this iteration type. */
	DRSYS_TYPE_UNKNOWN,     /**< Unknown type. */

	/* Inlined */
	DRSYS_TYPE_VOID,   	    /**< Void type. */
	DRSYS_TYPE_BOOL,   	    /**< Boolean type. */
	DRSYS_TYPE_INT,    	    /**< Integer type of unspecified signedness. */
	DRSYS_TYPE_SIGNED_INT,  /**< Signed integer type. */
	DRSYS_TYPE_UNSIGNED_INT,/**< Unsigned integer type. */
	DRSYS_TYPE_SIZE_T,      /**< Size_t type */
	DRSYS_TYPE_HANDLE,      /**< Windows-only: kernel/GDI/user handle type. */
	DRSYS_TYPE_NTSTATUS,    /**< Windows-only: NTSTATUS Native API/RTL type. */
	DRSYS_TYPE_ATOM,        /**< Windows-only: ATOM type. */
	DRSYS_TYPE_LCID,        /**< Windows-only: LCID type. */
	DRSYS_TYPE_LPARAM,      /**< Windows-only: LPARAM type. */
	DRSYS_TYPE_HMODULE,     /**< Windows-only: HMODULE type. */
	DRSYS_TYPE_HFILE,       /**< Windows-only: HFILE type. */
	DRSYS_TYPE_POINTER,     /**< Pointer to an unspecified type. */

	/* Structs */
	DRSYS_TYPE_STRUCT,      /**< Unspecified structure type. */
	DRSYS_TYPE_CSTRING,     /**< Null-terminated string of characters (C string). */
	DRSYS_TYPE_CWSTRING,    /**< Null-terminated string of wide characters. */
	DRSYS_TYPE_CARRAY,      /**< Non-null-terminated string of characters. */
	DRSYS_TYPE_CWARRAY,     /**< Non-null-terminated string of wide characters. */
	DRSYS_TYPE_CSTRARRAY,   /**< Null-terminated array of C strings. */
	DRSYS_TYPE_UNICODE_STRING,      /**< UNICODE_STRING structure. */
	DRSYS_TYPE_LARGE_STRING,        /**< LARGE_STRING structure. */
	DRSYS_TYPE_OBJECT_ATTRIBUTES,   /**< OBJECT_ATTRIBUTES structure. */
	DRSYS_TYPE_SECURITY_DESCRIPTOR, /**< SECURITY_DESCRIPTOR structure. */
	DRSYS_TYPE_SECURITY_QOS,        /**< SECURITY_QUALITY_OF_SERVICE structure */
	DRSYS_TYPE_PORT_MESSAGE,        /**< PORT_MESSAGE structure. */
	DRSYS_TYPE_CONTEXT,             /**< CONTEXT structure. */
	DRSYS_TYPE_EXCEPTION_RECORD,    /**< EXCEPTION_RECORD structure. */
	DRSYS_TYPE_DEVMODEW,            /**< DEVMODEW structure. */
	DRSYS_TYPE_WNDCLASSEXW,         /**< WNDCLASSEXW structure. */
	DRSYS_TYPE_CLSMENUNAME,         /**< CLSMENUNAME structure. */
	DRSYS_TYPE_MENUITEMINFOW,       /**< MENUITEMINFOW structure. */
	DRSYS_TYPE_ALPC_PORT_ATTRIBUTES,/**< ALPC_PORT_ATTRIBUTES structure. */
	DRSYS_TYPE_ALPC_SECURITY_ATTRIBUTES,/**< ALPC_SECURITY_ATTRIBUTES structure. */
	DRSYS_TYPE_LOGFONTW,            /**< LOGFONTW structure. */
	DRSYS_TYPE_NONCLIENTMETRICSW,   /**< NONCLIENTMETRICSW structure. */
	DRSYS_TYPE_ICONMETRICSW,        /**< ICONMETRICSW structure. */
	DRSYS_TYPE_SERIALKEYSW,         /**< SERIALKEYSW structure. */
	DRSYS_TYPE_SOCKADDR,            /**< struct sockaddr. */
	DRSYS_TYPE_MSGHDR,              /**< struct msghdr. */
	DRSYS_TYPE_MSGBUF,              /**< struct msgbuf. */
	DRSYS_TYPE_LARGE_INTEGER,       /**< LARGE_INTEGER structure. */
	DRSYS_TYPE_ULARGE_INTEGER,      /**< ULARGE_INTEGER structure. */
	DRSYS_TYPE_IO_STATUS_BLOCK,     /**< IO_STATUS_BLOCK structure. */
	DRSYS_TYPE_FUNCTION,            /**< Function of unspecified signature. */
	DRSYS_TYPE_BITMAPINFO,          /**< BITMAPINFO structure. */
	DRSYS_TYPE_ALPC_CONTEXT_ATTRIBUTES,/**< ALPC_CONTEXT_ATTRIBUTES structure. */
	DRSYS_TYPE_ALPC_MESSAGE_ATTRIBUTES,/**< ALPC_MESSAGE_ATTRIBUTES structure. */

	/* Additional types may be added in the future. */
	DRSYS_TYPE_LAST = DRSYS_TYPE_ALPC_MESSAGE_ATTRIBUTES,
} drsys_param_type_t;

/* Describes a library call parameter (inherited from drys_arg_t). */
typedef struct _drltrace_arg_t {
	/**
	* Whether operating pre-system call (if true) or post-system call (if false).
	* Set for the dynamic iterators only (drsys_iterate_args() and
	* drsys_iterate_memargs()).
	*/
	bool pre;

	/* Library call argument information ****************************/
	/** The ordinal of the parameter.  Set to -1 for a return value. */
	int ordinal;
	/** The mode (whether inlined, or read or written memory, etc.) of the parameter. */
	drsys_param_mode_t mode;
	/** The type of the parameter. */
	drsys_param_type_t type;
	/** A string further describing the type of the parameter.  May be NULL. */
	const char *type_name;
	/** A string describing the parameter.  This may be NULL. */
	const char *arg_name;
	/**
	* If not set to DR_REG_NULL, indicates which register the parameter's
	* value is stored in.
	*/
	reg_id_t reg;
	/**
	* For the arg iterator, holds the value of the parameter.
	* Unused for the memarg iterator.
	*
	* \deprecated For 32-bit applications, some platforms (namely
	* MacOS) support 64-bit arguments.  For such cases, this field
	* will hold only the bottom 32 bits of the value.  Use the \p
	* value64 field to retrieve the whole value.  For cross-platform
	* code, we recommend using \p value64 rather than this field.
	*/
	ptr_uint_t value;
	/**
	* For the memarg iterator, specifies the size in bytes of the memory region.
	* For the arg iterator, specifies the size in bytes of the parameter.
	*/
	size_t size;
	/**
	* Identical to \p value, except it holds the full value of the
	* parameter for the arg iterator for 32-bit applications on MacOS
	* when the value is an 8-byte type.  For cross-plaform code, we
	* recommend using this field rather than \p value.
	*
	* Unused for the memarg iterator.
	*/
	uint64 value64;
} drltrace_arg_t;


void parse_config(void);
std::vector<drltrace_arg_t *> *libcalls_search(const char *name);
void libcalls_hashtable_delete();
