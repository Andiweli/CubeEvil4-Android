# 32 bit: der Spielkern als eigener Prozess – Übergabe (20.10.2026)

**Entscheidung des Projektinhabers (20.10.2026): Weg A** – der Spielkern läuft auf
32-bit-Geräten in einem eigenen Prozess ohne Java (ART). Diese Datei ist die Übergabe an die
Sitzung, die das umsetzt. Stand des Codes: Branch `claude/re4-start-controls` (Alpha 4,
v0.38.3, versionCode 59).

## Das Problem

Der Kern braucht MEM1 fest bei `0x80000000..0x83000000` (48 MB, GameCube-Hauptspeicher;
Spielzeiger sind diese Adressen). Die App lädt den Kern heute **im eigenen Prozess**
(`port/android-decomp/app/src/main/cpp/core_mode.cpp`, `loadCore`: Platzhalter mit
`MAP_FIXED_NOREPLACE` vor `dlopen`, `RE4_MEM1_PLACEHOLDER=1`, `re4_mem1_init` mappt darüber).

Tester-Bericht `cubeevil4-report-20261020-100743.zip` (App 0.38.2): Mobicel P11 Plus,
Android 14, **nur 32 bit** (`armeabi-v7a armeabi`), PowerVR GE8322, 2 GB RAM, Bildschirm
576x1152. Meldung: „core not started: MEM1 0x80000000..0x83000000 is taken in this process
(by 80000000-90000000 rw-p [anon:dalvik-main space])“; Adresskarte: „MEM1 placeholder: NOT
reserved“, direkt darunter `7f7ff000-80000000 [anon:dalvik-Concurrent mark-compact compaction
buffers]`.

- Dort liegt der **Java-Speicher von ART** (Hauptfläche 256 MB, genau ab 0x80000000). Der
  Müllsammler „Concurrent mark-compact“ (CMC, userfaultfd) ist der neue von ART (Android 14,
  auch per Mainline-Update); der Retroid (Android 13, alter Sammler CC) hat 0x80000000 frei,
  dort läuft 32 bit.
- Der Java-Speicher entsteht im **Zygote**, bevor die App einen Befehl ausführt; jede App erbt
  ihn per `fork` an derselben Stelle. Aus der App heraus nicht zu ändern (auch nicht mit
  `largeHeap`, `android:process`, App-Zygote: alle stammen vom Zygote ab).
- 64 bit ist nicht betroffen: dort liegt die Hauptfläche tief (z. B. `02000000-42000000
  [anon:dalvik-main space (region space)]`, Samsung Galaxy M23 Android 14), MEM1 und das
  Fenster bis 4 GB sind frei. Belegt bisher nur für ein 32-bit-Gerät; vermutlich trifft es
  viele reine 32-bit-Geräte mit Android 14+.
- Mehr RAM hilft nicht (Adresslage, nicht Menge).

Verworfen: (B) MEM1 auf 32 bit an eine andere Adresse (jede Stelle, die 0x80000000
annimmt, müsste umrechnen: Spielcode, SDK, GX-Adressen, ARAM, Spielstände – zu riskant);
(C) nicht unterstützen (steht vorerst als „Known issue“ im Releasetext der Alpha 4).

## Was es schon gibt

- **Kern-Schnittstelle** `port/whole/runtime/re4_core.h` und `re4_gx_frame.h`: reine
  C-Aufrufe und flache Daten. `Re4CoreSnapshot` (Zustandsbild, große flache Struktur ohne
  Zeiger), `Re4GxChunk` (ein Bild der GX-Nachbildung: Kopf mit Zeigern auf `ops`,
  `vertices`, `indices`, `blob`; der Renderer liest **keinen** Spielspeicher, alles steckt im
  Chunk), Ton als PCM-Ring (`re4_core_audio`, 32 kHz stereo).
- **Linux-Host** `port/whole/host/loader.c` + `main.cpp` (`re4_host`): startet den Kern
  schon heute als eigenständiges Programm (MEM1 vor dem Laden reserviert, Module laden,
  `--core-test`, `--runtime-test`, Filmtest). Vorbild für das Programm des Kernprozesses.
- **App-Seite**: `core_mode.cpp` ruft den Kern nur über die Tabelle `CoreApi api`
  (`dlsym` auf `libre4_main.so`). Alles andere (JNI `CoreBridge_*`, Java) spricht nur mit
  `core_mode.cpp`. Ein Fernaufruf-Ersatz für genau diese Tabelle hält den Umbau klein.

### Die Aufrufe von `CoreApi` (core_mode.cpp)

| Aufruf | Richtung, Takt |
|---|---|
| `mem1Init`, `reserve(dataDir, moduleDir)`, `start`, `state`, `message` | Start, Zustand |
| `pad(const Re4CorePad*)` | App → Kern, jede Eingabe |
| `rumble()` | Kern → App, je Bild |
| `audio(int16_t*, n)`, `audioStats` | Kern → App, Tonthread laufend |
| `acquire()` / `release()` (Re4CoreSnapshot) | Kern → App, je Bild |
| `gxAcquire()` / `gxRelease()` / `gxDetach()` (Re4GxChunk) | Kern → App, je Bild, bis einige MB |
| `vsync(ns)` | App → Kern, je Bildschirmtakt |
| `pause(on, …)`, `debugMenu()`, `setDof(on)`, `gyro(yaw, pitch)` | App → Kern |
| `cardList`, `cardFree`, `cardName`, `cardExport`, `cardImport` | Menü, selten |
| `frameCount`, `gameThread`, `freeAt`, `logTail` | Diagnose |

Dazu außerhalb der Tabelle: Absturzdatei (`CoreBridge.setCrashFile`, Signal-Handler im
Kern), Umgebungsvariablen als Einstellungen (`RE4_*`, `coreSettings`), das Fehlerprotokoll
(`CoreLog.java` liest `logcat` der eigenen UID; ein Kindprozess hat dieselbe UID).

## Umsetzung (Vorschlag, in der neuen Sitzung prüfen und mit dem Projektinhaber klären)

1. **Kernprogramm**: ein ausführbares Programm (PIE) im Bibliotheksordner der APK, Name
   `lib*.so` (z. B. `libre4_coreproc.so`), gebaut wie `re4_host` für Android
   (`port/tools/whole_build_android.sh`). Reserviert MEM1 vor dem Laden der Module.
   - **Achtung Paketierung:** `app/build.gradle` hat `packaging { jniLibs { useLegacyPackaging
     = false } }` – die Bibliotheken bleiben unentpackt in der APK. Ausführen geht nur aus
     dem entpackten `nativeLibraryDir` (Android 10+: W^X, aus dem Datenordner verboten).
     Also `useLegacyPackaging = true` (größere Installation) oder ein anderer Weg (z. B.
     `/system/bin/linker <pfad>` – zu prüfen).
   - Start über `ProcessBuilder` (Java) oder `posix_spawn` (kein `fork` im JVM-Prozess);
     im Kind `prctl(PR_SET_PDEATHSIG, SIGKILL)`; Adresskarte des Kindes ins Protokoll.
2. **Geteilter Speicher**: ein Bereich (`ASharedMemory` ab API 26; darunter `/dev/ashmem`
   oder `memfd_create` per syscall – minSdk der App ist 23) für den GX-Chunk-Ring (die
   Puffer, in die der Kern ohnehin baut, liegen gleich dort; Zeiger im Chunk-Kopf als
   Abstände, die App rechnet sie auf ihre Abbildung um), das Zustandsbild, den Ton-Ring, die
   Eingabe und kleine Zähler (Rumble, Bildzähler). Wecken über `eventfd` oder Futex im
   geteilten Speicher.
3. **Befehle**: ein `socketpair`, beim Start vererbt, für die seltenen Aufrufe (Start,
   Pause, Debug-Menü, DoF, Gyro, Speicherkarte, Meldungen, Protokollende) und das Ende
   (Absturz des Kindes = Socket schließt → Fehlerdialog, Absturzdatei des Kindes lesen).
4. **Wann der eigene Prozess** (Entscheidung des Projektinhabers, 20.10.2026): **nur als
   Rückfall**, wenn MEM1 im App-Prozess belegt ist (der Platzhalter scheitert); sonst läuft
   der Kern wie heute im App-Prozess (64 bit immer, 32 bit mit freiem MEM1). Dazu ein
   Schalter, der den eigenen Prozess für Tests erzwingt (z. B. `RE4_CORE_PROCESS=1`,
   Testskript-Parameter), damit er auch auf dem Retroid geprüft werden kann.
5. **Risiken**: „Phantom-Prozesse“ (Android 12+ beendet Kindprozesse, die im Hintergrund
   CPU brauchen; der Kern pausiert im Hintergrund ohnehin), Hersteller, die Kindprozesse
   beenden, SELinux (Ausführen aus `nativeLibraryDir` ist für Apps erlaubt), Leistung
   (Kopien vermeiden), schwache Geräte (das Testgerät hat nur 2 GB und eine PowerVR GE8322).

## Plan (Sitzung 20.10.2026, Branch `claude/re4-32bit-kernprozess`)

Grundlage: die Übergabe oben und der Code von Alpha 4. Kurz: die App bleibt, wie sie ist; nur
wenn MEM1 im App-Prozess belegt ist (oder der Schalter es verlangt), ersetzt `core_mode.cpp`
die Tabelle `CoreApi api` durch einen **Fernaufruf-Ersatz** (Client), der mit einem
Kernprogramm im eigenen Prozess (Server) spricht. Alles oberhalb der Tabelle (JNI, Renderer,
Java) merkt davon nichts. Der Kern selbst (`libre4_main.so`, `core_api.cpp`, `gx_decode.cpp`,
`ai.cpp`) bleibt unverändert: der Server ist nur ein weiterer Benutzer der Kern-Schnittstelle,
wie die App und `re4_host`.

```
App-Prozess (Java, GL)                         Kernprozess (ohne Java)
core_mode.cpp ── api (Client) ──┐              ┌── Server ── libre4_main.so (Spiel)
   Renderer liest Chunks direkt │ geteilter    │   holt Chunks/Zustand/Ton beim Kern,
   aus dem geteilten Speicher   │ Speicher     │   gibt Eingaben/Vsync an den Kern
                                └─ Socket ─────┘   (Befehle, Antworten, neue Speicherstücke)
```

### 1. Wann der eigene Prozess

- `loadCore` (JNI_OnLoad), nur 32 bit: scheitert der Platzhalter für MEM1, lädt die App den
  Kern **nicht** in den eigenen Prozess und merkt sich „Kernprozess“. Adresskarte:
  „MEM1 placeholder: NOT reserved: the core runs in a process of its own“.
- `CoreBridge.start`: mit `RE4_CORE_PROCESS=1` in den Einstellungen (Testskript `-CoreProcess`)
  läuft der Kern auch dann im eigenen Prozess, wenn MEM1 frei war (der schon geladene Kern im
  App-Prozess bleibt unbenutzt liegen; die Einstellungen kommen erst mit `start`, nach
  JNI_OnLoad). Auf 64 bit wird der Schalter nicht beachtet (Meldung im Protokoll): dort gibt es
  das Problem nicht, und das Kernprogramm gibt es nur für `armeabi-v7a`.
- Sonst alles wie bisher (64 bit immer im App-Prozess, 32 bit mit freiem MEM1 auch).

### 2. Das Kernprogramm `libre4_coreproc.so`

- Ein ausführbares Programm (PIE) aus `port/whole/host/coreproc.cpp` (+ `core_server.cpp`), von
  `whole_build_android.sh` nur für `armeabi-v7a` gebaut und nach `jniLibs/armeabi-v7a` kopiert
  (Name `lib*.so`, damit Android es mit den Bibliotheken ausliefert).
- Ablauf im Kind: alle Signale frei, SIGPIPE ignorieren, fremde Dateien schließen (alles außer
  0–3), **MEM1-Platzhalter sofort** (wie die App), eigene Adresskarte für den Bericht,
  `PR_SET_PDEATHSIG`, `dlopen("libre4_main.so")` über `LD_LIBRARY_PATH` (der Bibliotheksordner
  der App), Aufrufe per `dlsym` wie in `loadCore`, `re4_mem1_init`, geteilten Speicher anlegen,
  „Hallo“ an die App (Prozessnummer, Adresskarte, Fehler). Auf `START`: Einstellungen als
  Umgebungsvariablen, `re4_core_reserve` + `re4_core_start`.
- Auf Linux dasselbe Programm (`re4_coreproc` neben `re4_host`), mit der Reservierung des
  Linux-Laders (`host/loader.c`, Teil in eine gemeinsame Datei), damit die Cloud-Prüfung fast
  denselben Code prüft wie das Gerät. Nur 32 bit (i686, armhf unter qemu).
- Start durch die App: `vfork` + `execve` aus `core_mode.cpp` (kein `fork` im Java-Prozess;
  `posix_spawn` gibt es erst ab API 28), Socket als Datei 3, Umgebung der App plus
  `LD_LIBRARY_PATH`. Der Pfad kommt aus `dladdr` der App-Bibliothek (kein Java nötig).
- **Entscheidung des Projektinhabers (20.10.2026): Bibliotheken entpacken**
  (`useLegacyPackaging = true`): Android entpackt beim Installieren die Bibliotheken des Geräts
  (nur eine ABI) in den Bibliotheksordner, aus dem Apps Programme ausführen dürfen; geht auf jeder
  Android-Version. Die APK-Datei wird kleiner (Bibliotheken gepackt), der Platz nach der
  Installation bleibt etwa gleich. Verworfen: Start über den Systemlader direkt aus der APK
  (`/system/bin/linker …apk!/lib/…`, erst ab Android 10, selten benutzt).

### 3. Geteilter Speicher

Vom Kind angelegt (`memfd_create`, sonst `ASharedMemory` über `libandroid.so`), per Socket
(`SCM_RIGHTS`) an die App gegeben. Seiten werden erst bei Benutzung belegt.

- **Steuerblock** (ein Stück): Kopf mit Version und Größen; Klingeln (Futex im geteilten
  Speicher) für Eingabe, GX und Ton; Warteschlange der Controller-Zustände (64 Einträge: ein kurzer
  Tipper zwischen zwei Spielbildern geht nicht verloren, `re4_pad_live` merkt ihn sich), der
  Vsync-Zeitstempel (16), letzter Gyro-Wert; Spiegel, die ein Rahmen-Haken des Kindes je
  Spielbild schreibt (Rumble, Bildzähler, `freeAt`), Tonzähler; der **Dreifachpuffer des
  Zustandsbilds** (3 × `Re4CoreSnapshot`, kopiert werden nur die belegten Teile, ohne Sperre);
  der **Ton-Ring** (Kanalzahl und Rate im Kopf, nicht fest 2: Abgleich mit dem DPL2-Branch).
- **GX: 3 Plätze**, je ein eigenes Speicherstück (anfangs 8 MB, wächst bei Bedarf: das Kind legt
  ein größeres an und schickt es per Socket). Der Server holt jeden Chunk mit
  `re4_core_gx_acquire`, kopiert ihn in einen freien Platz (Zeiger als Abstände, die App rechnet
  sie auf ihre Abbildung um) und **gibt den Chunk des Kerns erst frei, wenn die App ihren Platz
  freigibt**: so sind wie heute höchstens 4 Bilder unterwegs (gleiche Verzögerung der Eingabe),
  und der Kern bleibt unverändert. Die App fragt wie heute einmal je Bildschirmbild; erst dann
  holt der Server beim Kern (die „1 s ohne Abholen“-Regel des Kerns wirkt so wie heute).
  `gxDetach` erhöht eine Nummer; Plätze mit älterer Nummer verwirft die App, der erste Chunk
  danach beginnt sicher neu (RESET).
- **Ton**: der Ton-Thread der App liest aus dem Ring; das Kind liest beim Kern genau so viel
  nach, wie die App verbraucht hat (so bleibt der Gleichlauf des Kerns mit der Uhr des Geräts
  erhalten), Puffer etwa 16 ms zusätzlich.
- Eine Kopie der GX-Daten je Bild (im Kind, auf einem eigenen Thread, neben dem Spiel) statt
  Bau direkt im geteilten Speicher: der Kern bleibt unverändert; messen, ob es reicht
  (schwaches Gerät).

### 4. Befehle (Socket)

`socketpair(AF_UNIX, SOCK_STREAM)`, Nachrichten mit Kopf (Art, Nummer, Werte, Länge) und
Inhalt; ein Lese-Thread der App verteilt die Antworten. Befehle: `START`, `STATE`, `PAUSE`
(wartet bis `waitMs` wie heute), `DEBUG_MENU`, `SET_DOF`, `LOG_TAIL`, `CARD_LIST`,
`CARD_FREE`, `CARD_NAME`, `CARD_EXPORT`, `CARD_IMPORT`, `HANG` (das Kind tastet seinen
Spiel-Thread selbst ab, `tgkill` geht nicht über Prozesse), `CRASH_FILE` (die Absturzdatei als
Datei-Handle). Vom Kind: `HELLO`, `SEGMENT` (neues GX-Speicherstück).

### 5. Ersatz der Tabelle `CoreApi` (core_mode.cpp)

| Aufruf | im eigenen Prozess |
|---|---|
| `mem1Init`, `reserve`, `start` | Befehl `START` (das Kind reserviert MEM1 selbst) |
| `state`, `message` | Befehl; ist das Kind beendet: `FAILED` mit Grund (Signal, Exit-Code) |
| `pad` | Warteschlange + Klingel (jede Eingabe in Reihenfolge) |
| `vsync` | Warteschlange der Zeitstempel (dieselbe Uhr in beiden Prozessen) |
| `gyro` | letzter Wert (das Kind gibt ihn sofort weiter) |
| `rumble`, `frameCount`, `freeAt` | Spiegel im geteilten Speicher |
| `audio`, `audioStats` | Ton-Ring / Spiegel |
| `acquire`, `release` | Dreifachpuffer im geteilten Speicher |
| `gxAcquire`, `gxRelease`, `gxDetach` | GX-Plätze (oben) |
| `pause`, `debugMenu`, `setDof`, `card*`, `logTail` | Befehle |
| `gameThread` | 0; `reportHang` schickt `HANG` |

### 6. Absturz, Protokoll, Ende

- Das Kind hat denselben Absturz-Handler wie die App (`crash.txt` über das geschickte
  Datei-Handle, Zeilen „RE4Crash“ im Protokoll), danach der des Systems (Tombstone, Puffer
  `crash`; der Bericht holt ihn schon).
- Endet das Kind (Socket zu, `waitpid`), wird der Zustand `FAILED` („the game core process
  ended: signal 11 (Segmentation fault)“). Java zeigt dann einen Dialog wie beim Startfehler:
  „The game core stopped: …“ mit „Send report“ / „Quit“ (die App stürzt nicht mit ab).
- Endet die App, endet das Kind (Socket zu, `PR_SET_PDEATHSIG`).
- Protokoll: das Kind hat dieselbe Benutzerkennung, `logcat` der App sieht seine Zeilen; der
  Fehlerbericht bekommt „Core process: …“ (Prozessnummer, warum) und die Adresskarte des Kindes.

### 7. Testskript, Cloud-Prüfung, Gerätetest

- `test-core-app.ps1 -CoreProcess` → `RE4_CORE_PROCESS=1`; Bericht: Zeilen zum Kernprozess (Start,
  MEM1, Ende); ein Ende des Kernprozesses beendet die Beobachtung wie ein Absturz; `P` (Profil)
  misst den Kernprozess.
- Cloud: Testprogramm `re4_core_remote_test` als „App“ ohne eigenen Kern, belegt MEM1 vorher
  selbst (wie ART beim Tester), startet `re4_coreproc` mit einem Testeinstieg und prüft über den
  Client: Start/Zustand/Meldung, Controller (auch der kurze Tipper), Rumble, Zustandsbilder,
  GX-Chunks (Prüfsumme je Chunk gegen die des Kindes, Wachsen der Speicherstücke, RESET nach
  `gxDetach`), Ton (bekanntes Muster), Vsync, Pause, Debug-Menü, Tiefenschärfe, Speicherkarte
  (Liste, freie Blöcke, Export/Import), Protokollende, Absturz des Kindes (Zustand, `crash.txt`)
  und Ende der „App“ (das Kind endet). In `whole_build.sh` für i686 und armhf.
- Gerät: Retroid, `-Abi armeabi-v7a -CoreProcess`: bis r100, Ton, Film, Speichern/Laden, Menü.

### Stand (20.10.2026, Cloud)

Umgesetzt wie geplant (Einzelheiten und Prüfungen: Status-Datei, „32 bit: der Kern als eigener
Prozess“): `host/coreproc.cpp` (Kernprogramm), `runtime/re4_core_ipc.h` + `core_ipc.cpp`
(geteilter Speicher, Nachrichten), `runtime/re4_core_client.h` + `core_ipc_client.cpp` (Client der
App), `core_mode.cpp` (`useCoreProcess`), `CoreActivity`/`CoreBridge`/`CoreLog` (Dialog, Bericht),
`test-core-app.ps1 -CoreProcess`, `whole_build_android.sh` (Programm für `armeabi-v7a`), Linux-Prüfung
`re4_core_remote_test` in `whole_build.sh`. Cloud: i686 und armhf bestanden. Retroid
(20.10.2026, `-Abi armeabi-v7a -CoreProcess`): nach der Korrektur des Tonwegs (die AudioTrack nimmt
stoßweise) Spiel bis r100, Filme, Ton ohne Aussetzer, 30 FPS wie ohne Kernprozess (Tabelle in der
Status-Datei). DPL2 (`claude/re4-dpl2-raumklang`) ist zusammengeführt; der Kernprozess reicht
Stereo und die vier Kanäle über einen Leser (`re4_core_audio6`) weiter. Als **0.40.0-alpha**
vorbereitet (Releasetext `port/android-decomp/docs/releases/v0.40.0-alpha.md`). **Offen:** Menü,
Speichern/Laden und Debug-Menü auf dem Gerät mit dem Kernprozess, dann das Mobicel-Gerät des
Testers (der echte Rückfall).

**Erreicht (20.10.2026):** Release 0.40.0-alpha; auf dem Retroid in 64 bit und in 32 bit mit
erzwungenem Kernprozess ohne Befund, und beim Tester mit dem reinen 32-bit-Gerät (Android 14) läuft
das Spiel jetzt (Rückmeldung des Projektinhabers, ohne Bericht). Offen bleiben Leistungszahlen von
diesem Gerät (sein Fehlerbericht) und der Raumklang auf einem Gerät mit Spatializer.

Gerätetest (Windows-PC, Retroid per ADB):

1. `test-core-app.ps1 -Abi armeabi-v7a -CoreProcess`: automatischer Start bis r100; im Bericht
   „Kern im eigenen Prozess: ja (RE4_CORE_PROCESS=1), PID …“, „Spiel gestartet: ja“, Bilder,
   Ton-Zähler, keine „Kernprozess-FEHLER“-Zeile.
2. `test-core-app.ps1 -Abi armeabi-v7a -CoreProcess -SkipCore -SkipApk -Manual`: selbst spielen:
   Vorfilm mit Ton, Titel, Laden/Speichern an der Schreibmaschine, Menü (SELECT), Debug-Menü
   (SELECT halten), Vibration, Speicherkarten-Export im Menü.
3. Gegenprobe ohne Schalter (`test-core-app.ps1`, 64 bit): alles wie bisher, „Kern im eigenen
   Prozess: nein“.

Nachtrag Ton (20.10.2026): stimmt die Zahl der Kanäle des Kerns nicht mit dem Ring überein
(`Control::audioChannels`, 6), gibt die App Stille aus; beide Seiten kommen aus demselben Build.

## Parallel: Raumklang im DPL2-Modus (anderer Branch)

Entscheidung des Projektinhabers (20.10.2026): Weg 1 für „Dolby Pro Logic II“ – im DPL2-Modus
gibt der Kern die hinteren Kanäle getrennt aus, die App reicht mehrere Kanäle an Android
weiter (Androids Raumklang für Kopfhörer, wo das Gerät ihn hat). Umgesetzt im Branch
`claude/re4-dpl2-raumklang` (vom selben Stand). Dabei ändert sich der Tonweg zwischen Kern
und App (`re4_core_audio`, der Ring in `runtime/ai.cpp`, `CoreAudio.java`): den Ton-Ring im
geteilten Speicher bitte so anlegen, dass die Zahl der Kanäle nicht fest 2 ist; beim
Zusammenführen der beiden Branches den Tonweg abgleichen.

## 64 bit (31.10.2026, Wunsch des Projektinhabers)

Nutzerbericht Redmi K50 Ultra (Xiaomi 22081212C, Android 15, 64 bit): „MEM1 … is taken in this process (by
77480000-b7480000 rw-p [anon:dalvik-main space (region space)])“. Dort liegt der **verschiebbare** Hauptspeicher von
ART über MEM1 (der große-Objekte-Bereich, den `holdMem1InLargeObjectSpace` festhalten kann, liegt bei 0x10000). Seitdem
gilt der Kernprozess auch für 64 bit: `libre4_coreproc.so` wird für arm64-v8a gebaut (CMake `if(ANDROID)`,
`whole_build_android.sh`), lädt auf 64 bit `libre4_platform.so` und `libre4_main.so` wie die App in Bereiche oben im
Fenster unter 4 GB (`openInWindow`, `android_dlopen_ext` mit `ANDROID_DLEXT_RESERVED_ADDRESS`), und `core_mode.cpp
loadCore` nimmt ihn, wenn MEM1 weder frei noch im große-Objekte-Bereich zu halten ist; `RE4_CORE_PROCESS=1`
(`test-core-app.ps1 -CoreProcess`) erzwingt ihn auch auf 64 bit. Geprüft auf dem Retroid (arm64, erzwungen): Intro-Film,
r100, 240 s ohne Absturz, 30 FPS. Auf dem Xiaomi selbst nicht geprüft.

## Testen

- **Cloud:** 32-bit-Linux (`-m32`/i686): das Kernprogramm als Kindprozess, ein Testprogramm
  als „App“ (Chunks, Zustandsbild, Ton, Eingabe, Speicherkarte über die neue Schnittstelle;
  dieselben Prüfungen wie `--core-test`), `whole_build.sh` muss weiter alles bestehen.
- **Lokal (Retroid):** 32 bit erzwingen (`test-core-app.ps1 -Abi armeabi-v7a`) und den
  eigenen Prozess erzwingen (Schalter); Spiel bis r100, Ton, Film, Speichern/Laden, Menü.
- **Tester:** das Mobicel-Gerät (Android 14, nur 32 bit).
- Ein Meilenstein gilt erst nach dem echten Gerätetest.

## Bezug

Status-Datei `port/docs/whole-program-status.md` (Einträge „32 bit startet nicht“ vom
19./20.10.2026, MEM1-Diagnose der Alpha 3), `CLAUDE.md`, `port/docs/arm64-bestandsaufnahme.md`
(warum MEM1 fest bei 0x80000000 liegt).
