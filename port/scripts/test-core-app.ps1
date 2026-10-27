<#
.SYNOPSIS
Kernmodus der App RE4-Android-Decomp auf dem Retroid: Kern bauen, APK bauen,
installieren, starten, Bildschirmfotos und Protokoll holen.

.DESCRIPTION
Drei Schritte, jeder einzeln abschaltbar:
  1. WSL2 (Ubuntu): Android-NDK einmalig einrichten (whole_setup_linux.sh --android,
     braucht Internet) und den Original-Spielkern fuer ARM32 und ARM64 bauen
     (whole_build_android.sh). Die Bibliotheken landen in
     port\android-decomp\app\src\core\jniLibs\armeabi-v7a und ...\arm64-v8a
     (nicht im Repository); die APK enthaelt beide (Meilenstein 12).
  2. Windows: die App-Variante "core" bauen (gradlew.bat :app:assembleCoreDebug,
     mit dem Android-SDK von Android Studio).
  3. Retroid per ADB, unter der gemeinsamen Sperre Global\Retroid_ADB_Exclusive
     (port/docs/adb-coordination.md): APK installieren (App "CubeEvil4",
     com.ast.cubeevil4), starten, mehrere Bildschirmfotos aufnehmen, das
     Protokoll der App holen, App beenden.

App-Name "CubeEvil4" und Paketname com.ast.cubeevil4 seit 18.10.2026 (Entscheidung des
Projektinhabers; vorher "Resident Evil 4 Android", com.ast.re4decomp, bis 17.10.2026
"RE4 Kern", com.ast.re4decomp.core). Ist eine dieser frueheren Apps noch auf dem Retroid,
kopiert das Skript einmal ihre importierten Discs und ihre Speicherkarte in die neue App
(nur was dort fehlt; die fruehere App behaelt ihre Daten, bis du sie deinstallierst:
adb uninstall com.ast.re4decomp bzw. com.ast.re4decomp.core).
-NoCopy laesst das aus: ohne beide Discs zeigt die App dann ihren Startbildschirm, und
das Skript wartet wie unten beschrieben auf einen Import (Strg+C beendet es; ein
zweiter Lauf mit -SkipCore -SkipApk ohne -NoCopy kopiert dann).

Beim ersten Mal in der App "CubeEvil4" beide Discs importieren (Startbildschirm der
App, "Import Disc 1" und "Import Disc 2"; seit 17.10.2026 startet das Spiel erst mit beiden,
Entscheidung des Projektinhabers). Fehlt ein Import, startet das
Skript die App, bittet darum und wartet bis zu -ImportMinutes Minuten (Standard 45),
ohne die App zu beenden; erst danach beginnt der Test.

Standard ist ein Debug-Start in r120 (der Anfang eines neuen Spiels: Vorfilm,
dann die Autofahrt mit den Polizisten, dann r100) mit derselben
Test-Tastenfolge wie die PC-Pruefung (Speicherkarten-Dialog, Titel,
Debug-Menue); sobald der Raum laedt, endet die Tastenfolge und dein Controller
steuert. Der Vorfilm (movie/opening.sfd) laeuft seit Meilenstein 9 wie im Original
mit Bild und Ton; START ueberspringt ihn (laut Originalcode entfaellt dann auch die
Autofahrt). Fuer Film, Autofahrt und Ankunft in r100 reichen etwa -Seconds 480.
-WeaponTrace schreibt beim Nachladen jedes Bild eine Zeile ins Protokoll (Zustand
und Bewegung von Leon und Waffe, Munition, R-Taste; fuer die Nachlade-Suche).
-GpuBench misst die Grafik (GPU-Schritt a, 19.10.2026): Start in r100; sobald Leon frei steht
(Ankunft und Funkspruch vorbei, der Kern meldet es), schaltet die App nach 10 s nacheinander
die Einstellungen ihrer Messreihe durch (core_mode.cpp: Tiefenschaerfe, die Einstellung des
Testers mit 2x und 4x Kantenglaettung, 4x bei Aufloesung 2) und misst je 10 s GPU-Zeit, Last mal Takt, wie weit die GPU
hinterherhinkt und was im Bild liegt (Zeilen "GPU bench" im Bericht, dazu eine Zeile "GPU:" mit
den Erweiterungen des Grafikchips); danach gelten wieder deine Einstellungen.
Etwa 5 Minuten, waehrend der Messung den Controller nicht anfassen.
-MsaaTiles schaltet die Kantenglaettung auf den Kacheln ein (GPU-Schritt d2, nur fuer Versuche:
auf dem Retroid fehlen damit Haare und Texturen; sonst eigene Puffer).
-StartRoom 100 springt direkt nach r100 (nur die kurze Ankunft). -Manual
startet ohne Tastenfolge (Titelbild, du spielst selbst); dann beobachtet das
Skript 30 Minuten statt 240 s (Taste Q beendet frueher, -Seconds legt die Zeit
fest). Nach Ablauf der Zeit beendet das Skript die App selbst; das ist kein
Absturz (der Bericht sagt es dazu).

Die App zeigt, was das Spiel selbst zeichnet (GX-Nachbildung: Logos, Titel,
Menues, Anzeigen, Effekte). -Bridge zeigt zum Vergleich die fruehere Ansicht
(der Spielzustand, von der App selbst gezeichnet).

Die App blendet die Debug-Anzeigen der Entwicklerfassung aus (-DebugHud zeigt sie)
und schneidet die schwarzen Balken ab (Vollbild 16:9). -GxDump speichert alle 20 s
die vollstaendigen Grafikdaten eines Bildes (hoechstens 6) und legt sie als
gx-geraet.zip ab (zum Nachzeichnen in der Cloud, nicht ins Repository). Ein Druck
auf den rechten Stick (R3) speichert jederzeit die naechsten 12 Bilder ebenso (eine
Folge, damit Flackern sichtbar wird; auch ohne -GxDump; nach einem Spiel ohne
dieses Skript holt pull-gx-dumps.ps1 die Aufnahmen), dazu jedes dieser Bilder so, wie
die GPU des Geraets es gezeichnet hat (gx-geraet-NN-KK.bmp, ebenfalls in gx-geraet.zip).
Die Bilder des Kerns folgen dem Bildschirmtakt (60 Hz), damit Filme nicht ruckeln.
-GxReplay ORDNER-ODER-ZIP zeichnet R3-Aufnahmen (gx-geraet-NN-KK.bin, Folge -GxReplaySeries,
Standard 00) beim Start der App auf dem Geraet noch einmal: so wie damals (2x MSAA,
Anisotropie 16) dreimal, dazu ohne MSAA, mit 4x und ohne Anisotropie, jedes Bild so, wie die
GPU es zeichnet (gx-replay.zip, fuer die Cloud). Zeigt, ob das Geraet dieselben Daten jedes
Mal gleich zeichnet (Flackern duenner Dinge mit MSAA, 20.10.2026). Eine geteilte ZIP vorher
zusammensetzen (cmd /c copy /b NAME.zip.001+NAME.zip.002 NAME.zip).
-Scale waehlt die interne Aufloesung: 1 = 640x448 wie der GameCube, 2 = 1280x896
(Standard, der sichtbare 16:9-Teil entspricht etwa 720p), 3 = 1920x1344 (etwa 1080p),
4 = 2560x1792.
Speicherkarte (Meilenstein 10): Steckplatz A ist eine Datei im Speicher der App
(Android/data/com.ast.cubeevil4/files/game/memcard/card-a.bin, 2 MB wie eine
Memory Card 251); der Start prueft sie wie der GameCube (Karte lesen, Systemdatei mit den Optionen).
Gespeichert wird an der Schreibmaschine, geladen im Titel ("Load"). Die Optionen (Helligkeit,
Steuerung, Ton) stehen in der Systemdatei; das Spiel schreibt sie wie auf dem GameCube nur
zusammen mit einem Spielstand (Schreibmaschine) und liest sie beim Start. Die Test-Tastenfolge
laeuft OHNE Karte (RE4_MEMCARD=none: ihr Zeitplan ist auf den "keine Karte"-Dialog
abgestimmt, deine Spielstaende bleiben unberuehrt); mit -Manual spielst du mit der Karte.
Eine neue Karte ist wie eine aus dem Laden formatiert und leer (Entscheidung des
Projektinhabers, 17.10.2026); die Frage zum Formatieren kommt nur bei einer kaputten Karte.
Beim Start entfallen die Meldung "Systemdatei wird geladen" und die Frage "Systemdatei anlegen?"
(die Systemdatei entsteht still); RE4_CARD_BOOT_MESSAGE=1 verhaelt sich wie das Original.
-NewCard startet mit einer neuen Karte, -BlankCard mit einer unformatierten (Test des
Formatieren-Dialogs); die bisherige wird vorher als speicherkarte-a-vorher.bin gesichert.
In Ja/Nein-Fragen steht der Pfeil wie im Original zuerst auf der zweiten Antwort (meist "Nein").
-SaveTest oeffnet den Speicherbildschirm wie an einer Schreibmaschine von selbst, etwa 7 s nachdem
du im ersten Raum frei laufen kannst (Testhilfe RE4_TEST_SAVE; speichern wie gewohnt mit A,
"gespeichert" mit START schliessen). Achtung: Das speichert an einer Stelle, an der das Spiel sonst
nie speichert; in r100 vor dem ersten Haus fehlt nach dem Laden die Szene mit dem ersten Ganado
(das Spiel merkt sich im Spielstand, dass es sie schon vorgeladen hat). Die Schreibmaschine von
r100 steht weiter hinten am Weg, hinter dem ersten Haus. Nach jedem Lauf holt das Skript die Karte
(speicherkarte-a.bin, kommt mit in senden.zip) und listet im Bericht, was darauf ist.

Steuerung (Meilenstein 7) wie beim GameCube: A, B, X, Y nach Beschriftung, linker Stick =
Steuerstick, rechter Stick = C-Stick, Steuerkreuz, START, R2 = R (Waffe anlegen), L2 = L
(Messer), R1 = Z (Karte), L1 ohne Funktion; SELECT kurz = Menue der App (Grafik, Speicherkarte
als .gci, Fehlerprotokoll; das Spiel steht solange), SELECT 1 Sekunde halten = Debug-Menue des
Spiels (Meilenstein 11), R3 = Grafik-Aufnahme (bei diesem Skript immer an). Der Bericht listet,
welche Android-Taste welche GameCube-Taste wurde, die Eingabegeraete und die Vibration.
Reagieren A/B oder X/Y vertauscht: im Startmenue der App "Swap A/B and X/Y" ankreuzen.

Filme (Meilenstein 9): Der Kern spielt die Filme mit der CRI-Middleware des Spiels
(Sofdec-Bild ueber die GX-Nachbildung, ADX-Ton). Der Bericht nennt jeden Film mit
Laufzeit, Zahl der Bilder und den Bildern, die der Film-Spieler auslassen musste
(Decode/Anzeige zu langsam), ob er zu Ende lief oder mit START uebersprungen wurde,
und CRI-Fehlermeldungen. Die Demo-Filme des Titels: auf dem Titelbild warten (-Manual).

Ton (Meilenstein 8): Der Bericht nennt die Tonzaehler des Kerns und ob die App ihre
Tonausgabe gestartet hat. -AudioWav N nimmt die ersten N Sekunden Spielzeit so auf, wie der
Kern sie mischt (ton.wav, kommt bis 12 MB mit in senden.zip; 60 s sind knapp 8 MB), mit
Pegeln je 5 s im Bericht.

Taste P im PowerShell-Fenster misst 15 s lang, wo die Rechenzeit hingeht
(simpleperf aus dem Android-NDK in WSL; Spielkern, Grafik-Nachbildung,
Grafiktreiber), hoechstens dreimal; dabei weiterspielen. Ergebnis: profil-N.txt
und die Zusammenfassung im Bericht.

Das Skript prueft jede Sekunde, ob die App noch laeuft. Stuerzt sie ab, endet die
Beobachtung sofort: das Protokoll und der Absturzbericht des Systems werden gesichert
und die Absturzstelle in WSL mit den Bibliotheken des Kern-Builds in Funktionen und
Quelltextzeilen uebersetzt (port/tools/android_symbolize.sh). Bitte bis "Fertig" warten.

Mit -HitShots N markiert der Kern bei den ersten N Treffern auf Leon das Bild dieses
Moments; die App speichert die naechsten 24 Bilder so, wie der Bildschirm sie zeigt
(Ordner treffer), mit -HitDumps N dazu die Grafikdaten von 4 Bildern ab dort (Standard
jeweils 0 = aus; die Aufnahme laesst das Spiel kurz ruckeln).

Zum Schicken packt das Skript senden.zip (die des Laufs davor bleibt als senden-vorher.zip): bericht.txt, logcat.txt, profil-*.txt,
Absturzberichte und die Bilder als kleine JPEGs (meist unter 5 MB). gx-geraet.zip
(Grafikdaten) kommt dazu, wenn es hoechstens 25 MB hat.

Ergebnis in -OutDir (Standard: Desktop\RE4-Kern-App): bericht.txt, logcat.txt,
bild-*.png, kern-build.log, apk-build.log, nach einem Absturz absturz-system.txt und
absturz-stapel.txt. Bitte bericht.txt, logcat.txt und die Bilder schicken.

-SkipApk nimmt die APK aus app\build\outputs\apk\core\debug, wie sie ist. Run in Android Studio
erneuert diese Datei nicht (es installiert aus app\build\intermediates); deshalb vergleicht das
Skript ihre Version mit app\build.gradle und bricht bei einer alten APK ab. Ohne -SkipApk baut
es sie selbst.

Schnelle Kerne (Branch performance): Spiel- und Zeichen-Thread bleiben von den langsamsten
Kernen fern, und Android bekommt ab Version 13 einen Leistungshinweis (33,3 ms je Bild). Zum
Vergleich -NoFastCores (Kerne wie vorher) bzw. -NoPerfHint (ohne Hinweis); der Bericht zeigt die
Zeilen "cores: ..." und "performance hint: ...".

Mehrere Kerne (seit 0.45.0): Die Eckpunkte der Grafik rechnen Helfer-Threads um.
-GxThreads N legt ihre Anzahl fest (0: alles auf dem Spiel-Thread wie bis 0.44; ohne Angabe
Kerne minus 3, hoechstens 3). -GxJobCheck rechnet zur Fehlersuche jeden Zeichenauftrag
zusaetzlich sofort um und meldet im Bericht "GX job check", wenn sich Speicher geaendert hat,
bevor ein Helfer ihn las (kostet Leistung, nur zur Pruefung).

Kernprozess (32 bit, port/docs/32bit-kernprozess.md): Auf 32-bit-Geraeten, bei denen der
Java-Speicher von Android schon auf dem Speicher des Spiels (0x80000000) liegt (Android 14),
laeuft der Spielkern in einem eigenen Prozess ohne Java (libre4_coreproc.so). -CoreProcess
erzwingt das auch auf dem Retroid (zusammen mit -Abi armeabi-v7a): Der Bericht nennt den
Kernprozess (Prozessnummer, Grund, Ende), und endet der Kernprozess, waehrend die App noch
laeuft, endet die Beobachtung wie bei einem Absturz.

.EXAMPLE
powershell -ExecutionPolicy Bypass -File .\port\scripts\test-core-app.ps1

Gradle braucht Java 17 bis 23. Das Skript nimmt JAVA_HOME, wenn es passt, sonst
das Java von Android Studio (jbr) oder ein JDK 17/21 unter C:\Program Files;
-JavaHome gibt den Ordner fest vor. JAVA_HOME wird nur fuer diesen Lauf gesetzt.

.EXAMPLE
.\port\scripts\test-core-app.ps1 -SkipCore -Seconds 180
Nur APK bauen und testen (Kern-Bibliotheken liegen schon in der App), 3 Minuten.
#>
param(
    [string]$Serial = 'dbdcacc',
    [string]$Adb = '',
    [string]$OutDir = (Join-Path ([Environment]::GetFolderPath('Desktop')) 'RE4-Kern-App'),
    [int]$Seconds = 240,
    [switch]$Manual,
    [switch]$SkipCore,
    [switch]$SkipApk,
    [switch]$SkipDevice,
    [switch]$SkipSetup,
    [string]$Distro = '',
    [string]$JavaHome = '',
    [int]$ImportMinutes = 45,
    [string]$StartRoom = '120',
    [switch]$Bridge,
    [switch]$DebugHud,
    [switch]$GxDump,
    [int]$Scale = 0,
    [int]$HitDumps = 0,
    [int]$HitShots = 0,
    [int]$AudioWav = 0,
    [switch]$NewCard,
    [switch]$BlankCard,
    [switch]$SaveTest,
    [switch]$WeaponTrace,
    [switch]$GpuBench,
    [switch]$MsaaTiles,
    [switch]$NoCopy,
    [string]$GxReplay = '',
    [string]$GxReplaySeries = '00',
    [ValidateSet('auto', 'armeabi-v7a', 'arm64-v8a')][string]$Abi = 'auto',
    [switch]$CoreProcess,
    [int]$GxThreads = -1,
    [switch]$GxJobCheck,
    [switch]$NoFastCores,
    [switch]$NoPerfHint,
    [string]$CoreSettings = ''
)
$ErrorActionPreference = 'Stop'
# -Manual: you play yourself, 30 minutes unless -Seconds is given (Q ends earlier)
if ($Manual -and -not $PSBoundParameters.ContainsKey('Seconds')) { $Seconds = 1800 }
# -GpuBench: the GPU measurement of the app (core_mode.cpp RE4_GPU_BENCH) at a calm place: the
# start in r100 with the test keys, Leon standing; the first step 10 s after the player is free
# (about 110 s after the start on the Retroid, 19.10.2026), then 7 or 8 steps of 15 s
if ($GpuBench) {
    if ($Manual) { throw '-GpuBench braucht den automatischen Start (ohne -Manual).' }
    if (-not $PSBoundParameters.ContainsKey('StartRoom')) { $StartRoom = '100' }
    if (-not $PSBoundParameters.ContainsKey('Seconds')) { $Seconds = 330 }
}
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$app = Join-Path $repo 'port\android-decomp'
$package = 'com.ast.cubeevil4'
# the core app's ids before (until 18.10.2026 com.ast.re4decomp, until 17.10.2026
# com.ast.re4decomp.core): their discs and memory card move over once (see below); the
# activities keep their Java package com.ast.re4decomp
$oldPackages = @('com.ast.re4decomp', 'com.ast.re4decomp.core')
$activity = "$package/com.ast.re4decomp.MainActivity"
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$report = Join-Path $OutDir 'bericht.txt'
Remove-Item -LiteralPath $report -ErrorAction SilentlyContinue
Get-ChildItem -LiteralPath $OutDir -Filter 'bild-*.png' -ErrorAction SilentlyContinue | Remove-Item
# results of an earlier run must not pass for this one's
Get-ChildItem -LiteralPath $OutDir -Filter 'absturz-*.txt' -ErrorAction SilentlyContinue | Remove-Item
Remove-Item -LiteralPath (Join-Path $OutDir 'gx-geraet.zip') -ErrorAction SilentlyContinue
Remove-Item -LiteralPath (Join-Path $OutDir 'gx-replay.zip') -ErrorAction SilentlyContinue
# the zip of the run before stays as senden-vorher.zip (two runs one after the other: both can be sent)
$sendBefore = Join-Path $OutDir 'senden.zip'
if (Test-Path -LiteralPath $sendBefore) {
    Move-Item -LiteralPath $sendBefore -Destination (Join-Path $OutDir 'senden-vorher.zip') -Force
}
Remove-Item -LiteralPath (Join-Path $OutDir 'treffer') -Recurse -ErrorAction SilentlyContinue
Remove-Item -LiteralPath (Join-Path $OutDir 'speicherkarte-a.bin') -ErrorAction SilentlyContinue
Get-ChildItem -LiteralPath $OutDir -Filter 'profil-*' -ErrorAction SilentlyContinue | Remove-Item
Remove-Item -LiteralPath (Join-Path $OutDir 'gx-geraet') -Recurse -ErrorAction SilentlyContinue
Remove-Item -LiteralPath (Join-Path $OutDir 'logcat.txt') -ErrorAction SilentlyContinue
Remove-Item -LiteralPath (Join-Path $OutDir 'ton.wav') -ErrorAction SilentlyContinue
function Say([string]$Text) {
    Write-Host $Text
    Add-Content -LiteralPath $report -Value $Text -Encoding UTF8
}
# A native program with its output on the screen and in LOGFILE; returns the
# exit code. Windows PowerShell 5.1 would stop at the first line on stderr
# (compiler warnings, Gradle progress) while 'Stop' is in force.
function Invoke-Logged([string]$LogFile, [scriptblock]$Command) {
    $old = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        & $Command 2>&1 | ForEach-Object {
            # stderr lines arrive as error records; their text is the message
            $line = if ($_ -is [System.Management.Automation.ErrorRecord]) { $_.Exception.Message } else { "$_" }
            # leading CR: wsl.exe can leave the console without automatic
            # carriage return (lines would drift to the right)
            Write-Host ("`r" + $line)
            $line
        } | Set-Content -LiteralPath $LogFile -Encoding UTF8
        return $LASTEXITCODE
    } finally { $ErrorActionPreference = $old }
}
# the end of a log into the report (already shown on the screen)
function Add-ReportTail([string]$LogFile, [int]$Lines) {
    if (Test-Path -LiteralPath $LogFile) {
        Get-Content -LiteralPath $LogFile -Encoding UTF8 | Select-Object -Last $Lines |
            ForEach-Object { Add-Content -LiteralPath $report -Value "   $_" -Encoding UTF8 }
    }
}
# major version of the JDK in a folder, 0 when there is none
function Get-JavaMajor([string]$Folder) {
    if (-not $Folder) { return 0 }
    $java = Join-Path $Folder 'bin\java.exe'
    if (-not (Test-Path -LiteralPath $java)) { return 0 }
    $old = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try { $text = (& $java -version 2>&1 | ForEach-Object { "$_" }) -join ' ' } finally { $ErrorActionPreference = $old }
    if ($text -match 'version "(\d+)(\.(\d+))?') {
        $major = [int]$Matches[1]
        if ($major -eq 1 -and $Matches[3]) { $major = [int]$Matches[3] }  # 1.8 = 8
        return $major
    }
    return 0
}
Say "RE4 Kernmodus-Test  $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"
Say "Repository: $repo"
$previousEncoding = [Console]::OutputEncoding
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

# ---- 1. the game core for ARM32 (WSL) --------------------------------------------------
if (-not $SkipCore) {
    if (-not (Get-Command wsl.exe -ErrorAction SilentlyContinue)) {
        throw 'WSL fehlt. Einmal in PowerShell als Administrator: wsl --install -d Ubuntu-24.04'
    }
    $wslBase = @()
    if ($Distro) { $wslBase = @('-d', $Distro) }
    $full = (Resolve-Path -LiteralPath $repo).Path -replace '\\', '/'
    $repoWsl = ((& wsl.exe @wslBase --exec wslpath -a $full) | Select-Object -First 1).Trim()
    if (-not $SkipSetup) {
        Say '1. Werkzeuge und Android-NDK in WSL (beim ersten Mal ca. 700 MB Download) ...'
        & wsl.exe @wslBase -u root --exec bash "$repoWsl/port/tools/whole_setup_linux.sh" --android
        if ($LASTEXITCODE -ne 0) { throw 'Einrichtung in WSL fehlgeschlagen (Internet noetig).' }
    }
    # -Abi: only that core (the other one stays as it was in the app folder)
    $buildAbi = if ($Abi -eq 'auto') { 'all' } else { $Abi }
    Say "1. Spielkern bauen ($(if ($buildAbi -eq 'all') { 'ARM32 und ARM64' } else { $buildAbi }); beim ersten Mal 10 bis 60 Minuten) ..."
    $coreLog = Join-Path $OutDir 'kern-build.log'
    $code = Invoke-Logged $coreLog {
        wsl.exe @wslBase --exec bash "$repoWsl/port/tools/whole_build_android.sh" --source $repoWsl `
            --into "$repoWsl/port/android-decomp/app" --abi $buildAbi
    }
    Add-ReportTail $coreLog 30
    if ($code -ne 0) { throw 'Kern-Build fehlgeschlagen (Einzelheiten oben und in kern-build.log).' }
}
$needAbis = if ($Abi -eq 'auto') { @('armeabi-v7a', 'arm64-v8a') } else { @($Abi) }
foreach ($libAbi in $needAbis) {
    $libs = Join-Path $app "app\src\core\jniLibs\$libAbi"
    if (-not (Test-Path -LiteralPath (Join-Path $libs 'libre4_main.so'))) {
        throw "Kern-Bibliotheken fehlen in $libs (Schritt 1 ausfuehren)."
    }
    Say "Kern-Bibliotheken ${libAbi}: $((Get-ChildItem -LiteralPath $libs -Filter '*.so').Count) Dateien"
}

# ---- 2. the APK of the core flavor (Windows, Gradle) --------------------------------------
$apk = Join-Path $app 'app\build\outputs\apk\core\debug\app-core-debug.apk'
if (-not $SkipApk) {
    Say '2. APK bauen (gradlew :app:assembleCoreDebug) ...'
    # Gradle 8.11 with the Android plugin 8.9: Java 17 to 23
    $candidates = @()
    if ($JavaHome) { $candidates += $JavaHome }
    else {
        if ($env:JAVA_HOME) { $candidates += $env:JAVA_HOME }
        foreach ($base in @($env:ProgramFiles, $env:LOCALAPPDATA, (Join-Path $env:LOCALAPPDATA 'Programs'))) {
            if (-not $base) { continue }
            foreach ($studio in @(Get-ChildItem -Path (Join-Path $base 'Android') -Filter 'Android Studio*' -Directory -ErrorAction SilentlyContinue) +
                                @(Get-ChildItem -Path $base -Filter 'Android Studio*' -Directory -ErrorAction SilentlyContinue)) {
                $candidates += (Join-Path $studio.FullName 'jbr')
            }
        }
        foreach ($vendor in @('Eclipse Adoptium', 'Java', 'Microsoft', 'Zulu', 'Amazon Corretto')) {
            $candidates += @(Get-ChildItem -Path (Join-Path $env:ProgramFiles $vendor) -Directory -ErrorAction SilentlyContinue |
                Sort-Object Name -Descending | ForEach-Object { $_.FullName })
        }
    }
    $jdk = $null
    foreach ($c in $candidates) {
        $major = Get-JavaMajor $c
        if ($major -ge 17 -and $major -le 23) { $jdk = $c; break }
    }
    if (-not $jdk) {
        $seen = ($candidates | ForEach-Object { "$_ (Java $(Get-JavaMajor $_))" }) -join '; '
        throw "Kein Java 17 bis 23 gefunden (Gradle braucht es). Gefunden: $seen. Abhilfe: Android Studio installieren oder -JavaHome 'Ordner eines JDK 17' angeben."
    }
    $env:JAVA_HOME = $jdk
    Say "   Java fuer Gradle: $jdk (Java $(Get-JavaMajor $jdk))"
    $localProps = Join-Path $app 'local.properties'
    if (-not (Test-Path -LiteralPath $localProps)) {
        $sdk = Join-Path $env:LOCALAPPDATA 'Android\Sdk'
        if (-not (Test-Path -LiteralPath $sdk)) { throw 'Android-SDK nicht gefunden: einmal das Projekt in Android Studio oeffnen.' }
        "sdk.dir=$($sdk -replace '\\', '\\\\')" | Set-Content -LiteralPath $localProps -Encoding ASCII
    }
    $gradleLog = Join-Path $OutDir 'apk-build.log'
    Push-Location $app
    try {
        $code = Invoke-Logged $gradleLog { .\gradlew.bat ':app:assembleCoreDebug' }
    } finally { Pop-Location }
    Add-ReportTail $gradleLog 25
    if ($code -ne 0) { throw 'APK-Build fehlgeschlagen (Einzelheiten oben und in apk-build.log).' }
}
if (-not (Test-Path -LiteralPath $apk)) { throw "APK fehlt: $apk" }
Say "APK: $apk ($([math]::Round((Get-Item -LiteralPath $apk).Length / 1MB, 1)) MB)"
# The version the repository's state builds (build.gradle) against the APK's: Run in Android
# Studio installs from app\build\intermediates and leaves this file as it was, so with -SkipApk an
# old APK would go on the device (23.10.2026: 0.43.1 instead of 0.45.0, installed over it)
$gradleFile = Join-Path $app 'app\build.gradle'
$wantVersion = if ((Get-Content -LiteralPath $gradleFile -Raw) -match "versionName '([^']+)'") { $Matches[1] } else { '' }
$apkVersion = ''
$sdkDir = Join-Path $env:LOCALAPPDATA 'Android\Sdk'
$aapt = Get-ChildItem -Path (Join-Path $sdkDir 'build-tools') -Recurse -Include 'aapt2.exe', 'aapt.exe' -ErrorAction SilentlyContinue |
    Sort-Object FullName -Descending | Select-Object -First 1
if ($aapt) {
    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $badging = (& $aapt.FullName dump badging $apk 2>$null) -join "`n"
    $ErrorActionPreference = $previousPreference
    if ($badging -match "versionName='([^']+)'") { $apkVersion = $Matches[1] }
}
if ($apkVersion) {
    Say "   APK-Version: $apkVersion (Stand im Repository: $wantVersion)"
    if ($wantVersion -and -not $apkVersion.StartsWith($wantVersion)) {
        throw ("Die APK ist $apkVersion, der Stand im Repository ${wantVersion}: sie ist veraltet. Das Skript " +
            "ohne -SkipApk starten (es baut sie dann selbst) oder in Android Studio Build > Build App Bundle(s) / " +
            "APK(s) > Build APK(s) waehlen (Run in Android Studio erneuert diese Datei nicht).")
    }
} elseif ((Get-Item -LiteralPath $apk).LastWriteTime -lt (Get-Item -LiteralPath $gradleFile).LastWriteTime) {
    throw ("Die APK ist aelter als app\build.gradle (Stand $wantVersion) und damit wohl veraltet. Das Skript " +
        "ohne -SkipApk starten (es baut sie dann selbst) oder in Android Studio Build > Build App Bundle(s) / " +
        "APK(s) > Build APK(s) waehlen (Run in Android Studio erneuert diese Datei nicht).")
}

# ---- 3. the device ------------------------------------------------------------------------
if ($SkipDevice) {
    Say 'Geraetetest uebersprungen.'
    [Console]::OutputEncoding = $previousEncoding
    exit 0
}
if (-not $Adb) {
    $Adb = Join-Path $env:LOCALAPPDATA 'Android\Sdk\platform-tools\adb.exe'
    if (-not (Test-Path -LiteralPath $Adb)) { $Adb = 'adb' }
}
$settings = ''
if (-not $Manual) {
    # the timing of the PC check (port/tools/whole_r100_check.sh)
    # (RE4_MEMCARD=none: slot A empty for this run, so that the boot check shows the "no memory
    # card" dialog the timing was made for; the card of the app stays untouched. RE4_ROOM_MENU=1:
    # with the debug start menu after "new game" (the default, named because the timing needs it))
    $settings = "RE4_START_ROOM=0x$StartRoom;RE4_MEMCARD=none;RE4_ROOM_MENU=1;RE4_PAD_UNTIL_ROOM=1;RE4_PAD_SCRIPT=UP@100-60000/60 A@115-60000/60 START@130-60000/60"
}
if ($BlankCard) {
    # the new card (made below) unformatted instead of formatted (runtime/memcard.cpp)
    $settings = if ($settings) { "$settings;RE4_MEMCARD_NEW=blank" } else { 'RE4_MEMCARD_NEW=blank' }
}
if ($SaveTest) {
    # the save screen of a typewriter, 120 frames after the player is free in the first room
    # (runtime/state_trace.cpp RE4_TEST_SAVE)
    $settings = if ($settings) { "$settings;RE4_TEST_SAVE=free+120" } else { 'RE4_TEST_SAVE=free+120' }
}
if ($Bridge) {
    # the earlier view: the core's state drawn by the app's own renderer
    $settings = if ($settings) { "$settings;RE4_VIEW=bridge" } else { 'RE4_VIEW=bridge' }
}
if ($DebugHud) {
    # the debug build's own displays (event counters, score, process bars); the app hides them by default
    $settings = if ($settings) { "$settings;RE4_DEBUG_HUD=1" } else { 'RE4_DEBUG_HUD=1' }
}
if ($Scale -gt 0) {
    # internal resolution of the GX renderer (core_mode.cpp RE4_GX_SCALE, 1 to 4)
    if ($Scale -gt 4) { throw '-Scale geht von 1 bis 4.' }
    $settings = if ($settings) { "$settings;RE4_GX_SCALE=$Scale" } else { "RE4_GX_SCALE=$Scale" }
}
# GX dumps: every 20 s the full graphics data of one picture (for the analysis in the cloud)
$dumpRemote = "/sdcard/Android/data/$package/files/gx-dumps"
if ($GxDump) {
    $dumpSetting = "RE4_GX_DUMP_DIR=$dumpRemote;RE4_GX_DUMP_EVERY=20"
    $settings = if ($settings) { "$settings;$dumpSetting" } else { $dumpSetting }
}
# R3 series drawn again on the device at the app's start (core_mode.cpp replayGx)
$replayRemote = "/sdcard/Android/data/$package/files/gx-replay"
$replayBins = @()
if ($GxReplay) {
    if (-not (Test-Path -LiteralPath $GxReplay)) { throw "-GxReplay: $GxReplay gibt es nicht." }
    $replaySource = $GxReplay
    if ((Get-Item -LiteralPath $GxReplay).Extension -eq '.zip') {
        $replaySource = Join-Path $OutDir 'gx-replay-eingabe'
        Remove-Item -LiteralPath $replaySource -Recurse -ErrorAction SilentlyContinue
        Expand-Archive -LiteralPath $GxReplay -DestinationPath $replaySource -Force
    }
    $replayBins = @(Get-ChildItem -LiteralPath $replaySource -Recurse -Filter "gx-geraet-$GxReplaySeries-*.bin" |
        Sort-Object Name)
    if (-not $replayBins.Count) { throw "-GxReplay: keine gx-geraet-$GxReplaySeries-*.bin in $GxReplay." }
    $replaySetting = "RE4_GX_REPLAY=$replayRemote"
    $settings = if ($settings) { "$settings;$replaySetting" } else { $replaySetting }
}
# a hit on Leon (a dark bar at the bottom showed only then, too briefly for R3): the core
# marks the picture of that moment; the app keeps the next 24 pictures as shown
# (treffer-*.bmp, -HitShots: for how many hits) and the graphics data of 4 pictures from
# there (-HitDumps: for how many hits); 0 switches each off
if ($HitDumps -gt 0 -or $HitShots -gt 0) {
    $hitSetting = "RE4_GX_DUMP_FOLDER=$dumpRemote;RE4_GX_DUMP_ON_HIT=$HitDumps;RE4_GX_SHOTS_ON_HIT=$([math]::Max($HitDumps, $HitShots))"
    $settings = if ($settings) { "$settings;$hitSetting" } else { $hitSetting }
}
# the sound as the core mixed it, the first N seconds of game time (-AudioWav N) as a WAV
$wavRemote = "/sdcard/Android/data/$package/files/ton.wav"
if ($AudioWav -gt 0) {
    $wavSetting = "RE4_AUDIO_WAV=$wavRemote;RE4_AUDIO_WAV_SECONDS=$AudioWav"
    $settings = if ($settings) { "$settings;$wavSetting" } else { $wavSetting }
}
if ($GpuBench) {
    # the GPU measurement (see above): the app steps through the picture options itself
    $settings = if ($settings) { "$settings;RE4_GPU_BENCH=1" } else { 'RE4_GPU_BENCH=1' }
}
if ($MsaaTiles) {
    # MSAA on the tiles with the EFB drawn back after every pass (GPU step d2, gx_gl.cpp)
    $settings = if ($settings) { "$settings;RE4_GX_MSAA_TILES=1" } else { 'RE4_GX_MSAA_TILES=1' }
}
# the app's tools for the test: R3 saves graphics data (otherwise only with the menu's switch)
if ($WeaponTrace) {
    # every frame with the weapon in hand: the player's and the weapon's state, ammo, R trigger (state_trace.cpp)
    $settings = if ($settings) { "$settings;RE4_TRACE_WEAPON=1" } else { 'RE4_TRACE_WEAPON=1' }
}
$settings = if ($settings) { "$settings;RE4_APP_TOOLS=1" } else { 'RE4_APP_TOOLS=1' }
# further core switches as NAME=VALUE;NAME=VALUE (e.g. RE4_TEST_STREAM=free+60:0:0x16 with -AudioWav)
if ($CoreSettings) { $settings = "$settings;$CoreSettings" }
# the vertex threads of the GX decoder (gx_decode.cpp): how many, and the check that memory a
# draw reads does not change before its batch
if ($GxThreads -ge 0) { $settings = "$settings;RE4_GX_THREADS=$GxThreads" }
if ($GxJobCheck) { $settings = "$settings;RE4_GX_JOB_CHECK=1" }
# the game and render threads off the slowest cores, Android's performance hint (re4_cpu_info.h):
# switched off for a comparison
if ($NoFastCores) { $settings = "$settings;RE4_FAST_CORES=0" }
if ($NoPerfHint) { $settings = "$settings;RE4_PERF_HINT=0" }
if ($CoreProcess) {
    # the core in a process of its own also with MEM1 free (32 bit only; core_mode.cpp)
    if ($Abi -ne 'armeabi-v7a') { Say 'Hinweis: -CoreProcess wirkt nur mit 32 bit (-Abi armeabi-v7a).' }
    $settings = "$settings;RE4_CORE_PROCESS=1"
}
. (Join-Path $PSScriptRoot 'Retroid-Adb-Exclusive.ps1')
Invoke-RetroidAdbExclusive -ThrowOnUnavailable -LockTimeoutMs 60000 -Sequence {
    if ((& $Adb -s $Serial get-state) -ne 'device' -or $LASTEXITCODE -ne 0) {
        throw "Retroid ($Serial) ist nicht verbunden oder nicht freigegeben."
    }
    $abis = ((& $Adb -s $Serial shell getprop ro.product.cpu.abilist) -join '').Trim()
    Say "3. Geraet $Serial, ABIs: $abis"
    if ($Abi -ne 'auto' -and $abis -notmatch [regex]::Escape($Abi)) { throw "Das Geraet kann keine $Abi-Apps ausfuehren." }
    # -Abi: the APK carries both; without it the device picks (64 bit where it can). -d: the
    # app with this id may be an earlier "RE4 Decomp" (classic) build with a higher version
    $installArgs = @('install', '-r', '-d')
    if ($Abi -ne 'auto') { $installArgs += @('--abi', $Abi) }
    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $installOut = @(& $Adb -s $Serial @installArgs $apk 2>&1 | ForEach-Object { "$_" })
    $installCode = $LASTEXITCODE
    $ErrorActionPreference = $previousPreference
    $installOut | ForEach-Object { Say "   $_" }
    if (($installOut -join "`n") -match 'INSTALL_FAILED_UPDATE_INCOMPATIBLE') {
        throw ("Auf dem Retroid ist schon eine App $package mit einem anderen Signierschluessel " +
            "(z. B. die signierte Release-APK oder ein Bau von einem anderen Rechner). Sie muss einmal weg: " +
            "& '$Adb' -s $Serial uninstall $package   (loescht auch ihre Daten; Speicherstaende vorher im " +
            "App-Menue als .gci exportieren). Dann dieses Skript mit -SkipCore -SkipApk erneut starten.")
    }
    if ($installCode -ne 0) { throw 'Installation fehlgeschlagen.' }
    $info = (& $Adb -s $Serial shell dumpsys package $package) -join "`n"
    $runAbi = if ($info -match 'primaryCpuAbi=(\S+)') { $Matches[1] } else { '?' }
    if ($Abi -ne 'auto' -and $runAbi -ne $Abi) { throw "Die App laeuft als $runAbi statt $Abi." }
    Say "   App installiert ($runAbi$(if ($runAbi -eq 'arm64-v8a') { ', 64 bit' } elseif ($runAbi -eq 'armeabi-v7a') { ', 32 bit' }))"
    $installed = if ($info -match 'versionName=(\S+)') { $Matches[1] } else { '?' }
    Say "   App-Version auf dem Geraet: $installed"
    if ($wantVersion -and -not $installed.StartsWith($wantVersion)) {
        Say "   ACHTUNG: nicht der Stand im Repository ($wantVersion)."
    }
    # The import runs inside the app (several minutes per disc): the test waits for it instead of
    # stopping the app, because stopping it cancels the import.
    $game = "/sdcard/Android/data/$package/files/game"
    # the earlier core apps ($oldPackages): their complete disc imports and their memory card
    # are copied once into the new app's folder, each only where the new one lacks it (a disc
    # first into a temporary folder, so a broken copy never passes for an import); the old app
    # keeps its own until it is uninstalled
    foreach ($oldPackage in $oldPackages) {
        $oldGame = "/sdcard/Android/data/$oldPackage/files/game"
        $hasOld = ((& $Adb -s $Serial shell "[ -d $oldGame ] && echo yes") -join '').Trim() -eq 'yes'
        if ($hasOld -and $NoCopy) {
            Say "   Fruehere App $oldPackage gefunden; ihre Daten werden nicht uebernommen (-NoCopy)."
        }
        if ($hasOld -and -not $NoCopy) {
            # what the new app lacks: a complete disc (file system table and import record), the card
            $discCond = '[ -f OLD/$d/sys/fst.bin ] && [ -f OLD/$d/ready.properties ] && ' +
                '! { [ -f NEW/$d/sys/fst.bin ] && [ -f NEW/$d/ready.properties ]; }'
            $cardCond = '[ -f OLD/memcard/card-a.bin ] && [ ! -f NEW/memcard/card-a.bin ]'
            $check = "for d in disc1 disc2; do if $discCond; then echo need-`$d; fi; done; " +
                "if $cardCond; then echo need-card; fi"
            $copy = "mkdir -p NEW; for d in disc1 disc2; do if $discCond; then " +
                'rm -rf NEW/.copy-$d && cp -r OLD/$d NEW/.copy-$d && rm -rf NEW/$d && ' +
                'mv NEW/.copy-$d NEW/$d && echo copied-$d || { rm -rf NEW/.copy-$d; echo failed-$d; }; fi; done; ' +
                "if $cardCond; then " +
                'mkdir -p NEW/memcard && cp OLD/memcard/card-a.bin NEW/memcard/card-a.bin && ' +
                'echo copied-card || echo failed-card; fi'
            $check = $check.Replace('OLD', $oldGame).Replace('NEW', $game)
            $copy = $copy.Replace('OLD', $oldGame).Replace('NEW', $game)
            $needed = ((& $Adb -s $Serial shell $check) -join ' ').Trim()
            if ($needed) {
                Say "   Fruehere App $oldPackage gefunden: ihre Discs und Speicherkarte werden uebernommen (dauert einige Minuten) ..."
                $result = ((& $Adb -s $Serial shell $copy) -join ' ').Trim()
                foreach ($item in @('disc1', 'disc2', 'card')) {
                    $name = if ($item -eq 'card') { 'Speicherkarte A' } else { "Disc $($item.Substring(4))" }
                    if ($result -match "copied-$item") { Say "   $name uebernommen." }
                    elseif ($result -match "failed-$item") { Say "   $name konnte NICHT kopiert werden (Speicherplatz?); bitte in der App neu importieren." }
                }
            }
            Say "   Die fruehere App $oldPackage kann weg, sobald hier alles laeuft: & '$Adb' -s $Serial uninstall $oldPackage"
        }
    }
    function Get-ImportState {
        $out = (& $Adb -s $Serial shell "cd $game 2>/dev/null && ls -d disc1/sys/fst.bin disc1/ready.properties disc2/sys/fst.bin disc2/ready.properties .incoming .work-disc1 .work-disc2 2>/dev/null; du -sm .incoming .work-disc1 .work-disc2 2>/dev/null") -join "`n"
        $mb = 0
        foreach ($line in ($out -split "`n")) { if ($line -match '^(\d+)\s') { $mb += [int]$Matches[1] } }
        # the game starts only with both discs (since 17.10.2026)
        $d1 = ($out -match 'disc1/sys/fst\.bin') -and ($out -match 'disc1/ready\.properties')
        $d2 = ($out -match 'disc2/sys/fst\.bin') -and ($out -match 'disc2/ready\.properties')
        return [pscustomobject]@{
            Ready   = $d1 -and $d2
            Missing = @(@(if (-not $d1) { 'Disc 1' }) + @(if (-not $d2) { 'Disc 2' })) -join ' und '
            Busy    = $out -match '(^|\n)\.(incoming|work-disc\d)\s*(\n|$)'
            MB      = $mb
        }
    }
    $state = Get-ImportState
    if (-not $state.Ready -or $state.Busy) {
        # the installation above ended any running import; the app clears what it left behind
        # (DiscInstaller.recover) when it starts
        & $Adb -s $Serial shell input keyevent KEYCODE_WAKEUP
        & $Adb -s $Serial shell am start -n $activity | Out-Null
        Say "   In der App `"CubeEvil4`" fehlt $(if ($state.Missing) { $state.Missing } else { 'nichts' }) (oder ein Import laeuft noch)."
        Say '   Bitte jetzt auf dem Retroid in der App "Import Disc 1" bzw. "Import Disc 2" waehlen und die Datei (.gcm/.iso) aussuchen.'
        Say '   Das Spiel startet erst mit beiden Discs. Das Skript wartet und beendet die App waehrenddessen NICHT.'
        Say "   Wartezeit hoechstens $ImportMinutes Minuten; Abbruch mit Strg+C."
        $deadline = (Get-Date).AddMinutes($ImportMinutes)
        $lastShown = -1
        while ($true) {
            Start-Sleep -Seconds 10
            $state = Get-ImportState
            if ($state.Ready -and -not $state.Busy) { Say '   Disc 1 und Disc 2 sind importiert.'; break }
            if ($state.Busy -and $state.MB -ne $lastShown) {
                Write-Host ("`r   Import laeuft: {0} MB bisher geschrieben   " -f $state.MB) -NoNewline
                $lastShown = $state.MB
            }
            if ((Get-Date) -gt $deadline) {
                Write-Host ''
                throw "Nach $ImportMinutes Minuten noch nicht importiert: $($state.Missing). Bitte den Import in der App abschliessen und das Skript mit -SkipCore -SkipApk erneut starten."
            }
        }
        Write-Host ''
    }
    & $Adb -s $Serial shell am force-stop $package
    # device time in seconds (logcat -T accepts 'sssss.mmm'; no spaces through adb shell) and as
    # the system's crash reports date their entries
    $since = ((& $Adb -s $Serial shell date +%s) -join '').Trim() + '.000'
    $sinceText = ((& $Adb -s $Serial shell date +%Y-%m-%d_%H:%M:%S) -join '').Trim() -replace '_', ' '
    # dumps of this run only (periodic with -GxDump, any time with R3 on the controller)
    & $Adb -s $Serial shell rm -rf $dumpRemote
    # the memory card of slot A (runtime/memcard.cpp); -NewCard / -BlankCard: a new one (the
    # app makes it when the file is missing: formatted and empty, or unformatted with
    # -BlankCard), the old one kept on the PC
    $cardRemote = "$game/memcard/card-a.bin"
    if ($NewCard -or $BlankCard) {
        $backup = Join-Path $OutDir 'speicherkarte-a-vorher.bin'
        $previousPreference = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        & $Adb -s $Serial pull $cardRemote $backup 2>&1 | Out-Null
        & $Adb -s $Serial shell rm -f $cardRemote 2>&1 | Out-Null
        $ErrorActionPreference = $previousPreference
        if (Test-Path -LiteralPath $backup) {
            Say "   Neue Speicherkarte: die bisherige ist gesichert ($backup)"
        } else {
            Say '   Neue Speicherkarte (bisher gab es keine)'
        }
    }
    if ($replayBins.Count) {
        $previousPreference = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        & $Adb -s $Serial shell rm -rf $replayRemote 2>&1 | Out-Null
        & $Adb -s $Serial shell mkdir -p $replayRemote 2>&1 | Out-Null
        foreach ($bin in $replayBins) { & $Adb -s $Serial push $bin.FullName "$replayRemote/" 2>&1 | Out-Null }
        $ErrorActionPreference = $previousPreference
        Say "   Wiedergabe auf dem Geraet: $($replayBins.Count) Bilder der Folge $GxReplaySeries (dauert beim Start etwa eine Minute)"
    }
    & $Adb -s $Serial shell input keyevent KEYCODE_WAKEUP
    $start = "am start -n $activity"
    if ($settings) { $start += " --es coreSettings '$settings'" }
    & $Adb -s $Serial shell $start | ForEach-Object { Say "   $_" }
    Say "   gestartet ($(if ($Manual) { 'Titelbild, du steuerst' } else { "Debug-Start r$StartRoom mit Test-Tastenfolge" })), bis $Seconds s Beobachtung"
    # Every second: is the app still running? Pictures at the planned times; a crash ends the
    # observation at once, and the log is taken while the crash report is still in it.
    # pictures every 25 s, every 2 minutes on a long run
    $every = if ($Seconds -gt 300) { 120 } else { 25 }
    $shots = @(@(15, 40) + @(70..([math]::Max(70, $Seconds)) | Where-Object { ($_ - 70) % $every -eq 0 }) |
        Where-Object { $_ -le $Seconds })
    function Save-Shot([int]$At) {
        $remote = '/data/local/tmp/re4-core-shot.png'
        & $Adb -s $Serial shell screencap -p $remote
        & $Adb -s $Serial pull $remote (Join-Path $OutDir ('bild-{0:D3}s.png' -f $At)) | Out-Null
        & $Adb -s $Serial shell rm -f $remote
    }
    # P: where the CPU time goes for 15 s (simpleperf of the NDK in WSL, pushed to the
    # device once; the app is debuggable), turned into functions in WSL
    # (port/tools/android_profile.py with the libraries of this PC's builds)
    $script:profiles = 0
    $script:perfReady = $false
    function Save-Profile([int]$At) {
        $script:profiles++
        $n = $script:profiles
        $wslArgs = @()
        if ($Distro) { $wslArgs = @('-d', $Distro) }
        $toWsl = { param($p) ((& wsl.exe @wslArgs --exec wslpath -a ($p -replace '\\', '/')) | Select-Object -First 1).Trim() }
        $old = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        try {
            if (-not $script:perfReady) {
                $local = Join-Path $OutDir 'simpleperf'
                & wsl.exe @wslArgs --exec bash -c ('cp "${ANDROID_NDK_HOME:-/opt/android-ndk-r28c}/simpleperf/bin/android/arm64/simpleperf" "' + (& $toWsl $OutDir) + '/simpleperf"') 2>&1 | Out-Null
                if (-not (Test-Path -LiteralPath $local)) {
                    Say ("   {0,3} s: Leistungsmessung nicht moeglich (simpleperf nicht im NDK unter WSL gefunden)" -f $At)
                    return
                }
                & $Adb -s $Serial push $local /data/local/tmp/re4-simpleperf 2>&1 | Out-Null
                & $Adb -s $Serial shell chmod 755 /data/local/tmp/re4-simpleperf 2>&1 | Out-Null
                Remove-Item -LiteralPath $local -ErrorAction SilentlyContinue
                $script:perfReady = $true
            }
            Say ("   {0,3} s: Leistungsmessung {1} laeuft 15 s, bitte weiterspielen ..." -f $At, $n)
            $perf = '/data/local/tmp/re4-perf'
            $rec = @(& $Adb -s $Serial shell /data/local/tmp/re4-simpleperf record --app $package -e task-clock:u -f 1000 `
                --duration 15 -o "$perf.data" 2>&1 | ForEach-Object { "$_" })
            $rec | Set-Content -LiteralPath (Join-Path $OutDir "profil-$n-aufnahme.txt") -Encoding UTF8
            & $Adb -s $Serial shell /data/local/tmp/re4-simpleperf report -i "$perf.data" --sort tid,dso,vaddr_in_file -n `
                -o "$perf-roh.txt" 2>&1 | Out-Null
            & $Adb -s $Serial shell /data/local/tmp/re4-simpleperf report -i "$perf.data" --sort tid,comm -n `
                -o "$perf-threads.txt" 2>&1 | Out-Null
            $raw = Join-Path $OutDir "profil-$n-roh.txt"
            $threads = Join-Path $OutDir "profil-$n-threads.txt"
            & $Adb -s $Serial pull "$perf-roh.txt" $raw 2>&1 | Out-Null
            & $Adb -s $Serial pull "$perf-threads.txt" $threads 2>&1 | Out-Null
            & $Adb -s $Serial shell rm -f "$perf.data" "$perf-roh.txt" "$perf-threads.txt" 2>&1 | Out-Null
            if (-not ((Test-Path -LiteralPath $raw) -and (Test-Path -LiteralPath $threads))) {
                Say "   Leistungsmessung $n fehlgeschlagen (Meldungen in profil-$n-aufnahme.txt):"
                $rec | Select-Object -Last 5 | ForEach-Object { Say "     $_" }
                return
            }
            $repoWslPath = & $toWsl ((Resolve-Path -LiteralPath $repo).Path)
            $summary = @(& wsl.exe @wslArgs --exec python3 "$repoWslPath/port/tools/android_profile.py" (& $toWsl $raw) `
                (& $toWsl $threads) 2>&1 | ForEach-Object { "$_" })
            $summary | Set-Content -LiteralPath (Join-Path $OutDir "profil-$n.txt") -Encoding UTF8
            Say "   Leistungsmessung $n fertig (profil-$n.txt):"
            $summary | Select-Object -First 24 | ForEach-Object { Say "     $_" }
        } finally { $ErrorActionPreference = $old }
    }
    $clock = [Diagnostics.Stopwatch]::StartNew()
    $firstPid = ''
    # the core process (32 bit, MEM1 taken in the app's process or -CoreProcess): "<package>:core"
    $corePid = ''
    $crashAt = -1
    $nextShot = 0
    # Q (or Strg+C) ends the observation early; the log, the crash report and the
    # pictures are still collected (Strg+C would otherwise stop the script before that)
    $keys = $false
    try { $keys = -not [Console]::IsInputRedirected; if ($keys) { [Console]::TreatControlCAsInput = $true } } catch { $keys = $false }
    if ($keys) {
        Say '   Vorzeitig beenden: Taste Q druecken (Protokoll und Aufnahmen werden trotzdem geholt)'
        Say '   Leistungsmessung (15 s, hoechstens 3): Taste P druecken'
    }
    while ($true) {
        Start-Sleep -Seconds 1
        $now = [int]$clock.Elapsed.TotalSeconds
        if ($keys -and [Console]::KeyAvailable) {
            $k = [Console]::ReadKey($true)
            if ($k.Key -eq 'Q' -or ($k.Key -eq 'C' -and ($k.Modifiers -band [ConsoleModifiers]::Control))) {
                Say ("   {0,3} s: Beobachtung von Hand beendet" -f $now)
                Save-Shot $now
                break
            }
            if ($k.Key -eq 'P' -and $script:profiles -lt 3) {
                Save-Profile $now
            }
        }
        $alive = ((& $Adb -s $Serial shell pidof $package) -join '').Trim()
        if ($alive -and -not $firstPid) { $firstPid = $alive }
        $gone = if ($firstPid) { $alive -ne $firstPid } else { $now -ge 10 }
        # the core process ended while the app runs (the app shows "The game core stopped")
        if ($alive -and -not $gone) {
            $core = ((& $Adb -s $Serial shell pidof "${package}:core") -join '').Trim()
            if ($core -and -not $corePid) {
                $corePid = $core
                Say ("   {0,3} s: Kernprozess laeuft (PID {1})" -f $now, $core)
            }
            if ($corePid -and $core -ne $corePid) {
                $crashAt = $now
                Save-Shot $now
                Say ("   {0,3} s: Kernprozess (PID {1}) LAEUFT NICHT MEHR: Beobachtung endet, Bild aufgenommen" -f $now, $corePid)
                break
            }
        }
        if ($gone) {
            $crashAt = $now
            Save-Shot $now
            $what = if (-not $firstPid) { 'IST NIE GESTARTET' }
                    elseif ($alive) { "WURDE NEU GESTARTET (PID $alive, vorher $firstPid)" }
                    else { 'LAEUFT NICHT MEHR' }
            Say ("   {0,3} s: App {1}: Beobachtung endet, Bild aufgenommen" -f $now, $what)
            break
        }
        if ($nextShot -lt $shots.Count -and $now -ge $shots[$nextShot]) {
            Save-Shot $shots[$nextShot]
            Say ("   {0,3} s: Bild aufgenommen, App laeuft (PID {1})" -f $shots[$nextShot], $alive)
            while ($nextShot -lt $shots.Count -and $now -ge $shots[$nextShot]) { $nextShot++ }
        }
        if ($now -ge $Seconds) {
            Say ("   {0,3} s: Beobachtungszeit abgelaufen: das Skript beendet die App (kein Absturz; laenger: -Seconds)" -f $now)
            break
        }
    }
    if ($keys) { try { [Console]::TreatControlCAsInput = $false } catch { } }
    if ($crashAt -ge 0) { Start-Sleep -Seconds 3 }  # the system is still writing the crash report
    $log = @(& $Adb -s $Serial logcat -d -v threadtime -b main -b system -b crash -T $since)
    # every line of the app's processes (their IDs from the core's own lines) and the system's
    # crash lines
    $pids = @{}
    foreach ($p in ($firstPid -split '\s+')) { if ($p) { $pids[$p] = 1 } }
    foreach ($line in $log) {
        if ($line -match '^\S+\s+\S+\s+(\d+)\s+\d+\s+[VDIWEF]\s+(re4|RE4\w*)\s*:') { $pids[$Matches[1]] = 1 }
    }
    $keep = @($log | Where-Object {
        $_ -match ' (re4|RE4\w*|DEBUG|AndroidRuntime|libc|crash_dump\w*|scudo)\s*:' -or
        ($_ -match '^\S+\s+\S+\s+(\d+)\s' -and $pids.ContainsKey($Matches[1])) })
    $keep | Set-Content -LiteralPath (Join-Path $OutDir 'logcat.txt') -Encoding UTF8
    # the system's own crash report (kept for days, also when the log has moved on)
    $systemCrash = @()
    if ($crashAt -ge 0) {
        $box = @(& $Adb -s $Serial shell dumpsys dropbox --print data_app_native_crash)
        $starts = @(for ($i = 0; $i -lt $box.Count; $i++) {
            if ($box[$i] -match '^(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d) data_app_native_crash' -and $Matches[1] -ge $sinceText) { $i }
        })
        if ($starts.Count) {
            $systemCrash = @($box[$starts[0]..($box.Count - 1)])
            $systemCrash | Set-Content -LiteralPath (Join-Path $OutDir 'absturz-system.txt') -Encoding UTF8
        }
    }
    & $Adb -s $Serial shell am force-stop $package
    $dumpLocal = Join-Path $OutDir 'gx-geraet'
    Remove-Item -LiteralPath $dumpLocal -Recurse -ErrorAction SilentlyContinue
    # (no dumps: adb reports a missing folder on stderr, which 'Stop' must not turn into an error)
    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    & $Adb -s $Serial pull $dumpRemote $dumpLocal 2>&1 | Out-Null
    $ErrorActionPreference = $previousPreference
    # the pictures as shown after hits on Leon
    $shots = @(Get-ChildItem -LiteralPath $dumpLocal -Filter 'treffer-*.bmp' -ErrorAction SilentlyContinue)
    if ($shots.Count) {
        $shotDir = Join-Path $OutDir 'treffer'
        New-Item -ItemType Directory -Force -Path $shotDir | Out-Null
        $shots | ForEach-Object { Move-Item -LiteralPath $_.FullName -Destination $shotDir -Force }
        Say "   Bilder nach Treffern: $($shots.Count) (Ordner treffer, kommen klein in senden.zip)"
    }
    # the GX data and, for R3 series, each picture as the device's GPU drew it (gx-geraet-NN-KK.bmp)
    $dumps = @(Get-ChildItem -LiteralPath $dumpLocal -Filter 'gx-geraet-*' -ErrorAction SilentlyContinue |
        Where-Object { $_.Extension -in '.bin', '.bmp' })
    if ($dumps.Count) {
        $zip = Join-Path $OutDir 'gx-geraet.zip'
        Remove-Item -LiteralPath $zip -ErrorAction SilentlyContinue
        Compress-Archive -Path $dumps.FullName -DestinationPath $zip
        Say "   GX-Daten vom Geraet: $($dumps.Count) Dateien (gx-geraet.zip, $([math]::Round((Get-Item -LiteralPath $zip).Length / 1MB, 1)) MB)"
        # the zip holds them; the folder would only double the size of the results
        Remove-Item -LiteralPath $dumpLocal -Recurse -ErrorAction SilentlyContinue
    } elseif ($GxDump) {
        Say '   GX-Daten vom Geraet: keine (RE4_GX_DUMP_DIR)'
    }
    & $Adb -s $Serial shell rm -rf $dumpRemote
    # the pictures of the replay (-GxReplay) as the device's GPU drew them
    if ($replayBins.Count) {
        $replayLocal = Join-Path $OutDir 'gx-replay'
        Remove-Item -LiteralPath $replayLocal -Recurse -ErrorAction SilentlyContinue
        $previousPreference = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        & $Adb -s $Serial pull $replayRemote $replayLocal 2>&1 | Out-Null
        & $Adb -s $Serial shell rm -rf $replayRemote 2>&1 | Out-Null
        $ErrorActionPreference = $previousPreference
        $replayShots = @(Get-ChildItem -LiteralPath $replayLocal -Filter 'replay-*.bmp' -ErrorAction SilentlyContinue)
        if ($replayShots.Count) {
            $replayZip = Join-Path $OutDir 'gx-replay.zip'
            Compress-Archive -Path $replayShots.FullName -DestinationPath $replayZip -Force
            Say "   Wiedergabe auf dem Geraet: $($replayShots.Count) Bilder (gx-replay.zip, $([math]::Round((Get-Item -LiteralPath $replayZip).Length / 1MB, 1)) MB)"
        } else {
            Say '   Wiedergabe auf dem Geraet: KEINE Bilder (Protokoll: "GX replay")'
        }
        Remove-Item -LiteralPath $replayLocal -Recurse -ErrorAction SilentlyContinue
    }
    # the memory card after the run (a copy; it stays on the device)
    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    & $Adb -s $Serial pull $cardRemote (Join-Path $OutDir 'speicherkarte-a.bin') 2>&1 | Out-Null
    $ErrorActionPreference = $previousPreference
    if ($AudioWav -gt 0) {
        $previousPreference = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        & $Adb -s $Serial pull $wavRemote (Join-Path $OutDir 'ton.wav') 2>&1 | Out-Null
        & $Adb -s $Serial shell rm -f $wavRemote 2>&1 | Out-Null
        $ErrorActionPreference = $previousPreference
    }
    Say "   Protokoll: $($keep.Count) Zeilen (logcat.txt)$(if ($systemCrash.Count) { ', Absturzbericht des Systems (absturz-system.txt)' }); App beendet"

    Say ''
    Say '== Auswertung =='
    $text = $keep -join "`n"
    function Has([string]$Pattern) { return [bool]($text -match $Pattern) }
    Say ("Kern geladen, Speicher reserviert: {0}" -f $(if (Has 'core loaded, MEM1 reserved|core process: ready \(pid \d+, MEM1 placeholder reserved') { 'ja' } else { 'NEIN' }))
    # the core in a process of its own (core_mode.cpp useCoreProcess, host/coreproc.cpp)
    $ownLine = $keep | Where-Object { $_ -match 'core: in a process of its own \((.*)\):' } | Select-Object -First 1
    $ownWhy = if ($ownLine -and $ownLine -match 'core: in a process of its own \((.*)\):') { $Matches[1] } else { '' }
    $ownPid = $keep | Where-Object { $_ -match 'core process started: pid (\d+)' } | Select-Object -First 1
    $ownEnd = $keep | Where-Object { $_ -match 'the game core process ended: ' } | Select-Object -First 1
    $ownState = if (-not $ownWhy) { 'nein (im App-Prozess)' }
        else { "ja ($ownWhy)$(if ($ownPid -and $ownPid -match 'pid (\d+)') { ', PID ' + $Matches[1] })" +
               $(if ($ownEnd) { ', ENDE: ' + (($ownEnd -split 'ended: ')[-1]) } else { '' }) }
    Say "Kern im eigenen Prozess:          $ownState"
    $ownProblem = $keep | Where-Object { $_ -match 'core process: .*(does not start|did not answer|missing|taken in the core process|no shared memory)' } | Select-Object -First 1
    if ($ownProblem) { Say "Kernprozess-FEHLER:               $(($ownProblem -split 'core process: ', 2)[-1])" }
    # what the way through the core process costs: the copy of each picture, the sound's waits
    $ownCopies = $keep | Where-Object { $_ -match 'core process: GX copies: ' } | Select-Object -Last 1
    if ($ownCopies) { Say "Kernprozess Bilder kopiert:       $(($ownCopies -split 'GX copies: ')[-1])" }
    $ownSound = $keep | Where-Object { $_ -match 'core process sound: ' } | Select-Object -Last 1
    if ($ownSound) { Say "Kernprozess Ton zuletzt:          $(($ownSound -split 'core process sound: ')[-1])" }
    Say ("Spiel gestartet:                  {0}" -f $(if (Has 'core: running') { 'ja' } else { 'NEIN' }))
    # the black screen at the start (patch 0085): from the core's load to title.dat read (before
    # the first logo; 19.10.2026 without the patch 5.8 s on the Retroid)
    $loadedAt = $keep | Where-Object { $_ -match 'core process: ready \(pid' } | Select-Object -First 1
    if (-not $loadedAt) { $loadedAt = $keep | Where-Object { $_ -match 'core loaded, MEM1 reserved' } | Select-Object -First 1 }
    $titleAt = [Array]::FindIndex([string[]]$keep, [Predicate[string]]{ param($l) $l -match 'Read File: [Ss][Ss]/[a-z]+/title\.dat' })
    $titleOk = $null
    if ($titleAt -ge 0) { $titleOk = $keep[$titleAt..($keep.Count - 1)] | Where-Object { $_ -match 'DVD: Read Ok' } | Select-Object -First 1 }
    $bootLine = $keep | Where-Object { $_ -match 'DVD: start reads done' } | Select-Object -First 1
    $startTime = '?'
    try {
        if ($loadedAt -and $titleOk) {
            $t0 = [datetime]::ParseExact($loadedAt.Substring(0, 18), 'MM-dd HH:mm:ss.fff', $null)
            $t1 = [datetime]::ParseExact($titleOk.Substring(0, 18), 'MM-dd HH:mm:ss.fff', $null)
            $startTime = '{0:N1} s' -f ($t1 - $t0).TotalSeconds
        }
    } catch { }
    Say "Start bis title.dat gelesen:      $startTime$(if ($bootLine) { ' (' + ($bootLine -split 'DVD: ')[-1] + ')' } else { ' (ohne den schnellen Start)' })"
    $rooms = $keep | Where-Object { $_ -match 'RE4Core.*room [0-9a-f]{3}: ' }
    Say "Raeume aufgebaut:                 $(if ($rooms) { ($rooms | ForEach-Object { ($_ -split 'RE4Core\s*:\s*')[-1] }) -join ' | ' } else { 'keiner' })"
    $models = @($keep | Where-Object { $_ -match 'RE4Core.*model ' })
    Say "Figuren-Modelle geladen:          $($models.Count) (Fehler: $(@($models | Where-Object { $_ -notmatch 'joints' }).Count))"
    # the opening event and the radio call end on their own: the core logs the
    # moment the player is free (no event, no sub screen for 300 frames)
    $free = $keep | Where-Object { $_ -match 'run: player free' } | Select-Object -First 1
    Say "Intro und Funkspruch beendet:     $(if ($free) { 'ja (' + (($free -split 'run: ')[-1]) + ')' } else { 'NEIN (Spieler nie frei)' })"
    # the view: GX emulation (what the game draws) or the bridge
    $view = $keep | Where-Object { $_ -match 'RE4Core.*view: ' } | Select-Object -First 1
    Say "Ansicht:                          $(if ($view) { ($view -split 'view: ')[-1] } else { '?' })"
    # the replay of R3 series (-GxReplay): the variants it drew
    foreach ($line in @($keep | Where-Object { $_ -match 'GX replay' } | Select-Object -Last 12)) {
        Say "Wiedergabe:                       $(($line -split 'RE4Core\s*:\s*', 2)[-1])"
    }
    $gxReady = $keep | Where-Object { $_ -match 'GX renderer (ready|:)' } | Select-Object -First 1
    if ($gxReady) { Say "GX-Renderer:                      $(($gxReady -split 'RE4Core\s*:\s*')[-1])" }
    $gxStats = $keep | Where-Object { $_ -match 'RE4Core.*GX: \d+ chunks' } | Select-Object -Last 1
    if ($gxStats) { Say "GX zuletzt:                       $(($gxStats -split 'GX: ')[-1])" }
    # the core's side: game time per picture and the part spent waiting for the renderer
    $gxCore = $keep | Where-Object { $_ -match 'GX core: ' } | Select-Object -Last 1
    if ($gxCore) { Say "GX im Kern zuletzt:               $(($gxCore -split 'GX core: ')[-1])" }
    # the GL work per picture and the GPU time (the renderer's own measurement)
    # the sound's output (CoreAudio): the game's Dolby Pro Logic II and Android's spatializer
    $soundState = $keep | Where-Object { $_ -match 'RE4Audio\s*: sound: game ' } | Select-Object -Last 1
    if ($soundState) { Say "Raumklang:                        $(($soundState -split 'sound: game ')[-1])" }
    $soundTrack = $keep | Where-Object { $_ -match 'RE4Audio\s*: sound: AudioTrack' } | Select-Object -Last 1
    if ($soundTrack) { Say "Tonausgabe:                       $(($soundTrack -split 'sound: ')[-1])" }
    $gxWork = $keep | Where-Object { $_ -match 'GX work per chunk: ' } | Select-Object -Last 1
    if ($gxWork) { Say "GX-Arbeit je Bild zuletzt:        $(($gxWork -split 'GX work per chunk: ')[-1])" }
    # the uniform stream (gx_gl.cpp) moves on between pictures only (MSAA flicker on the Adreno,
    # 20.10.2026); a picture larger than a quarter's rest still switches while drawing
    $split = $keep | Where-Object { $_ -match 'RE4GX\s*: GX uniform stream: a chunk needs' } | Select-Object -First 1
    if ($gxReady) { Say "Uniform-Ring:                     $(if ($split) { 'Wechsel MITTEN IM BILD: ' + (($split -split 'stream: ')[-1]) } else { 'Wechsel nur zwischen Bildern' })" }
    # the GPU measurement (-GpuBench): one line per picture option
    $bench = @($keep | Where-Object { $_ -match 'RE4Core\s*: GPU bench' })
    if ($GpuBench -or $bench.Count) {
        Say "GPU-Messung:                      $(if ($bench.Count) { "$($bench.Count) Zeilen" } else { 'KEINE (lief das Spiel bis r100?)' })"
        foreach ($line in $bench) { Say "  $(($line -split 'RE4Core\s*:\s*')[-1])" }
    }
    # the screen's rate and how often the picture is drawn (GPU step c, CoreActivity / FramePacing)
    $display = @($keep | Where-Object { $_ -match 'RE4Input\s*: display at ' })
    foreach ($line in @($display | Select-Object -First 4)) { Say "Bildschirm:                       $(($line -split 'RE4Input\s*:\s*')[-1])" }
    # gyro aiming (GyroAim.java, runtime/gyro.cpp): the switch and whether the aim turned
    $gyro = @($keep | Where-Object { $_ -match 'RE4Input\s*: gyro aiming |re4\s*: gyro: ' })
    foreach ($line in @($gyro | Select-Object -Last 3)) { Say "Gyro-Zielen:                      $(($line -split '(?:RE4Input|re4)\s*:\s*', 2)[-1])" }
    # MSAA on the tiles (GPU step d2): what the device keeps over a pass's end (gx_gl.cpp TilesSelfTest)
    $tilesTest = $keep | Where-Object { $_ -match 'RE4GX\s*: MSAA on the tiles, self-test' } | Select-Object -First 1
    if ($tilesTest) { Say "Kacheln-Selbsttest:               $(($tilesTest -split 'self-test:\s*')[-1])" }
    # the GPU and the extensions step d (MSAA on the tiles) needs (gx_gl.cpp, once per run)
    $gpuInfo = $keep | Where-Object { $_ -match 'RE4GX\s*: GPU: ' } | Select-Object -First 1
    if ($gpuInfo) { Say "Grafikchip:                       $(($gpuInfo -split 'RE4GX\s*:\s*GPU:\s*')[-1])" }
    $gxStream = $keep | Where-Object { $_ -match 'GX uniform stream: ' } | Select-Object -First 1
    if ($gxStream) { Say "GX-Uniform-Puffer:                $(($gxStream -split 'GX uniform stream: ')[-1])" }
    $shaderFail = @($keep | Where-Object { $_ -match 'RE4GX.*shader failed' })
    if ($shaderFail.Count) { Say "GX-Shader-FEHLER:                 $($shaderFail.Count) (Ausschnitt in logcat.txt)" }
    # data the GX emulation could not reach, and a command stream out of step
    $notInMemory = @($keep | Where-Object { $_ -match 're4\s*: GX: .* is not in memory' })
    if ($notInMemory.Count) { Say "GX-Daten nicht erreichbar:        $($notInMemory.Count)x (erste: $(($notInMemory[0] -split 'GX: ')[-1]))" }
    $lost = @($keep | Where-Object { $_ -match 'GX: the command stream lost its place' })
    if ($lost.Count) { Say "GX-Befehlsstrom aus dem Tritt:    $($lost.Count)x (Ausschnitt in logcat.txt)" }
    # the game's own messages (pLog->err / warn, patch 0076): each distinct one once, e.g.
    # "GetPrimBuff() : OVERFLOW" (draws of a picture dropped)
    $gameLines = @($keep | Where-Object { $_ -match 're4\s*: game: ' })
    $gameSeen = [ordered]@{}
    foreach ($line in $gameLines) {
        $message = ($line -split 're4\s*: game: ', 2)[-1]
        if (-not $gameSeen.Contains($message)) { $gameSeen[$message] = 1 }
    }
    Say "Spielmeldungen (pLog):            $($gameLines.Count) Zeilen, $($gameSeen.Count) verschiedene"
    foreach ($message in @($gameSeen.Keys | Select-Object -First 30)) { Say "  $message" }
    # diagnosis on 64 bit (patch 0078, runtime/watch.cpp): who writes a watched place
    $watch = @($keep | Where-Object { $_ -match 're4\s*: (watch|MotionMoveCore): ' })
    if ($watch.Count) {
        Say "Ueberwachung (Hardware-Haltepunkte, Patch 0078):"
        foreach ($line in @($watch | Select-Object -First 80)) { Say "  $(($line -split 're4\s*: ', 2)[-1])" }
    }
    # R3 (picture dump): where the player stood, for checks on the PC
    $r3 = @($keep | Where-Object { $_ -match 'RE4Core.*R3: room' })
    foreach ($line in $r3) { Say "R3-Aufnahme:                      $(($line -split 'R3: ')[-1])" }
    # the room camera after the first room start (patch 0044) and the sub screens
    # (inventory, map, radio) opening and closing: the map stayed black
    $camera = $keep | Where-Object { $_ -match 'run: camera after the room start' } | Select-Object -First 1
    if ($camera) { Say "Kamera nach dem Raumstart:        $(($camera -split 'room start: ')[-1])" }
    $subs = @($keep | Where-Object { $_ -match 'run: sub screen (open|closed)' })
    if ($subs.Count) {
        Say "Untermenues (auf/zu):             $((($subs | Select-Object -Last 8) | ForEach-Object { ($_ -split 'sub screen ')[-1] }) -join ', ')"
        if ($subs[-1] -match 'sub screen open') { Say "Untermenue am Ende noch OFFEN (Karte/Inventar liess sich nicht schliessen)" }
    }
    # hits on Leon (life went down) and the pictures the app kept after them
    $hits = @($keep | Where-Object { $_ -match 'run: player hit' })
    if ($hits.Count) {
        $marked = @($keep | Where-Object { $_ -match 'GX: a hit on the player marked' })
        Say "Treffer auf Leon:                 $($hits.Count) (erster: $(($hits[0] -split 'player hit, ')[-1])); fuer Bilder markiert: $($marked.Count)x"
    }
    $mapRead = @($keep | Where-Object { $_ -match 'Read File: SS/.*/ss_map.dat' })
    if ($mapRead.Count) { Say "Kartendatei gelesen:              $($mapRead.Count)x" }
    $presses = @($keep | Where-Object { $_ -match 're4\s*: pad: frame' })
    Say "Tastendruecke im Kern angekommen: $($presses.Count)"
    # the controller (CoreActivity/CoreInput, milestone 7): the layout, the input devices
    # with their axes, which Android key became which GameCube button, the rumble
    $layout = $keep | Where-Object { $_ -match 'RE4Input\s*: layout: ' } | Select-Object -First 1
    if ($layout) { Say "Tastenbelegung:                   $(($layout -split 'layout: ')[-1])" }
    foreach ($line in @($keep | Where-Object { $_ -match 'RE4Input\s*: device ' })) {
        Say "Eingabegeraet:                    $(($line -split 'RE4Input\s*:\s*device ')[-1])"
    }
    $keyLines = @($keep | Where-Object { $_ -match 'RE4Input\s*: key ' })
    $seenKeys = [ordered]@{}
    foreach ($line in $keyLines) {
        if ($line -match 'key (\S+) \((\d+)[^)]*\) -> (.*)$') { $seenKeys[$Matches[1]] = "$($Matches[1]) ($($Matches[2])) -> $($Matches[3])" }
    }
    Say "Tasten gedrueckt (Android -> GC):  $($keyLines.Count) Druecke, $($seenKeys.Count) verschiedene"
    foreach ($k in $seenKeys.Values) { Say "  $k" }
    # the sound (milestone 8): the core's counters every 10 s, the app's output, the WAV
    $sound = $keep | Where-Object { $_ -match 're4\s*: audio: \d+ blocks' } | Select-Object -Last 1
    Say "Ton (Kern, zuletzt):              $(if ($sound) { ($sound -split 'audio: ')[-1] } else { 'keine Tonbloecke im Protokoll' })"
    # sounds the driver lost (AX stopped their voice: over its DSP budget or taken by another)
    $drops = @($keep | Where-Object { $_ -match 'ISS Voice Drop' })
    $limit = $keep | Where-Object { $_ -match 'dsp: AX may use' } | Select-Object -First 1
    $budget = ''
    if ($limit -match 'AX may use (\d+) DSP cycles.*DSP: (\d+)') { $budget = " (AX-Budget $($Matches[1]) DSP-Takte je Rahmen, GameCube $($Matches[2]))" }
    Say "Abgebrochene Toene (Voice Drop):  $($drops.Count)$budget"
    # the movies (milestone 9, runtime/movie_status.cpp): start, every 5 s, end or START
    $movies = @($keep | Where-Object { $_ -match 're4\s*: movie: ' })
    $movieDone = @($movies | Where-Object { $_ -match 'movie: .* (ended|skipped \(START\)),' })
    Say "Filme gestartet / beendet:        $(@($movies | Where-Object { $_ -match 'movie: .* started' }).Count) / $($movieDone.Count)"
    foreach ($line in $movieDone) { Say "  Film: $(($line -split 'movie: ')[-1])" }
    if ($movies.Count -and $movies[-1] -notmatch 'movie: .* (ended|skipped \(START\)),') {
        Say "  Film am Ende der Beobachtung noch offen: $(($movies[-1] -split 'movie: ')[-1])"
    }
    foreach ($line in @($keep | Where-Object { $_ -match 'File not found : movie/' } | Select-Object -First 3)) {
        Say "  Filmdatei FEHLT: $(($line -split 'File not found : ')[-1])"
    }
    # the CRI middleware reports errors as "E<number>: ..." (the game's callback then waits forever)
    $criErrors = @($keep | Where-Object { $_ -match 'OSReport: E\d{7,8}' })
    if ($criErrors.Count) { Say "CRI-FEHLER (Film):                $($criErrors.Count), zuerst: $(($criErrors[0] -split 'OSReport: ')[-1])" }
    # the memory card (milestone 10, runtime/memcard.cpp, patch 0058): the card, its files, the
    # steps of the game's card screens, the options after the boot check, a loaded save
    $cardLines = @($keep | Where-Object { $_ -match 're4\s*: memcard: ' })
    foreach ($line in @($cardLines | Where-Object { $_ -match 'memcard: (/|slot A)' } | Select-Object -First 2)) {
        Say "Speicherkarte:                    $(($line -split 'memcard: ')[-1])"
    }
    $files = @($cardLines | Where-Object { $_ -match 'memcard: files: ' })
    foreach ($line in $files) { Say "  Dateien: $(($line -split 'files: ')[-1])" }
    $checkDone = $keep | Where-Object { $_ -match 'run: memory card check done' } | Select-Object -First 1
    Say "Startpruefung der Karte:          $(if ($checkDone) { ($checkDone -split 'run: memory card check done ')[-1] } else { 'nicht beendet (Titel nicht erreicht?)' })"
    $screens = @($keep | Where-Object { $_ -match 're4\s*: card: ' })
    if ($screens.Count) {
        Say "Kartenbildschirm (Schritte, letzte 40 von $($screens.Count)):"
        $screens | Select-Object -Last 40 | ForEach-Object { Say "  $(($_ -split 're4\s*: card: ')[-1])" }
    }
    foreach ($line in @($keep | Where-Object { $_ -match 'run: save screen (opened|closed)' })) {
        Say "Testhilfe Speichern (-SaveTest):  $(($line -split 'run: ')[-1])"
    }
    foreach ($line in @($keep | Where-Object { $_ -match 'run: save loaded from the memory card' })) {
        Say "Spielstand geladen:               $(($line -split 'memory card ')[-1])"
    }
    $resets = @($keep | Where-Object { $_ -match 'stub called: OSResetSystem' })
    if ($resets.Count) { Say 'Hinweis: das Spiel wollte die Konsole neu starten (OSResetSystem, z. B. "kein Platz" -> Neustart)' }
    $cardFile = Join-Path $OutDir 'speicherkarte-a.bin'
    if (Test-Path -LiteralPath $cardFile) {
        Say "Karte vom Geraet:                 speicherkarte-a.bin ($([math]::Round((Get-Item -LiteralPath $cardFile).Length / 1MB, 1)) MB, kommt mit in senden.zip)"
        if (Get-Command wsl.exe -ErrorAction SilentlyContinue) {
            $wslArgs = @()
            if ($Distro) { $wslArgs = @('-d', $Distro) }
            $toWsl = { param($p) ((& wsl.exe @wslArgs --exec wslpath -a ((Resolve-Path -LiteralPath $p).Path -replace '\\', '/')) | Select-Object -First 1).Trim() }
            $old = $ErrorActionPreference
            $ErrorActionPreference = 'Continue'
            try {
                @(& wsl.exe @wslArgs --exec python3 "$(& $toWsl $repo)/port/tools/memcard_list.py" (& $toWsl $cardFile) 2>&1 |
                    ForEach-Object { "$_" }) | ForEach-Object { Say "  $_" }
            } finally { $ErrorActionPreference = $old }
        }
    } else {
        Say 'Karte vom Geraet:                 keine (noch keine Kartendatei oder -Manual nicht benutzt)'
    }
    $output = $keep | Where-Object { $_ -match 'RE4Audio\s*: sound' } | Select-Object -First 1
    Say "Tonausgabe der App:               $(if ($output) { ($output -split 'RE4Audio\s*:\s*')[-1] } else { 'NICHT gestartet' })"
    $wav = Join-Path $OutDir 'ton.wav'
    if (Test-Path -LiteralPath $wav) {
        Say "Ton-Aufnahme:                     ton.wav ($([math]::Round((Get-Item -LiteralPath $wav).Length / 1MB, 1)) MB, zum Anhoeren)"
        if (Get-Command wsl.exe -ErrorAction SilentlyContinue) {
            $wslArgs = @()
            if ($Distro) { $wslArgs = @('-d', $Distro) }
            $toWsl = { param($p) ((& wsl.exe @wslArgs --exec wslpath -a ((Resolve-Path -LiteralPath $p).Path -replace '\\', '/')) | Select-Object -First 1).Trim() }
            $old = $ErrorActionPreference
            $ErrorActionPreference = 'Continue'
            try {
                @(& wsl.exe @wslArgs --exec python3 "$(& $toWsl $repo)/port/tools/audio_wav_report.py" (& $toWsl $wav) 5 2>&1 |
                    ForEach-Object { "$_" }) | ForEach-Object { Say $_ }
            } finally { $ErrorActionPreference = $old }
        }
    } elseif ($AudioWav -gt 0) {
        Say 'Ton-Aufnahme:                     KEINE (ton.wav fehlt auf dem Geraet)'
    }
    $vibrator = $keep | Where-Object { $_ -match 'RE4Input\s*: rumble: ' } | Select-Object -First 1
    $rumbles = @($keep | Where-Object { $_ -match 'pad: rumble from frame' })
    Say "Vibration:                        $(if ($vibrator) { ($vibrator -split 'rumble: ')[-1] } else { '?' }); das Spiel ruettelte $($rumbles.Count)x$(if ($rumbles.Count -ge 16) { ' (oder oefter)' })"
    if ($crashAt -ge 0 -or (Has 'Fatal signal|FATAL EXCEPTION|SIGSEGV|SIGABRT|SIGBUS')) {
        Say "ABSTURZ$(if ($crashAt -ge 0) { " nach etwa $crashAt s" }); Ausschnitt:"
        $crashLines = @(($keep + $systemCrash) | Where-Object {
            $_ -match 'Fatal signal|FATAL|signal \d+ \(|Abort message|Cause:|fault addr|backtrace|#\d\d pc |FORTIFY|[Ss]cudo' })
        $crashLines | Select-Object -First 60 | ForEach-Object { Say "  $_" }
        if (-not $crashLines.Count) { Say '  (keine Absturzzeilen im Protokoll; letzte Zeilen der App unten)' }
        # the backtrace as functions and source lines (the libraries with symbols of this PC)
        $frames = @($keep | Where-Object { $_ -match '#\d+ pc [0-9a-f]+' })
        if (-not $frames.Count) { $frames = @($systemCrash | Where-Object { $_ -match '#\d+ pc [0-9a-f]+' }) }
        if ($frames.Count -and (Get-Command wsl.exe -ErrorAction SilentlyContinue)) {
            $stackFile = Join-Path $OutDir 'absturz-stapel.txt'
            $frames | Set-Content -LiteralPath $stackFile -Encoding UTF8
            $wslArgs = @()
            if ($Distro) { $wslArgs = @('-d', $Distro) }
            $toWsl = { param($p) ((& wsl.exe @wslArgs --exec wslpath -a ((Resolve-Path -LiteralPath $p).Path -replace '\\', '/')) | Select-Object -First 1).Trim() }
            $old = $ErrorActionPreference
            $ErrorActionPreference = 'Continue'
            try {
                $sym = @(& wsl.exe @wslArgs --exec bash "$(& $toWsl $repo)/port/tools/android_symbolize.sh" (& $toWsl $stackFile) 2>&1 |
                    ForEach-Object { "$_" })
            } finally { $ErrorActionPreference = $old }
            Say 'Absturzstelle im Code:'
            $sym | ForEach-Object { Say "  $_" }
        }
    }
    Say 'Letzte 40 Zeilen des Kern-Protokolls (ohne Tastenzeilen):'
    $keep | Where-Object { $_ -match ' (re4|RE4Core)\s*:' -and $_ -notmatch 'pad: frame' } | Select-Object -Last 40 |
        ForEach-Object { Say "  $_" }
}
# what to send: the texts and the pictures as small JPEGs in one zip (the PNGs, the
# build logs and the GX data stay here; gx-geraet.zip goes extra when it is small)
$send = Join-Path $OutDir 'senden.zip'
$staging = Join-Path $OutDir 'senden'
Remove-Item -LiteralPath $staging -Recurse -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $staging | Out-Null
foreach ($pattern in @('bericht.txt', 'logcat.txt', 'profil-*.txt', 'absturz-*.txt', 'kern-build.log', 'speicherkarte-a.bin')) {
    Get-ChildItem -LiteralPath $OutDir -Filter $pattern -ErrorAction SilentlyContinue |
        ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $staging }
}
# the sound recording (-AudioWav) when it is small enough to send
$wavFile = Get-Item -LiteralPath (Join-Path $OutDir 'ton.wav') -ErrorAction SilentlyContinue
if ($wavFile -and $wavFile.Length -le 12MB) { Copy-Item -LiteralPath $wavFile.FullName -Destination $staging }
try {
    Add-Type -AssemblyName System.Drawing
    $jpeg = [System.Drawing.Imaging.ImageCodecInfo]::GetImageEncoders() | Where-Object { $_.MimeType -eq 'image/jpeg' }
    $quality = New-Object System.Drawing.Imaging.EncoderParameters 1
    $quality.Param[0] = New-Object System.Drawing.Imaging.EncoderParameter ([System.Drawing.Imaging.Encoder]::Quality, [long]85)
    $pictures = @(Get-ChildItem -LiteralPath $OutDir -Filter 'bild-*.png' -ErrorAction SilentlyContinue)
    $pictures += @(Get-ChildItem -LiteralPath (Join-Path $OutDir 'treffer') -Filter 'treffer-*.bmp' -ErrorAction SilentlyContinue)
    $pictures | ForEach-Object {
        $image = [System.Drawing.Image]::FromFile($_.FullName)
        $width = [math]::Min(960, $image.Width)
        $height = [int]($image.Height * $width / $image.Width)
        $small = New-Object System.Drawing.Bitmap $width, $height
        $draw = [System.Drawing.Graphics]::FromImage($small)
        $draw.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $draw.DrawImage($image, 0, 0, $width, $height)
        $small.Save((Join-Path $staging ($_.BaseName + '.jpg')), $jpeg, $quality)
        $draw.Dispose(); $small.Dispose(); $image.Dispose()
    }
} catch {
    Get-ChildItem -LiteralPath $OutDir -Filter 'bild-*.png' -ErrorAction SilentlyContinue | Select-Object -Last 4 |
        ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $staging }
}
Compress-Archive -Path (Join-Path $staging '*') -DestinationPath $send -Force
Remove-Item -LiteralPath $staging -Recurse -ErrorAction SilentlyContinue
$sendMb = [math]::Round((Get-Item -LiteralPath $send).Length / 1MB, 1)
$gxZip = Join-Path $OutDir 'gx-geraet.zip'
$gxMb = if (Test-Path -LiteralPath $gxZip) { [math]::Round((Get-Item -LiteralPath $gxZip).Length / 1MB, 1) } else { 0 }
Say ''
$replayZip = Join-Path $OutDir 'gx-replay.zip'
if (Test-Path -LiteralPath $replayZip) {
    Say "Die Wiedergabe liegt in gx-replay.zip ($([math]::Round((Get-Item -LiteralPath $replayZip).Length / 1MB, 1)) MB, ebenfalls schicken)."
}
if ($gxMb -eq 0) {
    Say "Fertig. Bitte senden.zip ($sendMb MB) aus $OutDir schicken."
} elseif ($gxMb -le 25) {
    Say "Fertig. Bitte senden.zip ($sendMb MB) und gx-geraet.zip ($gxMb MB) aus $OutDir schicken."
} else {
    Say "Fertig. Bitte senden.zip ($sendMb MB) aus $OutDir schicken; gx-geraet.zip ($gxMb MB) ist zum Senden zu gross und bleibt da."
}
[Console]::OutputEncoding = $previousEncoding
