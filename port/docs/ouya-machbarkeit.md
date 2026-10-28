# OUYA: Machbarkeit eines Test-Builds

**Beendet am 22.10.2026 (Entscheidung des Projektinhabers):** die OUYA ist zu schwach. Der
Kern lief schnell genug (O1), der GLES-2-Renderer zeichnet richtig (O2), aber Wald und Dorf
brauchen auf der OUYA-GPU 200–390 ms je Bild, und selbst ohne jede Farb- und Lichtrechnung
noch 65–78 ms (Abschnitt „Ergebnisse auf der OUYA“). Die ganze OUYA-Arbeit liegt im Branch
`ouya-discarded` (Stand des Branches `claude/adoring-mendel-vclbu8`); sie wird nicht
weitergeführt und nicht in die Haupt-App übernommen.

**Wieder aufgenommen am 28.10.2026 (Wunsch des Projektinhabers):** `ouya-discarded` mit
`master` abgeglichen, um auf dem neuen Stand weiterzutesten (Abschnitt „Abgleich mit master“
am Ende). Nichts davon geht in die Haupt-App.

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
