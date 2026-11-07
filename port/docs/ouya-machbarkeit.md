# OUYA: Machbarkeit eines Test-Builds

**Beendet am 22.10.2026 (Entscheidung des Projektinhabers):** die OUYA ist zu schwach. Der
Kern lief schnell genug (O1), der GLES-2-Renderer zeichnet richtig (O2), aber Wald und Dorf
brauchen auf der OUYA-GPU 200–390 ms je Bild, und selbst ohne jede Farb- und Lichtrechnung
noch 65–78 ms (Abschnitt „Ergebnisse auf der OUYA“). Die ganze OUYA-Arbeit liegt im Branch
`ouya-discarded` (Stand des Branches `claude/adoring-mendel-vclbu8`); sie wird nicht
weitergeführt und nicht in die Haupt-App übernommen.

**Wieder aufgenommen am 28.10.2026 (Wunsch des Projektinhabers):** `ouya-discarded` mit
`master` abgeglichen, um auf dem neuen Stand weiterzutesten (Abschnitt „Abgleich mit master“
am Ende). Nichts davon geht in die Haupt-App. **07.11.2026:** erneut mit `master` (0.99.1-rc)
abgeglichen und eine **leichte OUYA-Fassung** gebaut (kürzere Sicht mit näherem Nebel,
vereinfachte Farbberechnung, kleinere Auflösung): auf der OUYA im Median 89 statt 210 ms je
Bild (Abschnitt „Leichte OUYA-Fassung“ am Ende).

Stand 21.10.2026, Branch `claude/adoring-mendel-vclbu8`. Nur Bestandsaufnahme
in der Cloud: nichts gebaut, nichts auf einer OUYA geprüft. Die Angaben zum
Gerät stammen aus den bekannten technischen Daten der OUYA, nicht aus einer
Messung; was am Gerät zu prüfen ist, steht jeweils dabei.

## Kurz

Ein OUYA-Build ist **möglich, aber kein kleiner Umbau** der heutigen APK. Drei
Hürden, jede für sich größer als eine Sitzung:

1. **Android 4.1 (API 16):** Der Kernmodus verlangt heute Android 6.0
   (`minSdk 23`), der Kern wird für Android 5.0 gebaut (`android-21`), das NDK
   r28c kann Android 4.1 gar nicht mehr.
2. **Nur OpenGL ES 2.0:** Die GX-Nachbildung ist durchgehend GLES 3 (Shader
   mit Ganzzahl-Rechnung, Uniform-Blöcke, MSAA, Fences …). Für die OUYA
   braucht es einen eigenen, vereinfachten GLES-2-Weg.
3. **Leistung (größte Unbekannte):** Schon auf dem Retroid rechnet der Kern
   14–20 ms je Spielbild (von 33,3 ms). Ein Cortex-A9 mit 1,7 GHz ist grob
   geschätzt 4–6× langsamer je Kern als der große Kern des Snapdragon 865.

Vorschlag: **erst messen, dann umbauen** (Stufe O1 unten). Die Messung braucht
keinen neuen Renderer und zeigt, ob sich der Rest lohnt.

## Das Gerät

| | OUYA | Retroid Pocket Flip 2 (zum Vergleich) |
|---|---|---|
| Prozessor | Nvidia Tegra 3, 4× Cortex-A9, 1,7 GHz, NEON | Snapdragon 865 |
| Grafik | ULP GeForce, **nur OpenGL ES 2.0**; Fragment-Shader nur `mediump` (fp20); Tiefenpuffer 16 bit (`GL_NV_depth_nonlinear`); S3TC/DXT vorhanden | Adreno 650, GLES 3.2 |
| Speicher | 1 GB RAM, 8 GB Flash (frei etwa 5 GB), USB-Anschluss für Sticks | |
| System | Android 4.1.2 (API 16), Linux-Kernel 3.1.10, Dalvik (kein ART) | |
| Controller | Standard-Gamepad: O/U/Y/A, Sticks, Steuerkreuz, L1/R1, L2/R2 analog, L3/R3, Touchpad, OUYA-Taste (`KEYCODE_MENU`); **kein SELECT und kein START** | |

ADB geht über das Micro-USB-Kabel (unter Windows braucht `adb` vermutlich die
Herstellerkennung `0x2836` in `%USERPROFILE%\.android\adb_usb.ini`; am Gerät zu
prüfen).

## Was die App heute voraussetzt

| Bereich | Heute | OUYA | Aufwand |
|---|---|---|---|
| Android-Version | Flavor `core`: `minSdk 23`; `ndkVersion 28.2.13676358`; Kern mit `-DANDROID_PLATFORM=android-21` (`whole_build_android.sh`) | API 16 geht nur mit NDK **r23c** (das letzte NDK für API 16–18; r24 hob auf 19, r26 auf 21) | mittel: eigenes NDK für den OUYA-Build, Kern neu bauen, prüfen, ob der Spielkern mit dem älteren clang (LLVM 12) gleich übersetzt |
| Java-Teile | APIs ab 21–33 teils ohne Versionsabfrage: `AudioAttributes` (17×), `AudioTrack(AudioAttributes, …)`, `setBufferSizeInFrames`/`getUnderrunCount` (24), `Spatializer` (32), `Display.Mode` (23), `VibrationEffect`/`VibratorManager` (26/31), `OnBackInvokedCallback` (33), `SUPPORTED_ABIS` (21), Thema `Theme.Material` (21) | Versionsabfragen und alte Wege (Ton über `AudioTrack` mit Stream-Typ, `Vibrator.vibrate(ms)`, Thema Holo) | klein bis mittel, mechanisch; `lint` mit `minSdk 16` listet alles |
| Disc-Import, Speicherkarte | `ACTION_OPEN_DOCUMENT`, `ACTION_OPEN_DOCUMENT_TREE`, `DocumentsContract` (API 19/21) | gibt es nicht; `ACTION_GET_CONTENT` (braucht einen Dateimanager) oder ein fester Ordner, z. B. `/sdcard/CubeEvil4/` mit `READ_EXTERNAL_STORAGE` | klein |
| Laden des Kerns | 32 bit: MEM1 bei `0x80000000..0x83000000`, die Kern-Bibliotheken oberhalb von `0x80000000` (das Spiel erkennt eigene Adressen an Bit 31); Bibliotheken mit `dlopen` nach Namen | Kernel 3.1 legt Speicher auf ARM vermutlich von unten nach oben an (zu prüfen); dann landen Bibliotheken unter `0x80000000`. Der **Kernprozess** (`libre4_coreproc.so`, `host/low_memory.h`) belegt schon heute alle Lücken unter `0x83000000` vorab: das passt. Vor Android 4.3/6.0 sucht der Linker abhängige Bibliotheken nicht im App-Ordner: Bibliotheken der Reihe nach mit vollem Pfad laden | mittel: auf der OUYA immer im Kernprozess laufen |
| Gemeinsamer Speicher | `core_ipc.cpp`: `memfd_create` (Kernel 3.17) oder `ASharedMemory_create` (API 26) | beides fehlt; `/dev/ashmem` direkt (gibt es auf Android 4.1) | klein |
| Absturzbericht | `coreproc.cpp`: `process_vm_readv` (Kernel 3.2) | fehlt; der Bericht verliert nur die Stapel-Suche | keiner |
| Grafik | `glEsVersion 0x00030000` Pflicht; GX-Shader in GLSL ES 3.00 mit Ganzzahl-TEV (8-bit-Rechnung, Bitschieben, `ivec`/`uvec`), Texturkoordinaten in Festkomma (1/128 Texel), Uniform-Blöcke `std140`, `glDrawElementsBaseVertex`, Sampler-Objekte, MSAA-Renderbuffer, `glBlitFramebuffer`, Fences, `glMapBufferRange`, Programm-Binärdaten | eigener GLES-2-Weg der GX-Nachbildung (siehe unten) | **groß**: mehrere Sitzungen |
| Steuerung | Tasten und Achsen über `InputDevice`/`MotionEvent`; App-Menü auf SELECT, Pause auf START | Achsen und Tasten kommen als normales Gamepad an (am Gerät zu prüfen); SELECT/START fehlen: andere Belegung nötig (z. B. OUYA-Taste kurz = START, Touchpad oder L3+R3 = App-Menü) | klein |
| Ton | Kern mischt selbst (DSP/AX in C) | gleich; nur die Java-Ausgabe auf den alten Weg | klein |

## GLES-2-Weg der GX-Nachbildung (Stufe O2)

(Plan vom 21.10.2026; umgesetzt und mit den Messwerten der OUYA-GPU: Abschnitt „Stufe O2:
der GLES-2-Renderer“ unten.) Was auf der OUYA nicht 1:1 geht und wie es ersatzweise ginge:

- **TEV in Gleitkomma statt Ganzzahl.** fp20 im Fragment-Shader stellt ganze
  Zahlen nur bis 8192 genau dar. Werte (0..255, Register mit 11 bit) passen,
  die Zwischenwerte der TEV-Interpolation (bis etwa 65 000) nicht: Interpolation
  normiert rechnen, Rundung der Hardware nur ungefähr. Vergleichsstufen
  (`comp16`/`comp24`) nur näherungsweise.
- **Indirekte Texturen** (Wasser, Hitzeflimmern, Verzerrungen) rechnen in
  Festkomma bis 131 072: in fp20 nicht darstellbar. Erst abschalten oder grob
  nachbilden; direkte Texturen nehmen die Koordinate unverändert aus dem
  Vertex-Shader (dort ist `highp` erlaubt).
- **Tiefe 16 bit** statt 24 bit: Flimmern bei entfernten Flächen möglich
  (`GL_NV_depth_nonlinear` hilft).
- **Uniform-Blöcke → Uniform-Felder** (XF-Block 208 `vec4`; der Grenzwert des
  Tegra 3 ist am Gerät zu prüfen), Matrix-Indizes als `float`-Attribute,
  `BaseVertex` über verschobene Attribut-Zeiger.
- **Ohne MSAA, ohne Blit, ohne Fences:** Kantenglättung, Tiefenschärfe und
  Helligkeits-Pass erst aus oder als einfache Zusatzpässe.
- **Auflösung** auf 640×480 (Spielauflösung, `-Scale 1`) festlegen.

## Leistung: was bekannt ist

Retroid, 32 bit (Status-Datei, „M11 Android-Feinschliff“, „Elfter
Gerätelauf“, zweiter Gerätelauf des Kernprozesses):

- Kern je Spielbild 33,3 ms (30 Hz wie das Original), davon 12,8–19,3 ms
  Leerlauf, also **14–20 ms Rechenzeit**. Gut zwei Drittel davon liegen in der
  Plattformschicht, vor allem im GX-Decoder (120 000–148 000 Punkte je Bild
  auf dem Prozessor umgerechnet); die Spiellogik selbst nur 4–6 %.
- GPU 14–20 ms je Bild bei etwa 1080p mit MSAA.

Grobe Schätzung für die OUYA (nicht gemessen): Kern 60–120 ms je Spielbild,
also nur 8–15 statt 30 Spielbilder je Sekunde, solange der GX-Decoder so
arbeitet wie heute. Die drei freien Kerne des Tegra 3 helfen nur, wenn
Decoder oder Ton auf eigene Threads gehen. Die GPU-Last bei 640×480 ohne MSAA
ist ganz offen.

**Gemessen (21.10.2026, Kern-Probe O1):** r100 10,6 Bilder/s (Kern 94 ms je
Bild), Dorf r101 13,7 (72 ms); gut 60 % davon die GX-Umrechnung, die Spiellogik
etwa 13 % (Status-Datei, Abschnitt „OUYA: Kern-Probe O1“). Entscheidung des
Projektinhabers: **erst die Umrechnung schneller machen (A, gleiche Ausgabe Bit
für Bit), dann in einen eigenen Thread verlegen (B, wie Dolphins „Dual Core“)**.
A ist gebaut (Cloud: −37 % für Modell-Listen, −56 % für Werte, die die CPU
einzeln schickt), B ebenso (`RE4_GX_THREAD=1`, `port/whole/runtime/gx_thread.cpp`:
Modell-Listen kosten den Spiel-Thread dann nichts mehr); beides auf der OUYA
noch nicht gemessen (`ouya-probe.ps1` misst mit und ohne Thread).

**Stand danach (21.10.2026):** mit Grafik-Thread r100 etwa 22–24 Bilder/s, ohne
Bremse 28–32; der Grafik-Thread braucht auf dem Tegra 3 etwa 380 ns je Punkt,
begrenzt durch die Speicheranbindung. Entscheidung des Projektinhabers: **erst
ein Kompaktformat der Punkte** (`RE4_GX_COMPACT=1`: die Bytes, die GX las,
etwa 20 statt 44 Bytes je Punkt; Umrechnen auf der GPU), mit der Probe messen,
**danach O2**: der GLES-2-Renderer liest dieses Format.

**Ergebnis (21.10.2026, sechste Messung):** mit Kompaktformat r100 in Echtzeit
28,3 Bilder/s (Median der Bildzeit 33,6 ms, 96 % der Bilder rechtzeitig), das
Dorf 28,7, ohne Bremse 51 (mit Gleitkomma-Punkten 40). Spiel-Thread 21–23 ms,
Grafik-Thread 24–25 ms je Bild, auf zwei Kernen nebeneinander. **Stufe O1
abgeschlossen**; der Kern reicht für die OUYA. Weiter mit O2.

## Vorschlag: in Stufen, erst messen

| Stufe | Inhalt | Wo | Ergebnis |
|---|---|---|---|
| **O1 Kern-Probe** | Der Kernprozess ohne Bild auf der OUYA: NDK r23c, Kern für `android-16`, `/dev/ashmem` statt `memfd`, per ADB nach `/data/local/tmp`, Disc-Ordner auf `/sdcard` oder USB-Stick, Tastenfolge wie `RE4_PAD_SCRIPT`, ohne Warten auf den Bildschirm (`RE4_VI_FAST=1`); misst Spielbilder je Sekunde mit GX-Decoder und Ton | Cloud (Code) + Windows-PC mit OUYA (Messung) | Zahl, die entscheidet: ab etwa 25 Bildern/s weiter, unter 15 eher nicht |
| O2 GLES-2-Renderer | GLES-2-Weg der GX-Nachbildung wie oben, prüfbar in der Cloud mit llvmpipe (GLES 2 erzwingen) | Cloud oder lokal (22.10.2026: lokal) | Titel und r100 als Bild auf dem PC: **erreicht** (22.10.2026, gleich mit GLES 3 bis auf Rundung); auf der OUYA-GPU: **Bild richtig, aber langsam** (22.10.2026: Wald und Dorf 200–390 ms je Bild, leichte Räume 70–105 ms; Abschnitt „Ergebnisse auf der OUYA“); **Versuch beendet** (22.10.2026) |
| O3 OUYA-APK (nicht begonnen) | eigener Flavor (z. B. `ouya`: nur `armeabi-v7a`, `minSdk 16`, immer Kernprozess, eigene App-ID), Java-Versionsabfragen, Disc-Import ohne SAF, Tastenbelegung | Cloud + OUYA | erster Test auf dem Gerät |

Entscheidungen des Projektinhabers (21.10.2026): **erst die Kern-Probe O1**;
die OUYA bekommt später **eine eigene App** (die Haupt-App bleibt bei Android 6.0
und GLES 3); eine OUYA hängt per ADB am Windows-PC.

## Stufe O1: die Kern-Probe (21.10.2026, Cloud gebaut und geprüft, auf der OUYA gemessen)

**Was sie ist:** `port/whole/host/probe.cpp`, ein kleines Programm (`re4_probe`),
das den Spielkern wie der Kernprozess lädt und laufen lässt, aber ohne Bild. Es
übernimmt die Rolle der App: nimmt jedes GX-Paket des Kerns ab und kopiert es so,
wie der Kernprozess es in den gemeinsamen Speicher kopiert (`--no-copy`: nur
abnehmen), liest den Ton in Echtzeit mit 32 kHz und misst alle 5 s: Spielbilder je
Sekunde, Bildzeiten, Rechenzeit des Spiel-Threads je Bild, Leerlauf (Warten auf
das nächste Halbbild), Prozessor gesamt, GX-Menge und Kopierzeit, Ton, Speicher,
Takt jedes Prozessorkerns. Am Ende eine Zusammenfassung über die Zeit, in der Leon
frei war (ohne Zwischensequenz, ohne Menü), sonst über die Hauptschleife des
Raums: `probe-result: key=value ...`.

**Auf der OUYA:** `port/scripts/ouya-probe.ps1` (Windows, aus dem eigenen Klon
`E:\Development\Android\RE4-OUYA`, Entscheidung des Projektinhabers, 21.10.2026) baut in
WSL mit dem NDK r23c (`whole_setup_linux.sh --ouya`, `whole_build_android.sh --ouya
--work re4-ouya`: Quelltext und Build unter `~/re4-ouya`, getrennt von `~/re4-kern` der
App), legt Disc 1 als Ordner an (einmalig, `RE4-OUYA-Daten` neben dem Klon), kopiert alles
unter der ADB-Sperre nach `/data/local/tmp/re4-ouya` und misst drei Läufe mit der
Tastenfolge der PC-Prüfung: r100 in Echtzeit, r101 (Dorf) in Echtzeit, r100 ohne
Bremse (`RE4_VI_FAST=1`: so schnell der Kern kann). Bericht auf Deutsch in
`Desktop\RE4-OUYA-Probe\bericht.txt`, alles zusammen in `senden.zip`. Mit
`-Serial` läuft dieselbe Probe auch auf dem Retroid (Vergleich).

**Was Android 4.1 anders macht** (Quelltext des Linkers von Android 4.1.2,
`bionic/linker/linker.c`, und die Symbollisten von Bionic nachgelesen):

| Punkt | Android 4.1 | Lösung |
|---|---|---|
| Hash-Tabelle | nur `DT_HASH` (ohne stürzt die Suche mit Division durch 0 ab) | `-Wl,--hash-style=both` für API < 23 (`CMakeLists.txt`), `whole_build_android.sh --ouya` prüft jede Datei |
| Symbolsuche | erst die Bibliothek selbst, dann ihre direkten `DT_NEEDED`; `dlsym(handle)` nur in der Bibliothek selbst | Probe sucht in `libre4_main.so` und `libre4_platform.so`; Module brauchen beide direkt (geprüft); nur `re4_strstr`/`re4_strrchr` stehen doppelt in beiden (harmlos) |
| Bibliotheken je Prozess | höchstens 128 (`SO_MAX`) | Probe: etwa 10 + geladene Module; in einer App mit Zygote (60–80 Systembibliotheken) zu knapp: auf der OUYA immer Kernprozess |
| Suchpfad | `LD_LIBRARY_PATH`, dann `/system/lib`; schon geladene Bibliotheken nach Dateiname | Probe lädt `libc++_shared`, Plattform und Kern mit Pfad; das Skript setzt `LD_LIBRARY_PATH` |
| Adressen | Kernel 3.1 (vermutlich, die Adresskarte im Log zeigt es): positionsunabhängige Programme knapp unter `0x80000000` (2/3 des Adressraums), der Heap zufällig bis 32 MB dahinter, also womöglich in MEM1 | Probe startet sich einmal ohne Adress-Zufall neu (`ADDR_NO_RANDOMIZE`) und belegt alles unter `0x83000000` vorab (`host/low_memory.h`); die Adresskarte steht im Log |
| fehlende Funktionen (API 21) | `fegetenv`/`fesetenv` (ARM), `dl_iterate_phdr` (ARM), `__libc_current_sigrtmin` | `port/src/stack_context.cpp` sichert das FPSCR selbst (`vmrs`/`vmsr`), `os_thread.cpp` findet die Spielbibliothek über `dladdr` und ihre Programmköpfe, `watch.cpp` nimmt Signal 32 + 6 (nur Diagnose) |
| gemeinsamer Speicher | weder `memfd_create` (Kernel 3.17) noch `ASharedMemory` (API 26) | für O1 nicht nötig; für die OUYA-App (O3) `/dev/ashmem` |

Der Abgleich der Bibliotheken des armhf-Builds gegen die Bionic-Listen
(Android 11, mit API-Stufen) fand sonst nur glibc-eigene Namen (`__errno_location`,
`fopen64`, `backtrace`, …), die der Android-Build nicht benutzt. Fehlende
Funktionen im Spielcode würde `whole_stub_check.py --android` als neue Platzhalter
melden.

**Geprüft (Cloud):** `whole_build.sh i686 armhf` grün, dazu die Probe mit dem
Test-Einstieg des Kernprozess-Tests auf i686 und armhf (qemu): Bilder gezählt,
GX-Pakete abgenommen und kopiert, Ton gelesen, Zusammenfassung; die neuen
Android-4.1-Zweige mit `-D__ANDROID__ -D__ANDROID_API__=16` für ARM übersetzt
(FPSCR-Befehle im Objekt); `ouya-probe.ps1` mit PowerShell 7 auf Syntax geprüft
und mit einem nachgebauten `adb` (eine „OUYA“ neben einem „Retroid“) durchgespielt:
OUYA erkannt, Befehle, Bericht, `senden.zip`.

**Nicht geprüft (Stand des ersten Entwurfs):** der Build mit dem NDK r23c (in
der Cloud gesperrt: `dl.google.com`), Windows PowerShell 5.1, alles auf der OUYA.
Inzwischen auf dem Windows-PC gebaut und auf der OUYA gemessen (Status-Datei).

## Stufe O2: der GLES-2-Renderer (22.10.2026)

**Entscheidungen des Projektinhabers (22.10.2026):** der Code liegt in einem eigenen
Ordner `port/ouya` (dort später auch die OUYA-App); der GLES-3-Renderer der Haupt-App
wird **kopiert und für GLES 2 umgebaut** (die Haupt-App bleibt unberührt, Verbesserungen
dort werden bei Bedarf von Hand nachgezogen); die OUYA-App (O3) wird ein **eigenes,
schlankes Projekt** (nur ARM32, Android 4.1, Kern immer im eigenen Prozess, eigene App-ID,
Java-Teile der Haupt-App, wo sie zu Android 4.1 passen); gearbeitet wird **lokal** auf dem
Windows-PC (WSL mit Mesa, Disc-Daten, OUYA per ADB) statt in der Cloud.

**Was die OUYA-Grafik wirklich kann** (gemessen 22.10.2026 mit
`port/ouya/tools/gles2_info.cpp`, `port/scripts/ouya-gpu-info.ps1`; „NVIDIA Tegra 3“,
OpenGL ES 2.0 16.05001):

| Punkt | Tegra 3 | Folge für den Renderer |
|---|---|---|
| Vertex-Uniforms | 256 gemeldet, ein Feld bis 252 `vec4` (darüber „constant register limit“) | nur die Felder, die ein Shader liest; Lichter und Nachtransformations-Zeilen gepackt (höchstens etwa 180) |
| Varyings | 15 gemeldet, nur 9 `vec4` linken | Texturkoordinaten als `vec2`/`vec3`, nur die gelesenen |
| Attribute | 16 | nur die gelesenen (höchstens 17 im Gleitkomma-Punkt, praktisch unter 10) |
| Fragment-Genauigkeit | `mediump` fp20 (13 Bit): ganze Zahlen genau bis 2^13 + 1 ... 2^14; `highp` übersetzt, rechnet aber auch nur so; `lowp` Festkomma in 1/256 | TEV in Gleitkomma mit kleinen Zwischenwerten; Texturen und Sampler `mediump` (in `lowp` würden manche der 255 Stufen zur nächsten) |
| Texturkoordinaten | ab etwa 4 Wiederholungen einer 2048er-Textur jeder Texel falsch, auch unverändert durchgereicht | kein Unterschied zwischen „im Vertex-Shader skaliert“ und „im Fragment-Shader gerechnet“; weit wiederholte Texturen werden ungenau (zu prüfen am Bild) |
| Tiefe | nur 16 Bit (`DEPTH_COMPONENT16`, `16_NONLINEAR_NV`), keine Tiefen-Texturen, kein 24 Bit | EFB-Tiefe als Renderbuffer 16 Bit nichtlinear; Tiefenkopien über `NV_read_depth` (RE4 macht keine: das Pixelformat Z24 setzt es nie, `main_sub.cpp`) |
| Texturen ohne Zweierpotenz | nur mit `CLAMP_TO_EDGE`, Mipmaps erlaubt (`NV_texture_npot_2D_mipmap`) | Wiederholen/Spiegeln dort auf Klemmen (gezählt) |
| Mipmaps | nur vollständige Ketten (kein `TEXTURE_MAX_LEVEL`) | fehlende Stufen werden ergänzt (je 2×2 ein Original-Texel, siehe unten) |
| Shader-Länge | höchstens 256 Anweisungen je Shader („C6002: Instruction limit of 256 exceeded“; gemessen am Vertex-Shader mit fünf Spot-Lichtern und fünf projektiven Texturkoordinaten: 270) | Vertex-Shader sparsam (Bildausschnitt einmal je Zeichenauftrag gerechnet, eine inverse Wurzel je Licht, Nachtransformation als `dot` mit `vec4`); ist er trotzdem zu lang, ein zweiter Versuch ohne den Sonderfall q = 0 (gezählt als `leanShaders`) |
| Shader-Bau | etwa 120 ms je Programm (gemessen 22.10.2026) | ein schweres Bild braucht 27–46 Programme: erster Durchlauf 2–3,7 s |
| Sonstiges | `NV_shader_framebuffer_fetch`, `OES_vertex_array_object`, S3TC/DXT1, `EXT_texture_filter_anisotropic` (15), `NV_timer_query`; **keine** Programm-Binärdaten | Shader werden bei jedem Start neu übersetzt (Ruckler beim ersten Auftreten eines Effekts) |

**Aufbau** (`port/ouya/app/src/main/cpp/gx_gl2.cpp`, `gx_gl2_shader.cpp`; Schnittstelle wie
`re4gx::GxRenderer`, Namensraum `re4gx2`):

- **TEV in Gleitkomma:** jede Größe eine ganze Zahl im `float`; `x & 255` als
  `x - 256 floor(x/256)`, Verschieben als Multiplikation und `floor`, die Interpolation als
  `a + (b - a) c'/256` (kleine Zwischenwerte statt bis 65 280); Vergleiche über 16/24 Bit
  der Reihe nach (b, g, r) statt als große Zahl. Mit `highp` (Mesa) Bit für Bit wie der
  GLES-3-Renderer, mit fp20 (OUYA) ist die Rundung der Interpolation nur ungefähr.
- **Kompaktformat direkt:** GL liest die GX-Typen selbst (`GL_BYTE`/`SHORT`/...), der
  Vertex-Shader skaliert mit den Nachkommabits (`uDequant`), Matrix-Indizes als Bytes;
  Plätze, die der Punkt nicht trägt, aus `Re4GxDraw.matrices` (`uMtxDef`). Der
  Gleitkomma-Punkt geht genauso.
- **Uniforms je Programm:** GLES 2 hat keine gemeinsamen Blöcke; jede Änderung trägt einen
  Zeitstempel, ein Programm lädt beim Zeichnen nur, was sich seit seinem letzten Mal
  geändert hat (Matrix-Zeilen in Läufen).
- **Texturkoordinaten:** liest nur eine Textur eine Koordinate, skaliert der Vertex-Shader
  sie auf diese Textur (der Fragment-Shader liest ohne Rechnung); sonst und bei indirekten
  Stufen der Festkomma-Weg (1/128 Texel) des GLES-3-Renderers in Gleitkomma.
- **TEV gefaltet** (nach der ersten Messung auf der OUYA): der Generator kennt den
  Wertebereich jedes Zwischenergebnisses und lässt Abschneiden, Runden und Klemmen weg, wo
  sie nichts ändern; Farbregister und Konstanten kommen zusätzlich schon auf 8 Bit
  abgeschnitten (`uColorsW`, `uKonstW`). Spart auf der OUYA gut ein Fünftel der Zeit je Bild.
- **Bildkopien** lesen das EFB pixelgenau (`NEAREST`, wie die GameCube; linear würde mit den
  fp20-Koordinaten der OUYA benachbarte Pixel einmischen); ein kleines Programm je
  Umwandlung (Format, Halbierung, Faktor) und ein fester Framebuffer je Ziel-Textur.
- **Mipmaps:** wo die GPU `APPLE_texture_max_level` hat (Mesa), endet die Kette wie bei GX;
  sonst (Tegra 3) werden die fehlenden Stufen ergänzt, je 2×2 ein Original-Texel (ein
  Mittelwert machte ausgeschnittenes Laub in der Ferne halb durchsichtig, und der
  Alpha-Test dünnte es aus).
- **Weggelassen:** MSAA, der Autofokus (Tiefenschärfe) der Haupt-App (die Markierung BP 0x5A
  wird gezählt und übergangen), Uniform-Ring, GPU-Zeitmessung, Programm-Binärdaten.

**Prüfung auf dem PC** (`port/ouya/tools/check.sh`; WSL oder Cloud, Mesa auf GLES 2.0
gezwungen mit `MESA_GLES_VERSION_OVERRIDE=2.0`; der GLES-3-Renderer der Haupt-App läuft
in einem eigenen Prozess als Vergleich):

- `gx_gl2_check`: 22 künstliche Szenen (Grundformen, Seiten, Tiefe, Mischen, Alpha-Test,
  TEV-Rechnung mit allen Skalen und Vorzeichen, Licht mit Spot, Kopien und Paletten, Nebel,
  Kopierfilter, 64 zufällige TEV-Aufbauten ohne und mit indirekten Stufen, alle
  Texturformate mit Mipmaps und Wiederholarten, Texturkoordinaten-Erzeugung mit
  Projektion und Nachtransformation, Emboss, Matrizen je Punkt, Perspektive mit Nebel,
  alle Kopierformate, Tiefenkopien, Punkte und Linien, Ziel-Alpha und Logik); der
  GLES-2-Renderer bekommt die Punkte im Kompaktformat, zufällig auf alle Typen,
  Nachkommabits und Komponentenzahlen verteilt. Drei Läufe: wie er läuft; mit der
  Rundung des GLES-3-Renderers (1/128 Texel, Mesas `round()`: **Bild für Bild gleich**,
  außer einer Zelle mit einem Rundungs-Gleichstand x,5 an einer Texel-Ecke); wie auf dem
  Tegra 3 (16-Bit-Tiefe, keine Tiefen-Texturen, NPOT nur geklemmt, die Grenzwerte).
- `gx_gl2_compare`: echte Spielbilder (Mitschnitte des Kerns, `RE4_GX_DUMP`) mit beiden
  Renderern; die Autofokus-Markierung wird beiden genommen.
- Der Renderer übersetzt mit dem NDK r23c für Android 4.1 (`-Wunguarded-availability`).

**Ergebnisse (22.10.2026):**

| Prüfung | wie er läuft | mit der Rundung von GLES 3 | wie auf dem Tegra 3 |
|---|---|---|---|
| 22 künstliche Szenen | bestanden | gleich (eine Zelle: Gleichstand x,5) | bestanden |
| 24 Gleitkomma-Mitschnitte der PC-Prüfung (18.10.; Logos, Titel, r100) | ≤ 0,3 % der Pixel | 0 Pixel | ≤ 2,9 % |
| 30 Kompakt-Mitschnitte (`compare_frames.sh`, r100 und r101 je 6000 Bilder, bis 137 000 Dreiecke und 921 Zeichenaufträge) | ≤ 0,13 % | ≤ 0,06 % | ≤ 2,7 % |

(„Pixel“: mehr als 2 Stufen daneben.) Die Abweichungen „wie er läuft“ sitzen an Texel-Grenzen
(der GLES-3-Renderer rundet Koordinaten auf 1/128 Texel), die „wie auf dem Tegra 3“ in der
Ferne (ergänzte Mipmap-Stufen statt einer endenden Kette, 16-Bit-Tiefe); mit bloßem Auge
sind die Bilder nicht zu unterscheiden.

**Auf der OUYA:** `port/scripts/ouya-gx-render.ps1` (unter der ADB-Sperre) zeichnet die
Mitschnitte mit `port/ouya/tools/gx_gl2_render.cpp` auf der OUYA-GPU, misst die Zeit je Bild
(erster Lauf mit Texturen und Shader-Bau, dann fünfmal wie die folgenden Bilder des Spiels:
Zeit bis zur Übergabe, Zeit mit der GPU, Prozessorzeit des Prozesses samt Treiber) und
vergleicht die Bilder mit GLES 3. `-Probes` misst dieselben Bilder in Varianten
(`RE4_GX2_PROBE`, siehe `GxOptions` in `gx_gl2.h`), `-Only` wählt Bilder aus.

### Ergebnisse auf der OUYA (22.10.2026, 30 Kompakt-Mitschnitte r100 und r101)

**Das Bild stimmt:** alle 30 Bilder vollständig, ohne Shader-Fehler; gegen GLES 3 sind 18 unter
5 % der Pixel daneben, der Rest 5–20 % (der Wald: fp20-Rundung, 16-Bit-Tiefe, ergänzte
Mipmap-Stufen); nebeneinander mit bloßem Auge nicht zu unterscheiden.

**Die Zeit je Bild** (Median der 30 Bilder / langsamstes; volle Auflösung 640×528):

| Stand | volle Auflösung | ein Viertel der Pixel (`half`) |
|---|---|---|
| erster Lauf (Stand `81d2b5bf`) | 276 ms / 501 ms | – |
| TEV gefaltet, Kopien mit eigenen Programmen | 214 ms / 403 ms | 105 ms / 226 ms |
| Vertex-Shader gekürzt (Wald-Shader passt unter 256 Anweisungen) | 210 ms / 377 ms | 129 ms / 259 ms |

Leichte Bilder (Logos, Titel, kleine Räume mit 20 000 Dreiecken) brauchen 15–105 ms, Wald und
Dorf (90 000–137 000 Dreiecke, 450–920 Zeichenaufträge) 200–390 ms. Dasselbe Bild schwankt
zwischen den Läufen um bis zu ein Drittel (der Wald r100-003073: 270 ms in einem kurzen Lauf,
376–390 ms nach längerer Last; vermutlich Wärme und Taktsenkung, nicht geprüft); Unterschiede
unter etwa 15 % zwischen zwei Läufen sagen daher nichts. Gleich gemessen (dieselben zwei
Bilder nacheinander) brachte das Kürzen der Vertex-Shader beim Wald 408 → 270 ms.

**Wohin die Zeit geht** (Messvarianten, drei schwere Bilder im selben Lauf: Wald
r100-003073, r100-004123, Dorf r101-001201; ms je Bild):

| Variante | Wald | r100-004123 | Dorf |
|---|---|---|---|
| normal | 390 | 268 | 306 |
| Fragment-Shader nur Farbe (`flat`): ohne TEV | 143 | 60 | 150 |
| ohne EFB-Kopien (`nocopy`) | 358 | 166 | 288 |
| `flat` + `nocopy` | 138 | 38 | 145 |
| dazu Vertex-Shader ohne Licht und Texturkoordinaten (`simplevs`) | 78 | 31 | 65 |
| ohne die Zeichenaufrufe selbst (`nodraw`: Hochladen, Zustände, Uniforms, Kopien) | 21 | 13 | 24 |
| ein Viertel der Pixel (`half`) | 263 | 140 | 228 |
| `half` + `flat` | 152 | 41 | 144 |

Daraus: der **TEV im Fragment-Shader** kostet 50–80 % der Zeit (bei einem Viertel der Pixel
etwa die Hälfte davon); Licht und Texturkoordinaten im **Vertex-Shader** bis 80 ms; die
**Geometrie selbst** (Punkte holen, Dreiecke aufbauen) mit leeren Shadern noch 20–60 ms; alles
um die Zeichenaufrufe herum nur 13–24 ms; die Kopien meist 20–30 ms (r100-004123: 100 ms).
Die Prozessorzeit (Treiber eingerechnet) liegt bei 20–110 ms je schwerem Bild, das Übergeben
blockiert, bis die GPU nachkommt.

**Shader-Bau:** etwa 120 ms je Programm, ein schweres Bild braucht beim ersten Mal 27–46
Programme: 2–3,7 s (Programm-Binärdaten gibt es auf dem Tegra 3 nicht; nur ein eigener
Vorrat übersetzter Shader beim Start oder Bau im Hintergrund hilft).

**Einordnung:** für 30 Bilder/s hat ein Bild 33 ms. Schon ohne TEV, Licht und
Texturkoordinaten brauchen Wald und Dorf auf der OUYA 65–78 ms, also höchstens 13–15
Bilder/s; mit dem ganzen TEV 3–5 Bilder/s, in leichten Räumen 10–30. Ein schnellerer Weg
müsste beim TEV ungenauer werden (Näherung statt Nachbildung auf die Stufe genau), die
Auflösung senken und Geometrie sparen (das hieße: ins Spiel eingreifen, z. B. Sichtweite).
**Entscheidung des Projektinhabers (22.10.2026): der OUYA-Versuch endet hier**, die OUYA ist
eindeutig zu schwach; die Arbeit liegt im Branch `ouya-discarded`.

## Abgleich mit master (28.10.2026)

`master` (Stand `f1554a06`, 0.95.0-beta, 224 Commits seit dem OUYA-Branch) in `ouya-discarded`
zusammengeführt. **Entscheidung des Projektinhabers (28.10.2026): master ist die Basis.**

- **Kern (GX-Umrechnung):** beide Seiten hatten sie unabhängig umgebaut. Übernommen ist
  master's Fassung: Punkte in Blöcken, Hilfs-Threads für die Punkte (bis zu 3,
  `RE4_GX_THREADS`, Standard: Prozessorkerne minus 3, auf der OUYA also 1), dazu alles Neue
  (Owner-Befehl für 60 FPS, Tiefenkarte für `GXPeekZ` …). **Der eigene Grafik-Thread der OUYA
  (`gx_thread.cpp`, `RE4_GX_THREAD=1`) entfällt**; ob master's Hilfs-Threads auf der OUYA
  genauso viel bringen, ist noch nicht gemessen.
- **Neu auf master's Umrechnung aufgesetzt:** das Kompaktformat der Punkte
  (`RE4_GX_COMPACT=1`; sein Befehl hat jetzt die Nummer 9, master belegt 8 mit `OWNER`) und
  die Textur-Stichprobe (`RE4_GX_TEXTURE_CHECK=sampled`), je mit ihrem Selbsttest aus dem
  OUYA-Branch (Kompaktformat zurückgerechnet = Gleitkomma-Punkte, Bit für Bit; Stichprobe
  erkennt geänderte Texturen).
- **Bleibt aus dem OUYA-Branch:** die Bau-Anpassungen für Android 4.1 (`ai.cpp`,
  `stack_context.cpp`, `watch.cpp`, Prelude, CMake mit `re4_probe` und `--hash-style=both`,
  `whole_build_android.sh --ouya`, `ndk23_check.sh`), die Kern-Probe `host/probe.cpp`
  (meldet ohne Grafik-Thread keine eigene Grafik-Zeit mehr), der GLES-2-Renderer `port/ouya`
  und die Skripte. `ouya-probe.ps1`: die „-gp“-Läufe heißen weiter so, schalten aber nur
  noch Kompaktformat und Textur-Stichprobe ein.
- **Geprüft (PC, WSL):** Kern i686 `-O2` mit `whole_build.sh`: gebaut ohne Fehler, alle
  Prüfungen bestanden bis auf `runtime-test deathlog`, der auf master genauso scheitert (die
  Raum-Logzeile hat dort `game_mode`/`game_country` dazubekommen, die Testerwartung nicht);
  die GX-Prüfsumme und die beiden neuen Selbsttests bestehen mit 3 Hilfs-Threads und ohne.
  `port/ouya/tools/check.sh`: 22 künstliche Szenen in allen drei Läufen bestanden, der
  Renderer übersetzt für Android 4.1. Kern für Android 4.1 (`whole_build_android.sh
  --ouya`): gebaut ohne Fehler (114 Module, Stubs wie erwartet), nachdem zwei Stellen von master angepasst sind, die es erst ab Android 5.0 gibt: das Modul-Fenster über `android_dlopen_ext` (ohne Fenster legt der Linker die Module, wie bei O1) und die Bereiche der Spielbibliotheken über `dl_iterate_phdr` (über `dladdr`, wie im OUYA-Branch schon einmal); keine Systemfunktion fehlt unter Android 4.1 (alle 119 Dateien gegen die Bibliotheken von API 16 geprüft). **Auf der OUYA selbst noch nicht gelaufen.**
- **Vor dem nächsten Gerätelauf nötig:** die Spielbild-Mitschnitte neu machen
  (`port/ouya/tools/compare_frames.sh --data …`): die alten tragen das Kompaktformat unter
  Nummer 8, die jetzt `OWNER` heißt; dann `ouya-probe.ps1` (Kern auf der OUYA, mit master's
  Hilfs-Threads) und `ouya-gx-render.ps1` (Grafik).

## Leichte OUYA-Fassung (07.11.2026)

**Wunsch des Projektinhabers (07.11.2026):** die OUYA-Fassung bekommt eine kürzere Sicht mit
näherem Nebel, die vereinfachte Farbberechnung und eine kleinere Auflösung, „ohne dass das
Spiel Pixelbrei wird“. Vorher `master` (0.99.1-rc, `33f87637`, 256 Commits) in
`ouya-discarded` geholt: ein Konflikt (`ai.cpp`: master's lernender Tonleser mit der
pthread-Sperre der OUYA); die OUYA-Grafik übernimmt die Pixelmitte bei 7/12 des
GLES-3-Renderers (seit 29.10.2026; in die vorberechneten Bildausschnitt-Werte eingerechnet,
keine Anweisung mehr im Shader).

**Die drei Schalter** (alle aus = wie bisher):

| Schalter | Wo | Was |
|---|---|---|
| `RE4_VIEW_DISTANCE=f` | Kern: Patch 0900, `runtime/view_distance.cpp` | Das Spiel koppelt seine Sichtgrenze schon an den Nebel (`cLightMgr::setFog`, `hokanMove`: hinter dem Nebelende wird nichts gezeichnet). Der Patch gibt der GPU Nebelbeginn und -ende mal f (das Ende nicht unter 6 m) und setzt die Sichtgrenze auf dieses Ende; der Nebelwert des Raums selbst bleibt (die Überblendung zwischen zwei Lichtschnitten liest ihn jedes Bild). Räume ohne Nebel (meist innen) bleiben unverändert. |
| `RE4_GX2_FAST_TEV=1` | OUYA-Grafik (`gx_gl2_shader.cpp`, `ShaderKey::fastTev`) | Die Farbstufen (TEV) in Bruchteilen 0..1 statt in ganzen Stufen 0..255: ohne Runden, Abschneiden auf 8 Bit und Überlauf; die Mischung als `mix()`. Vergleichsstufen und Alpha-Test weiter auf den 255 Stufen. |
| `RE4_GX2_SCALE=x` | OUYA-Grafik (`GxOptions::resolutionEighths`) | Das EFB in Achteln der GameCube-Auflösung (0,75: 480×396); Kopien und das Bild für den Schirm folgen. |

**Geprüft am PC** (WSL; Mitschnitte r100 und r101 wie `compare_frames.sh`, je 15 Bilder, mit
f = 1 und 0,5 an denselben Stellen):

- Kern i686 `-O2` und für Android 4.1 gebaut, alle Prüfungen von `whole_build.sh` bestanden,
  keine Systemfunktion fehlt unter API 16 (119 Dateien geprüft).
- `check.sh`: 22 künstliche Szenen in allen drei Läufen bestanden (die Szene „texture formats“
  mit etwas mehr Spielraum: durch die Pixelmitte bei 7/12 fallen im Lauf „wie er läuft“ mehr
  Abtastpunkte an Texelkanten, 833 Pixel; mit der Rundung von GLES 3: 0).
- **Sichtweite 0,5:** Nebelende im Dorf 175 → 87 m, im Wald 107 → 53 m. Gezeichnet wird in den
  schweren Bildern ungefähr die Hälfte: Wald (r100-003074) 137 000 → 70 000 Dreiecke, 524 → 357
  Zeichenaufträge; Dorf (r101-001201) 124 000 → 66 000 Dreiecke, 921 → 499 Aufträge; leichte
  Bilder und Innenräume unverändert. Am Bild: hinten neblig statt abgeschnitten.
- **Vereinfachte Farben:** auf den Spielbildern im Mittel unter 1 von 255 Stufen anders, kein
  Pixel mehr als 8 Stufen (künstliche Szenen: einfache Stufen höchstens 3; nur die
  Zufalls-TEV-Szenen mit absichtlichen Überläufen weichen stark ab).
- **Auflösung** (`gx_gl2_render` mit `RE4_GX2_PRESENT=1440x1080`: so hochgerechnet, wie ein
  1080p-Fernseher das 4:3-Bild zeigt; 1:1-Ausschnitte verglichen): **0,75× gut** (etwas
  weicher, Munitionszahl und Namensfeld gut lesbar); 0,625× Grenzfall (Treppenkanten an
  Leons Hand, Ziffern unsauber); **0,5× Pixelbrei** (die Munitionszahl nicht mehr lesbar).
  Bilder: Desktop `RE4-OUYA-Probe\leicht-bilder` (`sicht-*`, `farbe-*`, `aufl-*`).

**Auf der OUYA** (`ouya-gx-render.ps1`, neue Varianten `fasttev`, `scaleX`):

| Fassung | vier schwerste Bilder (Wald, Dorf), Median / langsamstes | alle 30 Bilder, Median / langsamstes |
|---|---|---|
| wie bisher | 356 / 408 ms | 210 / 377 ms (Lauf vom 22.10.) |
| nur Sichtweite 0,5 | 322 / 352 ms | – |
| Farben vereinfacht + 0,75×, volle Sicht | 225 / 269 ms | – |
| **Sicht 0,5 + Farben vereinfacht + 0,75×** | **123 / 197 ms** | **89 / 124 ms (etwa 11 Bilder/s)** |
| dasselbe mit 0,625× | 105 / 113 ms | 90 / 175 ms |

Die Werte schwanken auf der OUYA stark (dasselbe Bild 54 oder 90 ms in zwei Läufen; nach
langer Last offenbar Wärme und Taktsenkung); Unterschiede unter etwa einem Viertel sagen
nichts. **0,625× bringt gegenüber 0,75× kaum etwas** und sieht schlechter aus: die Pixel sind
nicht mehr der Engpass. Was bleibt (Messvarianten, Dorfbild r101-001201 mit der leichten
Fassung): Farbberechnung je Pixel etwa 50 ms, Licht und Texturkoordinaten je Eckpunkt etwa
55 ms, Kopien und der Rest etwa 35 ms; ohne die Zeichenaufrufe 14 ms.

**Empfehlung:** Sicht 0,5, vereinfachte Farben und 0,75× als Grundeinstellung der leichten
Fassung. Für mehr als etwa 11 Bilder/s (30 Bilder/s = 33 ms je Bild) müssten noch das Licht je
Eckpunkt (weniger Lichter oder einfachere Abschwächung) und die Kopien (der Potato-Modus von
master lässt die Bildverzerrungen weg, die das Bild kopieren) billiger werden; dazu „nur jedes
zweite Bild zeigen“ in einer späteren OUYA-App (dann reichen 66 ms je Bild für 15 Bilder/s).
Noch nicht gemessen: die Kern-Probe (`ouya-probe.ps1`) mit master's Hilfs-Threads statt des
eigenen Grafik-Threads.

### Potato-Modus, weniger Licht, 16-Bit-Farben (07.11.2026, zweiter Teil)

**Wünsche des Projektinhabers (07.11.2026):** auf der OUYA der Potato-Modus von master mit
beiden Schaltern (Wetter- und Spezialeffekte aus), das Licht je Eckpunkt vereinfachen, und die
Frage, ob weniger Farben (16 Bit) schneller wären.

- **Potato-Modus** (`RE4_POTATO=3`, Kern, unverändert von master): lässt in Wald und Dorf viele
  Effektfiguren weg, vor allem die großen halbdurchsichtigen Nebel- und Staubschleier über dem
  Bild (Dorf r101-001201: 499 → 257 Zeichenaufträge, Dreiecke fast gleich); die 8–9 Bildkopien
  je Bild bleiben (sie gehören zum Spiel selbst). Bilder: `potato-*`.
- **Höchstens 2 Lichter je Eckpunkt** (`RE4_GX2_LIGHTS=2`, OUYA-Grafik): gemessen an den
  Mitschnitten rechnen beleuchtete Eckpunkte meist 2 Lichter (40–100 %), sonst 3, selten 4–7;
  die Winkel-Abschwächung wird fast immer gebraucht; die zwei stärksten Lichter geben am Objekt
  92–100 % des Lichts. Deshalb wählt die Grafik je Zeichenauftrag die zwei stärksten (an seinem
  Ursprung) und gibt die übrigen als gleichmäßigen Anteil dazu (ihr Licht am Ursprung, halb, wo
  die Richtung zählt): nichts verschwindet, nichts springt. Bildvergleich gegen alle Lichter: im
  Mittel höchstens 1,6 von 255 Stufen, höchstens 7,5 % der Pixel mehr als 8 Stufen; mit nur
  einem Licht bis 69 % der Pixel (sichtbar heller und flacher). Bilder: `licht*`.
- **16 Bit:** Texturen in 16 Bit (`RE4_GX2_TEX16=1`: RGB565 ohne Alpha, RGBA5551 für
  Ausschnitte, weiches Alpha bleibt 32 Bit) sehen gleich aus (im Mittel unter 1 von 255
  Stufen), bringen auf der OUYA aber **nichts Messbares** (schnellste Läufe 82 gegen 82 ms): die
  OUYA hängt am Rechnen, nicht an der Datenmenge. Das Bild selbst in 16 Bit
  (`RE4_GX2_EFB16=1`, RGB565) geht nicht: RE4 legt jedes Bild durch einen Farbfilter, der den
  Alpha-Kanal des Bildes liest; ohne ihn wird alles orange und zu hell (Bild `efb16-*`). Beide
  Schalter bleiben als Messvarianten, nicht in der Voreinstellung.
- **Messmethode:** nach Stunden Last schwankt die OUYA stark (dasselbe Bild 41 oder 105 ms;
  nacheinander gemessene Varianten wurden langsamer, weil sie sich erwärmt). Jetzt
  `ouya-gx-render.ps1 -Interleave` (jedes Bild in allen Varianten direkt nacheinander) und
  `gx_gl2_render` meldet zusätzlich den schnellsten Lauf je Bild; verglichen wird der Median
  der schnellsten Läufe.

**Messung** (12 schwere Bilder aus r100 und r101, Sicht 0,5, vereinfachte Farben, 0,75×;
abwechselnd, schnellste Läufe):

| | alle Lichter | 2 Lichter |
|---|---|---|
| ohne Potato | 98 ms | 85 ms |
| mit Potato (beide Schalter) | 81 ms | 80 ms |

Potato und weniger Licht bringen je 13–17 %, zusammen etwa 18 %; die Zeiten im Median aller
Läufe (mit den warmen) liegen um 115–130 ms.

**Voreinstellung der leichten OUYA-Fassung (Stand 07.11.2026):** Kern `RE4_POTATO=3`,
`RE4_VIEW_DISTANCE=0.5` (dazu `RE4_GX_COMPACT=1`, `RE4_GX_TEXTURE_CHECK=sampled`; so in
`ouya-probe.ps1`, Läufe „-gp“); Grafik `RE4_GX2_FAST_TEV=1`, `RE4_GX2_SCALE=0.75`,
`RE4_GX2_LIGHTS=2`. Damit in Wald und Dorf etwa 80–120 ms je Bild, also 8–12 Bilder/s; kleine
Räume 40–65 ms.

## Stufe O3: die spielbare OUYA-App (07.11.2026)

**Wunsch des Projektinhabers (07.11.2026):** eine Fassung, die er selbst auf der OUYA spielen kann,
zuerst mit der Debug-Disc (G4BE08).

- **Die App:** `port/ouya/app` (Gradle, Paket `com.ast.cubeevil4.ouya`, Android 4.1 = API 16,
  nur `armeabi-v7a`, NDK 23.2 für den App-Teil). Der Spielkern läuft wie in der Haupt-App in einem
  eigenen Prozess (`libre4_coreproc.so`, Verbindung `core_ipc`); die App zeigt sein Bild mit dem
  GLES-2-Renderer aus O2, spielt den Ton über OpenSL ES (32 kHz) und gibt den OUYA-Controller als
  GameCube-Controller weiter (O = A, A = B, U = X, Y = Y, R1 = Z, L2/R2 = L/R, OUYA-Knopf = START).
  Disc 1 liegt auf `/sdcard/RE4-OUYA/disc0`, Speicherkarte und `core.log` daneben. Das Bild füllt
  den Schirm wie in der Haupt-App (Zeilen 56..391 des Spiels als 16:9).
- **Bauen und aufspielen:** `port/scripts/ouya-app.ps1` (Kern in WSL, App mit Gradle, unter der
  ADB-Sperre installieren, Disc kopieren, starten, Protokoll holen; `-SkipCore`, `-SkipDisc`,
  `-Seconds`). Ergebnis in `Desktop\RE4-OUYA-Probe\app` (`bericht.txt`, `logcat.txt`, `core.log`).
- **Hürden von Android 4.1** (alle behoben): Bibliotheken der App werden nicht von selbst geladen
  (`c++_shared` zuerst); `dladdr` nennt keinen Ordner (Bibliotheksordner aus Java); `dlsym` sucht
  nur in der genannten Bibliothek (Kernprozess fragt auch `libre4_platform.so`); der Kernel vergibt
  Adressen von unten (Kernprozess startet sich ohne Adress-Zufall neu und hält die Bereiche unter
  0x83000000 frei, wie die Probe O1); kein `memfd` (`/dev/ashmem`); `PR_SET_PDEATHSIG` gilt je
  Thread (der Start-Thread bleibt bestehen); der Kernel bricht bei schief liegenden
  Mehrfach-Ladebefehlen ab (Compiler-Schalter `-arm-assume-misaligned-load-store`).
- **Compiler (Fund vom 07.11.2026):** der Compiler des NDK r23c (clang 12) kappt Kommazahlen beim
  Umwandeln in kleine Ganzzahlen (u8, s8, u16, s16) **nicht**, auch nicht mit
  `-fno-strict-float-cast-overflow`: 300 wird 44 statt 255. Der GameCube und der Compiler der
  Haupt-App (NDK r28c, clang 19) kappen. Folge auf der OUYA: das Titel-Logo blendete plötzlich aus,
  das Spielmenü verlor seinen Hintergrund. Seitdem baut `whole_build_android.sh --ouya` den Kern mit
  dem Compiler des NDK r28c, aber den Bauvorlagen und Bibliotheken des r23c (Android 4.1,
  libc++ 12); zusammengestellt einmal in `~/.cache/re4-ndk-ouya`. Geprüft am Maschinencode
  (`idSysMove02` vergleicht mit 255); auf der OUYA vom Projektinhaber zu bestätigen.
- **Bildfluss:** ein eigener Thread der App holt jedes fertige Bildpaket sofort aus dem
  gemeinsamen Speicher (der Kernprozess hat nur drei Plätze und wartete sonst auf den Renderer);
  von mehreren seit dem letzten Bildschirmbild gezeichneten Paketen wird nur das neueste gezeichnet,
  die älteren laufen ohne Zeichnen durch (`Skip`). Der Renderer behält höchstens 64 MB entpackte
  Texturen (`RE4_GX_TEXTURE_BUDGET_MB`; ein Film bringt 1,4 MB je Bild, die 256 MB der Haupt-App
  füllten den Speicher der OUYA).
- **App-Menü auf L3** (Wunsch des Projektinhabers, 07.11.2026: Inhalt Grafik und Komfort, im
  Aussehen der Haupt-App): Auflösung (62/75/100 %), Sichtweite (50/75/100 %, wirkt ab der nächsten
  Tür oder Szene; neue Kern-Nachricht `kSetViewDistance`), Farbberechnung (vereinfacht/genau),
  Potato-Schalter, automatisch zielen, immer rennen, weite Kamera. Das Spiel hält an, solange es
  offen ist. Texte aus dem Menü der Haupt-App mit allen Übersetzungen
  (`port/ouya/tools/menu_strings.py`), die fünf neuen auf Englisch und Deutsch.

**Erster Stand auf der OUYA (07.11.2026, Debug-Disc):** startet, Logos, Titel und Film laufen,
Spiel bis r100 ohne Absturz. Titel und Logos 30 Bilder/s. In r100 schafft das Spiel nur etwa 15
Bilder/s (der Kern rechnet 52 ms je Bild, davon 37 ms die Umrechnung von etwa 120.000 Eckpunkten),
der Bildschirm zeigt 8–9 Bilder/s. Beim Betreten eines Raums ein Stocken von bis zu 20 s
(vermutlich das Übersetzen der Shader, je etwa 120 ms). Der Film ruckelte stark (Ursache offen,
Texturspeicher als erster Verdacht).

**Filme und Shader (07.11.2026, gemessen mit dem neuen Protokoll der App: Zeit des Renderers je
Bildschirmbild, gebaute Shader, Auspacken und Hochladen der Texturen; `ouya-log.ps1` holt es):**

- **Die App lief unoptimiert:** Gradle baut die Debug-Fassung, deren C++-Teil ohne Optimierung
  (`-O0`); die Messungen mit `ouya-gx-render.ps1` waren optimiert. Ein Filmbild auszupacken kostete
  so etwa 23 ms je Textur, der Film lief mit 1 Bild/s. Jetzt `-O2` auch in der Debug-Fassung
  (`CMakeLists.txt`): dieselben 44 Texturen 60 statt 1006 ms.
- **Ausgelassene Bilder packen nichts mehr aus:** Texturen eines Bildpakets, das der Frameskip nur
  durchlaufen lässt, bleiben roh liegen und werden erst ausgepackt, wenn ein gezeigtes Bild sie
  braucht (bei Filmen nie).
- **Shader:** einer kostet die OUYA 150–300 ms. Beim Betreten eines Raums entstanden viele, die sich
  nur in Dingen unterschieden, die kein Schritt liest (Farbtausch-Tabellen, Konstanten-Auswahl) oder
  nur in der Nummer der Lichter: diese Teile sind jetzt aus dem Schlüssel (am PC geprüft: 30 echte
  Bilder unverändert, mit 2 Lichtern dieselben Pixel wie vorher, 26 statt 28 Shader). Jeder neue
  Shader schreibt ins Protokoll, worin sich sein Schlüssel vom nächsten unterscheidet.
- **Ergebnis im Eröffnungsfilm:** etwa 32 Bildschirmbilder/s, der Film in Echtzeit (161 Bilder in
  5,35 s, keines ausgelassen). Der Ton läuft im Film fast ohne Lücken; in r100 läuft er trocken,
  weil das Spiel dort langsamer als Echtzeit rechnet (Kern 52 ms je Bild).

**Spieltempo: die GX-Umrechnung wieder auf einem eigenen Thread (07.11.2026, Wunsch des
Projektinhabers: r100 schneller; die Autoszene lief dem Ton davon).** Die Kern-Probe (ohne Bild,
`ouya-probe.ps1 -Runs r100-schnell-gp`, neu: `-RemoteData /sdcard/RE4-OUYA` nimmt die Disc der App,
`-Env` weitere Schalter) maß den Spiel-Thread in r100 mit 41,6 ms je Bild (24 Bilder/s ohne Bremse;
am 21.10. mit dem eigenen Grafik-Thread 51): 40,6 % die Umrechnung der GX-Befehle, dazu ein Teil der
10,8 % Speicherkopien, 19,4 % Spiellogik, 3 % die Zustandsbrücke. Seit dem Abgleich mit master
(28.10.) lief die Umrechnung auf dem Spiel-Thread, nur die Punkte gingen an einen Helfer.

- **Grafik-Thread** (`runtime/gx_thread.cpp`, `RE4_GX_THREAD=1`, in der App an): der Bau vom
  21.10. auf master's Umrechnung neu aufgesetzt. Die Bytes des FIFO gehen über einen Ring an den
  Thread `re4-gp`, die Aufrufe des Ports dazwischen (VI, Markierungen, Listen, `re4_gx_still`)
  als Ereignisse an ihrer Stelle; die Punkt-Helfer arbeiten nun dem Grafik-Thread zu. Der
  Spiel-Thread holt ihn ein, bevor er ein Display-List-Ziel überschreibt, und bei jedem
  Bildwechsel; wartet ein Spiel-Thread, bekommt er, was die Grafik schon fertig hat, und ganz
  gewartet wird erst, wenn das Spiel sonst nichts zu tun hat (wie Prozessor und Grafikchip des
  GameCube). Ohne den Schalter bleibt alles wie in master.
- **Zustandsbrücke ohne Figuren** (`RE4_BRIDGE_MODELS=0`): die OUYA-App liest keine Schnappschüsse.
- **Geprüft (PC, WSL, i686 `-O2`):** `whole_build.sh` ohne und mit `RE4_GX_THREAD=1` vollständig
  bestanden (Fingerabdruck der Umrechnung, Kerntest, Kern im eigenen Prozess, Formatvergleich,
  Film); `whole_build.sh` prüft Laufzeit- und Kerntest jetzt immer auch mit dem Grafik-Thread. Die
  Tests nehmen Bildpakete erst ab, wenn der Thread aufgeholt hat.
- **Gemessen auf der OUYA (Probe, r100 ohne Bremse, Leon frei):** mit dem Grafik-Thread (noch mit
  dem Aufholen bei jedem Warten) 37,4 statt 24,0 Bilder/s; Spiel-Thread 18,7 ms je Bild, Grafik-
  Thread 16,5 ms daneben.

**Für später (Wünsche des Projektinhabers, 07.11.2026, „wenn alles läuft“):**

1. Die OUYA-App bleibt eine eigene APK, unabhängig von der Haupt-App, und bekommt einen eigenen
   Installer für Disc 1 und 2: Debug-Disc, dazu die Verkaufs-Discs US, EU und JP.
2. Alle Spiel-Fixes der Haupt-App auch in der OUYA-Fassung. Die Fixes im Spielkern kommen mit jedem
   Abgleich mit master; die der Haupt-App außerhalb des Kerns (GLES-3-Renderer, Java-Teil) müssen
   für den OUYA-Renderer und die OUYA-App einzeln nachgezogen werden.
3. Der Installer fragt auf der OUYA, ob die Spieldaten in den internen Speicher oder auf einen
   USB-Speicher kommen.

**Spieltempo 30 Bilder/s (07.11.2026, Wunsch des Projektinhabers: „stabile 30, egal was man
runterschrauben müsste“).** Mit dem Grafik-Thread kam das Spiel in der Autoszene und in r100 auf 27–28
Bilder/s, die Szene lief dem Ton um ein paar Sekunden davon; alle vier Kerne waren ausgelastet
(Spiel, Grafik-Thread, Punkt-Helfer, Renderer, Kopien). Schritte:

- **Keine zweite Kopie:** 8 statt 3 Plätze im gemeinsamen Speicher (`re4_core_ipc.h`), 10 statt 4
  Bildpuffer im Kern; die App lässt die Bildpakete in ihren Plätzen, bis der Renderer fertig ist (bisher
  kopierte sie jedes, in r100 2,5 MB). Der Test des Kerns im eigenen Prozess holt jetzt jedes fertige
  Paket ab (mit 8 Plätzen kam die Tiefenkarte sonst zu spät).
- **Punkte vorab angefordert** (`convertCompact`: die Elemente 6 Punkte voraus, `__builtin_prefetch`;
  Ergebnis unverändert), dazu probeweise zwei Punkt-Helfer (`RE4_GX_THREADS=2` über `settings.txt`):
  28–29 Bilder/s.
- **Frameskip im Kern** (`RE4_GX_CHUNK_DRAWLESS`, `re4ipc_set_drawless`): der Renderer zeigt in r100
  nur jedes dritte bis vierte Spielbild, der Kern rechnete aber jedes um (30–50 ms Rechenzeit je Bild).
  Jetzt meldet der Renderer aus seiner Zeit je gezeigtem Bild, das wievielte er braucht; die übrigen
  Bildpakete tragen Zustand, Texturen und Bildkopien, aber keine Zeichenbefehle und Punkte, der
  Renderer lässt sie nur durchlaufen. Nie bei offenem Unterbildschirm des Spiels, 0,8 s aus nach START,
  Y, X, Z und A ohne Zielen (Tasten, die einen Unterbildschirm öffnen können: sein Hintergrund ist eine
  Bildkopie), 3 s aus, wenn ein späteres Bild eine Kopie eines ausgelassenen liest (Selbstkontrolle
  `CopiesReadLater`, im Protokoll „copy checks failed“). PC: Laufzeittest für Bilder ohne Punkte.
- **Ergebnis auf der OUYA** (Autoszene und r100, 50 % Sicht): **29–30 Spielbilder/s** (meist 300 in
  10 s), der Kern hat je Bild noch etwa 7 ms Luft; der Bildschirm zeigt 9–14 Bilder/s; die
  Selbstkontrolle schlug in gut 4 Minuten zweimal an (welcher Effekt, noch offen).
- **Cheats im OUYA-Menü** (Wunsch des Projektinhabers, 07.11.2026: die grundlegenden und die Waffen,
  keine Spielfiguren und Extra-Spiele): wie die Cheat-Seite der Haupt-App, über den Spielkern
  (`runtime/cheats.cpp`, `re4ipc_set_cheats`, `re4ipc_cheat_action`): unendlich Leben, unendlich
  Munition, Unsichtbarkeit, One-Hit-Kill, automatische QTEs (Schalter, gemerkt, beim Start über
  `RE4_CHEATS`), Koffer L, +1.000.000 Ptas und die sechs Waffen der Haupt-App (Knöpfe, mit Antwort
  darunter). Texte aus der Haupt-App mit allen Übersetzungen.
- **Disc-Installer** (Wunsch des Projektinhabers, 07.11.2026, Punkte 1 und 3 der Liste oben;
  `OuyaInstaller.java`): erscheint beim Start, wenn noch keine Discs eingerichtet sind, oder über
  „Andere Discs importieren“ im Menü auf L3 (die App startet dann neu; „Zurück zum Spiel“ nimmt die
  bisherigen). Sucht Disc-Abbilder (ISO, GCM, RVZ) im internen Speicher und auf USB-Speichern (bis zwei
  Ordner tief), zeigt Disc und Fassung (Debug, US, EU, JP), fragt nach dem Ziel (intern oder ein
  USB-Speicher, mit freiem Platz) und richtet dort `RE4-OUYA/disc1` und `disc2` ein, wie die Haupt-App
  (Prüfung gegen die Code-Liste der Fassung, beide Discs derselben Fassung). Lesen, Prüfen und Entpacken
  mit den Klassen der Haupt-App (`com.ast.re4decomp.disc`, kopiert von
  `port/ouya/tools/sync_disc_import.py`, ohne das erst ab Android 4.4 vorhandene `StandardCharsets`),
  RVZ über deren C-Teil (zstd) als `libre4rvz.so`. Mit den EU-Discs bekommt das Spiel die Sprache der
  OUYA (`RE4_GAME_LANGUAGE`). Gebaut und installiert; der Lauf des Installers auf der OUYA ist noch nicht
  getestet (intern ist dort zu wenig Platz frei: Abbild und Dateien brauchen knapp 3 GB).
