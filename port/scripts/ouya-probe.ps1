<#
.SYNOPSIS
OUYA-Probe (Schritt O1 aus port/docs/ouya-machbarkeit.md): der Original-Spielkern ohne
Bild auf der OUYA. Misst, wie schnell er dort laeuft.

.DESCRIPTION
Drei Schritte:
  1. WSL2 (Ubuntu): das Android-NDK r23c einmalig einrichten (whole_setup_linux.sh --ouya,
     etwa 700 MB Download; das letzte NDK fuer Android 4.1) und den Spielkern fuer
     Android 4.1 bauen (whole_build_android.sh --ouya, in WSL unter ~/re4-ouya, getrennt
     vom Build der App unter ~/re4-kern). Kern, Module und das Messprogramm re4_probe
     landen in port\build\ouya-probe (nicht im Repository).
  2. Disc 1 als Ordner: aus -DataDir (Disc-Abbilder .iso/.gcm, mit Dolphin entpackte
     Disc, Import-Ordner der App oder Port-Ordner). Abbilder werden einmalig nach
     -CacheDir entpackt (Standard: RE4-OUYA-Daten neben dem Repository, etwa 1,4 GB).
  3. OUYA per ADB, unter der gemeinsamen Sperre Global\Retroid_ADB_Exclusive
     (port/docs/adb-coordination.md): alles nach /data/local/tmp/re4-ouya kopieren
     (die Disc nur beim ersten Mal, einige Minuten), dann die Messlaeufe:
       r100          Debug-Start in r100, Echtzeit wie im Spiel (30 Bilder/s als Ziel)
       r101          dasselbe im Dorf (viele Gegner, die schwerste Stelle)
       r100-schnell  r100 ohne Warten auf den Bildschirm: so schnell der Kern kann
       ...-gp        derselbe Lauf mit den OUYA-Einstellungen: der Decoder prueft
                     Texturen nur stichprobenweise (RE4_GX_TEXTURE_CHECK=sampled) und
                     schreibt die Punkte im kompakten Format der GameCube
                     (RE4_GX_COMPACT=1): r100-gp, r101-gp, r100-schnell-gp. Den eigenen
                     Grafik-Thread von frueher gibt es seit dem Abgleich mit master
                     (28.10.2026) nicht mehr; die Punkte rechnen dort bis zu 3 Hilfs-Threads
                     um (RE4_GX_THREADS, Standard: Prozessorkerne minus 3).
       ...-ohnekopie die Grafikpakete werden nur abgenommen, nicht kopiert (wie --no-copy):
                     zeigt, wie sehr die Kopie die Speicherbandbreite belastet
       ...-gleitkomma  die Punkte im bisherigen Format (Gleitkomma, wie die Haupt-App):
                     zeigt, was das Kompaktformat bringt
     Standard: r100-gp, r101-gp, r100-schnell-gp, r100-schnell-gp-gleitkomma (r100 ohne
     Thread: 10,6 Bilder/s, Messung vom 21.10.2026; mit -Runs r100,... wieder dabei).
     Jeder Lauf dauert -Seconds Sekunden (Standard 240). Die Test-Tastenfolge der
     PC-Pruefung fuehrt durch Speicherkarten-Dialog und Titel in den Raum; danach geht
     Leon ein paar Schritte. Gemessen wird nur der Kern (Spiellogik, GX-Decoder, Ton);
     die Grafik (GPU) gibt es auf der OUYA noch nicht.

Ergebnis in -OutDir (Standard: Desktop\RE4-OUYA-Probe): bericht.txt (Zusammenfassung
auf Deutsch), probe-<lauf>.log (alles, was die Probe und der Kern ausgeben),
logcat-<lauf>.txt, kern-build.log und senden.zip mit allem zusammen.

Die OUYA wird automatisch gefunden (Modellname "OUYA"). Haengen mehrere Geraete am PC
und wird keine OUYA erkannt, die Kennung mit -Serial angeben (adb devices zeigt sie).
Mit -Serial geht derselbe Test auch auf einem anderen Geraet (etwa dem Retroid zum
Vergleich: dieselbe Probe laeuft auch auf neueren Android-Versionen).

Die OUYA-Arbeit hat einen eigenen Ordner neben RE4-Fixed (Entscheidung des
Projektinhabers, 21.10.2026): E:\Development\Android\RE4-OUYA, ein zweiter Klon des
Repositorys. So bleibt RE4-Fixed auf seinem Branch. Einmal anlegen:
  cd E:\Development\Android
  git clone https://github.com/Andiweli/RE4.git RE4-OUYA
Danach dort den Branch der Aufgabe holen, z. B.:
  cd E:\Development\Android\RE4-OUYA
  git fetch origin
  git checkout claude/adoring-mendel-vclbu8
  git pull
Disc-Daten (RE4-Discdata) und der Ordner fuer die entpackte Disc 1 (RE4-OUYA-Daten) liegen
neben dem Klon, also ebenfalls in E:\Development\Android.

.EXAMPLE
.\port\scripts\ouya-probe.ps1

.EXAMPLE
powershell -ExecutionPolicy Bypass -File .\port\scripts\ouya-probe.ps1
Falls Windows das Ausfuehren von Skripten sperrt.

.EXAMPLE
.\port\scripts\ouya-probe.ps1 -SkipBuild -Runs r100 -Seconds 120
Ohne neuen Build, nur ein kurzer Lauf in r100.

.EXAMPLE
.\port\scripts\ouya-probe.ps1 -Runs r100,r101,r100-schnell,r100-gp,r101-gp,r100-schnell-gp
Alle Laeufe, mit und ohne Grafik-Thread (etwa 25 Minuten).

.EXAMPLE
.\port\scripts\ouya-probe.ps1 -SkipBuild -Serial dbdcacc -OutDir "$env:USERPROFILE\Desktop\RE4-OUYA-Probe-Retroid"
Dieselbe Probe auf dem Retroid, als Vergleich.
#>
param(
    [string]$Serial = '',
    [string]$Adb = '',
    [string]$DataDir = '',
    [string]$CacheDir = '',
    [string]$OutDir = (Join-Path ([Environment]::GetFolderPath('Desktop')) 'RE4-OUYA-Probe'),
    [int]$Seconds = 240,
    [ValidateSet('r100', 'r101', 'r100-schnell', 'r100-gp', 'r101-gp', 'r100-schnell-gp', 'r100-schnell-gp-ohnekopie',
        'r100-schnell-gp-gleitkomma')]
    [string[]]$Runs = @('r100-gp', 'r101-gp', 'r100-schnell-gp', 'r100-schnell-gp-gleitkomma'),
    [switch]$SkipBuild,
    [switch]$SkipSetup,
    [switch]$SkipData,
    [switch]$NoCopy,
    [string]$Distro = ''
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$stage = Join-Path $repo 'port\build\ouya-probe'
$remote = '/data/local/tmp/re4-ouya'
$remoteBin = "$remote/bin"
$remoteDisc = "$remote/data/disc0"
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$report = Join-Path $OutDir 'bericht.txt'
# results of an earlier run must not pass for this one's
foreach ($pattern in @('bericht.txt', 'probe-*.log', 'logcat-*.txt', 'profil-*.txt', 'kern-build.log', 'senden.zip')) {
    Get-ChildItem -LiteralPath $OutDir -Filter $pattern -ErrorAction SilentlyContinue | Remove-Item
}
function Say([string]$Text) {
    Write-Host $Text
    Add-Content -LiteralPath $report -Value $Text -Encoding UTF8
}
# A native program with its output on the screen and in LOGFILE; returns the exit code.
# Windows PowerShell 5.1 would stop at the first line on stderr while 'Stop' is in force.
function Invoke-Logged([string]$LogFile, [scriptblock]$Command) {
    $old = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        & $Command 2>&1 | ForEach-Object {
            $line = if ($_ -is [System.Management.Automation.ErrorRecord]) { $_.Exception.Message } else { "$_" }
            Write-Host ("`r" + $line)
            $line
        } | Set-Content -LiteralPath $LogFile -Encoding UTF8
        return $LASTEXITCODE
    } finally { $ErrorActionPreference = $old }
}
function Add-ReportTail([string]$LogFile, [int]$Lines) {
    if (Test-Path -LiteralPath $LogFile) {
        Get-Content -LiteralPath $LogFile -Encoding UTF8 | Select-Object -Last $Lines |
            ForEach-Object { Add-Content -LiteralPath $report -Value "   $_" -Encoding UTF8 }
    }
}
$wslBase = @()
if ($Distro) { $wslBase = @('-d', $Distro) }
# Windows path -> WSL path (/mnt/e/...)
function ConvertTo-WslPath([string]$Path) {
    $full = (Resolve-Path -LiteralPath $Path).Path -replace '\\', '/'
    $converted = & wsl.exe @wslBase --exec wslpath -a $full
    if ($LASTEXITCODE -ne 0 -or -not $converted) { throw "Pfad laesst sich nicht nach WSL umrechnen: $Path" }
    return ($converted | Select-Object -First 1).Trim()
}
function Test-Wsl {
    if (-not (Get-Command wsl.exe -ErrorAction SilentlyContinue)) {
        throw 'WSL fehlt. Einmal in PowerShell als Administrator: wsl --install -d Ubuntu-24.04'
    }
}

Say "RE4 OUYA-Probe  $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"
Say "Repository: $repo"
$previousEncoding = [Console]::OutputEncoding
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

# ---- 1. the game core for Android 4.1 (WSL) --------------------------------------------------
if (-not $SkipBuild) {
    Test-Wsl
    $repoWsl = ConvertTo-WslPath $repo
    if (-not $SkipSetup) {
        Say '1. Werkzeuge und Android-NDK r23c in WSL (beim ersten Mal ca. 700 MB Download) ...'
        & wsl.exe @wslBase -u root --exec bash "$repoWsl/port/tools/whole_setup_linux.sh" --ouya
        if ($LASTEXITCODE -ne 0) { throw 'Einrichtung in WSL fehlgeschlagen (Internet noetig).' }
    }
    Say '1. Spielkern fuer Android 4.1 bauen (beim ersten Mal 10 bis 60 Minuten) ...'
    $buildLog = Join-Path $OutDir 'kern-build.log'
    $code = Invoke-Logged $buildLog {
        wsl.exe @wslBase --exec bash "$repoWsl/port/tools/whole_build_android.sh" --ouya --source $repoWsl `
            --work re4-ouya --stage "$repoWsl/port/build/ouya-probe"
    }
    Add-ReportTail $buildLog 30
    if ($code -ne 0) { throw 'Kern-Build fehlgeschlagen (Einzelheiten oben und in kern-build.log).' }
}
if (-not (Test-Path -LiteralPath (Join-Path $stage 're4_probe'))) {
    throw "Die Probe fehlt in $stage (ohne -SkipBuild starten)."
}
$stageFiles = @(Get-ChildItem -LiteralPath $stage -File)
Say "Probe: $($stageFiles.Count) Dateien, $([math]::Round(($stageFiles | Measure-Object Length -Sum).Sum / 1MB)) MB in $stage"

# ---- 2. disc 1 as a folder ---------------------------------------------------------------------
$disc = $null
if (-not $SkipData) {
    if (-not $DataDir) {
        $guess = Join-Path (Split-Path -Parent $repo) 'RE4-Discdata'
        if (Test-Path -LiteralPath $guess) { $DataDir = $guess } else { $DataDir = Read-Host 'Ordner mit den Disc-Daten' }
    }
    if (-not (Test-Path -LiteralPath $DataDir)) { throw "Ordner mit den Disc-Daten nicht gefunden: $DataDir" }
    Say "Disc-Daten: $DataDir"
    if (Test-Path -LiteralPath (Join-Path $DataDir 'disc0\sys\fst.bin')) {
        $disc = Join-Path $DataDir 'disc0'
    } elseif ((Test-Path -LiteralPath (Join-Path $DataDir 'sys\fst.bin')) -and (Test-Path -LiteralPath (Join-Path $DataDir 'files'))) {
        $disc = $DataDir
    } elseif (Test-Path -LiteralPath (Join-Path $DataDir 'disc1\sys\fst.bin')) {
        $disc = Join-Path $DataDir 'disc1'  # the app's import folder counts from 1
    } else {
        $images = @(Get-ChildItem -LiteralPath $DataDir -Recurse -Depth 1 -File |
            Where-Object { $_.Extension -match '^\.(iso|gcm)$' })
        if ($images.Count -eq 0) { throw "In $DataDir liegen weder entpackte Disc-Dateien noch Disc-Abbilder (.iso/.gcm)." }
        # disc 1 only: byte 6 of the disc header is the disc number (0 = disc 1)
        $firstDisc = @($images | Where-Object {
            $s = [System.IO.File]::OpenRead($_.FullName)
            try { $b = New-Object byte[] 8; [void]$s.Read($b, 0, 8) } finally { $s.Dispose() }
            [System.Text.Encoding]::ASCII.GetString($b, 0, 6) -eq 'G4BE08' -and $b[6] -eq 0
        })
        if ($firstDisc.Count -eq 0) { throw "Kein Abbild von Disc 1 (G4BE08) in $DataDir gefunden." }
        if (-not $CacheDir) { $CacheDir = Join-Path (Split-Path -Parent $repo) 'RE4-OUYA-Daten' }
        New-Item -ItemType Directory -Force -Path $CacheDir | Out-Null
        if (-not (Test-Path -LiteralPath (Join-Path $CacheDir 'disc0\sys\fst.bin'))) {
            Test-Wsl
            if (-not $repoWsl) { $repoWsl = ConvertTo-WslPath $repo }
            Say "2. Disc 1 einmalig entpacken nach $CacheDir (WSL, einige Minuten) ..."
            $code = Invoke-Logged (Join-Path $OutDir 'disc-ordner.log') {
                wsl.exe @wslBase --exec python3 "$repoWsl/port/tools/whole_disc_folder.py" `
                    --disc (ConvertTo-WslPath $firstDisc[0].FullName) --out (ConvertTo-WslPath $CacheDir)
            }
            if ($code -ne 0 -or -not (Test-Path -LiteralPath (Join-Path $CacheDir 'disc0\sys\fst.bin'))) {
                throw 'Disc 1 liess sich nicht entpacken (Einzelheiten in disc-ordner.log).'
            }
        }
        $disc = Join-Path $CacheDir 'disc0'
    }
    $discFiles = @(Get-ChildItem -LiteralPath $disc -Recurse -File).Count
    Say "Disc 1: $disc ($discFiles Dateien)"
}

# ---- 3. the OUYA (ADB, under the shared lock) -------------------------------------------------
if (-not $Adb) {
    $Adb = Join-Path $env:LOCALAPPDATA 'Android\Sdk\platform-tools\adb.exe'
    if (-not (Test-Path -LiteralPath $Adb)) { $Adb = 'adb' }
}
. (Join-Path $PSScriptRoot 'Retroid-Adb-Exclusive.ps1')
# adb prints progress on stderr; Windows PowerShell 5.1 must not take that for an error
function Invoke-Adb {
    $old = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try { return @(& $Adb -s $Serial @args 2>&1 | ForEach-Object { "$_".TrimEnd("`r") }) } finally { $ErrorActionPreference = $old }
}
# a file on the device (Android 4.1's ls prints differently from newer ones)
function Test-Remote([string]$Path) {
    return ((Invoke-Adb shell "[ -f $Path ] && echo yes") -join '').Trim() -eq 'yes'
}
function Stop-Probe {
    foreach ($line in (Invoke-Adb shell ps)) {
        if ($line -match '(re4_probe|re4-probe)\s*$') {
            $procId = ($line.Trim() -split '\s+')[1]
            Invoke-Adb shell kill $procId | Out-Null
        }
    }
}
# "key=value key=value" of a probe-result line
function Read-Result([string]$Line) {
    $values = @{}
    foreach ($pair in ($Line -replace '^probe-result:\s*', '' -split '\s+')) {
        $kv = $pair -split '=', 2
        if ($kv.Count -eq 2) { $values[$kv[0]] = $kv[1] }
    }
    return $values
}

$results = [ordered]@{}
Invoke-RetroidAdbExclusive -ThrowOnUnavailable -LockTimeoutMs 60000 -Sequence {
    if (-not $Serial) {
        # the OUYA among the connected devices (its model name)
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
        if (-not $Serial) {
            throw "Keine OUYA gefunden (verbunden: $($serials -join ', ')). Kennung mit -Serial angeben; adb devices zeigt sie."
        }
    }
    $state = ((Invoke-Adb get-state) -join '').Trim()
    if ($state -ne 'device') { throw "Geraet $Serial ist nicht verbunden oder nicht freigegeben ($state)." }
    $model = ((Invoke-Adb shell getprop ro.product.model) -join '').Trim()
    $release = ((Invoke-Adb shell getprop ro.build.version.release) -join '').Trim()
    $sdk = ((Invoke-Adb shell getprop ro.build.version.sdk) -join '').Trim()
    Say "3. Geraet $Serial`: $model, Android $release (API $sdk)"
    Say "   Speicher /data: $(((Invoke-Adb shell df /data) | Where-Object { $_.Trim() } | Select-Object -Last 1))"
    Stop-Probe

    Say '3. Probe auf das Geraet kopieren ...'
    Invoke-Adb shell rm -r $remoteBin | Out-Null
    Invoke-Adb shell mkdir $remote | Out-Null
    $pushed = Invoke-Adb push $stage $remoteBin
    if (-not (Test-Remote "$remoteBin/re4_probe")) {
        Say ($pushed | Select-Object -Last 5 | Out-String)
        throw "Kopieren der Probe fehlgeschlagen ($remoteBin/re4_probe fehlt)."
    }
    Invoke-Adb shell chmod 755 "$remoteBin/re4_probe" | Out-Null
    Say "   $(($pushed | Select-Object -Last 1))"

    if (-not $SkipData) {
        $ready = ((Invoke-Adb shell cat "$remoteDisc/probe-ready.txt") -join '').Trim()
        if ($ready -ne "$discFiles") {
            Say "3. Disc 1 auf das Geraet kopieren ($discFiles Dateien, etwa 1,4 GB; einige Minuten) ..."
            Invoke-Adb shell rm -r $remoteDisc | Out-Null
            Invoke-Adb shell mkdir "$remote/data" | Out-Null
            $pushed = Invoke-Adb push $disc $remoteDisc
            Say "   $(($pushed | Select-Object -Last 1))"
            if (-not (Test-Remote "$remoteDisc/sys/fst.bin")) {
                throw "Kopieren der Disc fehlgeschlagen ($remoteDisc/sys/fst.bin fehlt; genug Platz auf der OUYA?)."
            }
            Invoke-Adb shell "echo $discFiles > $remoteDisc/probe-ready.txt" | Out-Null
        } else {
            Say '3. Disc 1 liegt schon auf dem Geraet.'
        }
    }

    foreach ($run in $Runs) {
        $room = '100'
        if ($run -like 'r101*') { $room = '101' }
        $fast = $run -like 'r100-schnell*'
        $gp = $run -like '*-gp*'
        $runNoCopy = $NoCopy -or $run -like '*-ohnekopie'
        # the keys of the PC check (whole_r100_check.sh) through the memory card dialog and the
        # title into the room; once free, Leon walks around for about 50 s
        $settings = @(
            'RE4_LOG_STDERR=1', 'RE4_MEMCARD=none', 'RE4_ROOM_MENU=1', 'RE4_PAD_UNTIL_ROOM=1', "RE4_START_ROOM=0x$room",
            "RE4_PAD_SCRIPT='UP@100-60000/60 A@115-60000/60 START@130-60000/60'",
            "RE4_PAD_FREE_SCRIPT='STICKUP@30-400/1 STICKLEFT@400-440/1 STICKUP@440-900/1 STICKRIGHT@900-960/1 STICKUP@960-1500/1'"
        )
        # r101 starts with Leon's binoculars, a sub screen that waits for B (first OUYA run,
        # 21.10.2026: open for the whole run): B every 2 s from 10 s after the room loaded
        if ($room -eq '101') { $settings += "RE4_PAD_ROOM_SCRIPT='B@300-9000/60'" }
        if ($fast) { $settings += 'RE4_VI_FAST=1' }
        # textures checked by samples from frame to frame (gx_decode.cpp: 18 % of the decoder's
        # time otherwise; the GP thread of the first measurements is gone since the merge with
        # master, 28.10.2026: master's vertex jobs instead)
        if ($gp) { $settings += 'RE4_GX_TEXTURE_CHECK=sampled' }
        # the compact vertex (re4_gx_frame.h): about half the bytes; -gleitkomma: the float vertex
        $float = $run -like '*-gleitkomma'
        if ($gp -and -not $float) { $settings += 'RE4_GX_COMPACT=1' }

        $copyArg = ''
        if ($runNoCopy) { $copyArg = ' --no-copy' }
        # where the game's thread spends its time (port/tools/probe_profile.py names the functions)
        $profileRemote = "$remote/profile-$run.txt"
        $command = "cd $remoteBin && LD_LIBRARY_PATH=$remoteBin $($settings -join ' ') ./re4_probe --data $remote/data " +
            "--seconds $Seconds$copyArg --profile $profileRemote 2>&1"
        $log = Join-Path $OutDir "probe-$run.log"
        $label = if ($fast) { 'ohne Bremse' } else { 'Echtzeit' }
        if ($gp) { $label += ', OUYA-Einstellungen' }
        if ($runNoCopy) { $label += ', ohne Kopie' }
        if ($float) { $label += ', Gleitkomma-Punkte' } elseif ($gp) { $label += ', kompakte Punkte' }
        Say "4. Lauf $run ($label): $Seconds s ..."
        $writer = New-Object System.IO.StreamWriter($log, $false, (New-Object System.Text.UTF8Encoding($false)))
        $resultLines = @()
        try {
            $old = $ErrorActionPreference
            $ErrorActionPreference = 'Continue'
            try {
                & $Adb -s $Serial shell $command 2>&1 | ForEach-Object {
                    $line = "$_".TrimEnd("`r")
                    $writer.WriteLine($line)
                    if ($line -match '^probe-result:') { $resultLines += $line }
                    if (($line -match '^probe: ' -and $line -notmatch '^probe: setting ') -or $line -match 'CRASH|fatal|FAILED|HANG') {
                        Write-Host "   $line"
                    }
                }
            } finally { $ErrorActionPreference = $old }
        } finally { $writer.Dispose() }
        Stop-Probe
        Invoke-Adb logcat -d -v time | Set-Content -LiteralPath (Join-Path $OutDir "logcat-$run.txt") -Encoding UTF8
        Invoke-Adb pull $profileRemote (Join-Path $OutDir "profil-roh-$run.txt") | Out-Null
        Invoke-Adb shell rm $profileRemote | Out-Null
        # the GP thread's (RE4_GX_THREAD): the probe writes it next to the game's
        if (Test-Remote "$profileRemote.gp") {
            Invoke-Adb pull "$profileRemote.gp" (Join-Path $OutDir "profil-roh-$run-grafikthread.txt") | Out-Null
            Invoke-Adb shell rm "$profileRemote.gp" | Out-Null
        }
        $text = Get-Content -LiteralPath $log -Encoding UTF8
        $entry = @{ run = $run; fast = $fast; gp = $gp; noCopy = $runNoCopy; float = $float; log = $text }
        foreach ($set in @('play', 'loop', 'all')) {
            $line = $resultLines | Where-Object { $_ -match "^probe-result: set=$set " } | Select-Object -First 1
            if ($line) { $entry[$set] = Read-Result $line }
        }
        $results[$run] = $entry
    }
    Invoke-Adb shell rm -r $remoteBin | Out-Null
}

# ---- 4. the profiles: function names from the libraries of the build (WSL) -----------------
foreach ($run in @($results.Keys)) {
    foreach ($part in @(@{ suffix = ''; key = 'profile' }, @{ suffix = '-grafikthread'; key = 'profileGp' })) {
        $raw = Join-Path $OutDir "profil-roh-$run$($part.suffix).txt"
        if (-not (Test-Path -LiteralPath $raw) -or -not (Get-Command wsl.exe -ErrorAction SilentlyContinue)) { continue }
        if (-not $repoWsl) { $repoWsl = ConvertTo-WslPath $repo }
        $old = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        try {
            $named = @(& wsl.exe @wslBase --exec python3 "$repoWsl/port/tools/probe_profile.py" (ConvertTo-WslPath $raw) 2>&1 |
                ForEach-Object { "$_" })
        } finally { $ErrorActionPreference = $old }
        $named | Set-Content -LiteralPath (Join-Path $OutDir "profil-$run$($part.suffix).txt") -Encoding UTF8
        $results[$run][$part.key] = $named
    }
}

# ---- 5. the report ---------------------------------------------------------------------------
Say ''
Say '== Ergebnis =='
$first = $results.Values | Select-Object -First 1
if ($first) {
    foreach ($line in ($first.log | Where-Object { $_ -match '^probe: (device|kernel|cpuinfo|cpus|MemTotal)' })) {
        Say "   $($line -replace '^probe: ', '')"
    }
}
$verdictFps = $null
$verdictCpu = $null
foreach ($entry in $results.Values) {
    Say ''
    $mode = if ($entry.fast) { 'ohne Bremse: so schnell der Kern kann' } else { 'Echtzeit wie im Spiel' }
    if ($entry.gp) { $mode += '; OUYA-Einstellungen (Textur-Stichprobe)' }
    if ($entry.noCopy) { $mode += '; Grafikpakete nicht kopiert' }
    if ($entry.gp) { $mode += $(if ($entry.float) { '; Punkte als Gleitkomma' } else { '; kompakte Punkte' }) }
    Say "Lauf $($entry.run) ($mode)"
    $failed = $entry.log | Where-Object { $_ -match '^probe: (FAILED|HANG|CRASH)|^probe: the game (ended|failed)' } | Select-Object -First 3
    foreach ($f in $failed) { Say "   PROBLEM: $($f -replace '^probe: ', '')" }
    $crash = $entry.log | Where-Object { $_ -match '^probe:   (pc|lr) ' } | Select-Object -First 2
    foreach ($c in $crash) { Say "   $($c.Trim())" }
    $r = $null
    $setName = ''
    foreach ($set in @('play', 'loop', 'all')) {
        if ($entry.ContainsKey($set)) { $r = $entry[$set]; $setName = $set; break }
    }
    if (-not $r) {
        Say '   Keine Messung (die Probe endete vorher; Einzelheiten in der Log-Datei des Laufs).'
        continue
    }
    $what = @{ play = 'Leon frei (ohne Zwischensequenz, ohne Menue)'; loop = 'Hauptschleife des Raums'; all = 'gesamter Lauf (der Raum wurde nicht erreicht)' }[$setName]
    Say "   gemessen:                        $($r.seconds) s, $what, Raum $($r.room)"
    Say "   Spielbilder je Sekunde:          $($r.fps)$(if (-not $entry.fast) { ' (Ziel 30)' })"
    Say "   Rechenzeit des Kerns je Bild:    $($r.core_cpu_ms) ms$(if (-not $entry.fast) { ' (verfuegbar: 33,3 ms)' })"
    if ($r.gx_thread -eq '1') {
        Say "   Rechenzeit des Grafik-Threads:   $($r.gp_cpu_ms) ms je Bild (auf einem anderen Prozessorkern, neben dem Kern)"
        # per vertex: comparable between runs that show different parts of the room
        if ([double]$r.vertices -gt 0) {
            Say "   Grafik-Thread je Punkt:          $([math]::Round([double]$r.gp_cpu_ms * 1000000 / [double]$r.vertices)) ns"
        }
    }
    if (-not $entry.fast) {
        Say "   Leerlauf je Bild:                $($r.idle_ms) ms"
        Say "   Bildzeiten Median / 95 % / Max:  $($r.frame_ms_p50) / $($r.frame_ms_p95) / $($r.frame_ms_max) ms"
        Say "   verspaetete Bilder (> 45 ms):    $($r.late) von $($r.frames)"
    }
    Say "   Prozessor gesamt:                $($r.process_cpu_pct) % (100 % = ein Kern)"
    Say "   GX je Bild:                      $($r.gx_kb) KB, $($r.vertices) Punkte, Kopie $($r.copy_ms) ms"
    Say "   Ton aus dem Spiel:               $($r.sound_from_game_pct) %"
    Say "   Speicher des Prozesses:          $($r.rss_mb) MB"
    # where the time goes (the areas of probe_profile.py), for the game's thread and the GP thread
    foreach ($part in @(@{ key = 'profile'; title = 'Spiel-Threads' }, @{ key = 'profileGp'; title = 'Grafik-Threads' })) {
        if (-not $entry.ContainsKey($part.key)) { continue }
        $areas = @()
        $inAreas = $false
        foreach ($line in $entry[$part.key]) {
            if ($line -match '^Dazu .* wartete') {
                Say "   $($part.title -replace 's$', ''): $($line -replace '^Dazu \d+ Proben, in denen er wartete \(schlief\): ', 'wartet ' -replace '; die Anteile.*$', '')"
            }
            if ($line -match '^Bereiche') { $inAreas = $true; continue }
            if ($inAreas -and -not $line.Trim()) { break }
            if ($inAreas) { $areas += $line }
        }
        if ($areas.Count -gt 0) {
            Say "   Rechenzeit des $($part.title) nach Bereichen:"
            foreach ($a in ($areas | Select-Object -First 8)) { Say "   $a" }
        }
    }
    # the verdict: the fastest run without the brake, else the game's thread in r100
    if ($entry.fast -and $setName -ne 'all' -and ($null -eq $verdictFps -or [double]$r.fps -gt $verdictFps)) { $verdictFps = [double]$r.fps }
    if ($entry.run -like 'r100*' -and -not $entry.fast -and $setName -ne 'all' -and
        ($null -eq $verdictCpu -or [double]$r.core_cpu_ms -lt $verdictCpu)) { $verdictCpu = [double]$r.core_cpu_ms }
}
Say ''
if ($null -ne $verdictFps) {
    if ($verdictFps -ge 30) {
        Say "Einschaetzung: GRUEN. Der Kern schafft ohne Bremse $verdictFps Spielbilder/s (noetig: 30). Der Grafik-Umbau (O2) lohnt sich."
    } elseif ($verdictFps -ge 20) {
        Say "Einschaetzung: GELB. Der Kern schafft ohne Bremse $verdictFps Spielbilder/s (noetig: 30): knapp, nur mit Optimierung."
    } else {
        Say "Einschaetzung: ROT. Der Kern schafft ohne Bremse nur $verdictFps Spielbilder/s (noetig: 30)."
    }
} elseif ($null -ne $verdictCpu) {
    Say "Einschaetzung: Rechenzeit des Kerns in r100 $verdictCpu ms je Bild (verfuegbar: 33,3 ms)."
} else {
    Say 'Einschaetzung: keine, es gab keine Messung im Raum.'
}
Say 'Nur der Kern ist gemessen; die Grafik (GPU) der OUYA kommt spaeter dazu.'

$send = Join-Path $OutDir 'senden.zip'
$parts = @(Get-ChildItem -LiteralPath $OutDir -File | Where-Object { $_.Name -match '^(bericht\.txt|probe-.*\.log|logcat-.*\.txt|profil-.*\.txt|kern-build\.log|disc-ordner\.log)$' })
if ($parts.Count -gt 0) {
    Compress-Archive -Path $parts.FullName -DestinationPath $send -Force
}
Say ''
Say "Bericht: $report"
Say "Bitte schicke mir senden.zip aus $OutDir."
[Console]::OutputEncoding = $previousEncoding
