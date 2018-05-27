# **********************************************************
# Copyright (c) 2018 Maksim Shudrak. All rights reserved.
# Copyright (c) 2010-2017 Google, Inc.    All rights reserved.
# Copyright (c) 2009-2010 VMware, Inc.    All rights reserved.
# **********************************************************

# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# * Redistributions of source code must retain the above copyright notice,
#   this list of conditions and the following disclaimer.
#
# * Redistributions in binary form must reproduce the above copyright notice,
#   this list of conditions and the following disclaimer in the documentation
#   and/or other materials provided with the distribution.
#
# * Neither the name of VMware, Inc. nor the names of its contributors may be
#   used to endorse or promote products derived from this software without
#   specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL VMWARE, INC. OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
# DAMAGE.

cmake_minimum_required(VERSION 3.2)

function (append_property_string type target name value)
  # XXX: if we require cmake 2.8.6 we can simply use APPEND_STRING
  get_property(cur ${type} ${target} PROPERTY ${name})
  if (cur)
    set(value "${cur} ${value}")
  endif (cur)
  set_property(${type} ${target} PROPERTY ${name} "${value}")
endfunction (append_property_string)

function (set_output_dirs dir)
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${dir}" PARENT_SCOPE)
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${dir}" PARENT_SCOPE)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${dir}" PARENT_SCOPE)
  if ("${CMAKE_GENERATOR}" MATCHES "Visual Studio")
    # we don't support the Debug and Release subdirs
    foreach (config ${CMAKE_CONFIGURATION_TYPES})
      string(TOUPPER "${config}" config_upper)
      set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config_upper}
        "${dir}" PARENT_SCOPE)
      set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${config_upper}
        "${dir}" PARENT_SCOPE)
      set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${config_upper}
        "${dir}" PARENT_SCOPE)
    endforeach ()
  endif ()
endfunction (set_output_dirs)

# To match Makefiles and have just one build type per configured build
# dir, we collapse VS generator configs to a single choice.
# This must be done prior to the project() command and the var
# must be set in the cache.
if ("${CMAKE_GENERATOR}" MATCHES "Visual Studio")
  if (DEBUG OR "${CMAKE_BUILD_TYPE}" MATCHES "Debug")
    set(CMAKE_CONFIGURATION_TYPES "Debug" CACHE STRING "" FORCE)
  else ()
    # Go w/ debug info (DRMemory i#1392)
    set(CMAKE_CONFIGURATION_TYPES "RelWithDebInfo" CACHE STRING "" FORCE)
  endif ()
  # we want to use the _LOCATION_<config> property
  string(TOUPPER "${CMAKE_CONFIGURATION_TYPES}" upper)
  set(location_suffix "_${upper}")
else ("${CMAKE_GENERATOR}" MATCHES "Visual Studio")
  set(location_suffix "")
endif ("${CMAKE_GENERATOR}" MATCHES "Visual Studio")

project(drltrace)

if ("${CMAKE_VERSION}" VERSION_EQUAL "3.0" OR
    "${CMAKE_VERSION}" VERSION_GREATER "3.0")
  # XXX DrMemory i#1557: update our code to satisfy the changes in 3.x
  cmake_policy(SET CMP0026 OLD)
  # XXX DrMemory i#1375: if we make 2.8.12 the minimum we can remove the @rpath
  # Mac stuff and this policy, right?
  cmake_policy(SET CMP0042 OLD)
endif ()

# setup everything to be able to run drltrace

if (CMAKE_C_SIZEOF_DATA_PTR EQUAL 8 OR CMAKE_CXX_SIZEOF_DATA_PTR EQUAL 8)
  set(X64 ON)
  set(LIB_ARCH "lib64")
  set(BIN_ARCH "bin64")
  set(DEFINES ${DEFINES} -DX64)
else()
  set(X64 OFF)
  set(LIB_ARCH "lib32")
  set(BIN_ARCH "bin")
endif ()

if ("${CMAKE_BUILD_TYPE}" MATCHES "Debug")
    set(build_type "debug")
else()
    set(build_type "release")
endif()

set(output_dir "${PROJECT_BINARY_DIR}/${BIN_ARCH}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${output_dir}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${output_dir}")
if ("${CMAKE_GENERATOR}" MATCHES "Visual Studio")
  # we don't support the Debug and Release subdirs
  foreach (config ${CMAKE_CONFIGURATION_TYPES})
    string(TOUPPER "${config}" config_upper)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config_upper}
      "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${config_upper}
      "${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${config_upper}
      "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")
  endforeach ()
endif ()

option(SHOW_RESULTS
  "Display client results in pop-up (Windows) or console message (Linux)"
  ${SHOW_RESULTS_DEFAULT})
if (SHOW_RESULTS)
  add_definitions(-DSHOW_RESULTS)
endif (SHOW_RESULTS)

option(SHOW_SYMBOLS "Use symbol lookup in clients that support it" ON)
if (SHOW_SYMBOLS)
  add_definitions(-DSHOW_SYMBOLS)
endif (SHOW_SYMBOLS)
if (NOT DEFINED GENERATE_PDBS)
  # support running tests over ssh where pdb building is problematic
  set(GENERATE_PDBS ON)
endif (NOT DEFINED GENERATE_PDBS)

# DrMemory i#379: We usually want to build the samples with optimizations to improve the
# chances of inlining, but it's nice to be able to turn that off easily.  A
# release build should already have optimizations, so this should only really
# affect debug builds.
option(OPTIMIZE_SAMPLES
  "Build samples with optimizations to increase the chances of clean call inlining (overrides debug flags)"
  ON)
if (WIN32)
  set(OPT_CFLAGS "/O2")
else (WIN32)
  set(OPT_CFLAGS "-O2")
endif (WIN32)

if (DEBUG)
  set(OPT_CFLAGS "-DDEBUG")
endif (DEBUG)

if (DEFINED DynamoRIO_DIR)
  message(STATUS "Attempting to use pre-built DynamoRIO: ${DynamoRIO_DIR}")
  set(USER_SPECIFIED_DynamoRIO_DIR ON)
  set(user_DynamoRIO_DIR "dynamorio/cmake" CACHE PATH "DynamoRIO installation's cmake directory")
  get_filename_component(DynamoRIO_DIR "${PROJECT_BINARY_DIR}/${user_DynamoRIO_DIR}" ABSOLUTE)
  find_package(DynamoRIO)
  if (NOT DynamoRIO_FOUND)
    message(FATAL_ERROR "DynamoRIO package required to build")
  endif(NOT DynamoRIO_FOUND)
else ()
  set(USER_SPECIFIED_DynamoRIO_DIR OFF)
  set(DynamoRIO_DIR "${PROJECT_BINARY_DIR}/dynamorio/cmake" CACHE PATH "Path to DynamoRIO.")
  message(STATUS "Building DynamoRIO from local sources ${DynamoRIO_DIR}")
  if ("${CMAKE_BUILD_TYPE}" MATCHES "Debug")
    set(DEBUG ON CACHE BOOL "DynamoRIO option: debug build")
    set(INTERNAL ON CACHE BOOL "DynamoRIO option: internal build")
  endif ("${CMAKE_BUILD_TYPE}" MATCHES "Debug")
  # We do not want DR install, except for the targets we need for our
  # multi-export-set install of the DRMF.  We need those targets
  # even for BUILDING_SUB_PACKAGE.
  set(DO_DR_INSTALL OFF)
  set(DO_DR_INSTALL_TARGETS ON)
  # Stick the binaries somewhere outside of the install dir.
  # However, NSIS won't allow an absolute path (DrMemory i#1099).
  # So for an automated package.cmake build via cpack, we use .. which
  # is fine in the package dir structure.  We don't want .. for a
  # user-specified destination of course.  We simply don't
  # support creating a package manually outside of package.cmake.
  if (BUILDING_PACKAGE)
    set(DR_INSTALL_TARGETS_DEST ../ignored-installs)
  else ()
    set(DR_INSTALL_TARGETS_DEST ${PROJECT_BINARY_DIR}/dynamorio/installs)
  endif ()
  # DrMemory i#1449: we need cmake to remove the absolute path from the LC_LOAD_DYLIB
  # entries, which only happens on install.  We can't easily do a two-step
  # install b/c subdirs go after the main dir -- so we have DR cooperation.
  set(DR_INSTALL_DEPLOY_BIN_DEST ${DR_install_dir}/${BIN_ARCH})

  add_subdirectory(dynamorio)
  
  # don't show DR options in drmem cmake list
  # to really hide we should mark as INTERNAL but not worth it since would
  # have to do for all of DR's many options.
  # see comment above about DR prefixing its options.
  mark_as_advanced(BUILD_CORE BUILD_DOCS BUILD_SAMPLES BUILD_EXT BUILD_TESTS
      BUILD_TOOLS DEBUG INTERNAL)

  # do not import dynamorio lib target: we'd end up w/ duplicate
  # dynamorio targets
  set(DynamoRIO_INTERNAL ON)
  # our included DynamoRIO project will set DynamoRIO_SOURCE_DIR in cache
  # for us so we'll get proper include dirs for extensions.

  find_package(DynamoRIO ${DynamoRIO_VERSION_REQUIRED})
  if (NOT DynamoRIO_FOUND OR
      # make sure it didn't go find some other pre-built version after
      # seeing that the local one is somehow not suitable
      NOT "${DynamoRIO_CONFIG}" STREQUAL "${DynamoRIO_DIR}/DynamoRIOConfig.cmake")
    message(FATAL_ERROR "Local DynamoRIO mis-configured")
  endif ()

  # Restore global flags
  foreach (config "" ${CMAKE_BUILD_TYPE} ${CMAKE_CONFIGURATION_TYPES})
    if ("${config}" STREQUAL "")
      set(config_upper "")
    else ("${config}" STREQUAL "")
      string(TOUPPER "_${config}" config_upper)
    endif ("${config}" STREQUAL "")
    foreach (var CMAKE_C_FLAGS${config_upper};CMAKE_CXX_FLAGS${config_upper})
      set(${var} "${SAVE_${var}}")
    endforeach (var)
  endforeach (config)
  
endif ()

if (WIN32)
  # disable stack protection: "unresolved external symbol ___security_cookie"
  # disable the warning "unreferenced formal parameter" #4100
  # disable the warning "conditional expression is constant" #4127
  # disable the warning "cast from function pointer to data pointer" #4054
  set(CL_CFLAGS "/GS- /wd4100 /wd4127 /wd4054")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${CL_CFLAGS}")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CL_CFLAGS}")
  add_definitions(-D_CRT_SECURE_NO_WARNINGS)
endif (WIN32)

if (OPTIMIZE_SAMPLES)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${OPT_CFLAGS}")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OPT_CFLAGS}")
endif ()

#include(${PROJECT_SOURCE_DIR}/make/policies.cmake NO_POLICY_SCOPE)

##################################################
# drltracelib

set(srcs
    drltrace.cpp
    drltrace_libcalls.cpp
    drltrace_options.cpp
    drltrace_utils.cpp)

set(DynamoRIO_USE_LIBC OFF)

set(TOOLNAME "drltrace")

add_library(drltracelib SHARED ${srcs})

# We share the framework version # for now
#set_library_version(drltracelib ${DRMF_VERSION_MAJOR_MINOR})

if (WIN32)
  set_property(TARGET drltracelib PROPERTY COMPILE_DEFINITIONS
    "${DEFINES_NO_D};RC_IS_DRLTRACELIB")
else ()
  set_property(TARGET drltracelib PROPERTY COMPILE_DEFINITIONS ${DEFINES_NO_D})
endif ()

set(DynamoRIO_RPATH ON)
set_target_properties(drltracelib PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY${location_suffix} "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")

  
configure_DynamoRIO_client(drltracelib)

use_DynamoRIO_extension(drltracelib drmgr_static)
use_DynamoRIO_extension(drltracelib drwrap_static)
use_DynamoRIO_extension(drltracelib drx_static)
use_DynamoRIO_extension(drltracelib drcovlib_static)
use_DynamoRIO_extension(drltracelib droption)

# XXX DrMemory i#2429: DR's droption.h includes both <string.h> and <string.h>
# we should remove <string.h> and replace the strcmp comparison with
# a std::string. Then we can remove special if here and below.
if (UNIX)
  # to avoid conflicts with new declaration with "C" linkage in utils.h
  append_property_string(TARGET drltracelib COMPILE_FLAGS "-DNOLINK_STRCASESTR")
endif()
if ("${CMAKE_GENERATOR}" MATCHES "Visual Studio")
  # DrMemory i#1805: see comment in drstrace/CMakeLists.txt
  append_property_string(TARGET drltracelib LINK_FLAGS "/force:multiple")
endif ()

if (WIN32)
  if (USER_SPECIFIED_DynamoRIO_DIR)
    # already built so we can copy at config time
    configure_file("${DynamoRIO_DIR}/../${LIB_ARCH}/drconfiglib.dll"
      "${output_dir}/drconfiglib.dll" COPYONLY)
    configure_file("${DynamoRIO_DIR}/../${LIB_ARCH}/drinjectlib.dll"
      "${output_dir}/drinjectlib.dll" COPYONLY)
    configure_file("${DynamoRIO_DIR}/../${LIB_ARCH}/${build_type}/dynamorio.dll"
        "${output_dir}/dynamorio.dll" COPYONLY)
  else (USER_SPECIFIED_DynamoRIO_DIR)
    # XXX: I can't get "TARGET drconfiglib POST_BUILD" to work, maybe b/c
    # the target is in a subdir?
    set(drconfiglib_copy "${output_dir}/drconfiglib.dll")
    add_custom_target(drconfiglib_copy_tgt ALL DEPENDS "${drconfiglib_copy}")
    add_custom_command(OUTPUT "${drconfiglib_copy}" DEPENDS drconfiglib
      COMMAND ${CMAKE_COMMAND}
      ARGS -E copy "${DynamoRIO_DIR}/../${LIB_ARCH}/drconfiglib.dll"
      "${drconfiglib_copy}" VERBATIM)
    set(drinjectlib_copy "${output_dir}/drinjectlib.dll")
    add_custom_target(drinjectlib_copy_tgt ALL DEPENDS "${drinjectlib_copy}")
    add_custom_command(OUTPUT "${drinjectlib_copy}" DEPENDS drinjectlib
      COMMAND ${CMAKE_COMMAND}
      ARGS -E copy "${DynamoRIO_DIR}/../${LIB_ARCH}/drinjectlib.dll"
      "${drinjectlib_copy}" VERBATIM)
    set(drdll_copy "${output_dir}/dynamorio.dll")
    add_custom_target(drdll_copy_tgt ALL DEPENDS "${drdll_copy}")
    add_custom_command(OUTPUT "${drdll_copy}" DEPENDS dynamorio
        COMMAND ${CMAKE_COMMAND}
        ARGS -E copy "${DynamoRIO_DIR}/../${LIB_ARCH}/${build_type}/dynamorio.dll"
        "${drdll_copy}" VERBATIM)
  endif (USER_SPECIFIED_DynamoRIO_DIR)
endif (WIN32)

##################################################
# drltrace frontend

set(front_srcs
    drltrace_frontend.cpp
    drltrace_options.cpp)
#if (WIN32)
#  set(front_srcs ${front_srcs} ${PROJECT_SOURCE_DIR}/make/resources.rc)
#endif ()

add_executable(drltrace ${front_srcs})

configure_DynamoRIO_standalone(drltrace)

target_link_libraries(drltrace drinjectlib drconfiglib drfrontendlib)

#set_library_version(drltrace ${DRMF_VERSION})

if (WIN32)
  set_property(TARGET drltrace PROPERTY COMPILE_DEFINITIONS
               ${DEFINES_NO_D} RC_IS_DRLTRACE)
else ()
  set_property(TARGET drltrace PROPERTY COMPILE_DEFINITIONS ${DEFINES_NO_D})
endif ()

# XXX: see DR's i#2429 and comment above.
if (UNIX)
  # to avoid conflicts with new declaration with "C" linkage in utils.h
  append_property_string(TARGET drltrace COMPILE_FLAGS "-DNOLINK_STRCASESTR")
endif()

##################################################
# drltrace config

set(conf_out ${PROJECT_BINARY_DIR}/${BIN_ARCH}/drltrace.config)
if (WIN32)
  configure_file(${CMAKE_CURRENT_SOURCE_DIR}/drltrace_win.config ${conf_out} COPYONLY)
else ()
  configure_file(${CMAKE_CURRENT_SOURCE_DIR}/drltrace_linux.config ${conf_out} COPYONLY)
endif ()

##################################################
# drltrace tests

function(tobuild name source)
  set(srcs ${source})
  add_executable(${name} ${srcs})
endfunction(tobuild)

function (get_target_path_for_execution out target)
  if (ANDROID)
    DynamoRIO_get_target_path_for_execution(local ${target} ${DRM_DEVICE_BASEDIR})
  else ()
    DynamoRIO_get_target_path_for_execution(local ${target} "")
  endif ()
  set(${out} ${local} PARENT_SCOPE)
endfunction (get_target_path_for_execution)

function (prefix_cmd_if_necessary cmd_out use_ats cmd_in)
  DynamoRIO_prefix_cmd_if_necessary(local ${use_ats} ${cmd_in} ${ARGN})
  set(${cmd_out} ${local} PARENT_SCOPE)
endfunction (prefix_cmd_if_necessary)

# XXX DrMemory i#1960: add more tests for drltrace that check the output contains
# expected library calls.
#
if (BUILD_TOOL_TESTS)
  message(STATUS "building tests")
  enable_testing()

  tobuild(drltrace_app drsyscall_app.c)

  get_target_path_for_execution(drltrace_path drltrace)
  get_target_path_for_execution(app_path drltrace_app)

  prefix_cmd_if_necessary(drltrace_path OFF ${drltrace_path})
  add_test(drltrace ${drltrace_path} -- ${app_path})
  add_test(drltrace_libcalls ${drltrace_path} -logdir - -print_ret_addr -- ${app_path})
  add_test(drltrace_symargs ${drltrace_path} -logdir - -num_max_args 4 -- ${app_path})

  add_test(drltrace_libargs ${drltrace_path} -logdir - -only_from_app -- ${app_path})

  #regex strings for libcalls and arguments printing test
  set(libcall_ret "    and return to module id:([0-9]+), offset:0x([0-9a-f]+)\n")

if (WIN32)
  set(libcall_args1_01 "    arg 0: 0x([0-9a-f]+) => 0x([0-9a-f]+) \\(type=void\\*, size=0x([0-9a-f]+)\\)\n")
  set(libcall_args1_02 "    arg 2: 0x([0-9a-f]+) \\(type=size_t, size=0x([0-9a-f]+)\\)\n")

  set(libcall_args2_0 "    arg 0: 0x([0-9a-f]+) \\(type=HANDLE, size=0x([0-9a-f]+)\\)\n")
  set(libcall_args2_1 "    arg 1: 0x([0-9a-f]+) \\(type=int, size=0x([0-9a-f]+)\\)\n")

  set(libcall_name1 "~~([0-9a-f]+)~~ KERNELBASE.dll!VirtualQuery\n")
  set(libcall_name2 "~~([0-9a-f]+)~~ ntdll.dll!ZwQueryInformationProcess\n")
  set(libcall_printf "arg 0: done\n \\(type=char\\*, size=0x([0-9a-f]+)\\)\n")
  # some machines use WriteFile instead of printf, trying to handle both
  set(libcall_WriteFile "arg 2: 0x([0-9a-f]+) \\(type=DWORD, size=0x([0-9a-f]+)\\)\n")
  set(libcall_both_variants "${libcall_printf}\;${libcall_WriteFile}")
else ()
  set(libcall_both_variants "    arg 0: done \\(type=char\\*, size=0x([0-9a-f]+)\\)\n")
  set(libcall_args1_01 ${libcall_both_variants})

  set(libcall_args2_0 "    arg 0: /dev/null \\(type=char \\*\\*, size=0x([0-9a-f]+)\\)\n")
  set(libcall_args2_1 "    arg 1: 0x([0-9a-f]+) \\(type=<unknown>\\*, size=0x([0-9a-f]+)\\)\n")

  set(libcall_name1 "~~([0-9a-f]+)~~ libc.so.6!puts\n")
  set(libcall_name2 "~~([0-9a-f]+)~~ libc.so.6!open\n")
endif(WIN32)
  set_tests_properties(drltrace_libcalls PROPERTIES PASS_REGULAR_EXPRESSION
                       ${libcall_name1}${libcall_args1_01}${libcall_args1_02}${libcall_ret})
  set_tests_properties(drltrace_symargs PROPERTIES PASS_REGULAR_EXPRESSION
                       ${libcall_name2}${libcall_args2_0}${libcall_args2_1})
  set_tests_properties(drltrace_libargs PROPERTIES PASS_REGULAR_EXPRESSION
                       ${libcall_both_variants})
endif (BUILD_TOOL_TESTS)
