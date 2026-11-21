param([Parameter(Mandatory=$true)][string]$Exe)
$ErrorActionPreference = 'Stop'
$exePath = (Resolve-Path $Exe).Path
$exeDir = Split-Path $exePath
$configPath = Join-Path $exeDir 'gui_config.txt'
if (Test-Path $configPath) { throw 'Smoke test requires a clean executable directory without user settings.' }
$root = Join-Path ([IO.Path]::GetTempPath()) ('naraka-gui-smoke-' + [Guid]::NewGuid().ToString('N'))
$program = Join-Path $root 'Naraka\program'
$audio = Join-Path $program 'NarakaBladepoint_Data\StreamingAssets\Audio\GeneratedSoundBanks\Windows'
$gameExe = Join-Path $program 'NarakaBladepoint.exe'
New-Item -ItemType Directory -Path $audio -Force | Out-Null
[IO.File]::WriteAllText($gameExe, '')
foreach ($name in @('ChineseCN_Common.pck', 'ChineseCN_Ingame.pck', 'ChineseCN_Outgame.pck', 'Init.pck', 'MusicInGame.pck', 'SoundActor1.pck')) {
    [IO.File]::WriteAllText((Join-Path $audio $name), 'path detection only')
}
[IO.File]::WriteAllText($configPath, "gameexe=$gameExe`njp=`nlangref=`nroles=`nsys=0`n", [Text.UTF8Encoding]::new($true))
Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class NarakaGuiSmoke {
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr window, int id);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr SendMessage(IntPtr window, uint message, IntPtr count, StringBuilder text);
    [DllImport("user32.dll")] [return: MarshalAs(UnmanagedType.Bool)] public static extern bool IsWindowEnabled(IntPtr window);
}
'@
$process = $null
try {
    $process = Start-Process -FilePath $exePath -WorkingDirectory $exeDir -PassThru
    if (-not $process.WaitForInputIdle(15000)) { throw 'GUI did not become idle in 15 seconds.' }
    $process.Refresh()
    $window = $process.MainWindowHandle
    if ($process.HasExited -or $window -eq [IntPtr]::Zero) { throw 'Release executable did not create its main window.' }
    $statusControl = [NarakaGuiSmoke]::GetDlgItem($window, 1121)
    $text = [Text.StringBuilder]::new(512)
    [void][NarakaGuiSmoke]::SendMessage($statusControl, 0x000D, [IntPtr]512, $text)
    if ($text.ToString() -notlike '*已定位中文语音*3*') { throw "Unexpected detection status: $text" }
    $applyButton = [NarakaGuiSmoke]::GetDlgItem($window, 1162)
    if ($applyButton -eq [IntPtr]::Zero -or [NarakaGuiSmoke]::IsWindowEnabled($applyButton)) { throw 'Replacement must remain disabled without target language packages.' }
    Write-Host "PASS: release EXE GUI launched; $text; replacement disabled without target language"
} finally {
    if ($process -and -not $process.HasExited) {
        [void]$process.CloseMainWindow()
        if (-not $process.WaitForExit(10000)) { $process.Kill(); $process.WaitForExit() }
    }
    if ((Split-Path $root) -ne ([IO.Path]::GetTempPath().TrimEnd('\'))) { throw 'Unexpected GUI smoke-test cleanup path.' }
    Remove-Item -LiteralPath $root -Recurse -Force
    if ($configPath -ne (Join-Path $exeDir 'gui_config.txt')) { throw 'Unexpected test config cleanup path.' }
    Remove-Item -LiteralPath $configPath -Force
}
