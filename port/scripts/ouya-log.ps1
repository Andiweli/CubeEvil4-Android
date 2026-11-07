<#
.SYNOPSIS
Holt das Protokoll der laufenden OUYA-App (Stufe O3), ohne sie neu zu starten.
.DESCRIPTION
Unter der gemeinsamen ADB-Sperre (Global\Retroid_ADB_Exclusive): logcat (was Android seit dem
letzten Start der App aufgehoben hat) und /sdcard/RE4-OUYA/core.log. Die App laeuft weiter.
Ergebnis in -OutDir (Standard: Desktop\RE4-OUYA-Probe\log): logcat.txt, core.log, app.txt (nur
die Zeilen der App: Bilder je 10 s, Zeit des Renderers, Shader, Texturen).
.EXAMPLE
.\port\scripts\ouya-log.ps1
#>
param(
    [string]$Serial = '015d46d9751c1613',
    [string]$Adb = '',
    [string]$OutDir = (Join-Path ([Environment]::GetFolderPath('Desktop')) 'RE4-OUYA-Probe\log')
)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
if (-not $Adb) {
    $Adb = Join-Path $env:LOCALAPPDATA 'Android\Sdk\platform-tools\adb.exe'
    if (-not (Test-Path -LiteralPath $Adb)) { $Adb = 'adb' }
}
. (Join-Path $PSScriptRoot 'Retroid-Adb-Exclusive.ps1')
function Invoke-Adb {
    $old = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try { return @(& $Adb -s $Serial @args 2>&1 | ForEach-Object { "$_".TrimEnd("`r") }) } finally { $ErrorActionPreference = $old }
}
Invoke-RetroidAdbExclusive -ThrowOnUnavailable -LockTimeoutMs 60000 -Sequence {
    Invoke-Adb logcat -d -v time | Set-Content -LiteralPath (Join-Path $OutDir 'logcat.txt') -Encoding UTF8
    Invoke-Adb pull '/sdcard/RE4-OUYA/core.log' (Join-Path $OutDir 'core.log') | Out-Null
}
$lines = @(Get-Content -LiteralPath (Join-Path $OutDir 'logcat.txt') -Encoding UTF8)
$mine = @($lines | Where-Object { $_ -match 'RE4OUYA|RE4GX2|FATAL|DEBUG\s*\(' })
$mine | Set-Content -LiteralPath (Join-Path $OutDir 'app.txt') -Encoding UTF8
$mine | Select-Object -Last 30
"Ergebnis: $OutDir"
