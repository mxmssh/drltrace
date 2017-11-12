# drltrace
Drltrace is a dynamic API calls tracer for Windows and Linux applications. Drltrace is built on top of [DynamoRIO](http://www.dynamorio.org/) dynamic binary instrumentation framework.

# Source code
Source code is temporary distributed within Dr.Memory tool and can be found under the following link:
https://github.com/DynamoRIO/drmemory/tree/master/drltrace

# Usage Example
```
drltrace -logdir . -- calc.exe
```
# Command line options
```
 -logdir              [     .]  Log directory to print library call data
 -only_from_app       [ false]  Reports only library calls from the app
 -follow_children     [  true]  Trace child processes
 -print_ret_addr      [ false]  Print library call's return address
 -num_unknown_args    [     2]  Number of unknown libcall args to print
 -num_max_args        [     6]  Maximum number of arguments to print
 -default_config      [  true]  Use default config file.
 -config              [    ""]  The path to custom config file.
 -ignore_underscore   [ false]  Ignores library routine names starting with "_".
 -only_to_lib         [    ""]  Only reports calls to the library <lib_name>.
 -help                [ false]  Print this message.
 -version             [ false]  Print version number.
 -verbose             [     1]  Change verbosity.
 -use_config          [  true]  Use config file
 ```
# Configuration file syntax
Drltrace supports external configuration files where a user can describe how drltrace should print arguments for certain API calls.
```
HANDLE|CreateRemoteThread|HANDLE|SECURITY_ATTRIBUTES*|size_t|THREAD_START_ROUTINE*|VOID*|DWORD|__out DWORD*
```
Each function argument should be separated by ```|```. The first argument is return type, the second argument is a function name itself and the rest are the function arguments. A token ```__out``` is used to mark output arguments and ```___inout``` is used to mark input+output arguments.

