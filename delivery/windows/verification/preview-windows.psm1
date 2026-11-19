Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

if (-not ('CordisPreview.Windows' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;
namespace CordisPreview {
    public sealed class ModuleRow { public string path; public long address; public uint size; }
    public sealed class WindowRow { public long hwnd; public uint pid; public string title; public uint dpi; }
    public sealed class TokenRow { public bool elevated; public int elevationType; public uint integrityRid; }
    public static class Windows {
        [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]
        private struct ModuleEntry {
            public uint size, moduleId, processId, globalUsage, processUsage;
            public IntPtr address; public uint imageSize; public IntPtr module;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst=256)] public string name;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst=260)] public string path;
        }
        [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]
        public struct OsVersion {
            public uint size, major, minor, build, platform;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst=128)] public string servicePack;
        }
        private delegate bool WindowCallback(IntPtr window, IntPtr parameter);
        [DllImport("kernel32.dll", SetLastError=true)] private static extern IntPtr CreateToolhelp32Snapshot(uint flags, uint pid);
        [DllImport("kernel32.dll", SetLastError=true)] private static extern IntPtr OpenProcess(uint access, bool inherit, uint pid);
        [DllImport("kernel32.dll", EntryPoint="K32GetModuleFileNameExW", ExactSpelling=true,
            CharSet=CharSet.Unicode, SetLastError=true)]
        private static extern uint ModuleFileName(IntPtr process, IntPtr module, StringBuilder path, uint capacity);
        [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] private static extern bool Module32FirstW(IntPtr snapshot, ref ModuleEntry entry);
        [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] private static extern bool Module32NextW(IntPtr snapshot, ref ModuleEntry entry);
        [DllImport("kernel32.dll", SetLastError=true)] private static extern bool CloseHandle(IntPtr handle);
        [DllImport("kernel32.dll")] public static extern uint SetErrorMode(uint mode);
        [DllImport("kernel32.dll")] public static extern uint GetACP();
        [DllImport("kernel32.dll")] private static extern IntPtr GetCurrentProcess();
        [DllImport("ntdll.dll")] private static extern int RtlGetVersion(ref OsVersion version);
        [DllImport("user32.dll", SetLastError=true)] private static extern bool EnumWindows(WindowCallback callback, IntPtr parameter);
        [DllImport("user32.dll")] private static extern bool IsWindowVisible(IntPtr window);
        [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr window, out uint pid);
        [DllImport("user32.dll", CharSet=CharSet.Unicode)] private static extern int GetWindowTextW(IntPtr window, StringBuilder text, int capacity);
        [DllImport("user32.dll")] private static extern uint GetDpiForWindow(IntPtr window);
        [DllImport("advapi32.dll", SetLastError=true)] private static extern bool OpenProcessToken(IntPtr process, uint access, out IntPtr token);
        [DllImport("advapi32.dll", SetLastError=true)] private static extern bool GetTokenInformation(IntPtr token, int kind, IntPtr data, int length, out int needed);
        [DllImport("advapi32.dll")] private static extern IntPtr GetSidSubAuthorityCount(IntPtr sid);
        [DllImport("advapi32.dll")] private static extern IntPtr GetSidSubAuthority(IntPtr sid, uint index);
        public static OsVersion Version() {
            OsVersion version = new OsVersion(); version.size = (uint)Marshal.SizeOf(typeof(OsVersion));
            int status = RtlGetVersion(ref version);
            if (status != 0) throw new InvalidOperationException("RtlGetVersion NTSTATUS=" + status);
            return version;
        }
        private static int TokenInt(IntPtr token, int kind) {
            IntPtr memory = Marshal.AllocHGlobal(4);
            try {
                int needed;
                if (!GetTokenInformation(token, kind, memory, 4, out needed)) throw new Win32Exception();
                return Marshal.ReadInt32(memory);
            } finally { Marshal.FreeHGlobal(memory); }
        }
        public static TokenRow Token() {
            IntPtr token;
            if (!OpenProcessToken(GetCurrentProcess(), 8, out token)) throw new Win32Exception();
            try {
                TokenRow result = new TokenRow();
                result.elevated = TokenInt(token, 20) != 0; result.elevationType = TokenInt(token, 18);
                int needed; GetTokenInformation(token, 25, IntPtr.Zero, 0, out needed);
                if (needed < IntPtr.Size) throw new Win32Exception();
                IntPtr memory = Marshal.AllocHGlobal(needed);
                try {
                    if (!GetTokenInformation(token, 25, memory, needed, out needed)) throw new Win32Exception();
                    IntPtr sid = Marshal.ReadIntPtr(memory); byte count = Marshal.ReadByte(GetSidSubAuthorityCount(sid));
                    if (count == 0) throw new InvalidOperationException("Empty integrity SID");
                    result.integrityRid = unchecked((uint)Marshal.ReadInt32(GetSidSubAuthority(sid, (uint)count - 1)));
                } finally { Marshal.FreeHGlobal(memory); }
                return result;
            } finally { CloseHandle(token); }
        }
        private static bool IsCompleteModulePath(uint length, int capacity, int actualLength) {
            // A null-terminated truncated result may occupy capacity-1 characters.
            return length > 0 && capacity > 1 && actualLength >= 0 &&
                length < (uint)(capacity - 1) && length == (uint)actualLength;
        }
        private static string WideModulePath(IntPtr process, IntPtr module) {
            for (int capacity = 512; capacity <= 32768; capacity *= 2) {
                StringBuilder path = new StringBuilder(capacity);
                uint length = ModuleFileName(process, module, path, (uint)capacity);
                if (length == 0) throw new Win32Exception(Marshal.GetLastWin32Error(), "Wide module path query failed.");
                string value = path.ToString();
                if (IsCompleteModulePath(length, capacity, value.Length)) return value;
            }
            throw new InvalidOperationException("Wide module path exceeds the observation limit.");
        }
        public static ModuleRow[] Modules(int pid) {
            // Toolhelp supplies module identity/base/size. Its stored path can lose
            // non-ACP characters, so query the real Unicode path from the same PID.
            IntPtr process = OpenProcess(0x410, false, (uint)pid);
            if (process == IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error(), "Cannot query module paths.");
            try {
                IntPtr snapshot = CreateToolhelp32Snapshot(0x18, (uint)pid);
                if (snapshot == new IntPtr(-1)) throw new Win32Exception();
                try {
                    ModuleEntry entry = new ModuleEntry(); entry.size = (uint)Marshal.SizeOf(typeof(ModuleEntry));
                    if (!Module32FirstW(snapshot, ref entry)) throw new Win32Exception();
                    List<ModuleRow> rows = new List<ModuleRow>();
                    do { rows.Add(new ModuleRow { path=WideModulePath(process, entry.module),
                        address=entry.address.ToInt64(), size=entry.imageSize }); }
                    while (Module32NextW(snapshot, ref entry));
                    int error = Marshal.GetLastWin32Error();
                    if (error != 18) throw new Win32Exception(error);
                    return rows.ToArray();
                } finally { CloseHandle(snapshot); }
            } finally { CloseHandle(process); }
        }
        public static WindowRow[] VisibleWindows(int pid) {
            List<WindowRow> rows = new List<WindowRow>();
            WindowCallback callback = delegate(IntPtr window, IntPtr unused) {
                uint owner; GetWindowThreadProcessId(window, out owner);
                if (owner == (uint)pid && IsWindowVisible(window)) {
                    StringBuilder text = new StringBuilder(512); GetWindowTextW(window, text, text.Capacity);
                    uint dpi = 0;
                    try { dpi = GetDpiForWindow(window); } catch (EntryPointNotFoundException) { }
                    rows.Add(new WindowRow { hwnd=window.ToInt64(), pid=owner, title=text.ToString(), dpi=dpi });
                }
                return true;
            };
            if (!EnumWindows(callback, IntPtr.Zero)) throw new Win32Exception();
            GC.KeepAlive(callback);
            return rows.ToArray();
        }
    }
}
'@ -ErrorAction Stop
}

function Resolve-PreviewDirectory([string]$Path) {
    $directory = Get-Item -LiteralPath ([IO.Path]::GetFullPath($Path)) -Force
    if ($directory -isnot [IO.DirectoryInfo]) { throw 'Expected a filesystem directory.' }
    $current = $directory
    while ($null -ne $current) {
        if (($current.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'Directory reparse points are not allowed.' }
        $current = $current.Parent
    }
    return $directory.FullName
}

function Get-PreviewFileSnapshot {
    param([Parameter(Mandatory=$true)][string]$Path)
    $pathValue = [IO.Path]::GetFullPath($Path)
    if (-not [IO.File]::Exists($pathValue)) {
        return [pscustomobject]@{ path=$pathValue; exists=$false; bytes=0; sha256=$null }
    }
    [void](Resolve-PreviewDirectory ([IO.Path]::GetDirectoryName($pathValue)))
    $file = Get-Item -LiteralPath $pathValue -Force
    if (($file.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'File reparse points are not allowed.' }
    $stream=[IO.File]::Open($pathValue,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
    $algorithm=[Security.Cryptography.SHA256]::Create()
    try { $digest=[BitConverter]::ToString($algorithm.ComputeHash($stream)).Replace('-','').ToLowerInvariant() }
    finally { $algorithm.Dispose(); $stream.Dispose() }
    return [pscustomobject]@{ path=$pathValue; exists=$true; bytes=$file.Length; sha256=$digest }
}

function Get-PreviewTreeSnapshot {
    param([Parameter(Mandatory=$true)][string]$Root)
    $rootValue = Resolve-PreviewDirectory $Root
    $prefix = $rootValue.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    $pending = [Collections.Generic.Queue[string]]::new(); $pending.Enqueue($rootValue)
    $result = [Collections.Generic.SortedDictionary[string,object]]::new([StringComparer]::OrdinalIgnoreCase)
    while ($pending.Count) {
        foreach ($item in @(Get-ChildItem -LiteralPath $pending.Dequeue() -Force)) {
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'Tree contains a reparse point.' }
            if ($item.PSIsContainer) { $pending.Enqueue($item.FullName); continue }
            $relative = $item.FullName.Substring($prefix.Length).Replace('\', '/')
            if ($result.ContainsKey($relative)) { throw 'Case-colliding file paths.' }
            $snapshot = Get-PreviewFileSnapshot $item.FullName
            $result.Add($relative, [pscustomobject]@{ bytes=$snapshot.bytes; sha256=$snapshot.sha256 })
        }
    }
    return ,$result
}

function Get-PreviewHostFacts {
    $errors = [Collections.Generic.List[string]]::new()
    $version = $null; $token = $null
    try { $version = [CordisPreview.Windows]::Version() } catch { $errors.Add($_.Exception.Message) }
    try { $token = [CordisPreview.Windows]::Token() } catch { $errors.Add($_.Exception.Message) }
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    try { $userName=$identity.Name; $userSid=$identity.User.Value } finally { $identity.Dispose() }
    $ownProcess=[Diagnostics.Process]::GetCurrentProcess()
    try { $sessionId=$ownProcess.SessionId } finally { $ownProcess.Dispose() }
    $local = [Environment]::GetFolderPath([Environment+SpecialFolder]::LocalApplicationData)
    $environment = [ordered]@{}
    foreach ($entry in [Environment]::GetEnvironmentVariables().GetEnumerator()) {
        if ($entry.Key -match '^(PATH|QT.*|QML.*|QTDIR|CMAKE_PREFIX_PATH|VSINSTALLDIR|VCINSTALLDIR|VSCMD_ARG_TGT_ARCH|INCLUDE|LIB|LIBPATH)$') {
            $value = [string]$entry.Value
            $environment[$entry.Key] = $value.Substring(0, [Math]::Min(8192, $value.Length))
        }
    }
    $commands = [ordered]@{}
    foreach ($name in @('cl.exe','cmake.exe','ninja.exe','qmake.exe','python.exe','py.exe')) {
        $found = @(Get-Command $name -CommandType Application -ErrorAction SilentlyContinue)
        $commands[$name] = @($found | ForEach-Object { $_.Source })
    }
    $installed = [Collections.Generic.List[string]]::new()
    foreach ($key in @('HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*',
                       'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*',
                       'HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*')) {
        foreach ($entry in @(Get-ItemProperty -Path $key -ErrorAction SilentlyContinue)) {
            if ($entry.PSObject.Properties['DisplayName'] -and $entry.DisplayName -match 'Visual Studio|Build Tools|Qt |Python|CMake') {
                if ($installed.Count -lt 100) { $installed.Add([string]$entry.DisplayName) }
            }
        }
    }
    return [pscustomobject]@{ machine=[Environment]::MachineName; user=$userName; sid=$userSid;
        utc=[DateTime]::UtcNow.ToString('o'); os=$version; token=$token; process64=[Environment]::Is64BitProcess;
        os64=[Environment]::Is64BitOperatingSystem; interactive=[Environment]::UserInteractive;
        sessionId=$sessionId; acp=[CordisPreview.Windows]::GetACP();
        culture=[Globalization.CultureInfo]::CurrentCulture.Name; powershell=$PSVersionTable.PSVersion.ToString();
        preferencesFile=(Join-Path $local 'CordisConfigWorkbench/ui-preferences.json');
        originalEnvironment=$environment; commandPaths=$commands; installedDevelopmentNames=@($installed.ToArray());
        installationAbsenceProven=$false; errors=@($errors.ToArray()) }
}

function Get-PreviewObservation {
    param([Parameter(Mandatory=$true)][ValidateRange(1,2147483647)][int]$ProcessId)
    $errors = [Collections.Generic.List[string]]::new(); $modules = @(); $windows = @()
    try { $modules = @([CordisPreview.Windows]::Modules($ProcessId) | ForEach-Object {
        [pscustomobject]@{ path=$_.path; base=('0x{0:x}' -f $_.address); size=$_.size }
    }) } catch { $errors.Add('modules: ' + $_.Exception.Message) }
    try { $windows = @([CordisPreview.Windows]::VisibleWindows($ProcessId)) } catch { $errors.Add('windows: ' + $_.Exception.Message) }
    return [pscustomobject]@{ pid=$ProcessId; utc=[DateTime]::UtcNow.ToString('o'); modules=$modules;
        windows=$windows; errors=@($errors.ToArray()); liveDllFilesRead=$false }
}

function New-PreviewProcessStartInfo {
    param([Parameter(Mandatory=$true)][string]$Executable, [string[]]$Arguments=@(),
          [Parameter(Mandatory=$true)][string]$WorkingDirectory)
    $pathValue = [IO.Path]::GetFullPath($Executable)
    if (-not [IO.File]::Exists($pathValue)) { throw 'Executable does not exist.' }
    [void](Get-PreviewFileSnapshot $pathValue)
    $cwd = Resolve-PreviewDirectory $WorkingDirectory
    $quoted = foreach ($argument in $Arguments) {
        # CommandLineToArgvW/MSVC quoting, including trailing backslashes.
        '"' + ([regex]::Replace([regex]::Replace($argument, '(\\*)"', '$1$1\"'), '(\\+)$', '$1$1')) + '"'
    }
    $info = [Diagnostics.ProcessStartInfo]::new($pathValue, ($quoted -join ' '))
    $info.WorkingDirectory=$cwd; $info.UseShellExecute=$false; $info.CreateNoWindow=$true
    $info.RedirectStandardOutput=$true; $info.RedirectStandardError=$true
    foreach ($key in @($info.EnvironmentVariables.Keys)) {
        if ($key -match '^(QT.*|QML.*|QTDIR|CMAKE_PREFIX_PATH|VSINSTALLDIR|VCINSTALLDIR|VSCMD.*|INCLUDE|LIB|LIBPATH)$') {
            $info.EnvironmentVariables.Remove($key)
        }
    }
    $windowsRoot = [Environment]::GetEnvironmentVariable('SystemRoot')
    $info.EnvironmentVariables['PATH'] = (Join-Path $windowsRoot 'System32') + ';' + $windowsRoot + ';' + (Join-Path $windowsRoot 'System32/Wbem')
    $info.EnvironmentVariables['QT_QPA_PLATFORM']='windows'
    return $info
}

function Set-PreviewErrorMode {
    param([Parameter(Mandatory=$true)][uint32]$Mode)
    return [CordisPreview.Windows]::SetErrorMode($Mode)
}

function New-PreviewLogReader {
    param([Parameter(Mandatory=$true)]$InputStream,[Parameter(Mandatory=$true)][string]$Path)
    $buffer=[byte[]]::new(65536)
    return @{ Input=$InputStream; File=[IO.File]::Open($Path,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write);
        Buffer=$buffer; Pending=$InputStream.ReadAsync($buffer,0,$buffer.Length); seen=0L; saved=0L; eof=$false; truncated=$false }
}

function Receive-PreviewLog {
    param([Parameter(Mandatory=$true)]$Reader)
    $turns=0
    while (-not $Reader.eof -and $Reader.Pending.IsCompleted -and $turns -lt 32) {
        $count=$Reader.Pending.GetAwaiter().GetResult(); $turns+=1
        if ($count -eq 0) { $Reader.eof=$true; break }
        $Reader.seen+=$count
        $save=[int][Math]::Min($count,[Math]::Max(0L,4194304L-$Reader.saved))
        if ($save) { $Reader.File.Write($Reader.Buffer,0,$save); $Reader.saved+=$save }
        if ($save -lt $count) { $Reader.truncated=$true }
        $Reader.Pending=$Reader.Input.ReadAsync($Reader.Buffer,0,$Reader.Buffer.Length)
    }
}

Export-ModuleMember -Function Get-PreviewHostFacts,Get-PreviewObservation,New-PreviewProcessStartInfo,
    Get-PreviewFileSnapshot,Get-PreviewTreeSnapshot,Set-PreviewErrorMode,Resolve-PreviewDirectory,
    New-PreviewLogReader,Receive-PreviewLog
