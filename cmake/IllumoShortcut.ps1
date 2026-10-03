# Writes a Windows shortcut (.lnk). Used by IllumoDistribution.cmake to leave a
# launcher for the default application beside IllumoRuntime.exe.
#   powershell -File IllumoShortcut.ps1 -Path <x.lnk> -Target <exe>
#     [-Arguments <text>] [-Icon <ico>] [-Description <text>]
#
# The shell stores the target as an absolute path and, separately, relative to
# the shortcut's folder. It follows the relative one when the absolute one no
# longer exists, so a distribution folder can be moved or zipped and the
# shortcut still starts the runtime beside it. WScript.Shell cannot write the
# relative path, hence IShellLink here.
param(
  [Parameter(Mandatory)][string]$Path,
  [Parameter(Mandatory)][string]$Target,
  [string]$Arguments = '',
  [string]$Icon = '',
  [string]$Description = ''
)
$ErrorActionPreference = 'Stop'

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;

[ComImport, Guid("00021401-0000-0000-C000-000000000046")]
public class IllumoShellLink {}

[ComImport, InterfaceType(ComInterfaceType.InterfaceIsIUnknown),
 Guid("000214F9-0000-0000-C000-000000000046")]
public interface IIllumoShellLink
{
  void GetPath([Out, MarshalAs(UnmanagedType.LPWStr)] StringBuilder file, int max, IntPtr data, uint flags);
  void GetIDList(out IntPtr list);
  void SetIDList(IntPtr list);
  void GetDescription([Out, MarshalAs(UnmanagedType.LPWStr)] StringBuilder text, int max);
  void SetDescription([MarshalAs(UnmanagedType.LPWStr)] string text);
  void GetWorkingDirectory([Out, MarshalAs(UnmanagedType.LPWStr)] StringBuilder directory, int max);
  void SetWorkingDirectory([MarshalAs(UnmanagedType.LPWStr)] string directory);
  void GetArguments([Out, MarshalAs(UnmanagedType.LPWStr)] StringBuilder text, int max);
  void SetArguments([MarshalAs(UnmanagedType.LPWStr)] string text);
  void GetHotkey(out short hotkey);
  void SetHotkey(short hotkey);
  void GetShowCmd(out int show);
  void SetShowCmd(int show);
  void GetIconLocation([Out, MarshalAs(UnmanagedType.LPWStr)] StringBuilder path, int max, out int index);
  void SetIconLocation([MarshalAs(UnmanagedType.LPWStr)] string path, int index);
  void SetRelativePath([MarshalAs(UnmanagedType.LPWStr)] string path, uint reserved);
  void Resolve(IntPtr window, uint flags);
  void SetPath([MarshalAs(UnmanagedType.LPWStr)] string path);
}

[ComImport, InterfaceType(ComInterfaceType.InterfaceIsIUnknown),
 Guid("0000010b-0000-0000-C000-000000000046")]
public interface IIllumoPersistFile
{
  void GetClassID(out Guid id);
  [PreserveSig] int IsDirty();
  void Load([MarshalAs(UnmanagedType.LPWStr)] string file, uint mode);
  void Save([MarshalAs(UnmanagedType.LPWStr)] string file, [MarshalAs(UnmanagedType.Bool)] bool remember);
  void SaveCompleted([MarshalAs(UnmanagedType.LPWStr)] string file);
  void GetCurFile([MarshalAs(UnmanagedType.LPWStr)] out string file);
}

public static class IllumoShortcutWriter
{
  public static void Write(string link, string target, string relative,
                           string directory, string arguments,
                           string description, string icon)
  {
    IIllumoShellLink shell = (IIllumoShellLink)new IllumoShellLink();
    shell.SetPath(target);
    shell.SetRelativePath(relative, 0);
    shell.SetWorkingDirectory(directory);
    if (arguments.Length != 0) shell.SetArguments(arguments);
    if (description.Length != 0) shell.SetDescription(description);
    shell.SetIconLocation(icon, 0);
    ((IIllumoPersistFile)shell).Save(link, true);
  }
}
'@

$link = [IO.Path]::GetFullPath($Path)
$target = [IO.Path]::GetFullPath($Target)
$folder = [IO.Path]::GetDirectoryName($link)
# SetRelativePath wants a path from the shortcut's folder, and the shell
# resolves it the same way.
# (Windows PowerShell 5.1 has no Path.GetRelativePath.)
$from = New-Object Uri ($folder.TrimEnd('\') + '\')
$to = New-Object Uri $target
$relative = [Uri]::UnescapeDataString($from.MakeRelativeUri($to).ToString()).Replace('/', '\')
if (-not $relative.StartsWith('.')) { $relative = ".\$relative" }

$iconPath = if ($Icon) { [IO.Path]::GetFullPath($Icon) } else { $target }
[IllumoShortcutWriter]::Write($link, $target, $relative,
  [IO.Path]::GetDirectoryName($target), $Arguments, $Description, $iconPath)
