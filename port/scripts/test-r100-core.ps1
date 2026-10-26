<#
.SYNOPSIS
Lokale Pruefung des Spielkerns mit deinen Disc-Daten (Raum r100).

.DESCRIPTION
Der Spielkern ist fuer den Test ein Linux-Programm, deshalb laeuft die Pruefung in
WSL2 (Ubuntu). Dieses Skript erledigt alles von Windows aus:

  1. richtet die Werkzeuge in WSL einmalig ein (port/tools/whole_setup_linux.sh,
     braucht Internet; danach schnell),
  2. startet port/tools/whole_r100_check.sh: kopiert den Quelltext dieses
     Ordners nach ~/re4-kern in WSL, baut den Kern und prueft ohne Disc-Daten,
     vergleicht die Formatumwandler mit den App-Lesern auf deinen Disc-Dateien,
     startet das Spiel vom Titelbild mit Debug-Start nach r100 und laesst es
     10.000 Bilder laufen, spielt danach jeden Film der Disc ganz ab
     (Schnelllauf; Standbilder im Ordner bilder-film, Ton des Vorfilms als
     film-d1-opening.wav), prueft danach die Speicherkarte (Meilenstein 10:
     Systemdatei anlegen, in r100 speichern, Neustart, im Titel laden; die
     Karte dieses Laufs liegt als speicherkarte-a.bin bei den Ergebnissen),
  3. legt den Bericht und die Logs in -OutDir ab (Standard: Desktop\RE4-Kern-Pruefung).

Raum-Rundgang (-Rooms): statt Spiellauf, Filmen und Speicherkarte startet das
Skript nach dem Build jeden angegebenen Raum ueber den Debug-Start und laesst
ihn -TourFrames Bilder laufen (Standard 6000, gut eine Spielminute). Sobald Leon
frei ist, laeuft er, rennt, dreht sich, zielt, schiesst, oeffnet die Karte und
drueckt ab und zu A (Tueren, Gegenstaende); -TourScript none laesst ihn stehen.
-TourJobs Raeume laufen gleichzeitig (Standard: halb so viele wie Prozessorkerne).
Der Bericht hat eine Zeile je Raum und sammelt danach: Abstuerze mit Stelle,
Stillstaende (Haenger), Raeume, die nicht starten, ungueltige Positionen ("not a
number"), Todesschleifen, die "check:"-Hinweise des Ports, Tode und Raumwechsel.
-Rooms 206,300,401 nimmt diese Raeume, -Rooms set zwei Raeume je Raumprogramm
(27 Raeume), -Rooms all jeden Raum des Spiels (126, auf dem PC etwa 10 Minuten).
-Sanitize nimmt dafuer den Pruef-Build mit Sanitizern (meldet Speicherfehler
sofort, mit Datei und Zeile; etwa dreimal langsamer; mit -Arm64 nur UBSan, die
Pruefung undefinierter Rechenschritte), -Arm64 den ARM64-Build unter qemu (wie
arm64-v8a, aber viel langsamer, etwa eine Stunde fuer alle Raeume; der Bericht
vergleicht danach mit dem letzten i686-Rundgang).
-Arm32 nimmt den ARM32-Build unter qemu (wie armeabi-v7a, der Kern auf den meisten
Geraeten; ebenfalls langsam, mit -Sanitize nur UBSan).

Nachlade-Pruefung (-Reload WAFFE): statt Spiellauf, Filmen und Speicherkarte
startet das Skript r100, gibt Leon die Waffe (15 = Super Magnum) wie das
Debug-Menue und laedt zweimal nach: einmal mit gehaltenem Zielen (R), einmal
mit frueh losgelassenem. Der Bericht fasst beide zusammen (wie lange Leons
Nachlade-Bewegung lief, wie nah seine linke Hand der Waffe kam) und legt
Bilder beider Nachladevorgaenge in bilder-gx ab.

Disc-Daten (-DataDir), eine dieser Formen:
  - ein Ordner mit den Disc-Abbildern (.iso oder .gcm), z. B. D:\RE4\Disc
  - mit Dolphin entpackte Disc ("Gesamte Disc extrahieren": Ordner mit sys und files)
  - der Import-Ordner der App (disc1, disc2) oder ein Port-Ordner (disc0, disc1)
Die Disc-Daten werden nie ins Repository kopiert. Die Debug-Discs (G4BE08) und die
europaeischen Verkaufs-Discs (G4BP08, port/docs/retail-eu-plan.md) gehen beide; Abbilder
einer anderen Version als beim letzten Lauf werden neu entpackt, gemischte Discs abgelehnt.

Vorher den Branch der aktuellen Aufgabe holen (Name steht in der Aufgabe), z. B.:
  git fetch origin
  git checkout claude/nifty-feynman-jv729h
  git pull

.EXAMPLE
.\port\scripts\test-r100-core.ps1 -DataDir 'D:\RE4\Disc'

.EXAMPLE
powershell -ExecutionPolicy Bypass -File .\port\scripts\test-r100-core.ps1 -DataDir 'D:\RE4\Disc'
Falls Windows das Ausfuehren von Skripten sperrt.

.EXAMPLE
.\port\scripts\test-r100-core.ps1 -DataDir 'E:\Development\Android\RE4-DiscdataEuro'
Mit den europaeischen Verkaufs-Discs (Meilenstein Retail-Discs).

.EXAMPLE
.\port\scripts\test-r100-core.ps1 -DataDir 'D:\RE4\Disc' -OnlyCard

.EXAMPLE
.\port\scripts\test-r100-core.ps1 -Rooms set

.EXAMPLE
.\port\scripts\test-r100-core.ps1 -Rooms all -Sanitize
Der Rundgang ueber alle Raeume mit dem Pruef-Build mit Sanitizern.

.EXAMPLE
.\port\scripts\test-r100-core.ps1 -Rooms all -Arm64
Der Rundgang mit dem ARM64-Build unter qemu.

.EXAMPLE
.\port\scripts\test-r100-core.ps1 -Rooms all -Arm32
Der Rundgang mit dem ARM32-Build unter qemu.

.EXAMPLE
.\port\scripts\test-r100-core.ps1 -Reload 15
Die Nachlade-Pruefung mit der Super Magnum.
Nur bauen und den Speicherkarten-Teil pruefen (Abschnitt 7, wenige Minuten).

.EXAMPLE
.\port\scripts\test-r100-core.ps1 -DataDir 'D:\RE4\Disc' -Arm
Zusaetzlich der ARM32-Build unter qemu (wie auf dem Retroid, aber langsam).
.EXAMPLE
.\port\scripts\test-r100-core.ps1 -Compare
Vergleichslauf 32/64 bit (Meilenstein 12): baut i686 und x86_64, spielt beide mit derselben
Tastenfolge ab r120 (Vorfilm, Polizeifahrt, r100; 30000 Bilder, mit -Room und -Frames
aenderbar), vergleicht Bild fuer Bild und meldet das erste Bild, ab dem 64 bit abweicht, mit
den Werten beider Seiten und GX-Bildern beider Seiten (Ordner bilder-vergleich).

.EXAMPLE
.\port\scripts\test-r100-core.ps1 -Lp64
Alles mit dem 64-bit-Build (x86_64, steht fuer arm64-v8a) statt dem 32-bit-PC-Build;
laesst sich mit -OnlyCard, -Rooms und -Reload kombinieren.
#>
param(
    [string]$DataDir = '',
    [string]$OutDir = (Join-Path ([Environment]::GetFolderPath('Desktop')) 'RE4-Kern-Pruefung'),
    [int]$Frames = 10000,
    [string]$Room = '100',
    [switch]$Arm,
    [switch]$Lp64,
    [switch]$Compare,
    [switch]$OnlyCard,
    [string]$Rooms = '',
    [int]$TourFrames = 6000,
    [int]$TourJobs = 0,
    [string]$TourScript = '',
    [switch]$Sanitize,
    [switch]$Arm64,
    [switch]$Arm32,
    [int]$Reload = 0,
    [switch]$SkipSetup,
    [string]$Distro = ''
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
# Linux programs write UTF-8; show it (and read wslpath's answer) as such.
$previousEncoding = [Console]::OutputEncoding
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

if (-not (Get-Command wsl.exe -ErrorAction SilentlyContinue)) {
    throw 'WSL fehlt. Einmal in PowerShell als Administrator: wsl --install -d Ubuntu-24.04 , Rechner neu starten, dann dieses Skript erneut starten.'
}
# without -DataDir: the disc data folder next to the repository (RE4-Discdata,
# as on the project owner's PC), else ask
if (-not $DataDir) {
    $guess = Join-Path (Split-Path -Parent $repo) 'RE4-Discdata'
    if (Test-Path -LiteralPath $guess) {
        $DataDir = $guess
        Write-Host "Disc-Daten: $DataDir"
    } else {
        $DataDir = Read-Host 'Ordner mit den Disc-Daten'
    }
}
if (-not (Test-Path -LiteralPath $DataDir)) { throw "Ordner mit den Disc-Daten nicht gefunden: $DataDir" }
if ($Room -notmatch '^[0-5][0-9A-Fa-f]{2}$') { throw "Raum als drei Hex-Ziffern angeben, z. B. 100 (nicht: $Room)" }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
Remove-Item -LiteralPath (Join-Path $OutDir 'bericht.txt') -ErrorAction SilentlyContinue

$wslBase = @()
if ($Distro) { $wslBase = @('-d', $Distro) }

# Windows path -> WSL path (/mnt/c/...); --exec runs without a shell, so
# spaces and backslashes in the path stay as they are.
function ConvertTo-WslPath([string]$Path) {
    $full = (Resolve-Path -LiteralPath $Path).Path -replace '\\', '/'
    $converted = & wsl.exe @wslBase --exec wslpath -a $full
    if ($LASTEXITCODE -ne 0 -or -not $converted) { throw "Pfad laesst sich nicht nach WSL umrechnen: $Path" }
    return ($converted | Select-Object -First 1).Trim()
}

$repoWsl = ConvertTo-WslPath $repo
$dataWsl = ConvertTo-WslPath $DataDir
$outWsl = ConvertTo-WslPath $OutDir

if (-not $SkipSetup) {
    Write-Host 'Werkzeuge in WSL pruefen und bei Bedarf einrichten (beim ersten Mal einige Minuten) ...'
    & wsl.exe @wslBase -u root --exec bash "$repoWsl/port/tools/whole_setup_linux.sh"
    if ($LASTEXITCODE -ne 0) { throw 'Einrichtung der Werkzeuge in WSL fehlgeschlagen (Internetverbindung noetig).' }
}

$checkArgs = @('--source', $repoWsl, '--data', $dataWsl, '--out', $outWsl)
# the comparison (-Compare) starts in r120 with 30000 frames unless -Room / -Frames are given
if (-not $Compare -or $PSBoundParameters.ContainsKey('Frames')) { $checkArgs += @('--frames', "$Frames") }
if (-not $Compare -or $PSBoundParameters.ContainsKey('Room')) { $checkArgs += @('--room', $Room) }
if ($Arm) { $checkArgs += '--arm' }
if ($Lp64) { $checkArgs += '--lp64' }
if ($Compare) { $checkArgs += '--compare' }
if ($OnlyCard) { $checkArgs += '--card-only' }
if ($Rooms) { $checkArgs += @('--rooms', $Rooms, '--tour-frames', "$TourFrames") }
if ($TourJobs -gt 0) { $checkArgs += @('--tour-jobs', "$TourJobs") }
if ($TourScript) { $checkArgs += @('--tour-script', $TourScript) }
if ($Sanitize) { $checkArgs += '--sanitize' }
if ($Arm64) { $checkArgs += '--aarch64' }
if ($Arm32) { $checkArgs += '--armhf' }
if ($Reload -gt 0) { $checkArgs += @('--reload', "$Reload") }
Write-Host 'Pruefung laeuft (Build beim ersten Mal 10 bis 30 Minuten, danach schneller) ...'
& wsl.exe @wslBase --exec bash "$repoWsl/port/tools/whole_r100_check.sh" @checkArgs
$code = $LASTEXITCODE

$report = Join-Path $OutDir 'bericht.txt'
Write-Host ''
if (Test-Path -LiteralPath $report) {
    if ($code -eq 0) { Write-Host 'Ergebnis: ALLES OK' -ForegroundColor Green }
    else { Write-Host 'Ergebnis: NICHT BESTANDEN (Einzelheiten im Bericht)' -ForegroundColor Yellow }
    Write-Host "Bericht: $report"
    Write-Host 'Bitte schicke mir die Datei bericht.txt (die uebrigen Dateien im Ordner nur, wenn ich danach frage).'
} else {
    Write-Host "Kein Bericht entstanden (Exit-Code $code). Bitte schicke mir die Ausgabe oben." -ForegroundColor Red
}
[Console]::OutputEncoding = $previousEncoding
exit $code
