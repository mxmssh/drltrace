<#
## This script checks if the PE file given is compiled to 32 or 64-bits
## and call the right version of drltrace.
## It expects to have drltrace_win32 and drltrace_win64 folders at the same
## directory level.
#>

# Optional default parameters for drltrace
#Set-Variable drltrace_params -option Constant '-print_ret_addr -only_from_app'

function Get-ScriptDirectory {
    Split-Path -parent $PSCommandPath
}

function Get-PE-Arch($path) {
    $buf = New-Object byte[] 512
    $fs = New-Object IO.FileStream($path, [IO.FileMode]::Open, [IO.FileAccess]::Read)
    $num = 0
    $fs.Read($buf, $num, 512) > $null
    $fs.Close()
    
    # Check MZ header
    if ($buf[0] -ne 0x4d -or $buf[1] -ne 0x5a) {
        return ""
    }
    
    # DOS header has a pointer to the PE signature at 0x3c
    $pesig = [System.BitConverter]::ToUInt32($buf, 0x3c)
    # PE signature is 4 bytes long and it's followed by a 2-byte Machine ID
    $machine = [System.BitConverter]::ToUInt16($buf, $pesig + 4)
    
    if ($machine -eq 0x8664) {
        return 64
    }
    return 32
}

if ($args.count -lt 1) {
    cmd /c "$(Get-ScriptDirectory)\drltrace_win32\bin\drltrace.exe"
    exit
}

# Set $file to the *last* argument given
$file = $args[$args.count - 1]

if (![System.IO.File]::Exists($file)) {
    # If the file does not exist, call drltrace and let it handles that
    cmd /c "$(Get-ScriptDirectory)\drltrace_win32\bin\drltrace.exe $args"
    exit
}

$arch = Get-PE-Arch($file)

if ($arch -eq 32) {
    cmd /c "$(Get-ScriptDirectory)\drltrace_win32\bin\drltrace.exe $drltrace_params $args"
} elseif ($arch -eq 64) {
    cmd /c "$(Get-ScriptDirectory)\drltrace_win64\bin64\drltrace.exe $drltrace_params $args"
}