<#
.SYNOPSIS
Die spielbare OUYA-Fassung (Stufe O3 aus port/docs/ouya-machbarkeit.md) bauen, auf die OUYA
bringen und starten.

.DESCRIPTION
Schritte:
  1. WSL: den Spielkern fuer Android 4.1 bauen (whole_build_android.sh --ouya, wie
     ouya-probe.ps1) und seine Bibliotheken samt Kernprozess (libre4_coreproc.so) nach
     port\ouya\app\src\main\jniLibs\armeabi-v7a legen (nicht im Repository).
  2. Windows: die App bauen (port\ouya, Gradle mit dem Java von Android Studio und dem NDK r23c
     des Android-SDK): port\ouya\app\build\outputs\apk\debug\app-debug.apk.
  3. OUYA per ADB, unter der gemeinsamen Sperre Global\Retroid_ADB_Exclusive
     (port/docs/adb-coordination.md): die App installieren (App-ID com.ast.cubeevil4.ouya,
     neben allem anderen), Disc 1 nach /sdcard/RE4-OUYA/disc0 kopieren (nur beim ersten Mal,
     etwa 1,3 GB, einige Minuten), die App starten und -Seconds Sekunden lang ihr Protokoll
     mitschreiben.
Ergebnis in -OutDir (Standard: Desktop\RE4-OUYA-Probe\app): bericht.txt, logcat.txt, core.log
(das Protokoll des Spielkerns).

Die App laeuft in der leichten Fassung (Potato-Modus mit beiden Schaltern, halbe Sicht,
vereinfachte Farben, 0,75x, hoechstens 2 Lichter je Eckpunkt). Eine Datei settings.txt in
/sdcard/RE4-OUYA mit Zeilen NAME=WERT (RE4_*) aendert das fuer Versuche.

Controller: O = A, A = B, U = X, Y = Y, R1 = Z, L2/R2 = L/R, linker Stick, rechter Stick =
C-Stick, Steuerkreuz; START = OUYA-Taste kurz gedrueckt oder L3.

.EXAMPLE
powershell -ExecutionPolicy Bypass -File .\port\scripts\ouya-app.ps1
(Windows fuehrt nicht signierte Skripte sonst nicht aus; -ExecutionPolicy Bypass gilt nur fuer
diesen einen Aufruf und aendert keine Einstellung.)

.EXAMPLE
powershell -ExecutionPolicy Bypass -File .\port\scripts\ouya-app.ps1 -SkipCore -Seconds 120
Nur die App neu bauen und installieren (der Kern ist schon gebaut), zwei Minuten mitschreiben.

.EXAMPLE
powershell -ExecutionPolicy Bypass -File .\port\scripts\ouya-app.ps1 -PrepareOnly
Nur den Kern bauen und in die App legen, fuer einen Bau von Hand in Android Studio.
#>
param(
    [string]$Serial = '',
    [string]$Adb = '',
    [string]$DataDir = (Join-Path (Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))) 'RE4-OUYA-Daten'),
    [string]$OutDir = (Join-Path ([Environment]::GetFolderPath('Desktop')) 'RE4-OUYA-Probe\app'),
    [int]$Seconds = 60,
    [switch]$SkipCore,
    [switch]$SkipBuild,
    [switch]$SkipDisc,
    [switch]$NoStart,
    # only the game core: built and copied into the app (app\src\main\jniLibs), then the app is built by hand
    # in Android Studio (port\ouya; wish of the project owner, 07.11.2026)
    [switch]$PrepareOnly,
    [string]$Distro = ''
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$project = Join-Path $repo 'port\ouya'
$stage = Join-Path $repo 'port\build\ouya-probe'
$jniLibs = Join-Path $project 'app\src\main\jniLibs\armeabi-v7a'
$apk = Join-Path $project 'app\build\outputs\apk\debug\app-debug.apk'
$package = 'com.ast.cubeevil4.ouya'
$remote = '/sdcard/RE4-OUYA'
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$report = Join-Path $OutDir 'bericht.txt'
foreach ($pattern in @('bericht.txt', 'logcat.txt', 'core.log', 'kern-build.log', 'app-build.log')) {
    Get-ChildItem -LiteralPath $OutDir -Filter $pattern -ErrorAction SilentlyContinue | Remove-Item
}
function Say([string]$Text) {
    Write-Host $Text
    Add-Content -LiteralPath $report -Value $Text -Encoding UTF8
}
# A native program with its output on the screen and in LOGFILE; returns the exit code.
function Invoke-Logged([string]$LogFile, [scriptblock]$Command) {
    $old = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        & $Command 2>&1 | ForEach-Object {
            $line = if ($_ -is [System.Management.Automation.ErrorRecord]) { $_.Exception.Message } else { "$_" }
            Write-Host $line
            $line
        } | Set-Content -LiteralPath $LogFile -Encoding UTF8
        return $LASTEXITCODE
    } finally { $ErrorActionPreference = $old }
}
$wslBase = @()
if ($Distro) { $wslBase = @('-d', $Distro) }
function ConvertTo-WslPath([string]$Path) {
    $full = (Resolve-Path -LiteralPath $Path).Path -replace '\\', '/'
    $converted = & wsl.exe @wslBase --exec wslpath -a $full
    if ($LASTEXITCODE -ne 0 -or -not $converted) { throw "Pfad laesst sich nicht nach WSL umrechnen: $Path" }
    return ($converted | Select-Object -First 1).Trim()
}

Say "RE4 OUYA-App  $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"
Say "Repository: $repo"

# ---- 1. the game core for Android 4.1 (WSL) ---------------------------------------------------
if (-not $SkipCore) {
    $repoWsl = ConvertTo-WslPath $repo
    Say '1. Spielkern fuer Android 4.1 bauen (WSL, Compiler des NDK r28c, Bibliotheken des r23c) ...'
    $code = Invoke-Logged (Join-Path $OutDir 'kern-build.log') {
        wsl.exe @wslBase --exec bash "$repoWsl/port/tools/whole_build_android.sh" --ouya --source $repoWsl `
            --work re4-ouya --stage "$repoWsl/port/build/ouya-probe"
    }
    if ($code -ne 0) { throw 'Kern-Build fehlgeschlagen (kern-build.log).' }
}
if (-not (Test-Path -LiteralPath (Join-Path $stage 'libre4_coreproc.so'))) {
    throw "Der Kernprozess fehlt in $stage (ohne -SkipCore starten)."
}
# the core's libraries and its process into the app (not the probe, not the NDK's libc++: Gradle brings it)
if (Test-Path -LiteralPath $jniLibs) { Remove-Item -LiteralPath $jniLibs -Recurse -Force }
New-Item -ItemType Directory -Force -Path $jniLibs | Out-Null
$libs = @(Get-ChildItem -LiteralPath $stage -File -Filter 'libre4_*.so')
$libs | Copy-Item -Destination $jniLibs
Say "   $($libs.Count) Kern-Bibliotheken in die App ($([math]::Round(($libs | Measure-Object Length -Sum).Sum / 1MB)) MB)"

if ($PrepareOnly) {
    Say "Kern liegt in $jniLibs. Jetzt in Android Studio $project oeffnen und bauen (Build > Build APK, oder Run)."
    return
}

# ---- 2. the app (Windows, Gradle) ---------------------------------------------------------------
if (-not $SkipBuild) {
    Say '2. App bauen (Gradle) ...'
    $jbr = Get-ChildItem 'C:\Program Files\Android' -Directory -Filter 'Android Studio*' -ErrorAction SilentlyContinue |
        ForEach-Object { Join-Path $_.FullName 'jbr' } | Where-Object { Test-Path (Join-Path $_ 'bin\java.exe') } |
        Select-Object -First 1
    if (-not $jbr) { throw 'Kein Java von Android Studio gefunden (C:\Program Files\Android\Android Studio*\jbr).' }
    $env:JAVA_HOME = $jbr
    if (-not $env:ANDROID_HOME) { $env:ANDROID_HOME = Join-Path $env:LOCALAPPDATA 'Android\Sdk' }
    Push-Location $project
    try {
        $code = Invoke-Logged (Join-Path $OutDir 'app-build.log') { & .\gradlew.bat assembleDebug --no-daemon -q }
    } finally { Pop-Location }
    if ($code -ne 0 -or -not (Test-Path -LiteralPath $apk)) { throw 'App-Build fehlgeschlagen (app-build.log).' }
}
Say "   APK: $apk ($([math]::Round((Get-Item -LiteralPath $apk).Length / 1MB)) MB)"

# ---- 3. the OUYA (ADB, under the shared lock) -------------------------------------------------
$disc = Join-Path $DataDir 'disc0'
if (-not $SkipDisc -and -not (Test-Path -LiteralPath (Join-Path $disc 'sys\fst.bin'))) {
    throw "Disc 1 fehlt in $disc (entpackt, wie ouya-probe.ps1 sie anlegt)."
}
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
    if (-not $Serial) {
        $old = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        try { $listed = @(& $Adb devices 2>&1 | ForEach-Object { "$_" }) } finally { $ErrorActionPreference = $old }
        $serials = @($listed | Where-Object { $_ -match '^(\S+)\s+device\s*$' } | ForEach-Object { ($_ -split '\s+')[0] })
        foreach ($candidate in $serials) {
            $Serial = $candidate
            $model = ((Invoke-Adb shell getprop ro.product.model) -join '').Trim()
            $maker = ((Invoke-Adb shell getprop ro.product.manufacturer) -join '').Trim()
            if ("$maker $model" -match 'OUYA') { break }
            $Serial = ''
        }
        if (-not $Serial) { throw "Keine OUYA gefunden (verbunden: $($serials -join ', ')). Kennung mit -Serial angeben." }
    }
    $model = ((Invoke-Adb shell getprop ro.product.model) -join '').Trim()
    $release = ((Invoke-Adb shell getprop ro.build.version.release) -join '').Trim()
    Say "3. Geraet $Serial`: $model, Android $release"

    Say '   App installieren ...'
    Invoke-Adb shell am force-stop $package | Out-Null
    $installed = Invoke-Adb install -r $apk
    $result = ($installed | Where-Object { $_ -match 'Success|Failure' } | Select-Object -Last 1)
    Say "   $result"
    if ($result -notmatch 'Success') { throw "Installation fehlgeschlagen: $($installed -join ' ')" }

    if (-not $SkipDisc) {
        $discFiles = @(Get-ChildItem -LiteralPath $disc -Recurse -File).Count
        $ready = ((Invoke-Adb shell "cat $remote/disc0/ready.txt 2>/dev/null") -join '').Trim()
        if ($ready -ne "$discFiles") {
            Say "   Disc 1 nach $remote/disc0 kopieren ($discFiles Dateien, etwa 1,3 GB; einige Minuten) ..."
            Invoke-Adb shell "rm -r $remote/disc0; mkdir -p $remote" | Out-Null
            $pushed = Invoke-Adb push $disc "$remote/disc0"
            Say "   $(($pushed | Select-Object -Last 1))"
            Invoke-Adb shell "echo $discFiles > $remote/disc0/ready.txt" | Out-Null
        } else {
            Say "   Disc 1 liegt schon auf der OUYA ($discFiles Dateien)."
        }
    }

    if (-not $NoStart) {
        Say "   App starten, $Seconds s Protokoll ..."
        Invoke-Adb logcat -c | Out-Null
        Invoke-Adb shell am start -n "$package/.OuyaActivity" | Out-Null
        Start-Sleep -Seconds $Seconds
        Invoke-Adb logcat -d -v time | Set-Content -LiteralPath (Join-Path $OutDir 'logcat.txt') -Encoding UTF8
        Invoke-Adb pull "$remote/core.log" (Join-Path $OutDir 'core.log') | Out-Null
        $lines = @(Get-Content -LiteralPath (Join-Path $OutDir 'logcat.txt') -Encoding UTF8)
        $mine = @($lines | Where-Object { $_ -match 'RE4OUYA|RE4GX2|re4-core|re4ipc|AndroidRuntime|FATAL|DEBUG\s*\(' })
        $mine | Select-Object -Last 25 | ForEach-Object { Say "   $_" }
    }
}
Say "Ergebnis: $OutDir"
