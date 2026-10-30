# Ganzprogramm-Port: Stand

Diese Datei ist das Gedächtnis zwischen den Sitzungen. Jede Sitzung liest sie
zuerst und aktualisiert sie am Ende (siehe `CLAUDE.md`).

## Retail-Discs (US) (28.10.2026 spätabends, lokal, WSL2, Branch `retail-us`)

Auftrag: die US-Verkaufs-Discs (`Resident Evil 4 (USA) (Disc 1/2)`, ISO und RVZ in `E:\Development\Android\RE4-DiscdataUS`)
parallel zu Debug- und EU-Discs. Branch `retail-us` vom Stand `960dc116` (0.95.0-beta mit RVZ-Import; `master` hat beides noch
nicht). Analyse, Unterschiedstabelle und Weichen: **`port/docs/retail-us-plan.md`**.

- **Erkennung:** US- und Debug-Disc tragen beide `G4BE08`; die US-Disc hat die Dateitabelle bei 0x231A00 (Debug 0x28FE00),
  `main.dol` 2 168 544 Byte. Die Regel stand schon in `disc_release.py`/`dvd.cpp`, `retail-us` wird jetzt angenommen
  (`whole_disc_folder.py`, `whole_r100_check.sh` mit eigenem Rundgang-Ergebnis `-us`, App `release=us`).
- **Befund US gegen EU:** die Daten sind bis auf die Sprachen wie die EU-Daten aufgebaut (dieselben fehlenden Debug-Dateien,
  dieselben umsortierten Archive, dieselben 15 neuen Kern-Texte ab 0x56, Funkgerät, Karte, Lebensanzeige, Kapitelende wie EU).
  Wie die Debug-Fassung: nur Englisch (Sprache 1), `title.dat`/`option.dat` bytegleich mit Debug, Stage 4 auf beiden Discs,
  r221-Texte, keine Film-Untertitel; `core.das` passt in den festen Platz. **Keine neue Korrektur nötig**, nur Weichen.
- **Neue Regeln:** `re4_disc_retail()` (EU oder US) und `re4_disc_retail_us()` in `runtime/dvd.cpp`. Mit `re4_disc_retail()`
  jetzt: Patches 0120, 0123 (Verschiebung), 0124, 0125, 0126, 0129, 0133, 0135 (Kapitelende, Baustein 0; die 999-Grenze des
  Spielendes nur EU), das Umsortieren in `formats.cpp`, kein Debug-Menü (Kern und App). Nur EU bleiben 0121, 0128, 0131, 0132,
  0136 und die Sprachwahl. Patch-Köpfe und Patch-Tabelle unten ergänzt.
- **App:** `GameCubeDisc` nimmt US-Discs im Kernmodus an, Kataloge `references-us.tsv` (134), `rooms-us.tsv` (142),
  `blocks-us.tsv` (gleich Debug), `files-us.tsv` (1 975) aus `make_disc_catalogs.py --iso … --data … --suffix=-us`; Texte
  „US release“, Startseite nennt die US-Fassung. US- und Debug-Discs gemischt werden erkannt (verschiedene `release`).
  Spielstände: US und Debug haben dieselbe Kennung, die Speicherkarte bleibt gemeinsam (wie mit EU).

**Prüfungen (PC, WSL, i686 -O2)**
- `whole_r100_check.sh` mit US-Discs: **ALLES OK** – Build und Laufzeittests (`disc release` mit US-Fall), Formatvergleich
  407 Dateien 0 Fehler, Titel → r120 → r100 10 000 Bilder ohne Absturz, Soft-Reset, alle 13 Filme beider Discs ohne
  CRI-Fehler, Speichern/Laden mit Systemdatei. GX-Bilder: Capcom-Logo, Lebensanzeige mit „Leon“ (kein „KiLeonr“).
- Raum-Rundgang `--rooms all` mit US-Daten (Debug-`roomInfo.dat` über `--room-info`): **126 von 126 Räumen ohne Fehler**;
  gegen den Debug-Rundgang 1 Raum verschieden (r21d: mit Debug einmal gestorben), **0 Spielmeldungen nur mit US**.
- App-Import (Java, `DiscTests` mit den echten US-ISOs im Kernmodus): beide Discs geprüft und entpackt, jede Datei gegen
  `files-us.tsv`, 3 990 Prüfungen bestanden; synthetische Tests 24 bestanden (US angenommen, außerhalb des Kernmodus
  abgelehnt, gemischt mit Debug erkannt). **RVZ:** die beiden echten, von Dolphin geschriebenen US-RVZ mit `rvz_check`
  ausgepackt: SHA-1 gleich den ISOs (Disc 1 45 s, Disc 2 73 s) – damit ist der offene Punkt „RVZ aus Dolphin“ am PC geprüft.
- Android: Kern für armeabi-v7a und arm64-v8a 0 Fehler (`whole_build_android.sh --abi all`), Java der App mit Gradle kompiliert.
- `test_whole_disc_folder.py` mit US-Fall.
- **Nicht geprüft:** Gerät (Retroid), ganze Kapitel mit US-Discs, GX-Bilder der Untermenüs (Funkspruch, Karte, Kapitelende)
  mit US-Daten; 0133 (Händler-Umzug r204) nur aus dem US-Kartencode geschlossen.

## Offene Punkte aus den Testerberichten (Stand 28.10.2026 abends, lokal, Branch `claude/determined-allen-36nj6r`)

Bearbeitet in der lokalen Sitzung vom 28.10.2026 (Abschnitte darunter). Stand am Abend:

1. **Mercenaries mit Hunk** – behoben (Patch 0134), am PC mit allen Figuren geprüft; auf dem Gerät bestätigt
   (Projektinhaber, 28.10.2026).
2. **Kapitelende EU: vorderste Gegner-Ziffer** – behoben (Patch 0135); auf dem Retroid bestätigt (28.10.2026).
3. **Kapitelende mit 60 FPS (`%` zwischen den Zeilen)** – Korrektur von `c6716cb`; auf dem Retroid bestätigt.
4. **Intro-Film ohne Untertitel** – nachgebaut wie die EU-Fassung (Patch 0136); auf dem Retroid (Deutsch) bestätigt.
5. **Händler „BUY“: Koffer sichtbar** – Dolphin ist auf dem PC nicht installiert, Vergleich offen (Projektinhaber);
   16:9 schneidet nur die Balken ab, ändert die Überdeckung nicht: weiter sehr wahrscheinlich Original.
6. **60 FPS ohne Flackern** – umgesetzt (Patch 0137, Renderer), auf dem Retroid weitgehend ruhig; vereinzeltes
   Fackelflackern bleibt (Entscheidung des Projektinhabers). Der Projektinhaber hat das Spiel mit 0.94.0 durchgespielt.
7. **Weiße Texturen mit 60 FPS** (Waffen und Kleidung der Ganados, Nutzer mit itel P661N und Samsung S24) – behoben
   in 0.95.0-beta (Abschnitt darunter); **Gerätetest beim Nutzer offen**.
8. **RVZ beim Disc-Import** (Wunsch des Projektinhabers, 28.10.2026) – eingebaut in 0.95.0-beta (Abschnitt darunter);
   **Gerätetest mit einer RVZ aus Dolphin offen**.
9. **Touch-Zielen** (Wunsch des Projektinhabers, 28.10.2026) – eingebaut in 0.95.0-beta (Abschnitt darunter);
   **Gerätetest offen**.
10. **Schalter „Nebel aus“** – nicht umgesetzt (Entscheidung des Projektinhabers, 28.10.2026: nur die
    Leistungsvorschläge 1 und 2).
11. **Labyrinth in r20b versperrt (EU-Discs)** – behoben (Patch 0138, Abschnitt darunter), am PC geprüft und auf
    dem Retroid mit dem Spielstand des Nutzers bestätigt (Projektinhaber, 28.10.2026: Brüstung weg, Treppe begehbar).
12. **Schwierigkeitsauswahl mit den EU-Discs** (Projektinhaber, 28.10.2026) – eingebaut (Patches 0139–0141, Abschnitt
    darunter), am PC geprüft; **Gerätetest offen**. Alle EU-Stellen mit dem Easy-Byte geprüft (Liste im Abschnitt).
13. **Zielfernrohr: nur die senkrechte Linie des Fadenkreuzes sichtbar** (Nutzermeldung mit Bildschirmfoto,
    29.10.2026, Auflösung 1×, 640×336) – behoben im Renderer (Abschnitt „Pixelmitte wie der GameCube“), am PC geprüft;
    **Gerätetest offen**.
14. **EMPTY-Anzeige wechselt die Sprache (EU-Discs)** (Nutzervideo, 29.10.2026) – behoben (Patch 0142, Abschnitt
    „Lokale Sitzung 29.10.2026 abends“), am PC in allen fünf Sprachen geprüft; auf dem Retroid bestätigt (Projektinhaber,
    29.10.2026: „LEER“ flackert nicht mehr).
15. **Minenwerfer mit Zielfernrohr: sofortiger Neustart** (Nutzerbericht 29.10.2026, realme RMX2001, 64 bit,
    **0.93.0-beta**, Debug-Discs; ohne Zielfernrohr kein Fehler). Drei Abstürze, alle gleich beim Ausrüsten:
    `HermiteInterpolation` ← `MotionSetCore` ← `cPlNeck::init` ← `pl_R1_Footwork`, davor „MotionMoveCore(): Pl, Invalid
    parts 195“. Der Minenwerfer mit Zielfernrohr (Gegenstand 0xAB, Waffe 0xE Typ 1) lädt `em/wep25.drs` (615 232 Byte)
    statt `em/wep14.drs` (628 704 Byte), aber dasselbe Modul `wep14` mit denselben Eintragsnummern
    (`cObjMine::setMotion` bis `WEP_ARC 0x34`). Bei voller Gesundheit (`dmMotCk`) nimmt der Nacken `m_MotTbl[0x3F]` =
    `WEP_ARC 0x2F`. Gleiches Bild wie Hunks Maschinenpistole (Patch 0134, ab 0.94.0): sehr wahrscheinlich hat `wep25.drs`
    weniger Einträge, das Spiel liest hinter die Tabelle; Patch 0134 gilt auch für `WEP_ARC_PTR` (über `PL_ARC_PTR`).
    **Bestätigt (Tester, 29.10.2026):** mit 0.95.0-beta kein Absturz mehr.
16. **Ton knackt, „nicht klar“** (Nutzerbericht 29.10.2026) – zwei Verbesserungen (Abschnitt „Ton: Kompressor …“), in
    der Cloud geprüft; **Hörtest auf dem Gerät offen**.
17. **„4:3“ (ohne Balken) zeigt im Spiel doch die Balken** (Bildschirmfoto eines Nutzers, 29.10.2026, 1600×720,
    Auflösung 1,5×, Wald mit Schrotflinte, Lebensanzeige sichtbar). Das Bild ist 1029×720 groß: das ist das
    Bildformat 3 (10:7 über die ganze Höhe, `core_mode.cpp drawGx`), die App füllt die Höhe; die Balken malt also das
    Spiel (`Draw_cinesco` bzw. die Schere), `re4_letterbox_open` war in dem Moment 0. Kein Ereignis (Lebensanzeige da),
    kein Untermenü; Ursache aus dem Code allein nicht gefunden (Filter 09 ist nur der Pausenfilter). **Offen:**
    Fehlerbericht direkt danach (Zeilen „core: letterbox bars …“ und „picture: the game draws … its letterbox (rNNN)“),
    Raum und Version. Projektinhaber 29.10.2026: auch auf dem Retroid (16:9-Bildschirm) – allgemein.
    **Retroid-Berichte 29.10.2026 18:48/18:56 (Stand mit exaktem 4:3, r104, MSAA 2×, FXAA):** Bild 1440×1080 (4:3
    richtig), das Spiel meldet „draws without its letterbox (r104)“, trotzdem sind oben und unten je 136 Zeilen
    schwarz (= Zeilen 0..56 und 392..448), mit gerader Kante durch Wände und Türrahmen: das 3D wird an der alten
    Balkengrenze abgeschnitten, gemalt wird kein Balken. Im Code (`Render`, `SetScissorState`, `ScreenGXSet`, Filter,
    Viewports, Kopie aufs XFB, Renderer: Schere, Auflösen, FXAA, gezeigte Zeilen) keine Ursache gefunden.
    **Ursache gefunden (Grafikaufnahme des Projektinhabers, r104, Cloud 30.10.2026):** Schere 0..511 × 0..447 und
    Viewport voll, das 3D füllt die Höhe (mit `gx_chunk_render` nachgezeichnet, bis Zeichnung 364 Zeilen 0..447). Die
    letzten zwei Zeichnungen (365/366) sind zwei schwarze Streifen 640×60 bei y ±210: die Anzeige-Einheiten der Klasse
    `IDC_CINESCO`, die `Cockpit::roomInit` bei jedem Raumstart setzt – der dritte Weg der Balken neben `Draw_cinesco`
    und der Schere. **Behoben:** `runtime/picture.cpp re4_letterbox_frame` (jedes Bild, aus dem Bild-Haken): solange
    „4:3“ offen ist, `IdSys.dispSw(IDC_CINESCO, 0)`, beim Schließen wieder an (Ereignisse, Untermenüs, andere
    Bildformate wie bisher). Test `letterbox` erweitert (Einheiten aus bei offen, wieder an bei Rno0 2; Gegenprobe 1
    Fehler). Am PC mit Disc-Daten nachgeprüft (Abschnitt „Lokale Sitzung 29.10.2026 abends“): in r104
    keine schwarze Zeile mehr. Auf dem Retroid bestätigt (Projektinhaber, 29.10.2026: „nahezu perfekt“, im Spiel ohne
    Balken; Zwischensequenzen behalten sie, wie vorgesehen).
18. **Inventar: Leons Messer fehlt am Modell rechts** (Nutzerbericht 30.10.2026, EU-Discs, OPPO CPH2127). Im Spiel
    lädt `cPlLeon::setModel` Eintrag 0xD (`Body->m_pKnife`, mit `setKnife(false)` auf 0 skaliert = Messer in der
    Hand), das Untermenü (`Sscrn/ss_model.cpp leonModelInit`) nur 4/5 und 0xA, 8, 6, 9. Ob das Messer an der Brust
    im Körpermodell steckt oder die Verkaufsfassung ein Teil mehr lädt, zeigt nur der EU-Programmcode (`retail_code.py`,
    Sscrn) – lokal; ebenso der Vergleich mit den Debug-Discs. **Behoben (Patch 0144, Fehler des Prototyps):** die
    EU-Fassung lädt ohne Jacke (Kostüm 1–3) Teil 0x10 dazu (Messer in der Scheide am Brustgurt), wie das Spiel; in
    den Posen des Inventars liegt es fast ganz hinter Arm und Waffe (am PC gesehen). Auf dem Retroid bestätigt
    (Projektinhaber, 29.10.2026: Messer in der Brusttasche).
19. **Handcannon: Feuerkraft „0.00“, Munition wird mit dem Exklusiv-Tuning nicht unendlich** (Nutzerbericht 30.10.2026;
    beigelegtes Protokoll aus 0.91, nur Titelmenü). Befund im Code: Feuerkraft = `WeaponLevelTbl[Waffe][Stufe-1] /
    WeaponLevelTbl[2][0]` (`item.cpp getPowerRatio`); die Zeile des Handcannon (Waffe 0x0F, Gegenstand 0x37) ist im
    Prototyp ein Platzhalter mit 100.0 auf allen Stufen (`em_dm_val.cpp`); Händler und Untersuchen zeigen den Wert ×10
    mit drei Ziffern (`ss_shop.cpp`, `examine.cpp`), aus 1000 wird „0.00“. Magazin: `wep_info` 0x37
    {3,4,5,6,8,10,0xFFFF} (0xFFFF = unendlich erst auf Stufe 7), `wep_level_info` 0x37 {6,1,3,6} (Magazin höchstens
    Stufe 6), der Händler hebt mit `level_ext_sw500` {7,1,3,6} nur die Feuerkraft auf 7. Die Werte und die
    Exklusiv-Regel der Verkaufsfassung zeigt nur der EU-Programmcode (`retail_code.py`: `WeaponLevelTbl`, `wep_info`,
    `wep_level_info`, `level_ext_sw500`, der Kauf des Exklusiv-Tunings in `ss_shop.cpp`) – lokal; Behebung als Patch
    „a bug of the prototype“. **Zweiter Bericht (0.95.1, Debug-Discs, 29.10.2026 17:23) bestätigt:** Magazin bis Stufe
    6, Feuerkraft 1→6 und dann das Exklusiv-Tuning als Feuerkraft-Stufe 7 („levels 7/1/3/6“, 200000 Pesetas); das
    Magazin bleibt auf Stufe 6, also nicht unendlich.
    **Behoben (Patch 0146, Fehler des Prototyps, beide Disc-Arten; Abschnitt „Handcannon und Schuss-Ton“).** Am PC
    geprüft; **Gerätetest und Händler-Bildschirm im Bild offen**.
20. **Ton beim Schuss stottert einmal** (Nutzerbericht 30.10.2026, Infinix X6711, 0.95.1-beta, Debug-Discs, Pistole
    in r20a): Abschuss, kurze Pause, Rest des Schusses. Im Bericht keine Aussetzer beim Schießen (`ran dry` nur beim
    Raumwechsel), Kompressor nie aktiv, DSP weit unter Budget, 30 Bilder/s gleichmäßig: die Lücke liegt im Mischen
    selbst (z. B. Adresswechsel einer Stimme auf einen zweiten Teil des Klangs, Schleife/ADPCM-Zustand am Übergang).
    Nutzer: nur manchmal, nicht bei jedem Schuss.
    **Nächster Schritt lokal:** am PC mit Disc-Daten schießen, `RE4_AUDIO_WAV_SECONDS` aufnehmen, Lücke im WAV finden,
    die Stimme dazu (`dsp.cpp`, PB-Adressen) verfolgen.
    **Am PC untersucht (29.10.2026 nachts, Abschnitt „Handcannon und Schuss-Ton“): im Ton des Kerns keine Lücke** (Pistole
    und Schrotflinte, Debug- und EU-Discs, 32 und 64 bit, Schnelllauf und Echtzeit; 64 bit und mit DSP-Protokoll
    bitgleich). Sehr wahrscheinlich lief die Android-Ausgabe (AudioTrack) leer; das stand bisher nur in logcat. Jetzt
    auch im Kernprotokoll („sound: the Android output ran out …“). **Offen:** Bericht des Nutzers mit dieser Zeile.
21. **r214 vor dem Uhrturm versperrt (EU-Discs)** – behoben (Patch 0145), am PC geprüft; **Gerätetest offen**.
22. **FPS-Einbrüche Moto e13** und **Ruckler beim Schießen / in Speicherräumen** – Befund und Programmliste ergänzt
    (Abschnitt „Berichte für 0.95.2“); Vorschlag zum Vorladen offen.
23. **Ashley durch Rüstungsstatuen (r20e)** – EU-Abschaltung ergänzt (Patch 0145); mit Debug-Discs Stand der
    Prototyp-Daten. Disc-Art des Nutzers offen.
24. **Japanische und koreanische Discs** – analysiert (`retail-jp-plan.md`), Umsetzung J0–J7 offen.
25. **Leistung 0.95.1 (Redmi 15C, Helio G81, EU-Discs, 29.10.2026 17:51, Nutzertext und Protokollanfang):** der Nutzer
    sieht deutlich weniger Ruckler (Zeichen-Thread 40–50 % statt 62–72 % eines Bildstücks, Leerlauf des Kerns in r101
    3–11 ms statt 0,7–2,2 ms). **Aber:** die Eckpunkt-Threads kamen gar nicht auf die langsamen Kerne – „cores: the 3
    vertex threads left to Android (no slow core allowed to the app)“: `PutOnSlowCores` nimmt nur Kerne, die der
    Thread schon darf, und die drei Threads durften zu dem Zeitpunkt nur 6 und 7 (woher die Einschränkung kommt, ist
    offen). Die Verbesserung kommt also nicht von dieser Maßnahme. Ob sie wirksam gemacht werden soll (langsame Kerne
    ohne die Schnittmenge mit der jetzigen Zuordnung setzen), entscheidet der Projektinhaber.
    Dazu meldet der Nutzer wieder „heap: no room for 0x214b40 bytes in heap 4“ in r101 (17:54:24, vor einem Bildstück mit
    923 Zeichnungen und 43 ms); die Zeile mit Datei und Zeilennummer fehlt im eingefügten Text. Solche Meldungen aus
    `datactrl.cpp`/`cDataSwap.cpp` sind ein vorgesehener Ausweg des Spiels (Daten bleiben im ARAM, vgl. M12-Abschnitt
    „Weitere Meldungen ohne Fehler“); 0x214b40 passt zu einer Ereignisdatei. **Offen:** der vollständige Bericht.
26. **Dimensity 700: Einbrüche auf 20 FPS seit 0.95.1** (Nutzertext, 30.10.2026, ohne Protokoll). Verdacht zuerst
    `PutOnSlowCores` (zwei schnelle Kerne); nach Punkt 25 greift die Maßnahme dort vermutlich ebenso wenig. Nutzer
    gefragt (Force Performance aus, Bericht direkt nach einem Einbruch).

### Branch `bughunt` (30.10.2026, Cloud): `golden-master` in `master` übernommen, drei Nutzermeldungen

`master` enthält seit 30.10.2026 den ganzen Stand von `golden-master` (Merge `02b1c54`, README in der Fassung des
Projektinhabers); `bughunt` zweigt davon ab.

27. **EU-Spielstände aus Dolphin lassen sich nicht importieren** („should be G4BE08, not G4BP08“). Ursache: der
    `.gci`-Import (`runtime/gci.cpp`, `kGame`/`kCompany`) nimmt nur `G4BE08` an; das Spiel sieht auch mit EU-Discs
    `G4BE08` (gemeinsame Speicherkarte). Ein Spielstand der EU-Verkaufsfassung ist mit dem Spielcode des Prototyps
    nur verwendbar, wenn Version (Debug 0x116, `kSaveVersion`) und Aufbau (GLOBAL_WK-Block 0x36F8, Gegenstände 0x1204,
    Räume, Händler; `save_layout.inc`) gleich sind.
    **Behoben (lokal, 30.10.2026):** Die Karte des Nutzers (`Card.A.zip`) ist ein Dolphin-GCI-Ordner: 20 Spielstände
    `08-G4BP-bh4_data00..19.gci` (je 8 Blöcke) und `08-G4BP-bh4_system.gci` (1 Block), alle `G4BP08`, Version 0x116,
    beide Prüfsummen an den Stellen des Prototyps richtig. Der EU-Code schreibt sie wie der Prototyp: `makeSaveData`
    (EU 0x80009bac) mit denselben Stellen 0x2034/0x572C/0x6930/0xE7D0/0xE7D4/0xEAF8 und Größen 0x36F8/0x1204, Magie
    0x116; `cItemMgr::save`, `Merchant save`, `cRoomData::save/load` Wort für Wort gleich, `SscrnDataSize` 4,
    `MerchantDataSize` 0x304. Einziger Unterschied: das GLOBAL_WK der EU hält den Block 0xC Byte später (0x4F8C statt
    0x4F80, `cGameSave::save/load` EU 0x8011a064/0x80119f28); im Block selbst liegt jeder Zugriff über `pG` 0xC weiter
    (alle 119 benutzten Stellen 0x4F84..0x8678, Zählung über beide `main.dol`), der Inhalt hat also denselben Aufbau.
    Die Systemdatei ist gleich groß (CRC bei 0x1E78); ihre Sprache ersetzt die App ohnehin (`game_language.cpp`).
    `runtime/gci.cpp re4_gci_import` nimmt jetzt `G4BP08` an und legt die Datei als `G4BE08` ab (Protokollzeile
    „European G4BP08 as G4BE08“); andere Kennungen bleiben abgelehnt. Kartentest erweitert (EU-Datei über eine
    vorhandene, danach `08-G4BE-…`). **Am PC geprüft (i686, WSL):** alle 21 Dateien importiert, `memcard_list.py` zeigt
    sie heil (Räume r103..r333, Kapitel 0..18); Export und erneuter Vergleich: jede Datei Byte für Byte gleich bis auf
    Kennung und Blocknummer. Mit EU-Discs geladen: Datei 5 (r10e), 7 (r204), 13 (r22a), 19 (r326) laufen im
    gespeicherten Raum weiter, Leon frei, „Retry from a continue point“ läuft wieder an, Inventar mit Waffen und Stufen;
    Datei 1 ist ein Spielstand nach dem Durchspielen (Art 3) und startet wie vorgesehen die neue Runde in r120.
    `--self-test`, `--check-modules`, `--runtime-test`, `--core-test` (mit Kartentest) 0 Fehler. Debug-Discs:
    Kartenprüfung (`whole_r100_check.sh --card-only`) A und B OK; dieser Debug-Spielstand als `.gci` exportiert, auf
    eine neue Karte importiert, lädt wieder in r100 (mit „Retry“); EU-Datei 19 läuft auch mit den Debug-Discs in r326.
    **Dolphin-Kartendatei (`.raw`/`.gcp`, Wunsch des Projektinhabers 30.10.2026):** der Import im App-Menü erkennt eine
    ganze Karte (`DolphinCard.java`: 512 KB..16 MB, Prüfsumme des Kopfs, Größe in Mbit passt), nimmt die gültige, neuere
    Kopie von Verzeichnis und FAT (wie das SDK) und reicht jede Datei von `G4BE08`/`G4BP08` als `.gci` an den
    bisherigen Import; Dateien anderer Spiele bleiben liegen (Protokollzeile „memory card with N files of this game“),
    eine Karte ohne Spielstände dieses Spiels meldet das. Menüpunkt „Import Savegames (.gci, .raw)“ in allen
    Sprachen. Test `DolphinCardTest` (Karte mit verstreuten Blöcken, neuere Verzeichniskopie kaputt, Kette aus der
    Karte, keine Karte) 4/4 mit JUnit am PC. Mit einer aus den 21 Nutzerdateien gebauten 2-MB-Karte (Blöcke verstreut,
    eine fremde Datei dabei): 21 Dateien herausgelöst, Byte für Byte gleich bis auf die Startblock-Nummer, importiert und
    Datei 18 (r31d) mit EU-Discs geladen. Nicht geprüft: eine von Dolphin selbst angelegte `.raw`.
    **ZIP (Wunsch des Projektinhabers 30.10.2026):** der Import nimmt auch eine ZIP-Datei (`SaveImport.java`): darin
    jede `.gci` und jede Kartendatei, andere Dateien (`__MACOSX/`, `.DS_Store`, Texte) bleiben liegen; je Datei
    höchstens 16 MB, zusammen 64 MB. Menüpunkt „Import Savegames (.gci, .zip, .raw)“ in allen Sprachen. Tests
    `SaveImportTest` 5/5 und `DolphinCardTest` 4/4 (JUnit am PC); eine ZIP mit dem GCI-Ordner des Nutzers („Card A/…“)
    und eine ZIP mit der gebauten `.raw` geben je alle 21 Dateien unverändert heraus.
    **Offen:** Kurztest auf dem Retroid (Import über das App-Menü: `.gci`, `.zip`, `.raw`).
28. **EU, leichter Modus: die Schrotflinte gehört von Anfang an ins Inventar** (Nutzermeldung). Der Prototyp gibt im
    neuen Spiel nur den Startsatz (`item.cpp cItemMgr::gameInit` → `set_game(0)`), ohne Abfrage der Schwierigkeit.
    **Offen, lokal:** die EU-Fassung von `gameInit`/`set_game` lesen (Gegenstand, Munition, Stufen, Abfrage
    `game_country`/Easy wie Patch 0140), als Patch nur mit EU-Discs; prüfen, dass r101 (Haus des Dr. Salvador) die
    Schrotflinte dann nicht ein zweites Mal hinlegt (EU-Code von r101/Gegenstandsliste).
    **Behoben (Patch 0148, EU- und JP-Discs; Abschnitt „Branch bughunt lokal“), am PC geprüft; Gerätetest offen.**
29. **Verdugo-Kampf (r21d) mit Einbrüchen** (Nutzerbericht 29.10.2026 21:01–21:50, Xiaomi 11 Lite 5G NE, Adreno 642L,
    0.95.2, EU-Discs, Auflösung 2,5×, FXAA). Der Kern hält 30 Bilder/s (33,4 ms, 17–21 ms Leerlauf), der Zeichen-Thread
    15–21 ms. **Die GPU ist der Engpass:** in r21d im Mittel 28,2 ms je Bild (Spitze 79,9 ms), 28 von 64 Messungen über
    33 ms; in r229/r223/r225 davor und danach 10,8–15,7 ms, keine über 33 ms. EFB-Kopien in r21d wie in r223 (8,7 je
    Bild), es ist also Füllarbeit des Raums (Nebel, Flüssigstickstoff, Wasser) bei 2,5×. **Rat an den Nutzer:**
    Auflösung „Dynamisch“ oder 1,5×/2×. **Für eine Optimierung:** Grafikaufnahme (R3) im Kampf, die teuren Zeichnungen
    suchen (große durchsichtige Flächen, Kopien in voller Auflösung).
    **Stand 30.10.2026 (lokal):** am PC nur bis zum Debug-Start von r21d (Höhlengang, Verdugo aktiv, kein
    Kampfbild mit Flüssigstickstoff); Zählung dort: 44 Nebel-Sprites der Raumtabelle, Verdugos eigene Effekte (Besitzer
    37) und Funken/Tropfen. **Offen:** Grafikaufnahme (R3) im Kampf und GPU-Messung auf dem Retroid (war nicht verbunden).
30. **„Potato-Mode“ (Entscheidung des Projektinhabers, 30.10.2026):** ein Schalter im Ingame-Menü auf der Seite
    „Performance“ (`CoreMenu.performance()`), der nur die teuren Umgebungseffekte weglässt: Nebelschwaden, Regen,
    Wasserspritzer, Dampf, die Wolke des Flüssigstickstoffs (Verdugo) und die Effekte, die das Bild kopieren und
    verzerrt darüberlegen (Hitzeflimmern, Wasser). Bleiben: Mündungsfeuer, Blut, Explosionen, Feuer, Dynamit,
    Treffer, Hinweise und der Entfernungsnebel (GX-Fog, kostet kaum etwas, verdeckt die Raumränder). Umsetzung in der
    Effektverwaltung des Spiels nach Effektart (Port-Laufzeit/Patch mit Schalter aus der App), nicht im Renderer.
    Plan und Effektliste: lokale Sitzung mit Disc-Daten (Bildvergleich, GPU-Zeit in r21d vorher/nachher).
    **Wunsch des Projektinhabers (30.10.2026):** als Zwischenüberschrift nur „Potato-Modus“, darunter zwei Schalter
    „Wettereffekte“ (Nebelschwaden, Regen, Wasserspritzer) und „Spezialeffekte“ (Dampf, Flüssigstickstoff,
    Hitzeflimmern, Wasser), die Erklärungen nur als Fußtext. **Umgesetzt (Patch 0149, `runtime/potato.cpp`, App),
    am PC geprüft; Gerätetest und Messung offen.**
31. **Geisterbilder mit 60 FPS: Farbrätsel r307, Feuer, sich auflösende Gegner** (Nutzermeldung mit Bildschirmfoto,
    30.10.2026) – behoben im Renderer (Abschnitt „Geisterbilder in den Zwischenbildern“), am PC mit Bildfolgen geprüft;
    **Gerätetest offen**.

## Geisterbilder in den Zwischenbildern (30.10.2026, lokal, Branch `golden-master`, WSL-Baum `~/re4-ghost/RE4`)

- **Meldung:** vivo V2426 (Mali-G715-Immortalis MC11, Android 16, 64 bit), **0.95.2-beta**, Debug-Discs; Einstellungen
  laut Protokoll: Auflösung 4× (`scaleHalves=8`), MSAA aus (`samples=1`), **60 FPS an** (`smooth60`), Frame-Skip aus
  (schließt sich mit 60 FPS aus), FXAA an, Tiefenschärfe an, Anisotropie 16, Force Performance an; Potato-Modus gab es
  in 0.95.2 noch nicht (keine Zeile im Protokoll). Spielstand in r301 geladen, danach r303–r306. Das Rätsel („Turn
  which color panel?“) steht in **r307** (Stage 3, Insel): neun Teile, jede Farbwahl dreht die Teile einer Farbe
  (`r307_turnPiece`); jedes Teil ist ein Raumeffekt (`EstSet`, Effekte 0x1A–0x29, Teilchen aus dem Effekt-Pool). Auf dem
  Foto: blasse, gestauchte Dreiecke zwischen zwei Feldern, Teile fehlen. Dazu laut Nutzer dasselbe bei Feuer und bei
  Gegnern, die sich nach dem Tod auflösen.
- **Neu: Bildfolgen am PC** (`core_api.cpp`, `RE4_GX_DUMP_SERIES=N`): jedes gewählte Bild (`RE4_GX_DUMP_FREE`/`_AT`)
  beginnt eine Folge aus N aufeinanderfolgenden Bildstücken wie die R3-Serie des Geräts; `gx_chunk_render` zeichnet sie
  auf einem Renderer und mit `RE4_GX_INTERP=1` die Zwischenbilder dazu. Bisher gab es solche Folgen nur vom Gerät. Das
  Rätsel am PC: Debug-Start r307, `RE4_TEST_PLACE="free+30:area=5"`, `RE4_PAD_FREE_SCRIPT="A@60 A@160 A@300 A@400"`
  (untersuchen, ja, Rot drehen), Folgen ab `free+340` und `free+392`.
- **Ursache (nachgestellt):** Im Ruhezustand sind die Zwischenbilder richtig. Beim Drehen setzt das Spiel neue Effekte
  (Funkeln, rotes Aufleuchten); deren Teilchen-Plätze haben im Bild davor nichts gezeichnet. `BlendDraw` fand sie also
  nicht über ihre Marke und **riet** über Aussehen und Abstand (`MatchDraw`). Dabei nahm es das Gegenstück eines
  vorhandenen Teils des Rätsels; dessen eigene Zeichnung fand ihr Gegenstück dann belegt, riet ihrerseits und nahm das
  des nächsten Teils, und so weiter. Die Dreiecke wurden halb zum Nachbarfeld gezeichnet, ihre Drehung linear gemischt
  (gestaucht, bis zum flachen Streifen), Farbe und Deckkraft gemischt (blass); Bild 1204 der Folge zeigt genau das Foto.
  Gemessen: 15 Zeichnungen mit Marke gerieten in diese Kette, nur 378 statt 393 über ihr Objekt.
- **Behebung (`gx_gl.cpp`, nur Renderer, Kern unverändert):** (1) ein Teilchen (Marke mit Lebenszähler), dessen Platz im
  Bild davor nichts gezeichnet hat, ist neu und steht wie im nächsten Bild (Zähler „neu belegt“); (2) beim Raten bleiben
  die Gegenstücke von Objekten tabu, die im nächsten Bild gleich oft zeichnen (`recReserved`): sie gehören deren eigenen
  Zeichnungen. Das Raten bleibt für Zeichnungen ohne Marke und für Objekte mit wechselnder Zahl Vierecke.
- **Prüfungen am PC (Debug-Discs, alter gegen neuen Renderer, je Folge 40 Bilder, Zwischenbild mit Anteil 0,5 und 0):**
  r307 Rätsel: Zwischenbild beim Drehen ohne Geister, Ruhezustand unverändert, alle echten Bilder byte-gleich; r304
  Gegner mit One-Hit-Kill erschossen (Tod bis Ausblenden): neuer Mündungsrauch steht gleich an seinem Platz statt
  geschrumpft geraten, sonst gleich (der Körper lag hinter dem Gitter, das Auflösen selbst war kaum im Bild); Feuer
  r224 (Lava) und r226 (Fackeln) sowie r301/r305 (Insel): Zwischenbilder alt und neu gleich (r224: 3 Bilder mit
  höchstens 42 Bildpunkten Unterschied). Test `TestInterpolationSparkle` (`gx_gl_check` 0 Fehler; Gegenprobe mit dem
  alten `gx_gl.cpp`: 2 Fehler). Kern i686-O2 mit der Folgen-Option gebaut, 0 Fehler.
- **Nicht getestet:** auf dem Gerät; Feuer und Auflösen in genau den Szenen des Nutzers (keine Aufnahme davon).

## Ingame-Menü: Seite Steuerung neu, Zielgeschwindigkeit, Update-Knopf (30.10.2026, lokal, Branch `golden-master`)

Wünsche des Projektinhabers (30.10.2026):

1. **Seite Steuerung umgebaut** (`CoreMenu.controls`): Abschnitt „Komfortsteuerung“ nur noch mit „Alternative
   Steuerung“, „Messer per Wischen“, „L und R einrasten“, „B einrasten“, direkt darunter nebeneinander der neue Schalter
   „Vertikal umkehren“ und der Regler „Zielgeschwindigkeit“ (0,5×–3×, Schritte 0,25, Standard 1×), dann die drei
   Umschalter für Touch-Zielen und die Fußtexte. Neuer Abschnitt „Bewegungssteuerung“ darunter: „Gyro-Zielen“ mit
   „Gyro-Empfindlichkeit“, „Vertikal umkehren“ / „Horizontal umkehren“ des Gyros (wie bisher). Zeile Schalter + Regler
   als gemeinsamer Baustein `switchWithSlider` (Schalter mittig in der Zeile, wie beim Gyro seit 27.10.).
2. **„Vertikal umkehren“ (Touch und Controller, nicht Gyro)** ist die eingebaute Spieloption `CFG_AIM_REVERSE`
   (`pSys->Config_flg`, im Spiel unter Optionen/Controller; `PlWepLockCtrl`, Zielfernrohr, Fernglas, Harpune lesen
   sie). Neu `runtime/aim_options.cpp`: die App schickt `re4_core_set_aim` (Bit 0 umkehren, ab Bit 8 Geschwindigkeit in
   Prozent; auch über den Kernprozess, `kSetAim = 35`) beim Start und bei jeder Änderung; ein Bild-Haken setzt die
   Option im nächsten Spielbild, `re4_game_language_after_card` setzt sie nach dem Lesen der Systemdatei wieder auf
   den Wert der App (die App gewinnt wie bei der Sprache; eine Änderung im Optionsmenü des Spiels gilt bis zum
   nächsten Start/Laden). Einstellungen `aimInvertVertical`, `aimSpeed` (`core_input`).
3. **„Zielgeschwindigkeit“**: eine solche Einstellung gab es für Stick/Touch noch nicht (nur Gyro und Maus).
   **Patch 0151** (`pl_wep.cpp`, `cam_extra.cpp`): `PlWepLockCtrl` (Waffen, Messerhaltung, Bogen: Neigung `m3r`,
   Hüftdrehung `m_Fwork0`, Körperdrehung an der Grenze) und `CameraScope::move` (Zielfernrohr) multiplizieren die
   Stick-Werte mit `re4_aim_speed()`; Steuerkreuz-Schritte unverändert; bei 1× ändert sich nichts. Die Wischbewegung
   beim Touch-Zielen „Touchfläche“ (`CoreBridge.aimTurn`) nimmt Geschwindigkeit und Umkehren in der App mit (die
   Maus nicht, sie hat ihren eigenen Regler).
4. **Update-Knopf im Menükopf** links neben „Zurück zum Spiel“ (`AppUpdate.java`, `MenuLayout.Header` bekommt die
   beiden Knöpfe als eine Reihe): beim Öffnen des Menüs fragt die App einmal pro Lauf
   `api.github.com/repos/Andiweli/CubeEvil4-Android/releases/latest` (Tag = Version, die eine `.apk`). Gleiche (oder
   neuere) Version: grün „Aktuell“, nicht drückbar; ältere: rot „Update“; der Knopf lädt die APK mit Fortschritt in
   eine Sitzung des Paket-Installers von Android, Android fragt vor dem Installieren. Fehlt die Erlaubnis „Apps
   installieren“, öffnet sich die Einstellungsseite dazu. Ohne Internet bleibt der Knopf unsichtbar. Neue Rechte
   `INTERNET`, `REQUEST_INSTALL_PACKAGES`. Versionsvergleich `AppUpdate.compare` (Zahlen, dann Zusatz). Hinweis: eine
   selbst gebaute APK (anderer Signaturschlüssel als das Release) kann Android nicht überschreiben, dann meldet der
   Installer einen Fehler.

Texte in zehn Sprachen (`check_translations.py` ok, prüft jetzt auch `AppUpdate.java`).
**Geprüft (PC, 30.10.2026):** `:app:compileCoreDebugJavaWithJavac`, `:app:externalNativeBuildCoreDebug`, alle 8
Tests von `CoreMenuLayoutTest` (alle Sprachen, Schriftgrößen, Breiten, beide Knopfzustände; neu: Reihenfolge der
Steuerungsseite, Update-Knopf links neben „Zurück zum Spiel“, Versionsvergleich). Der Test der Leistungsseite fand
die FPS-Knöpfe noch über feste Positionen (seit dem Potato-Modus falsch), jetzt über ihre Beschriftung. Die geänderten
Kern-Dateien (Patch 0151 angewandt nach 0079, `aim_options.cpp`, `core_api.cpp`, `game_language.cpp`,
`core_ipc_client.cpp`, `coreproc.cpp`) mit den Schaltern des i686-Builds einzeln übersetzt: fehlerfrei. **Nicht
getestet:** voller Kern-Build und Spiel mit Disc-Daten, Gerätetest (Zielgeschwindigkeit, Umkehren, Update-Knopf).

**Nachtrag 30.10.2026 (Wünsche des Projektinhabers nach dem Gerätetest, Bildschirmfoto Retroid):** auf der Seite
Steuerung steht „Bewegungssteuerung“ jetzt über „Komfortsteuerung“; „Tastenbelegung“ bleibt unten, unter der eigenen
Überschrift „Controller, Tastatur und Maus“ (zehn Sprachen). **Ein Abstand für alle Knopfreihen:** 14 dp wie um die
Reglerzeile (`MenuLayout.GAP` = 7 dp Rand oben und unten an jedem Knopf in `Buttons` und `Keys`; die Reihen mit
Beschriftung links verlieren ihre 3 dp Innenabstand, Beschriftungen 48 + 14 dp hoch, also wie bisher 14 dp; die
Speichern/Laden-Knöpfe behalten ihre 12 dp Einzug). Die Zeilen Schalter + Regler sind jetzt so hoch wie eine
Knopfreihe: der Regler nimmt die Höhe, die sein Titel übrig lässt (mindestens 32 dp). Waagerecht bleiben 6 dp
zwischen zwei Knöpfen. `CoreMenuLayoutTest` prüft Reihenfolge, 14 dp zwischen den Knopfreihen und die Höhe der
Reglerzeilen (8/8 grün; die Schrift der Testumgebung ist etwa doppelt so hoch wie auf dem Gerät, dort wird die
Reglerzeile daher höher). Gerätetest offen.

## Vorfilm-Untertitel mit JP-/KR-Discs (30.10.2026, lokal, Branch `golden-master`, WSL-Baum `~/re4-jpsub/RE4`)

Nutzermeldung mit zwei Bildschirmfotos (koreanische Discs, `movie/opening.sfd` vor der Autofahrt): unten japanische
Kana mit anderen Zeichen gemischt („ミここ向工“) bzw. sinnloses Hangul. **Ursache:** Patch 0136 baut das EU-`cSofdec`
nach, ließ aber dessen japanischen Zweig weg: bei Sprache 0 macht `initApp` (EU 0x80161210, JP 0x80160b50) die Schrift
aus `etc/movie.das` (Eintrag 5, 28 × 28, dieselbe wie `font/movie_j.fnt`) zur Schrift 2, mit der die Untertitel
gezeichnet werden; `finishMovie` (JP 0x801610b0) setzt danach die Stage-Schrift (`pG->pStFnt`) zurück. Ohne das
zeichnete der Port die Filmtexte mit der Stage-Schrift (andere Zeichen an denselben Nummern). Die koreanische
Übersetzung hat eine eigene Filmschrift (Hangul). Gelesen mit `retail_code.py`; der koreanische `main.dol` ist hier der
japanische. **Patch 0150** (`sofdec.cpp`). Die englischen Zeilen oben im Bild („1998…“, „I'll never forget it …“)
stehen im Filmbild selbst (Bild ohne Untertitel: nur das Filmbild), in allen Fassungen; die Untertitel übersetzen sie.

- **Geprüft (PC, i686-O2, Debug-Start r120, GX-Bilder mit `gx_chunk_render`):** Korea „1998년“, „발단은 아클레이
  산지의 엽기 살인사건이었다“; Japan „1998年“, „発端はアークレイ山地での猟奇殺人事件だった“; EU Deutsch unverändert („Das war
  das Jahr der furchtbaren Morde in den Arklay Mountains.“). Japan weiter bis r100: die Untertitel der Autofahrt
  (Stage-/Ereignisschrift) richtig, kein Fehler. `runtime-test` 0 Fehler. Debug- und US-Discs laden `etc/movie.das`
  nicht (Code unverändert), EU hat nie Sprache 0.
- **Nicht geprüft:** Gerät.
- **`master` = `golden-master`** (Wunsch des Projektinhabers, 30.10.2026: `master` hat den kompletten Stand): `master`
  auf diesen Stand vorgezogen (ohne Zusammenführen, `master` lag ganz in `golden-master`; dazu Potato-Modus 0149,
  EU-Easy-Schrotflinte 0148, Patch 0150). Der OUYA-Branch `ouya-discarded` bleibt bis auf Weiteres getrennt, nichts
  davon in `master`.

## Nutzermeldungen 30.10.2026 spätabends (lokal, `golden-master`)

- **r222 (Drachenhalle über der Lava, Debug-Discs, Bericht 08:06): „die Ketten der Plattformen gehen nicht kaputt“.**
  Jeder Drache hängt an zwei Treffer-Kästen (`cEmHit` unter den Objekten 0x16/0x17, 0xD/0xE, 0x12/0x13); einer auf 0
  stürzt ihn in die Lava. Der Prototyp gibt jedem 7000 Lebenspunkte, die EU-Fassung (`R222Init`, `st2_0.rel` 0x1409c)
  2000; `Hit` und `dragon_down_ck` sind Wort für Wort gleich. **Patch 0155** (Fehler des Prototyps, alle Discs). Gebaut;
  im Spiel am PC nicht nachgeschossen, **Gerät offen**.
- **Mercenaries, Zeitgeber:** der Ton der letzten 30 s ist in Debug, EU und US `SndCall(6, 0x78)` / Ende `0x7A`
  (`MercSysMoveMain`, Vergleich `cmplwi 899`); die EU-Schleife ruft zusätzlich eine Funktion, wenn `Status_flg[3]` Bit
  0x00100000 gesetzt ist, und ihr `MercSysWork` ist 4 Bytes anders aufgeteilt. Ursache von „Geld-Ton“ und doppelter
  Uhr noch **offen** (Disc-Art des Nutzers unbekannt, kein Bericht).
- **Super Salvador:** die Kettensäge startet in Debug und EU mit demselben Ton (`SndCall(6, 0x4C)`, `C_SawStart`
  158 von 159 Befehlen gleich); **offen** ohne Bericht. **Assignment Ada, Gegner durch die Tür nach dem Tritt:**
  **offen** (kein Bericht). **„Vibration“ im Ton gegenüber Dolphin:** die DSP-Nachbildung rechnet Lautstärke-Rampen je
  Probe und kubisch statt mit den ROM-Filtern; **offen**, bis eine Aufnahme zeigt, wo es klingt.
- **Seite Steuerung: „Auto-Zielen“ und „Immer laufen“ (Wunsch des Projektinhabers).** Reihe unter „Latch L & R“ /
  „Latch B“, vor „Vertikal umkehren“ mit der Zielgeschwindigkeit; Hinweistexte in allen zehn Sprachen. „Auto-Zielen“
  ist die eingebaute Zielhilfe des Spiels (`CFG_LOCK_ON`, bisher nur im Debug-Menü „AUTO_LOCK_ON“): beim Anlegen
  wählt `cPlWep::lockInit` den besten Gegner vorn (`SearchLockEm`, 50°-Kegel), `PlSetLockPitch` und `lockMove`/
  `PlWepAutoTrack` drehen die Waffe 10 Bilder lang dorthin; die App setzt die Option wie „Vertikal umkehren“
  (`runtime/aim_options.cpp`, Bit 1). „Immer laufen“ (Bit 2): **Patch 0156** – die vier Lauf-Abfragen in
  `player.cpp` (`KEY_B` gehalten) über `re4_run_held`: Leon läuft beim Bewegen, mit gehaltener Lauftaste geht er;
  B behält Menüs, Abbrechen und die schnelle Drehung. Testhilfe `RE4_TEST_AIM=bits`. **Geprüft am PC** (i686,
  Debug-Discs): r100 nach vorn ohne B – ohne Schalter gut 1000 Einheiten je 30 Bilder (Gehen), mit „Immer laufen“
  gut 3400 (Laufen), mit gehaltenem B wieder 1000; r10a, Leon 5000 neben einem Ganado, 21° daneben – Anlegen ohne
  Schalter: Blick bleibt 1,20; mit „Auto-Zielen“ springt er auf 1,61 (zum Ganado). `CoreMenuLayoutTest` 8/8, App-Tests
  17/17. Gerät offen.

## US-Daten gegen die neueren EU-Patches, Chicago Typewriter, Nutzermeldungen (30.10.2026 nachts, lokal, `golden-master`)

Gearbeitet direkt in `RE4-Fixed` (Wunsch des Projektinhabers: golden-master), gebaut im WSL-Baum `~/re4-bughunt/RE4`.

- **Neuere EU-Patches mit den US-Discs** (am PC, i686, Debug-Start mit `RE4_TRACE_AT=1`, EU/US/Debug je r20b, r203,
  r211, r214, r20e verglichen): r20b, r203, r211 und r214 haben mit den US-Discs die Bereiche der Debug-Daten (keine
  der EU-Wände 0x1C, 6, 0x8E, 0x17–0x1B) – Patches 0138, 0141 und der r214-Teil von 0145 bleiben ohne US. r20e hat mit
  den US-Discs die vier Statuen-Wände 0x13–0x16 wie EU, und ihr `r20e_startArmor` (`st2_1.rel` 0x1a0b4) schaltet sie
  ab wie der EU-Code: **Patch 0145 (r20e-Teil) jetzt mit allen Verkaufsdiscs.** Die US-Discs haben keinen Easy-Modus
  (kein `SS/Esy`): 0139–0141 und 0148 betreffen sie nicht; `game_country` bleibt dort 1 (über Easy, wie 0140 es für EU
  setzt). **Patch 0142** („EMPTY“-Schild) jetzt auch mit US-Discs (Sprache 1 → Bild 0, Englisch; ein Schild mit nur
  einem Bild bleibt wie bisher), weiter nicht mit JP.
- **Chicago Typewriter nach Assignment Ada (Nutzermeldung):** `r40e` `gameResult` setzt im Debug-Code
  `EXT_GET_SW500` (Handcannon, das die Mercenaries freischalten), nichts setzt `EXT_GET_TOMPSON`, das
  `MerchantRoomInit` für die Chicago Typewriter abfragt. EU (`st4_0.rel` 0xECF8) und US (0xEB50) prüfen und setzen
  dort Bit 3 (`oris 0x1000`). **Patch 0154** (Fehler des Prototyps, alle Discs). Gebaut; das Ende von Assignment Ada
  am PC nicht nachgespielt, **Gerät offen**. (Der Endfilm-Wechsel adaend_m/adaend_c hängt in der Verkaufsfassung an
  einem anderen Merkmal als `EXT_ASHLEY_ARMOR`; nicht übernommen, er wechselt weiter bei jedem Durchgang.)
- **Russischer Text `ingame_resolution_line_auto`:** die Übersetzung des Nutzers übernommen.
- **„heap: no room for 0x214b40 bytes in heap 4“ in r101 (Nutzermeldung zu 0.96.0, wie Punkt 25):** der volle Ablauf
  steht im Bericht vom 28.10.2026 05:24 (EU-Discs): nach dem Eingangsereignis `r101s00` will `DataCtrl` die Szene
  `evd/r101s21.evd` (2,1 MB, das Haus-Ereignis) in den Raum-Heap legen (`datactrl.cpp(452)`, Flag 0 = Versuch mit
  Ausweg), findet keinen Platz und nimmt den vorgesehenen Weg über das ARAM („set ARAM_LOAD“, „Trans ARAM“, „check
  ARAM_OK“). Das ist Spielcode mit derselben Speicheraufteilung wie auf dem GameCube; mit dem Debug-Start geht die Szene
  von vornherein ins ARAM. Das Lesen läuft im Takt des Spiels (ein Leseschritt je Bild) und dauerte auf dem Gerät
  3,9 s, ohne das Spiel anzuhalten. Die Umwandlung der Datei braucht am PC unter 1 ms (alle Dateien in r101 höchstens
  1 ms, gemessen mit der neuen Protokollzeile). Heap 4 zu vergrößern hieße, die feste Speicheraufteilung des Spiels zu
  ändern – nicht nötig, die Meldung ist kein Fehler. Der Ruckler an der Stelle (Ende des Eingangsereignisses, die
  Dorfbewohner erscheinen) hat damit eine andere Ursache; das „max 2141.9 ms“ der GX-Zeile ist das Maximum seit dem
  Start, nicht dieser Augenblick. **Neu:** `formats` schreibt jede Umwandlung ab 50 ms ins Kernprotokoll („took N ms to
  convert“, höchstens 50 Zeilen). **Offen:** ein Bericht direkt nach dem Ruckler.
  Danach (Wunsch des Projektinhabers): die Zeile für Versuche mit Ausweg (Flag 0) lautet jetzt „heap: heap 4 full for
  0x… bytes (datactrl.cpp(452)): the game loads it through ARAM instead, as on the GameCube; not an error“; echte
  Fehlschläge (Flag 1, `Debug_alloc`) bleiben „heap: no room …“. `room_tour_report.py` zählt beide.
- **Blacktail in den Mercenaries (Nutzermeldung):** `set_char(0)` gibt Leon 0x27 (Blacktail) und 0x94 (Riot Gun),
  in Debug-, EU- und US-`main.dol` dieselbe Tabelle (`retail_code.py`; nur Krausers Bogen hat in der
  Verkaufsfassung Nachladestufe 2 statt 1). Mit dem Debug-Start in r400 (oder einem Raumsprung des Debug-Menüs)
  läuft dagegen der normale Spielstart (`set_game`: Pistole 0x23). Die beiden Fehlerberichte des Nutzers lagen
  nicht vor; **offen**, wie er die Mercenaries gestartet hat.

## US-Discs in golden-master und master (30.10.2026 abends, lokal, Worktree `RE4-bughunt`)

Wunsch des Projektinhabers: `retail-us` (28.10.2026, Abschnitt „Retail-Discs (US)“ unten) zusammengeführt. Konflikte mit
der Japan-Arbeit gelöst: die Erkennung kennt beide (`releaseOf`: G4BJ08 Japan, G4BE08 mit FST 0x231A00 US),
`re4_disc_retail()` gilt für EU, US und JP, neu `re4_disc_retail_us()`; App: `GameCubeDisc.US` neben `JAPANESE`/`KOREAN`,
Kataloge `*-us.tsv`. Die Patches, die der US-Branch auf `re4_disc_retail()` gestellt hatte, bleiben so (Kopf um den
US-Absatz ergänzt); in 0135 kappt die Spielende-Anzeige die Fortsetzungen nur mit EU- und JP-Discs (die US-Fassung
kappt nicht). **Nicht gegen die US-Daten geprüft** und daher nur EU (bzw. EU+JP): die später entstandenen Patches 0138
(r20b-Labyrinth), 0139–0141 (Schwierigkeit), 0142 (leerer Teller), 0145 (Wände r214/r20e), 0148 (Easy-Schrotflinte).
Geprüft: Kern-Tests (`--self-test`, `--runtime-test`, `--core-test` je 0 Fehler), `test_whole_disc_folder.py` und
`test_retail_compare.py` ok, Java `DiscTests` 30/30, App-Unit-Tests; **PC-Prüfung mit den US-Discs**
(`whole_r100_check.sh --data ~/re4-us/disc`, i686): Formatvergleich 407 Dateien ohne Fehler, r100 ohne Absturz, alle
Filme, Speicherkarte A/B – „ALLES OK“. Gerät und Raum-Rundgang mit US-Discs offen.

## Version 0.96.0-beta (30.10.2026, lokal, Branches `golden-master` und `master`)

Wunsch des Projektinhabers: `versionName` 0.96.0-beta, `versionCode` 82, Releasetext
`port/android-decomp/docs/releases/v0.96.0-beta.md` (seit 0.95.2: Patch 0148 EU Easy, 0149 Potato-Modus, 0150
JP/KR-Vorfilm-Untertitel, 0151 Zielgeschwindigkeit, 0152 Handcannon, 0153 Lautstärke; Seiten AUDIO und CHEATS,
neue Menüreihenfolge, Spielstand-Import EU/Dolphin-Karte/ZIP, 60-FPS-Geisterbilder, Update-Knopf). `master` danach
auf `golden-master` vorgespult (OUYA bleibt getrennt). Gerätetest der neuen Seiten offen.

Danach (Wunsch des Projektinhabers): Knöpfe nebeneinander mit demselben Abstand wie untereinander, 14 dp
(`MenuLayout.GAP` jetzt auf allen vier Seiten, vorher seitlich 3 dp); die Knöpfe rücken dadurch 4 dp nach innen,
der Rand der Schnellspeicher-Knöpfe bleibt 12 dp. `CoreMenuLayoutTest` 8/8.

## Handcannon gegen Kettensäge, Ton im Endfilm, Menüs AUDIO und CHEATS (30.10.2026 nachmittags, lokal, golden-master)

- **Handcannon gegen den Kettensägen-Mann (Nutzerbericht 30.10.2026 11:40, Debug-Discs):** „firepower doesn't match
  … the guy with the chainsaw tanked 5 shots“. Im Bericht: Ganados fallen mit einem Schuss, der Kettensägen-Mann (0x15/4,
  5000 Lebenspunkte, `Wep_type` 4) verliert je Treffer nur 910 (5000 → 4090 → 3180 → 2270). Die Schadenstabellen
  (`Dmg_tbl_*`) haben in der EU-Fassung dieselbe Handcannon-Spalte (EU hat eine Tabelle mehr), `GetWepDmVal` rechnet
  gleich. Ursache: `em10SetDmVal` begrenzt gegen Gegner mit `Wep_type` 4 jeden Treffer außer den Raketenwerfern
  (0xD, 0x12) auf 1000; die EU-Fassung (`em10.rel` in `em/em10.drs`, Text 0x40104) nimmt auch das Handcannon (0x0F) aus
  und halbiert dafür Waffe 0x0B (gegen ihn und Typ 2) – Balance, nicht übernommen. **Patch 0152** (Fehler des
  Prototyps, beide Disc-Arten). Geprüft am PC (i686, Debug-Start r101, `RE4_TEST_WEAPON=free+40:15,0,lv=0x6026`): mit
  Patch 3000 → 0 mit einem Treffer, Gegenprobe ohne Patch 3000 → 2000 → 1000 → 0. Gerät offen.
- **Ton im Endfilm (Nutzervideo 30.10.2026: „alle paar Sekunden ein Störgeräusch“):** `Movie/ending.sfd` am PC einmal
  schnell und einmal in Echtzeit (`RE4_MOVIE_FAST=0`, `RE4_VI_FAST=0`, 216 s) aufgenommen: beide Aufnahmen sind Bit für
  Bit gleich, ohne Lücken; die 23 gefundenen Spitzen liegen in beiden an denselben Stellen (Geräusche des Films). Der
  Kern liefert den Filmton also sauber. Der Gerätebericht von 09:48 (nicht vom Endfilm) zeigt keine Aussetzer der
  Android-Ausgabe („ran out“ 0), der Kern 6-mal „ran dry“ beim Start. Ein Puffer-Schieber im Menü bringt deshalb
  wenig: die App vergrößert den Puffer bei Aussetzern schon selbst (`CoreAudio` LatencyTuner) und meldet sie im
  Kernprotokoll. Offen: ein Bericht direkt nach dem Endfilm vom betroffenen Gerät.
- **Seite AUDIO (Wunsch des Projektinhabers):** Schieber „Musik“ und „Soundeffekte“, 0–100 % in 5er-Schritten
  (Grafik-Einstellungen `musicVolume`/`effectsVolume`, 100 wie das Spiel). App → `CoreBridge.setSoundVolume` →
  `re4_core_set_sound_volume` (IPC `kSetSoundVolume` im Kernprozess) → `runtime/sound_volume.cpp`; **Patch 0153**
  skaliert die Ausgabelautstärke der drei Tonberechnungen (I.S.S.-Effekte, Sequenzen, Streams; Typ 2 = Effekt, sonst
  Musik), danach rechnet `Snd_reset_vol_all` im nächsten Spielbild alles neu. Die Filme behalten ihren Ton. Testhilfe
  `RE4_SOUND_VOLUME=musik,effekte`. **Geprüft am PC** (i686, r101 mit Schüssen, 70 s): Effekte 0 → nur noch Musik,
  Musik 0 → nur noch Effekte, 50 % → etwa ein Viertel der Amplitude (−12 dB, die Lautstärkekurve des Tontreibers;
  hört sich etwa halb so laut an). Änderung während des Spiels und Gerät offen.
- **Menü neu geordnet:** Anzeige › Audio › Steuerung › Performance › Cheats › Management (drei Reihen); die Cheats
  haben eine eigene Seite CHEATS (vorher oben in MANAGEMENT); auf PERFORMANCE steht vor „Bildausgabe“ die Überschrift
  „Experimentell“. Texte in allen zehn Sprachen (`check_translations.py` ok), `CoreMenuLayoutTest` 8/8.

## Branch bughunt lokal (30.10.2026, Windows-PC, Worktree `RE4-bughunt`, WSL-Baum `~/re4-bughunt/RE4`)

- **EU Easy: Schrotflinte von Anfang an (Punkt 28), gelesen mit `retail_code.py`:**
  - EU-`set_game` (main.dol 0x80018c70) liest nach der Pistole `game_mode` (EU `pG+0x8360`): bei 3 oder weniger
    (Easy) `PutInCase(0x2C, 1, 0)` (Schrotflinte, Stufen 0, 6 Patronen geladen), `PutInCase(5, 1, 0)` (Spray),
    `PutInCase(4, 20, 0)` (Pistolenmunition), `PutInCase(0x18, 15, 0)` (Schrotpatronen); der Koffer setzt sie selbst.
    Darüber wie der Debug-Code (Munition und Spray an festen Plätzen). EU-`gameInit` (0x80019a40) ruft nur
    `set_game(0)`; die Abfrage steckt allein in `set_game`. Die JP-Fassung ist gleich.
  - EU-`R101Init` (`st1_1.rel` 0x9a4) schaltet am Ende auf Easy den Gegenstandsbereich 0x8A ab (`SceAtSetEnable`,
    Relokation 0x801527d8): die Schrotflinte im Haus (1086 4704 −17573). Die Raumdaten haben sie auf jeder Stufe
    (Bereiche auf Easy und Normal gleich). JP gleich.
  - **Patch 0148** (`item.cpp`, `r101.cpp`), nur EU-/JP-Discs.
  - Neue Testhilfe `RE4_TRACE_ITEMS=1`: Koffer und Gegenstandsbereiche des Raums ins Protokoll („items (room
    runs)“/„items (free)“).
  - **Geprüft (PC, i686, Debug-Start r101 mit `RE4_TEST_GAME_MODE`, der Weg über `gameInit` wie beim neuen Spiel):**
    EU Easy: Pistole, Schrotflinte (6 geladen), Spray, 20 Pistolenmunition, 15 Schrotpatronen im Koffer, Bereich 0x8A
    aus. EU Normal: Startsatz wie bisher, 0x8A an. Debug-Discs mit 5 und mit 3: wie bisher, 0x8A an.
    **Nicht geprüft:** neues Spiel über das Titelmenü (gleiche Funktion), Gerät.
  - **Weitere Easy-Stellen der EU, die die frühere Liste nicht hatte** (sie suchte `game_country`, diese lesen
    `game_mode` direkt), nicht übernommen (Easy-Feinheiten): `itemInfo` (main.dol 0x80019b94: auf Easy mehr Munition
    je Fundstück, z. B. 25 statt 15), r100 `r100_trap_set`, `r100_DoorCk`, `r100_WindowBreakCk`, `r100_StreanChk`,
    `r100_Sce_zombi_dead` (`st1_0.rel`), Händler `stockNum`/`sellPrice`, `GetDropBullet`, `RandomItemCk`,
    `GameAddPoint`. Auf Wunsch einzeln nachziehen.

- **Potato-Modus (Punkt 30):**
  - **Kern:** Patch 0149 fragt in `EspTrans` (`esp.cpp`) für jede lebende Effektfigur `re4_esp_skip`
    (`runtime/potato.cpp`); eine ausgelassene Figur wird nur nicht gezeichnet, sie läuft weiter (Lebensdauer, Ton,
    was das Spiel von ihr liest). Zwei Bits (`re4_core_set_potato`, auch über den 32-bit-Kernprozess: Nachricht
    `kSetPotato` 34): Bit 0 **Wettereffekte** = Regen-/Staubschleier über dem Bild (esp 0x04), Wetterteilchen (0x15)
    und die bedeckenden Figuren der Raumtabelle (Besitzer `EFF_SST`, dauerhaft `Core_flg` 0x4000, Mischart 0/4/5 oder
    Streifen 0x08), die nicht aufsteigen (Nebelbänke, Dunst, Spritzer); Bit 1 **Spezialeffekte** = Bildverzerrungen
    (esp 0x0F, 0x18, jede Figur mit Flimmerart: Hitzeflimmern, Wasser), aufsteigende bedeckende Raumfiguren (Dampf) und
    bedeckende Figuren aus Raumcode (Besitzer `EFF_ROOM`, z. B. eine Wolke). Bleiben: alles Additive (Feuer,
    Mündungsfeuer, Glühen), alles von Spieler, Gegnern, Waffen (Blut, Treffer, Explosionen, Dynamit), Effekte an einem
    Modell, Ereignis-Effekte (`Core_flg` 0x1000) und **alles während eines Ereignisses** (r11b: die Adern auf Leons
    Gesicht sind Figuren der Raumtabelle), der Entfernungsnebel (GX-Fog, keine Effektfigur), Wasserflächen
    (`Espgen42`/`Espgen45`, sonst fehlte das Wasser ganz).
  - **Prüfhilfen:** `RE4_POTATO=Maske` (1 Wetter, 2 Spezial, 3 beides), `RE4_ESP_CENSUS=N` (alle N Bilder die
    Effektfiguren nach Besitzer, Art, Mischart, Flimmerart, Aufsteigen, an einem Modell, `Core_flg`, und was die
    Schalter mit ihnen tun).
  - **App:** Seite „Performance“ unter „Force Performance“: Zwischenüberschrift „Potato-Modus“, zwei Schalter
    „Wettereffekte“ / „Spezialeffekte“ (an = Effekte sichtbar, Standard an; gespeichert in den Grafik-Einstellungen als
    `weatherEffects`/`specialEffects`), darunter der Fußtext mit den Erklärungen; beim Start und bei jeder Änderung an
    den Kern (`CoreBridge.setPotato`, über `drawGx` wie Force Performance). Texte in allen zehn Sprachen
    (`check_translations.py` ohne Befund).
  - **Bildvergleich am PC (EU-Discs, i686, Debug-Start, Bild 60 und 300 nach „Spieler frei“, App-Renderer):**
    Zeichnungen normal / Wetter aus / Spezial aus / beides aus: r105 (Dorfweg mit Dunst) 609/409/609/409 – der
    Dunstschleier verschwindet, sonst gleich; r108 691/524/649/482 – im Bild kein sichtbarer Unterschied (die
    Scheibe vor Leon ist ein Modell); r209 (Burg) 581/563/581/563, r30a (Insel) 536/506/525/495 – nur kleine
    Partikel; r103 (See) 1034→837, r11b in der Zwischensequenz unverändert; r21d am Debug-Start 248 unverändert
    (Höhlengang, die Nebelfiguren dort liegen außerhalb des Bildes). Ein Raum mit Regen und einer mit Feuer im Bild
    fehlen noch (Feuer ist additiv und bleibt nach der Regel).
  - **Geprüft:** Test `potato` (neun Fälle: aus, Nebel/Regen/Dampf/Feuer je Schalter, Wolke aus Raumcode, Flimmern,
    Gegner-Effekt, Ereignis-Effekt, alles im Ereignis), `whole_build.sh i686` mit allen Tests 0 Fehler; App Java und
    nativ (arm64, armv7) übersetzt. **Nicht geprüft:** Gerät, GPU-Zeit (Retroid nicht verbunden), Verdugo-Kampf mit
    Flüssigstickstoff (ob der eingefrorene Verdugo ohne die Wolke erkennbar bleibt; die Wolke ist nur dann aus, wenn
    sie eine bedeckende Figur aus Raumcode ist – zu bestätigen mit `RE4_ESP_CENSUS` im Kampf).
  - **Retroid (30.10.2026 mittags):** die GPU-Messreihe der App kann jetzt halbe Stufen (`2.5`) und als achten Wert die
    Potato-Maske (`RE4_GPU_BENCH=2.5/1/1/1/0/0/1/3,...`); neue Testhilfe `RE4_TEST_LOAD_FILE=N` (der Ladebildschirm
    beginnt auf „FILE N“). Mit den EU-Discs des Retroid gibt es keinen Debug-Start (kein `roomInfo.dat`): der Lauf mit
    `-StartRoom 21d` landete in r120 (Film, Messung wertlos). Ein Lauf mit Laden landete nicht im Spielstand, sondern über die Tastenfolge (A und START im
    Titel) in The Mercenaries, Dorf (r400; Bildschirmfoto, vom Projektinhaber bestätigt); dort 15,4 / 11,6 / 16,4 / 15,6 / 13,5 ms GPU je Bild
    (aus / Wetter aus / Spezial aus / beides aus / aus) – Leon und Gegner bewegten sich, die Werte schwanken zu stark.
    **Verdugo-Kampf gemessen (Retroid, Debug-Discs vom Projektinhaber importiert, 30.10.2026 11:10):** Debug-Start r221,
    `RE4_TEST_PLACE=free+30:area=11` (Auslösebereich 0x0B: Verdugo 0x2C erscheint, `r221_appearBosstail`), `RE4_CHEATS=1`,
    Messreihe bei 2,5×, je 10 s: Potato aus **14,0 ms** GPU je Bild (403 Zeichnungen), Wetter aus 12,8 ms (433), Spezial aus
    14,7 ms (474), beides aus **10,7 ms**, wieder aus **12,3 ms** (393). Also etwa −2 bis −3 ms (−15 bis −24 %) mit beiden
    Schaltern; die Einzelschritte schwanken mit dem Bild (Verdugo greift an, Leon liest zwischendurch die Schalttafel).
    Im Kampf fallen weg: 75 aufsteigende Dampf-Figuren der Raumtabelle (Spezial), rund 40 Wetterteilchen (esp 0x15) und
    der Schleier esp 0x04 (Wetter); Verdugos eigene Effekte (Besitzer 36) bleiben. Der Retroid (Adreno 650) bleibt so
    oder so weit unter 33 ms; beim Tester (Adreno 642L, 28 ms) sind −15 bis −24 % geschätzt, **nicht gemessen**.
    **Nicht geprüft:** die Wolke des Flüssigstickstoffs (kein Tank wurde benutzt) und ob der gefrorene Verdugo ohne sie
    erkennbar bleibt; Aufnahmen in `gx-geraet.zip` des Laufs (nicht im Repository).
    **Stickstoffwolke geprüft (Retroid, 30.10.2026 11:25, Projektinhaber spielt: zwei Tanks mit allen Effekten, zwei mit
    beiden Schaltern aus, je R3):** die Tanks heißen im Code „bonbe“ (`r221` `r201_throwBonbe`, Aktionsbereiche 1, 2, 3,
    0x10; Leon öffnet sie in einer kurzen Szene, die Wolke steht danach etwa 10 s). Die Wolke sind Raumcode-Effekte
    (Besitzer `EFF_ROOM`, `Core_flg` 1) mit **additiver** Mischung (1 und 3): sie bleibt mit beiden Schaltern aus
    sichtbar, der gefrorene Verdugo ist eisig-weiß gut zu erkennen (R3-Bilder des Geräts). Weggefallen sind 77 000 Dampf-
    und 37 000 Wetter-Sprites. **Geändert:** die Regel „bedeckende Raumcode-Effekte = Spezialeffekte“ entfällt – sie traf
    die Wolke ohnehin nicht, hätte aber den Rauch explodierender Fässer (`embarrel.cpp`, `EFF_ROOM`) und den Staub fallender
    Felsen (`emrock.cpp`) weggenommen; Test `potato` angepasst. **Fußtext:** „Flüssigstickstoff“ in allen zehn Sprachen gestrichen
    (Entscheidung des Projektinhabers, 30.10.2026); „Spezialeffekte“ = Dampf, Hitzeflimmern, Wasser.
    **Projektinhaber auf dem Retroid (30.10.2026 mittags):** Feuer-/Lavaraum mit Hitzeflimmern und Rauch: Potato an
    **54 FPS**, Potato aus **37 FPS** (Raum nicht genannt; die bisher größte gemessene Wirkung, das Hitzeflimmern kopiert
    das Bild und zeichnet es verzerrt darüber).
  - **Hinweis:** Verdugo (em2c) gehört laut Code zu r221 (Aufzug, Boss); im Debug-Start von r21d sind Novistadoren (em2d)
    in einem Höhlengang. Der Nutzerbericht nennt r21d.

## Handcannon und Schuss-Ton (29.10.2026 nachts, lokal, Branch `golden-master`)

- **Handcannon (Punkt 19), gelesen in der EU-`main.dol` und im EU-`Sscrn.rel` (`retail_code.py`):**
  - `WeaponLevelTbl` Zeile 0x0F (0x8020846c): {30, 35, 40, 45, 50, 60, 99.9} statt des Platzhalters 100.0 (Händler
    „30.0“ … „60.0“, Exklusiv „99.9“; der Schaden war bisher 100-mal der Klassenwert, jetzt 30- bis 99,9-mal).
  - `wep_info` 0x34 und 0x37 (0x801e3a9c): Magazin-Stufe 7 = **0x8000** (Prototyp 0xFFFF, durch den u8-Cast in
    `WeaponId2ChargeNum` 255 Schuss, und nichts prüfte darauf); `WeaponId2ChargeNum` gibt den u16-Wert zurück.
  - `level_ext_sw500` (0x801f87c8): {0x37, {7, 1, 3, **7**}}: das Exklusiv-Tuning hebt Feuerkraft **und** Magazin auf 7.
  - 0x8000 = unendlich: `trigger` verbraucht nichts, `reloadable` antwortet 1, `reload` lädt nichts,
    `bulletNum(cItem*)` zählt 1 (nur 0x37/0x34); Anzeige mit Ziffer 13 in HUD (`BulletInfo::move`, auch für den
    unendlichen Raketenwerfer 0x6D), Koffer (Stückzahl, auch 0x6D), `numDisp`, Untersuchen und Händler
    (`levelItemDisp`, `weaponLevelDisp`). Ziffer 13 ist auf beiden Disc-Arten „---“ (am PC im Bild gesehen).
  - Nicht übernommen (Balance des fertigen Spiels, kein Fehler): `WeaponLevelTbl` Zeilen 3, 4, 9, 0x11 und 0x2C
    (0x2C ist im Prototyp ebenfalls ein 100er-Platzhalter, EU {25,30,35,35,35,35,35}; Waffe unbekannt), der
    Chicago Typewriter der EU (wird mit Magazin 7 = unendlich angelegt, `construct` lv 0x6006, kein Exklusiv-Tuning,
    `wep_level_info` {6,1,1,6}). Im Port bleibt der Typewriter wie im Prototyp (50 Schuss, Exklusiv = Feuerkraft 7).
  - Die Munition auf eine unendliche Waffe kombinieren (`reload_main` mit 0x8000) ist in der EU-Fassung genauso.
  - **Patch 0146**; dazu `runtime/cheats.cpp` (die Cheat-Nachfüllung lässt 0x8000 aus, sonst wäre Bit 15 der
    Munitionsart gesetzt) und `RE4_TEST_WEAPON` mit `lv=L` (Stufen der Waffe statt Debug-Endlosmunition).
  - **Geprüft:** Test `handcannon` (Feuerkraft ×10 auf Stufen 1–7 = 300…999, Kapazität 7 = 0x8000, Schuss ohne
    Verbrauch, Zähler 1, Nachladen; Gegenprobe Stufe 6 verbraucht); `whole_build.sh i686` alle Tests 0 Fehler. Am PC
    (i686, r20a, `RE4_TEST_WEAPON=free+20:15,0,lv=0x6026`, R halten, A alle 45 Bilder): Debug- und EU-Discs je 27
    Schüsse, geladen bleibt 0, HUD „---“; Gegenprobe `lv=0x5025` (Magazin 6): 10 Schüsse, dann leer.
    **Nicht geprüft:** der Händler-Bildschirm im Bild (nur die Werte, die er zeigt), Gerät.
- **Schuss-Ton „stottert“ (Punkt 20):**
  - Aufnahmen am PC (i686, r20a, `RE4_TEST_WEAPON=free+20:W` mit Debug-Endlosmunition, R halten, A alle 24 Bilder,
    `RE4_AUDIO_WAV`; Auswertung: Schussanfänge an der Hüllkurve je 1 ms, Lücken unter 3 % und kurze Einbrüche unter
    35 % der Nachbarn im Schuss): Pistole Debug-Discs 40 Schüsse, Schrotflinte 19, Pistole EU-Discs 46, Pistole in
    Echtzeit (`RE4_VI_FAST=0`) 46: **keine Lücke, kein Einbruch**. (Alle 10 Schuss ein Ausreißer: das Nachladen.)
  - x86_64 (Zeigerbreite wie ARM64): WAV **bitgleich** mit i686. Stand vor b13dcb6 (ohne Kompressor): im ganzen
    Schussbereich bitgleich, Unterschiede nur 16–20 s (Raumstart, dort regelt der Kompressor).
  - Stimmen (neu: `RE4_DSP_TRACE=1` in `dsp.cpp`, je Stimme Start, Stopp durch die CPU, Adresswechsel durch die
    Tonsteuerung, Ende/Schleife mit Liste und Millisekunde): ein Pistolenschuss sind zwei ADPCM-Stimmen (Stereo-Paar
    0x315cd2..0x31eb59 und 0x31eb62..0x3279e9, je ~1 s, ohne Schleife); der nächste Schuss startet zwei neue
    Stimmen, die CPU stoppt die alten eine Liste (5 ms) später. Kein Adresswechsel mitten im Schuss.
  - Folgerung: die Lücke entsteht hinter dem Kern. Der Ring des Kerns lief beim Nutzer nicht leer („ran dry“ nur
    beim Raumwechsel); im App-Prozess (64 bit) bleibt die AudioTrack: läuft sie leer, spielt der Lautsprecher dort
    Stille und danach den Rest – genau „Abschuss, kurze Pause, Rest“, nur manchmal. Ihre Unterläufe standen nur in
    logcat (`RE4Audio`, „buffer … after N underruns“), das auf manchen Geräten nichts aufzeichnet. **Neu:** jede
    Zunahme der Unterläufe als Zeile „sound: the Android output ran out (N times on this track, buffer F frames = M
    ms) (game frame G)“ auch in `core-log.txt` (`CoreAudio.tune` → `CoreBridge.audioNote` → `re4_log_file_line`;
    im 32-bit-Kernprozess nur logcat), höchstens 200 Zeilen.
  - **Nicht geändert:** die Größe des AudioTrack-Puffers (`LatencyTuner`: zwei Perioden, wächst nach jedem Unterlauf
    um eine). Ein größerer Startwert verhindert solche Lücken, verzögert aber jeden Ton (früher Beschwerde über
    späte Schüsse mit Bluetooth) – Entscheidung des Projektinhabers, sobald ein Bericht die neue Zeile zeigt.
  - **Geprüft:** `whole_build.sh i686` mit `RE4_DSP_TRACE` im Kern: 0 Fehler (runtime-test, core-test mit `ax`,
    Kernprozess-Test, Film-Test); das DSP-Protokoll ändert den Ton nicht (WAV bitgleich). App:
    `compileCoreDebugJavaWithJavac` und `externalNativeBuildCoreDebug` (arm64, armv7) ohne Fehler übersetzt.
    **Nicht geprüft:** Gerät.

## Version 0.95.2-beta (30.10.2026, lokal, Branch `golden-master`)

Wunsch des Projektinhabers (für die Tester): `versionName` 0.95.2-beta, `versionCode` 81, Releasetext
`port/android-decomp/docs/releases/v0.95.2-beta.md` (seit 0.95.1: Patch 0145 r214/r20e, Patch 0146 Handcannon,
168 Programme in der Liste der App, Aussetzer der Tonausgabe im Fehlerbericht). **Mit Japan/Korea** (Wunsch des
Projektinhabers, 30.10.2026: ein Tester in Korea soll es testen): `retail-jp` nach `golden-master` übernommen, README
und Releasetext ergänzt. Geprüft vorher am zusammengeführten Stand (eigener WSL-Baum `~/re4-jp/RE4`, Bau mit 0
Fehlern, alle Selbst-, Modul-, Laufzeit- und Kerntests grün): Prüfstrecke Japan, EU und Debug „ALLES OK“, Korea alles
bis auf den bekannten Formatvergleich von `St1/r100.das` (Klassik-Leser). Gerät nicht getestet.

## Japanische und koreanische Discs, J0–J5 (30.10.2026 nachts, lokal, Branch `retail-jp`)

Entscheidung des Projektinhabers (29.10.2026): `retail-us` bleibt, wie es ist; Japan und Korea auf einem eigenen
Branch `retail-jp` (von `golden-master`). Plan und Befunde: `port/docs/retail-jp-plan.md`.

- **Erkennung (J1):** Kern `RE4_DISC_RETAIL_JP` (`G4BJ08`), Korea an zwei Befehlen in `sys/main.dol`;
  `re4_disc_retail_jp()`, `re4_disc_retail_kr()`, `re4_disc_retail()` (EU, US oder Japan). App: Freigaben `jp`/`kr`
  mit eigenen Katalogen (`references-jp/kr.tsv` …), Import, Startseite, kein Debug-Menü mit Verkaufs-Discs.
- **Sprache (J3):** mit JP-Discs Sprache/Land 0 (Japanisch; Korea schreibt Koreanisch in dieselben Stellen).
- **Patches (J4):** Code beider Verkaufsfassungen an den Patch-Stellen verglichen; 0120, 0123–0126, 0129, 0133, 0135
  jetzt `re4_disc_retail()`, 0128, 0136, 0138, 0140, 0141, 0145 EU oder Japan, 0121/0122/0131/0132/0139/0142 nur EU.
- **Korea (J5):** **Patch 0147** (`emwep.cpp`, `obj16.cpp`): die Enthauptungs-Tode wie in der US-Fassung, wie die
  Übersetzung (0146 ist der Handcannon-Patch auf `golden-master`).
- **Geprüft (PC, i686):** Laufzeittests 0 Fehler (neu: Japan/Korea in `disc release`), Java-`DiscTests` 24 Prüfungen,
  echter App-Import beider Abbildpaare (je 4 082 Prüfungen, `release=jp`/`kr`); Prüfstrecke mit `--room-info` der
  Debug-Disc: **Japan „ALLES OK“** (Formatvergleich, Spiellauf r100, Filme, Speichern und Laden; Systemdatei
  „biohazard4 システムファイル“), **Korea alles bestanden außer dem Formatvergleich von `St1/r100.das`** (der strengere
  Yz2-Leser des alten Klassik-Modus lehnt das neu gepackte Archiv ab; der Kern spielt den Raum, der Raum-Check der App
  vergleicht nur SHA-256), Gegenprobe EU und Debug „ALLES OK“. Python-Tests der Werkzeuge ok.
- **Nicht geprüft:** Android-Build, Retroid, Raum-Rundgang mit JP-Daten (J7), Filmuntertitel auf Japanisch im Bild,
  die Enthauptung mit koreanischen Discs im Spiel.
- **Hinweis Arbeitsordner:** `RE4-Fixed` gehört `golden-master` (eine zweite Sitzung arbeitet dort); `retail-jp` liegt
  im Worktree `E:\Development\Android\RE4-JP`. Der gemeinsame WSL-Baum `~/re4-kern/RE4` bleibt `golden-master`.


## Berichte für 0.95.2 und Japan/Korea (29.10.2026 spätabends, lokal, Branch `golden-master`)

- **r214 „Uhrturm“ versperrt (EU-Discs, Bildschirmfoto eines Nutzers):** gleicher Fehlertyp wie r20b. Die EU-Daten
  von r214 (Wehrgang mit den Katapulten vor dem Uhrturm) haben zwei Stellungen des Wegs: Modellgruppen 0x2B–0x31,
  Kollisionswände 0x17/0x18/0x19 (Typ 0xB), einen Hebel (Bereich 0x1B) und eine zweite Meldung für Bereich 7. Der
  EU-Code (`st2_2.rel` `R214Init` 0xa300) setzt über Easy Stellung 0 (0xac80 mit 0): Wand 0x18 aus, 0x17/0x19 an,
  0x2C/0x2F/0x30 aus, Bereich 0x1B aus; auf Easy überspringt ein Hebel die Katapulte (etwa 10 KB neuer Code am
  Anfang von `st2_2.rel`, nicht übernommen – Easy spielt hier wie in den anderen Räumen den Normal-Weg). Im Port stand
  alles wie in den Daten: alle Wände an. Wand 0x18 liegt quer über dem Fuß der Treppe (x −24087…−19117, z ≈ −15000).
  Gefunden über Raumarchiv-Vergleich (AEV-Einträge aller st2-Räume Debug gegen EU), `RE4_TRACE_AT`, die Relokationen
  der Module und eine Namenszuordnung EU→Debug über gleiche Funktionen aller REL-Module (189 Adressen).
  **Patch 0145** (`r214.cpp`, nur EU-Discs, außer in Part 2 wie der EU-Code). Geprüft (PC, i686, Debug-Start r214):
  Wand 0x18 und Bereich 0x1B aus; Leon läuft mit dem Stick von (−21600 5 −12500) die Treppe hinauf bis
  (−21776 5206 −24000), mit EU- und Debug-Discs gleich. Gerät offen.
- **Ashley läuft durch die Rüstungsstatuen (r20e, Nutzermeldung ohne Protokoll):** die EU-Daten geben den vier
  Statuen Kollisionswände (Bereiche 0x13–0x16), die Debug-Daten nicht – mit Debug-Discs ist das also der Stand der
  Prototyp-Daten (nicht ohne erfundene Wände zu beheben). Der EU-Code schaltet die Wände in `r20e_startArmor`
  (`st2_1.rel` 0x1a814) ab, wenn die Ritter erwachen; das fehlte im Port (die Wände wären danach stehen geblieben).
  Ebenfalls in **Patch 0145**. Geprüft: die vier Wände mit EU-Daten beim Raumstart an; das Erwachen der Ritter nicht
  im Spiel geprüft. **Offen:** mit welchen Discs der Nutzer spielte.
- **FPS-Einbrüche (Moto e13, Unisoc T606, Mali-G57, Debug-Discs, 0.95.0):** im Protokoll hält der Kern 30 Bilder/s
  (14–20 ms von 33 frei), der Zeichen-Thread braucht 8–15 ms, gezeigt werden 57–60/s, ein Bild ausgelassen. Nach dem
  Laden des Spielstands lädt die App etwa 1,5 Minuten lang 9 135 Shader-Programme aus dem Cache vor (21,7 s Arbeit,
  je Bildwechsel 4 ms, `GxRenderer::Prewarm`); in dieser Zeit 11–16 ms je Bild mit Spitzen bis 75 ms und 56–58
  gezeigte Bilder/s, danach 8–11 ms und 60/s. Ein Bild mit 140 ms beim Schießen in r205 fällt mit dem einzigen live
  gebauten Programm zusammen (nicht in der Liste). GPU-Zeit auf dem Gerät nicht gemessen.
- **Ruckler beim Schießen, Einbrüche in Speicherräumen (Nutzermeldung ohne Protokoll):** passt zu beiden Befunden
  oben – neue Effekt-Shader werden beim ersten Schuss gebaut, und nach dem Laden (man startet im Speicherraum) läuft das
  Vorladen der Programme. Die Tiefenabfrage für Leuchteffekte wartet nicht (PBO und Fence). **Maßnahme:** die
  168 neuen Programme des Moto-Berichts in die Liste der App übernommen (`gx-programs.keys`, jetzt 9 174;
  `gx_program_list.py merge`). **Vorschlag (nicht umgesetzt, Entscheidung offen):** das Vorladen auf schwachen
  Geräten anders verteilen (z. B. nur Programme des Raums zuerst). Für eine Messung: Fehlerbericht direkt nach einem
  Ruckler.
- **Japan und Korea:** Analyse in `port/docs/retail-jp-plan.md` (Kennung `G4BJ08`, Aufbau wie EU/US, Korea = Japan
  mit übersetzten Texten/Schriften und zwei entzensierten Stellen im Code). Plan J0–J7; J0 (Übernahme von `retail-us`
  als Grundlage) ist eine Entscheidung des Projektinhabers. Noch nichts eingebaut.

## Gerätetest `golden-master` und Version 0.95.1-beta (29.10.2026 abends, lokal, Branch `golden-master`)

Projektinhaber auf dem Retroid, Stand `8890f2be`:

- **Bestätigt:** „4:3“ ohne Balken im Spiel (Zwischensequenzen mit Balken, wie vorgesehen), „4:3 (GC)“ unverändert,
  EMPTY/LEER ruhig, Messer am Brustgurt im Inventar, Tastenbelegung mit Controller, Tastatur und Maus, Touch-Zielen
  per Wischen, alle Cheats (auch automatische QTEs und Extra-Spiele), Dreierreihen im Ingame-Menü.
- **Ton:** klingt auf dem Retroid gut; der Test beim Nutzer mit dem Knack-Bericht steht aus.
- **Noch bei Testern:** Zielfernrohr bei 1× (passendes Gewehr), Leistung auf dem Redmi 15C (zwei schnelle Kerne).
- **Version 0.95.1-beta** (Wunsch des Projektinhabers): `versionName` 0.95.1-beta, `versionCode` 80 (0.95.0 hatte
  noch 79 wie 0.94.1), Releasetext `port/android-decomp/docs/releases/v0.95.1-beta.md` (Änderungen seit 0.95.0).

## Lokale Sitzung 29.10.2026 abends: EMPTY, Balken bei „4:3“, Messer im Inventar (lokal, Branch `golden-master`)

Offene Punkte 14, 17 und 18, am Windows-PC mit Debug- und EU-Discs (WSL-Baum `~/re4-kern/RE4`, i686-O2).

- **Patch 0142 (EMPTY, nur EU-Discs):** Einheit 0x3F des Lebensbalkens (`IDC_LIFE_METER`) hat auf den EU-Discs
  fünf Sprachbilder (texNo 0 Englisch, 1 Spanisch, 2 Italienisch, 3 Deutsch, 4 Französisch), `BulletInfo::move`
  schaltet sie nur ein, so liefen sie der Reihe nach durch. Die EU-Fassung (`main.dol` 0x800c19a0; der Wortvergleich
  von `retail_code.py find` traf nicht, gefunden über die Folge `li 0x3F/0xB/0xA/0x17` vor `unitPtr`) setzt
  `tex_flag |= 2` und texNo nach `pSys->language`: 3 → 3, 4 → 4, 5 → 1, 6 → 2, sonst 0. Der Patch macht das mit
  `re4_disc_retail_eu()`. Geprüft: EU-Discs r100, Schrotflinte ohne Patronen (`RE4_TEST_WEAPON`, sechs Schüsse,
  ab Bild 3445 leer), GX-Bilder 3524..3644 in `eng/ger/fra/esp/ita`: EMPTY, LEER, VIDE, VACÍO, VUOTO stehen ruhig;
  Debug-Discs: EMPTY wie bisher.
- **Balken bei „4:3“ (dieselbe Ursache wie die Cloud-Sitzung, deren Korrektur gilt):** Neuer Testschalter
  `RE4_TEST_LETTERBOX_OFF=1` (`state_trace.cpp`, setzt `re4_core_set_letterbox(1)` wie die App). Debug-Start r104
  (`RE4_CHEATS=9`: Gesundheit und automatische QTEs, sonst stirbt Leon im Luis-Film), GX-Bild 7153: Schere (BP
  0x20/0x21) und Viewport (XF 0x101A..) stehen auf 512×448, das 3D reicht bis Zeichnung 175 über die volle Höhe
  (Bild schrittweise nachgezeichnet: Zeichnungsliste abgeschnitten, Display-Kopie angehängt); die Zeichnungen
  178/179 sind die zwei schwarzen Vierecke der Klasse `IDC_CINESCO`. Hier zuerst als Patch in `IDSystem::trans`
  gebaut und geprüft; beim Abgleich lag schon die gleichwertige Korrektur der Cloud (`picture.cpp
  re4_letterbox_frame`, `dispSw`) auf GitHub, der eigene Patch ist wieder heraus (Nummer 0143 bleibt frei). Geprüft mit der Cloud-Korrektur
  (PC, r104): mit Schalter keine einzige schwarze Zeile, Leons Waffe unten sichtbar; ohne Schalter die Balken wie
  vorher. Nicht geprüft: Ereignisse mit Schalter, Gerät. Nebenbei: `esp08`/`esp0e`/`esp45`/`espgen01` fragen
  `SYS_SCISSOR_ON` ab (Hitzeflimmern, Linsenreflexe rechnen dann mit 56 Zeilen Rand); mit dem Schalter kann das am
  Rand leicht abweichen, nicht gesehen.
- **Patch 0144 (Messer, Fehler des Prototyps, beide Disc-Arten):** EU-`leonModelInit` (`Sscrn.rel` 0x17000, über
  `wep47Init` davor gefunden; `find` traf Krausers Funktion) lädt nach Teil 0xA für Kostüm 1–3 zusätzlich
  `PL_ARC(0x10)` mit `PL_ARC(5)`, wie `cPlLeon::setModel` im Spiel; der Rest der Funktion ist bis auf Register gleich,
  und es ist die einzige Abfrage von `pl_costume` im EU-`Sscrn.rel` (im Debug-Modul keine). `em/pl00.drs` hat auf
  beiden Disc-Arten dieselbe Tabelle (`archive_dump.py`). Der Debug-Start gibt außer in den ersten Dorfräumen
  Kostüm 1 (ohne Jacke). Geprüft: r104, Inventar (Y), Debug- und EU-Discs: eine Zeichnung mehr (240 Dreiecke); ohne
  Tiefentest gezeichnet liegt sie als Messer in der Scheide am Brustgurt, in den Posen mit Pistole, Schrotflinte,
  Gewehr und TMP aber fast ganz hinter Arm und Waffe (wenige Pixel des Griffs sichtbar) – so auch in der EU-Fassung.
  **Offen:** ob der Nutzer etwas anderes meint (Bildschirmfoto mit Kostüm und Waffe).
- **Prüfstrecke** (`whole_r100_check.sh --no-build`, Stand mit 0142–0144, vor dem Abgleich): Debug-Discs **„ALLES OK“**, EU-Discs
  (`--room-info` der Debug-Disc) **„ALLES OK“** (Formatvergleich, Spiellauf, Filme, Speichern und Laden).
  Nicht geprüft: ARM unter qemu, Android-Build, Gerät.

## Bildformat „4:3“ jetzt exakt 4:3 (29.10.2026, Cloud, Branch `golden-master`)

- **Wunsch des Projektinhabers:** echtes 4:3 über die volle Höhe, nichts abschneiden, die Oberfläche richtig
  (Vorschlag 1, Entscheidung 29.10.2026). Bisheriges „4:3“ ist 10:7 (1029×720 auf 1600×720).
- **Befund:** Das Spiel zeichnet in 512×448 (`main_sub.cpp` Render-Modus: fbWidth 0x200, efbHeight 0x1C0, viWidth
  640), der Fernseher zeigte das als 10:7; die 3D-Projektion ist aber 4:3 (`camera.cpp:98`, `C_MTXPerspective(…,
  1.3333334f, …)`). Auf dem GameCube war die 3D-Welt also um 15/14 (≈ 7 %) in die Breite gezogen, die flachen
  Zeichnungen (Anzeigen, Menüs, Texte, in Pixeln von 512×448 gestaltet) nicht.
- **Umsetzung:** Bildformat 3 „4:3“ (ohne Balken) zeigt jetzt in einem exakten 4:3-Rahmen (`core_mode.cpp drawGx`;
  zuerst als eigenes Format 4 „4:3 (exakt)“ gebaut, auf Wunsch des Projektinhabers ersetzt es das bisherige „4:3“;
  ein gespeichertes 4 gilt als 3): Bild über die volle Höhe, Balken des Spiels aus (Patch 0105), alle Zeilen. `GxRenderer::SetWiden(15/14)`: Der
  Vertex-Shader flacher Zeichnungen (orthografisch) hat `uWiden` und verbreitert um die Mitte des Viewports
  (`gx_gl_shader.cpp`); `WidenFor` gibt den Faktor nur Zeichnungen über den ganzen Bildschirm (Viewport ab dem linken
  Rand, mindestens 500 Pixel breit) ohne EFB-Kopie als Textur (Hitzeflimmern, Wasser, Pausenfilter, Unschärfen liegen
  passend über dem Bild) – Zeichnungen in einen Teil des EFB (Bilder für Texturen) bleiben wie gesendet. Fingertipps
  in Untermenüs gehen nach der verbreiterten Ebene (`notePicture`). Menü unverändert („4:3 (GC)“, „4:3“, „16:9“,
  „Gestreckt“).
- **Folgen:** Die 3D-Welt ist im richtigen Verhältnis (Kreise rund), die Oberfläche in ihrer Form vom Fernseher. Die
  flache Ebene ist 15/14 breiter als der Rahmen: ganz außen je etwa 18 von 512 Pixeln (3,5 %) liegen außerhalb, auf
  dem Fernseher war das der Überstand am Bildrand (dort zeichnet das Spiel normalerweise nichts Wichtiges).
  Flache Markierungen, die das Spiel über 3D-Punkte legt (z. B. Ziel- und Hinweiszeichen, Linsenreflexe), sitzen
  seitlich um bis zu 7 % ihres Abstands zur Bildmitte daneben.
- **Geprüft (Cloud):** `gx_gl_check` 0 Fehler, neu `TestWiden` (flach über den ganzen Bildschirm: Spalten 200..300
  werden 191..298; ohne Faktor wie gesendet; in einen Teil des EFB unverändert), Gegenprobe ohne Verbreiterung: 2
  Fehler; strenge Shader-Prüfung 1114 Shader, 0 abgelehnt; `re4decomp_link_check`; alle Java-Quellen mit javac.
  **Nicht getestet:** auf dem Gerät (Anzeigen, Inventar, Händler, Karte, Untertitel, Zielfernrohr, Filme, Tipps im
  Inventar).

## Ton: Kompressor des DSP und kubische Angleichung (29.10.2026, Cloud, Branch `golden-master`)

- **Meldung:** „Audio and sound isn't hd and clear“, Knacken. Bericht (vivo V2426, Android 16, 0.95.0-beta): im Spiel
  keine Aussetzer („ran dry“ nur beim Öffnen/Schließen des App-Menüs), DSP weit unter Budget. Die Ursache liegt also im
  Signal selbst. Zwei Schwachstellen gefunden:
  1. **Kompressor fehlte:** Das Spiel schaltet ihn ein (`snd_main.cpp AXSetCompressor(1)`, `AXCL.c` Befehl 0x12:
     Schwelle 0x8000, 10 Stufen, Tabelle `AXComp.c`); `dsp.cpp` übersprang ihn, laute Stellen wurden hart auf 16 bit
     abgeschnitten (Knacken). Nachgebaut aus dem Aufbau der Tabelle (21 Zeilen zu 160 Werten: Zeile n = von Stufe n
     binnen eines Blocks auf die tiefste Verstärkung 0x50C3 ≈ −4 dB, Zeilen 11..20 = eine Stufe zurück): Erreicht die
     Spitze eines Blocks (Stereo mit eingemischtem Raumklang, bei Dolby auch die hinteren Kanäle) die Schwelle, regelt
     der Block herunter, leisere Blöcke regeln je 5 ms eine Stufe hinauf. Gilt für Stereo und die vier Kanäle.
  2. **Angleichung an den Takt des Geräts linear** (`ai.cpp readRing`): Zwischen zwei Abtastwerten dämpfte das die
     Höhen um bis zu 3 dB bei 8 kHz, schwankend etwa 100-mal je Sekunde (die Lage zwischen zwei Werten wandert mit der
     Angleichung von rund 0,3 %); jetzt 4-Punkt-kubisch (Catmull-Rom), Gleichanteil bleibt exakt.
- **Protokoll:** Die Zeile „audio: …“ nennt jetzt „compressor N blocks, M samples clipped“ (abgeschnittene Werte trotz
  Kompressor) für die nächsten Berichte.
- **Geprüft (Cloud):** `whole_build.sh i686` mit allen Tests 0 Fehler; `core-test ax` neu: zwei Stimmen zu je 20000
  ergeben 25238 statt abgeschnittener 32767, nach dem Stoppen einer Stimme steigt der Ton gleichmäßig auf 20000; die
  Lese-Prüfung der App (1000 bleibt 1000) und der Ton des Kernprozesses (`re4_core_remote_test`) bestehen; `dsp.cpp`
  und `ai.cpp` für arm64 ohne Warnung übersetzt. **Nicht getestet:** Hören auf dem Gerät.

## Tastenbelegung für jede Aktion – Schritt 2: Maus mit Empfindlichkeitsregler (29.10.2026, Cloud, Branch `golden-master`)

- **Wunsch des Projektinhabers:** Schritt 2 (Maus) und ein Regler für die Empfindlichkeit der Maus.
- **Zeiger einfangen (Android 8 und neuer, `requestPointerCapture`):** Solange das Spiel läuft (kein App-Menü, kein
  Bearbeiten der Touch-Tasten, Fenster im Vordergrund), gehört die Maus dem Spiel; der Zeiger verschwindet, die App
  bekommt die reine Bewegung (`OnCapturedPointerListener` auf der Spielfläche). F1 (bzw. die Taste des App-Menüs)
  gibt den Zeiger frei, nach dem Schließen fängt die App ihn wieder ein (auch nach Fensterwechsel und beim ersten
  Klick). Protokollzeile „mouse: captured …“.
- **Bewegung:** dreht beim Zielen das Ziel wie das Wischen (`CoreBridge.aimTurn`, 0,0025 Bogenmaß je Zählschritt mal
  Empfindlichkeit), sonst die Kamera über den C-Stick (`CoreInput.mouseLook`, 60 ms nach der letzten Bewegung zurück
  auf null).
- **Tasten der Maus** (links, rechts, Mitte, Seitentasten 4 und 5) sind Tasten wie die der Tastatur und gehören zum
  selben Gerät (`CoreInput.KEYBOARD`), also auch in Kombinationen: Standard jetzt Zielen = rechte Maustaste,
  Feuern = rechts + links, Nachladen = rechts + R, Messer = C, Messerhieb = C + links (`ActionMap.MOUSE_*`,
  `mouseButton`). Auf der Seite „Tastatur & Maus“ (vorher „Tastatur“) lassen sie sich wie Tasten abfragen.
- **Regler „Mausempfindlichkeit“** auf derselben Seite: 0,25× bis 3× in Schritten von 0,25 (Standard 1×), gespeichert
  als `mouseSensitivity`, mit Hilfetext. Texte in allen zehn Sprachen (`check_translations.py` ok, 198 Texte).
- **Geprüft (Cloud):** `ActionMapTests` 47, `CoreInputTests` 192 (neu: Maustasten mit R aus zwei Eingabegeräten als
  Nachladen, `mouseLook` als C-Stick), `CoreTouchTests` 296, alle Java-Quellen und `CoreMenuLayoutTest` mit javac
  übersetzt. **Nicht getestet:** auf dem Gerät mit Maus (Einfangen, Empfindlichkeit, Freigabe mit F1);
  `CoreMenuLayoutTest` ausgeführt (Gradle am PC). Unter Android 7 und älter bleibt die Maus ohne Einfangen.

## Tastenbelegung für jede Aktion, mit Kombinationen und Tastatur – Schritt 1 (29.10.2026, Cloud, Branch `golden-master`)

- **Wunsch des Projektinhabers:** jede Aktion des Spiels selbst belegen, links die Aktion, rechts die Belegung;
  Aktionen aus zwei Tasten (Zielen + Feuern) bleiben Kombinationen und werden als zwei Tasten abgefragt; Tastatur und
  Maus zum Spielen. **Entscheidungen (29.10.2026):** die Menüs des Spiels behalten die Standardbelegung des Geräts;
  die Maus dreht Kamera und Ziel (eingefangener Zeiger); eine Taste, die eine andere einfache Aktion hatte, tauscht;
  zwei Schritte (1: Controller und Tastatur mit Kombinationen, 2: Maus).
- **Aus dem 27.10.2026 gelernt:** Die erste „alles frei“-Fassung schaltete bei jedem Hinweis („A Öffnen“) auf die
  Standardbelegung um und war unbrauchbar. Jetzt schaltet nur ein echtes Menü um: Der Kern meldet in
  `re4_core_rumble` Bit 4 nur noch Menüs (Titel, Laden, Untermenüs), Hinweise in Bit 5 (nur Protokoll; `pad.cpp`).
  Ein Controller ohne eigene Belegung geht den bisherigen Weg unverändert, die alternative Steuerung behält ihre.
- **`ActionMap.java` (neu, ohne Android, `tests/ActionMapTests.java` 43 Prüfungen):** 16 Aktionen (Vorwärts/Rückwärts/
  Links/Rechts nur Tastatur, Aktion A, Rennen B, Zielen R, Feuern = Zielen-Taste + Taste → A, Nachladen = Zielen +
  Taste → B, Messer L, Messerhieb = Messer + Taste → A, Ashley X, Inventar Y, Karte Z, Optionen START, App-Menü).
  Die erste Taste einer Kombination hält die gehaltene Aktion (R bzw. L), die zweite drückt ihre Taste nur dazu;
  allein tut sie nur, was sie als einfache Aktion tut. Schnelle Drehung (Zurück + Rennen) und die Reaktionstasten
  (A+B, L+R) folgen aus den Tasten ihrer Aktionen. Standard Controller = bisherige Belegung (A, B, R2, R2+A, R2+B, L2,
  L2+A, X, Y, R1, START, SELECT); Tastatur: WASD, E, Shift links, Leertaste, Leertaste+F, Leertaste+R, C, C+F, Q,
  Tab, M, Esc, F1. Fest und nicht belegbar: beim Controller Sticks und Steuerkreuz, bei der Tastatur Pfeile
  (Steuerkreuz), Enter (A), Rücktaste (B). Gespeichert `mappingController`/`mappingKeyboard` in `core_input`; die
  freien Tasten (`keyMap`, 27.10.2026) gehen in der Aktionsliste auf und werden einmal gelöscht (Protokollzeile).
- **`CoreInput`:** gehaltene Tasten je Gerät, Tastaturen als eigene Geräte (`keyboardKey`), eigene Belegung im Spiel,
  in Menüs die Standardbelegung (`setMenu` aus dem Bit des Kerns, `CoreActivity.rumbleTick`), Trigger als L2/R2 ab
  halbem Weg, „Einrasten“ wirkt weiter, das App-Menü auf seiner Taste; `CoreInputTests` 189 Prüfungen (neu: eigene
  Belegung, Feuern auf L2+R2, Menüwechsel mit gehaltener Taste, Einrasten, Tastatur mit Kombination und festen Tasten,
  Tastatur neben Controller).
- **Seite „Tastenbelegung“** (vorher „Controller-Konfiguration“; nutzbar mit Controller oder Tastatur): Gerät wählen,
  jede Aktion links, rechts ihre Taste(n) und der Standard, wo er abweicht; ein Druck wartet auf die Taste, eine
  Kombination auf die zu haltende und dann die zweite (8 s, derselbe Knopf noch einmal bricht ab; Trigger als L2/R2;
  falsches Gerät oder feste Taste wird gemeldet); nach einem Tausch steht da, wohin die andere Aktion kam;
  „Standardbelegung“. `CoreActivity`: echte Tastaturen (`keyboardDevice`: Buchstaben-Tastatur, kein Gamepad) an das
  Spiel, Esc nie an Android. Texte in allen zehn Sprachen (alte `ingame_fn_*` entfernt; `check_translations.py` ok).
- **Geprüft (Cloud):** `ActionMapTests` 43, `CoreInputTests` 189, `CoreTouchTests` 296, `whole_build.sh i686`, alle
  Java-Quellen und `CoreMenuLayoutTest` (neu: `buttonMappingPutsActionsOnKeysAndCombinationsOnTwo`, Seiten in 10
  Sprachen auch für die Tastatur) mit javac übersetzt, `re4decomp_link_check`. **Nicht getestet:** `CoreMenuLayoutTest`
  ausgeführt (Gradle am PC); auf dem Gerät mit Controller und mit einer Tastatur. Schritt 2 (Maus) siehe oben.

## Touch-Zielen mit der Touchfläche: Wischen statt Stick (29.10.2026, Cloud, Branch `golden-master`)

- **Meldung (Nutzer, vom Projektinhaber weitergegeben):** Mit „Touch Aiming: Touch Area“ wirkte der Fingerweg wie ein
  ausgelenkter Stick: kurz ziehen und stehen bleiben, das Ziel lief weiter. Gewünscht wie in Handy-Shootern: das Ziel
  folgt dem Finger und steht, wenn er steht.
- **Umsetzung:** Solange L oder R mit „Touch Area“ gehalten wird, geht die Bewegung des Fingers in der Blickzone
  (`CoreTouch.look` → `CoreInput.touchSwipe`) nicht mehr auf den Steuerstick und nicht auf den C-Stick, sondern als
  Winkel an den Kern (`CoreInput.takeAimSwipe` nach jedem Ereignis → `CoreBridge.aimTurn` → `re4_core_aim_turn`,
  im eigenen Kern-Prozess Nachricht `kAimTurn` 33). `gyro.cpp` summiert die Winkel bis zum nächsten Spielbild und dreht
  dort wie das Gyro (Körper und Zielneigung, Zielfernrohr mit Zoom-Faktor, Harpune im Boot), ohne Verstärkung;
  `turnAim` ist jetzt der gemeinsame Teil von Gyro und Wischen (Gyro unverändert: Rate × 0,35 × 1/30 s).
  Empfindlichkeit `CoreInput.SWIPE_RADIANS` = 1,6 rad je kürzere Bildschirmseite (ein Wisch quer darüber etwa 90°).
  Der Touch-C-Stick zielt weiter wie ein Stick (er ist einer). Hilfetext in allen zehn Sprachen angepasst.
- **Geprüft (Cloud):** `CoreTouchTests` 296 Prüfungen (neu: kein Stick und kein Blick beim Wischen, Winkel = Weg ×
  1,6, steht der Finger, kommt nichts mehr, zwei Bewegungen addiert, ohne R schaut die Zone wieder), `CoreInputTests`
  170, Laufzeittest `gyroboat` erweitert (Wisch-Winkel addiert und genau einmal gedreht, im Untermenü verworfen),
  `whole_build.sh i686`, alle Java-Quellen mit javac, `re4decomp_link_check` gebaut. **Nicht getestet:** auf dem Gerät
  (Gefühl, Empfindlichkeit; ein Regler dafür wäre ein nächster Schritt).

## Pixelmitte wie der GameCube: Fadenkreuz des Zielfernrohrs bei 1× (29.10.2026, Cloud, Branch `golden-master`)

- **Meldung:** Mit dem Gewehr zeigt das Zielfernrohr nur die senkrechte Linie (Bildschirmfoto des Nutzers, 640×336,
  Auflösung 1×). Das Fadenkreuz sind 2D-Einheiten des Spiels (`IdScope`, `cam_extra.cpp`; texturierte Vierecke,
  `IdCommonTrans`); die waagerechte Linie liegt in der Bildmitte, also auf der Grenze zweier Pixelreihen, und ist
  dünner als ein Pixel.
- **Ursache:** Der GameCube prüft ein Pixel bei 7/12 seiner Breite und Höhe, GLES bei 1/2 (Dolphin gleicht das als
  „pixel center correction“ aus). Eine Fläche dünner als ein Pixel auf einer Pixelgrenze trifft dort eine Reihe, bei
  uns keine. Bei 2× ist die Linie mindestens ein Pixel hoch, darum nur bei 1×.
- **Fix (`gx_gl_shader.cpp`, Vertex-Shader):** alles um 1/12 GameCube-Pixel nach links oben, wie Dolphin. Die
  Shader-Programme werden dadurch einmal neu gebaut (der Programm-Zwischenspeicher hängt am Quelltext; die
  Vorab-Liste baut sie vor ihrer ersten Zeichnung).
- **Geprüft (Cloud):** neuer Test in `gx_gl_check` (`TestPixelCentre`: waagerechte und senkrechte Linie 0,94 Pixel
  dick auf einer Pixelgrenze in genau einer Reihe/Spalte, Kanten bei 10,55 und 50,65 wie beim GameCube); Gegenprobe
  ohne die Korrektur: beide Linien fehlen ganz, wie auf dem Foto. `gx_gl_check` 0 Fehler, strenge Shaderprüfung
  (glslangValidator) 1114 Shader, 0 abgelehnt, `re4decomp_link_check` gebaut. **Nicht getestet:** auf dem Gerät
  (Zielfernrohr bei 1×; ob sich anderswo eine Kante sichtbar verschiebt – höchstens 1/12 Pixel).

## Cheats: automatische QTEs und Extra-Spiele freischalten (29.10.2026, Cloud, Branch `golden-master`)

- **Wunsch des Projektinhabers:** zwei weitere Cheats in einer eigenen untersten Reihe, zwei gleich breite Knöpfe:
  „Automatische QTEs“ und „Extra-Spiele freischalten“ (Assignment Ada, The Mercenaries; mit Neustart ins Titelmenü,
  dort wechselt auch der Hintergrund). Einzelheiten: `port/docs/cheat-menu-plan.md`, „Erweiterung 29.10.2026“.
- **Kern:** `runtime/qte_touch.cpp` `re4_qte_auto` (Automatik auf dem Weg von „QTE per Tipp“; nicht einfaches A,
  Stick-Symbole, X), `runtime/cheats.cpp` Bit 8 (QTEs) und 16 (Extra-Spiele behalten), Knopf 4 (Merker wie `r333.cpp`
  ohne Kostüm), `re4_core_restart_game(0)` = Neustart mit gleicher Sprache, `RE4_TEST_CHEATS` kennt Knopf 4. Die
  Bild-Haken (`pad.cpp`) haben jetzt 16 statt 8 Plätze (in der App waren schon 7 belegt).
- **App:** `CoreMenu.cheats()` unterste Reihe (`MenuLayout.Buttons` mit 2 Spalten), Rückfrage „Spiel neu starten?“
  (Neustart/Später), Meldungen; `Host.restartGame()`; neue Texte in allen zehn Sprachen (`check_translations.py` ok).
- **Geprüft (Cloud):** `whole_build.sh i686` mit allen Prüfungen; neue Laufzeittests `qteauto` (L+R 2 an/2 aus,
  Hämmern jedes zweite Bild, nichts bei A, X, Untermenü, inaktivem Knopf, aus) und `cheatextras` (Merker gesetzt, kein
  Kostüm, Rekorde einmal gelöscht, danach „schon da“, Bit 16 setzt sie nach einer Systemdatei ohne sie wieder) bestanden;
  alle Java-Quellen und `CoreMenuLayoutTest` (neu: unterste Reihe, Bit 8) mit javac übersetzt. **Nicht getestet:**
  `CoreMenuLayoutTest` ausgeführt (Gradle am PC), auf dem Gerät (QTEs in Szenen wie Felsen r101/r104, Messerkampf r317,
  El Gigante; Titelmenü nach dem Neustart mit beiden Disc-Arten; Ada und Mercenaries starten).

## Leistung auf Handys mit zwei schnellen Kernen, Schritt 1 (29.10.2026, Cloud, Branch `golden-master`)

- **Anlass (Testerberichte 28.10.2026):** Redmi 15C (Helio G81: 2× Cortex-A75 2,0 GHz, 6× A55 1,7 GHz, Mali-G52 MC2,
  EU-Discs, Auflösung 1×, kein MSAA, Force Performance an) im Dorf r101: 27–35 Bilder/s, „GX render thread: 29.8 ms of
  processor time per chunk of 37.7 ms run“ bei 862 Zeichnungen (≈ 35 µs je Zeichnung), „GX core: 43.3 ms each, 1.5 ms
  waiting for the renderer, 0.4 ms idle, 22.0 ms of processor time“ – das Spiel selbst schaffte dort nur ~23 Bilder/s,
  ~19 ms je Bild lief der Spiel-Thread nicht. Spiel- und Zeichen-Thread lagen beide auf den Kernen 6–7, die drei
  Eckpunkt-Threads (mit dem Leistungshinweis) dazu. Vergleich Xiaomi 11 Lite NE (Snapdragon 778G, Adreno 642L): 873
  Zeichnungen in 12,2 ms bei 1,2 GHz (≈ 14 µs). Galaxy S7 (Snapdragon 820) in r108: GPU 23–31 ms je Bild gemessen,
  Zeichen-Thread 15–22 ms, die schnellen Kerne gedrosselt auf 1,25–1,32 GHz – dort ist die GPU der Engpass.
- **Vulkan bis auf Weiteres ausgeschlossen** (Entscheidung des Projektinhabers, 29.10.2026, nach der Aufwandsschätzung:
  neuer Grafikteil statt der ~5 400 Zeilen von `gx_gl.cpp`/Shader-Erzeuger, Pipelines, Shader-Übersetzer, alle Extras und
  PC-Prüfwerkzeuge neu, GLES bliebe für ältere Geräte; der Spiel-Thread würde nicht schneller). Weiter mit GLES.
- **Licht je Pixel (Nutzerfrage) nicht gewünscht** (Entscheidung des Projektinhabers, 29.10.2026): Es würde schwache
  Geräte nicht schneller machen, sondern mehr GPU-Arbeit kosten; das Licht bleibt je Eckpunkt wie beim GameCube.
- **Umsetzung (Entscheidung des Projektinhabers, 29.10.2026: Leistungsoptimierung sofort):**
  - `re4_cpu_info.h` `PutOnSlowCores`: Mit „Force Performance: Use performance cores“ kommen die Eckpunkt-Threads auf die
    langsamste Kernart, wenn höchstens zwei Kerne schneller sind (die zwei bleiben dem Spiel- und dem Zeichen-Thread).
    Mehr schnelle Kerne (Retroid: vier) ändern nichts. Logzeile „cores: the 3 vertex threads on cores 0 1 2 3 4 5, the
    slowest (only 2 faster cores)“ bzw. „left to Android (…)“. Etwa einmal pro Sekunde zurück, wenn Android sie
    verschoben hat (wie beim Spiel-Thread); „Default“ gibt sie wieder frei. `RE4_HELPERS_SLOW=0` schaltet es ab.
  - `gx_gl.cpp` / `GxStats::splitNs`: Die Laufzeit des Zeichen-Threads je Art der Arbeit (steady clock, jeder Run),
    neue Logzeile „GX render thread split per chunk: draw calls …, uniform blocks …, binds …, render state …, vertex
    buffers …; shader keys and programs …, textures in …, BP …, XF …, the rest …“ – zeigt auf dem Mali, wie viel der
    Treiber (die ersten fünf) und wie viel unser eigener Code braucht. Grundlage für Schritt 2 (weniger Treiberaufrufe
    je Zeichnung) und die Vulkan-Frage.
  - Nebenbei: `runtime-test deathlog` schlug auf `master` fehl (die Raum-Zeile ist seit `game_mode`/`game_country`
    länger als die 119 Zeichen, die der Protokollspeicher je Zeile hält); der Test vergleicht jetzt nur diesen Teil.
- **Geprüft (Cloud):** `whole_build.sh i686` 0 Fehler, alle Prüfungen bestanden (runtime-test 0 failed, neu: Hilfs-Thread
  eines Linux-Threads mit „zwei schnellen Kernen“ landet auf den anderen, Freigabe, Retroid- und A30s-Aufteilung);
  `gx_gl_check` 0 Fehler (neu: die Aufteilung ergibt zusammen die Laufzeit eines Runs, Mesa: 2400 Zeichnungen
  6,6 ms, davon Zeichenaufrufe 4,2 ms); `re4decomp_link_check` (ganze native App-Bibliothek) gebaut. **Nicht getestet:**
  auf einem Gerät, ob die Verteilung auf dem Redmi 15C oder dem S7 mehr Bilder bringt; die Aufteilung auf dem Mali.

## Ingame-Menü: Dreierreihen in Vierteln (28.10.2026, Cloud, Branch `golden-master`)

- **Auftrag (Projektinhaber):** Unter STEUERUNG die Knöpfe „Touch-Zielen“ mit demselben Abstand zu den Knöpfen
  darüber wie die Zweierreihen untereinander, und je zwei der drei Knöpfe so breit wie ein großer Knopf darüber;
  die Knöpfe der Spielsprache genauso zu den großen Knöpfen (Leistung, Verwaltung …).
- **Umsetzung:** `CoreMenu.quarterChoices`: Beschriftung im ersten Viertel der Zeile, jeder Knopf ein Viertel
  (`MenuLayout.Columns.setWeights(1, 3)`), ohne den zusätzlichen Rand von 3 dp oben und unten (nur die 4 dp der
  Knöpfe, wie zwischen den Zweierreihen). Damit liegen Knopf 2 und 3 genau unter dem rechten großen Knopf, Knopf 1
  unter der rechten Hälfte des linken. Gilt für Spielsprache, Touch-Zielen und – damit die beiden Dreierreihen der
  Seite übereinander stehen – auch für Touch-Steuerung. Lange Übersetzungen brechen dort erst auf zwei Zeilen um,
  bevor die Reihe einen Knopf verliert (`MenuLayout.Buttons(…, lines)`); schmale Fenster (unter 640 dp) stapeln wie
  bisher.
- **Geprüft (Cloud):** alle Java-Quellen der App mit javac gegen die Android-Klassen (Robolectric `android-all` 14)
  und die RetroTouch-AAR übersetzt (0 Fehler); `check_translations.py` ok. **Nicht getestet:** auf dem Gerät
  (Aussehen, lange Übersetzungen); ein Messtest mit Robolectric scheiterte am gesperrten Google-Maven.

## Schwierigkeitsauswahl und Easy-Zweige der EU-Discs (28.10.2026 nachts, lokal, Branch `claude/determined-allen-36nj6r`)

- **Auftrag:** Die EU-Fassung fragt beim neuen Spiel nach der Schwierigkeit, der Debug-Code nicht. Abfrage einbauen
  wie im EU-Code und alle Stellen prüfen, an denen der EU-Code das „über Easy“-Byte liest.
- **Wichtigster Befund:** Das Byte (EU `pG+0x4F9F`) ist **`game_country`** (Debug `0x4F93`; das EU-`GLOBAL_WK` ist dort
  um 0xC verschoben). Die EU-Fassung macht daraus in `gameStageInit` (main.dol 0x80118a28) bei jedem Abschnittsstart
  die Schwierigkeitsklasse: 0 = Easy, 1 = Normal/Professional (0 in den Bonusspielen nur mit japanischen Texten).
  Damit schalten die Szenenbereichs-Flaggen der Daten „JPN“/„USA“ (`SceAtRoomSet`) auf Easy bzw. darüber ab,
  `GameAddPoint` hält den Schwierigkeitswert nur über Easy bei mindestens 1000, und die Räume verzweigen selbst. Der Port
  hatte dort die Sprache (2–6, Patch 0121) – also immer „über Easy“.
- **Patch 0139** (`title.cpp`): Abfrage wie EU-`titleMain` (main.dol 0x80165d58, Menü 0x80165370/0x80165570): „New
  Game“ öffnet immer Professional (nur wenn freigeschaltet, `EXT_HARD_MODE`) / Normal (Start) / Easy, A oder START
  wählt (`game_mode` 6/5/3), der Cursor läuft rundum und überspringt Professional, B zurück auf „New Game“. Die
  EU-Rückfrage danach (Kernmeldungen 0xA8–0xAA mit Ja/Nein) gibt es nur auf Japanisch (die EU-Sprachen haben dort
  „aaa“) – weggelassen. Beim Debug-Start der Prüfungen (`RE4_START_ROOM`) keine Abfrage; neuer Prüfschalter
  `RE4_TEST_GAME_MODE=N` (setzt die Schwierigkeit im Debug-Startmenü) und `game_mode`/`game_country` in der Zeile
  „room: … runs“.
- **Patch 0140** (`game.cpp` `gameStageInit`): `game_country = game_mode > 3` wie die EU-Fassung (nur EU-Discs).
- **Patch 0141**: r203 (Normal: Meldungsbereich 6 an Tür 5 aus – im Port blieb er an der offenen Tür stehen; Easy: Tür 5
  zu, Tür nach r201 ohne Schlüssel, weniger Ganados), r211 (Easy: erster Kelch eingesetzt, Modellgruppe 0x20 mit
  eigener Kollision aus Raumarchiv-Eintrag 5; Normal: Gruppe 0x20 aus – im Port stand sie ohne Kollision da),
  Karte (Easy: Objekte aus `SS/Esy`).
- **Geprüft am PC** (i686, EU-Discs, frische Speicherkarte): Titel → START → Menü NORMAL/EASY (Bild), Easy →
  `game_mode 3`, Normal und START → 5, B zurück und Umblättern (Normal→Easy) → 3. Debug-Discs: Spiel startet wie
  bisher sofort (`game_mode 5`). Debug-Start r203: Normal Bereich 6 aus, Tür 5 offen; Easy Bereich 6 an, Tür 5 zu;
  Debug-Discs ohne Bereich 6 wie bisher. r211 Easy: Bereich 4 aus (Kelch eingesetzt), Normal/Debug unverändert.
  Karte auf Easy lädt `SS/esy/map_obj2a.dat`, auf Normal `SS/cmn/…`. r20b auf Easy läuft. Keine Fehlermeldungen.
  **Nicht geprüft:** Gerät; Professional-Eintrag (braucht einen Spielstand mit Freischaltung); ob Bereich 6 in r203
  die Tür im Port wirklich blockierte (die EU-Fassung schaltet ihn auf Normal ab, das genügt); ganze Kapitel auf Easy.
- **US-Discs:** Der US-Code (Branch `retail-us`) fragt beim neuen Spiel nicht (wie der Debug-Code: direkt Normal,
  Professional/Normal erst nach Freischaltung, A oder START), kennt kein Easy und setzt `game_country` nicht als
  Schwierigkeitsklasse (keine Schreibstelle außer Sprache/Reset). Der Port verhält sich mit US-Discs damit schon wie
  die US-Fassung; die Patches 0139–0141 gelten nur für EU-Discs. Entscheidung des Projektinhabers (28.10.2026): mit
  US-Discs bleibt es wie im Original (kein Easy).
- **Prüfliste der EU-Stellen** („Easy“ = nur auf Easy anders; die Normal-Seite entspricht dem Debug-Code; nicht
  übernommen heißt: der Port spielt dort auch auf Easy die Normal-Fassung):

| Stelle (EU) | Funktion | Befund |
|---|---|---|
| main 0x80118a28 (Schreiben) | `gameStageInit` | `game_country = game_mode > 3` → **Patch 0140** |
| main 0x801680d0 / 0x80124fd4 / 0x801253ac / 0x801247e0 (Schreiben) | Titel-Ende / `systemWorkInit` / `setLanguage` / Reset-Rückholen | wie Debug (Sprache bzw. Rücksicherung), 0x801680d0 wie 0140 |
| main 0x80004c3c | `CameraControl::areaHitCheck` | Ashley-Kamera nur mit japanischen Texten – EU-Sprachen wie Debug, nichts zu tun |
| main 0x80118860 | `gameInit` | `game_mode` 0 → 3/5; nie erreicht (`systemWorkInit` setzt 5) |
| main 0x80119cf4 / 0x80119ed8 | `clearGlobalSaveData` | Wert bleibt erhalten, wie Debug |
| main 0x801251e4 | Reset-Sicherung (`pRK`) | wie Debug |
| main 0x801521ac | `SceAtRoomSet` | Bereichs-Flaggen JPN/USA, gleicher Code; Bedeutung Easy/Normal → **Patch 0140** |
| main 0x80155810 | `sceAtSetItem` | prüft die Flaggen schon vorab; Debug legt an und löscht gleich wieder – gleiches Ergebnis |
| `st1_3` 0xc038 | `r11c_EventBesiegedStart` | Belagerung 9000 Bilder/40 (Normal = Debug), Easy 7200/30 – Easy, nicht übernommen |
| `st2_0` 0xa558, 0xa5f8, 0xa6e0 | `R203Init` | Normal: Bereich 6 aus (**Sperre**), Easy: Tür 5 zu, Schlüsselflagge, weniger Ganados → **Patch 0141** |
| `st2_1` 0x4af0, 0x4f88 | `R205Init`, `R205Main` | Ganados 0x6D/0x57, Wellen 106/111 nur über Easy – Easy |
| `st2_1` 0xc8ec, 0xca40, 0xcbcc, 0xcda4, 0xd8d8 | `r209` zweiter Kampf, Armbrust, Brücke | weniger/spätere Gegner auf Easy – Easy |
| `st2_1` 0x10f80, 0x1147c | `R20bInit` | Normal: Wand 0x1C/Brüstung 0xA6 weg (**Sperre**), Easy: Mondstein-Zustand → **Patch 0138** |
| `st2_1` 0x148d4 | `R20cExecCageMain` | Raum-Flagge auf Easy oder niedrigem Rang – Easy |
| `st2_1` 0x1cc38 | Gegnersatz r20f (EU 0x1cc14) | andere Gegner 0xDC–0xEC auf Easy – Easy |
| `st2_2` 0x3524 | `R211Init` | Normal: Gruppe 0x20 aus, Easy: Kelch + Gruppe 0x20 mit Kollision → **Patch 0141** |
| `st2_2` 0x80b4, 0x83d4, 0x84b0 | `R213SuBreakModel`, `R213EmSet` | weniger Gegner auf Easy – Easy |
| `st2_2` 0xa448 | `R214Init` | Gegnersatz je Schwierigkeit in eigener Hilfsfunktion – Easy |
| `st2_2` 0x1146c, 0x11a48, 0x11d90 | `R218Init`, `r218_checkClawManDead`, `r218_appearClawMan` | ein Garrador weniger auf Easy – Easy |
| `st2_3` 0x3644, 0x38f4 | `R21aInit` (EU 0x3604) | Easy: Raum-Flagge, Bereich 8/Gruppe 0x41 aus – Easy |
| `st2_3` 0x3ce0, 0x4194 | `R21aEmSetMain` | weniger Gegner auf Easy – Easy |
| `st2_3` 0x62b0, 0x7240, 0x72dc, 0x73e8, 0x7440 | `r21b_HalfWayGanadoSet`, `r21d` Gegnersätze | Werte/Gegner nur über Easy – Easy |
| `st2_3` 0xa128, 0xb5b0, 0xbde8 | `r221` Rolltor, Aufzug, Boss | Werte auf Easy – Easy |
| `st2_3` 0xc4e4 | `r221_checkSwitchboard` | Easy eigener Hinweistext (0xA statt 4) – Easy |
| `st2_3` 0x15770, 0x15db8, 0x1613c | `R226` Gegner, Roboter-Start | Werte auf Easy – Easy |
| `st2_3` 0x1a414 | `r227_setEmOnElv1` | Ganados 0x85/0x86 nur über Easy – Easy |
| `st3_2` 0x15db4, 0x16324 | `r31c_CountDownEnd`, `r31c_TowerExplodeEndProc` | Anzeige/Kamera auf Easy – Easy |
| `Sscrn` 0x10fe4 | `mapAreaFilename` | Easy: Kartenobjekte aus `SS/Esy` → **Patch 0141** |

  Zuordnung mit `retail_code.py`, den Relokationen der Module und einer Aufruf-Abstimmung (Hilfsskripte nur in der
  Sitzung). Nur r20b und r203 hatten auf Normal eine Easy-Sperre der Daten aktiv; Patch 0138 liest weiter `game_mode`
  (gleiches Ergebnis außer in den Bonusspielen, die r20b nicht betreten).

## Testerbericht „Zugang zum Labyrinth versperrt“ (28.10.2026 abends, lokal, Branch `claude/determined-allen-36nj6r`)

- **Meldung:** Nutzer mit Xiaomi 2412DPC0AG (0.94.0, EU-Discs, Spanisch, Normal: `game_mode` 5 im Spielstand),
  Spielstand `bh4_data00` in r20a; Bildschirmfoto: Leon auf dem Podest über der Treppe in r20b, an der Brüstung, das
  Labyrinth unten. Der Projektinhaber hat es auf dem Retroid mit 0.94.0, EU-Discs (Deutsch) und dem Spielstand
  nachgestellt: alle Szenen kommen, der Abgang ist versperrt.
- **Ursache (Verkaufsdaten):** Die EU-Discs sperren in r20b die Treppe vom Podest ins Labyrinth für **Easy**:
  Bereich 0x1C (Typ 0xB, eine unsichtbare Wand quer über ihr oberes Ende, (37900 −8064)…(37911 −7676), Höhe
  3196–5066) und Modellgruppe 0xA6 (die Brüstung darüber). Die Debug-Discs haben beides nicht (dort ist die Treppe
  frei; darum lief das Labyrinth mit ihnen). Die EU-Fassung von `R20bInit` (`st2_1.rel`, Text 0x10F44, mit
  `retail_code.py` und den Relokationen des Moduls gelesen) öffnet sie über Easy: ein Byte (EU `pG+0x4F9F`, gesetzt
  auf `game_mode > 3`, in den Bonusspielen 0) → `SceAtSetEnable(0x1C, 0)`, `SmdSetTrans(0xA6, 0)`. Auf Easy bleibt
  das Labyrinth zu und der Raum tut, als sei das Mondstein-Tor schon offen (Raum-Flag 1; Truhen/Gegenstände 0x86–0x8A,
  Bereiche 0x80–0x82 und 0x88, Kisten 9/10/14/15 weg; kein Funkspruch, keine Eingangskamera, keine Gegner). Der
  Debug-Code hat keinen der beiden Zweige.
- **Fix: Patch 0138** (`r20b.cpp`, nur mit Verkaufsdaten, `re4_disc_retail_eu`): beide Zweige wie der EU-Code, das
  Byte aus `game_mode`. Der Originalweg (Komplettlösung StrategyWiki/Fandom: Kante, verschlossene Tür, Funkspruch
  Salazars „do you like my garden?“, dann die Treppe hinunter zum Tor ins Labyrinth) ist damit wieder da.
- **Geprüft am PC** (i686, EU- und Debug-Discs, Debug-Start in r20b, Normal): Leon läuft vom Podest (39700 4000 −7000)
  die Treppe hinunter bis (39710 0 −21886) im Labyrinth; vorher blieb er bei z −7285 stehen. Keine Fehlermeldung
  (`AT NOT FOUND` / `INVALID INDEX`) mit beiden Disc-Arten. **Auf dem Retroid bestätigt** (Projektinhaber,
  28.10.2026, EU-Discs, Spielstand des Nutzers): Brüstung weg, Treppe begehbar. Easy: siehe Abschnitt
  Schwierigkeitsauswahl (Patches 0139–0141).
- **Weitere EU-Räume mit demselben Byte:** gelesen in `st2_0`, `st2_1` (11-mal), `st2_2`, `st2_3`, `st1_3`, `st3_2`
  und `Sscrn` (EU) – geprüft, siehe Abschnitt Schwierigkeitsauswahl (das Byte ist `game_country`).
- **Schwierigkeitsgrad:** Die Debug-Fassung fragt beim neuen Spiel nicht nach Leicht/Normal (Dolphin mit EU-Discs
  zeigt die Abfrage nach START, Projektinhaber 28.10.2026) – mit EU-Discs eingebaut (Patch 0139).
- **Neue Prüfschalter** (`state_trace.cpp`): `RE4_TRACE_AT=1` (Auslöse-Bereiche mit Türzielen, Kollisionswänden und
  den Türen im Raum), `RE4_TRACE_POS=N` (Leons Ort alle N Bilder), `RE4_TEST_PLACE2="N:x,y,z,a"` (zweites Versetzen,
  z. B. nach einer Szene), `RE4_TEST_AT_OFF`, `RE4_TEST_SMD_TRANS` (Bereiche / Modellgruppen schalten),
  `RE4_FLOOR_MAP`, `RE4_FLOOR_FILL` (Boden-, Wand- und grobe Erreichbarkeitskarte; die Linienprüfung zählt Wände von
  beiden Seiten, Leons Kugel nur von vorn).

## Leistung: Vorschläge 1 und 2 gemessen (28.10.2026 abends, lokal, Branch `claude/determined-allen-36nj6r`)

Auftrag des Projektinhabers: Vorschlag 1 (umgewandelte Modelle aufheben) und 2 (doppelte Uniform-Übergaben sparen).

- **Vorschlag 2, eingebaut:** `gx_gl.cpp` `UploadXf` vergleicht die geänderten Zeilen eines XF-Teils mit dem zuletzt
  gesendeten (`xfSent`) und lässt gleiche aus (`GxStats::uniformSame`, in „GX work per chunk“ als „the same not
  sent“); ungültig nach jedem Wechsel des Ring-Viertels und zu Beginn jedes Chunks (die alten Plätze können
  überschrieben sein). PX wurde schon so verglichen. `RE4_GX_UNIFORM_SAME=0` schaltet es ab (Gegenprobe). Messung mit
  der Retroid-Serie r226 (`gx_chunk_render`, 48 Chunks): 2 011 von 43 345 Übergaben gespart (**knapp 5 %**, nicht „viele“
  wie vermutet: die Spielwerte ändern sich fast bei jeder Zeichnung). Bilder mit und ohne pixelgleich: 48 Bilder, mit
  60-FPS-Zwischenbildern 92 Bilder (44 Zwischenbilder). `gx_gl_check` 0 Fehler.
- **Vorschlag 1, gemessen und verworfen:** ein Zwischenspeicher für die umgewandelten Eckpunkte jeder
  Zeichenlisten-Zeichnung (gültig, solange die Listenbytes, das Vertexformat, die Arrays und deren gelesene Bytes per
  Prüfsumme gleich sind). Korrekt (r100 mit `RE4_GX_DL_CHECK`, jeder Treffer zusätzlich umgewandelt und verglichen:
  0 Abweichungen), aber nur 57–78 % Treffer, und Prüfen + Kopieren auf dem Kern-Thread kosten mehr als die schon auf
  3 Threads verteilte Umwandlung: r100 am PC 4,2 → 8,0 ms Kern-Thread je Bild (Batches 1,9 → 0,7 ms), zusammen mehr
  Prozessorzeit. Nicht eingebaut. Ein Weg mit Aussicht bräuchte Schreibüberwachung des Spielspeichers statt
  Prüfsummen (aufwendig, eigene Entscheidung).
- **Neu für Messungen am PC:** `RE4_GX_DRAIN=1` (`core_api.cpp`): jeder Chunk wird abgenommen und gleich
  freigegeben, so zeichnet der Kern ohne Renderer jedes Bild auf wie mit der App (die „GX core“-Zeile im PC-Lauf).

## Touch-Zielen (28.10.2026 abends, lokal, Branch `claude/determined-allen-36nj6r`)

- **Wunsch des Projektinhabers:** in STEUERUNG eine Option, mit der Touchfläche oder dem C-Stick zu zielen.
- **Umsetzung:** Menü STEUERUNG, unter „L und R einrasten“/„B einrasten“: Auswahl **„Touch Aiming“** mit „Move Stick“
  (Vorgabe, wie am GameCube), „Touch Area“ (die rechte Blickzone) und „C Stick“ (der Touch-C-Stick, den man über das
  Zahnrad von RetroTouch hinzufügt), Hilfetext darunter; Texte in allen zehn Menüsprachen (`check_translations.py`
  ohne Fehler). `CoreTouch` meldet Blickzone und Touch-C-Stick getrennt (`CoreInput.touchLook`); `CoreInput.pad`: solange
  L oder R gehalten wird (Touch, eingerastet oder ein Controller), wird die gewählte Quelle zum Steuerstick (der weiter
  ausgelenkte von ihr und dem Bewegungsstick zielt), die andere bleibt C-Stick; ohne L/R sind beide wie bisher der
  C-Stick. Wie bei der Blickzone üblich: der Weg des Fingers seit dem Aufsetzen ist die Auslenkung (wie ein Stick,
  weiter = schneller), Loslassen hält das Ziel an. Einstellung `touchAim` in den Eingabe-Einstellungen, Logzeile
  „touch aiming: …“. Vorbild: die alternative Belegung (rechter Stick zielt, solange L1/L2 gehalten).
- **Tests:** `CoreTouchTests` mit neuen Prüfungen (Vorgabe ändert nichts, Blickzone zielt und der C-Stick bleibt
  C-Stick, umgekehrt mit „C Stick“, der weitere Weg zielt, R los: beide wieder C-Stick, falscher Wert = Vorgabe):
  291 Prüfungen bestanden; `CoreInputTests` 170 bestanden. APK 0.95.0-beta gebaut.
- **Nicht getestet:** auf dem Gerät (Gefühl beim Zielen, ob die Empfindlichkeit passt).

## RVZ beim Disc-Import (28.10.2026 abends, lokal, Branch `claude/determined-allen-36nj6r`)

- **Wunsch des Projektinhabers:** RVZ (Dolphins gepacktes Format) neben ISO/GCM; beim Import ein Hinweis, dass das
  Entpacken mehr Speicher braucht; vorher prüfen und bei zu wenig Speicher ablehnen, mit Verweis auf ISO/GCM.
- **Umsetzung:** `app/src/main/cpp/disc_rvz.cpp` liest RVZ einer GameCube-Disc (Kopf, Tabellen der Rohdaten und
  Gruppen, Gruppen gespeichert oder mit zstd, „gepackte“ Gruppen mit Daten- und Füllbyte-Läufen; die Füllbytes
  erzeugt der Generator aus Dolphins `LaggedFibonacciGenerator` aus dem gespeicherten Startwert). zstd: der offizielle
  Einzeldatei-Entpacker aus zstd 1.5.6 (BSD, `third_party/zstd/`, Prüfsumme des Release-Archivs geprüft). Eigene
  Bibliothek `libre4rvz.so` (`disc_rvz_jni.cpp`, Java `disc/RvzImage.java`), weil `libre4decomp.so` beim Laden den Kern
  lädt. `ImportManager`: erkennt RVZ an den ersten Bytes, prüft vor dem Kopieren den Platz für RVZ-Kopie + entpacktes
  Abbild + entpackte Dateien (`RvzImage.storageNeeded`, etwa 2,9 GB plus RVZ-Größe bis zum Löschen der Kopie), packt
  nach `source.gcm` aus (Fortschritt „unpacking RVZ“), löscht die RVZ-Kopie, dann der gewohnte Import mit Prüfung aller
  Dateien. Zu wenig Speicher: „Import not possible …“ mit Bedarf und freiem Platz; der Verweis auf ISO/GCM sagt dazu,
  dass auch sie zuerst kopiert werden und fast so viel brauchen. Abgelehnt mit Erklärung: Wii-Discs, bzip2/LZMA
  (Hinweis: in Dolphin mit Zstandard neu umwandeln oder ISO/GCM), beschädigte Dateien. Überschrift der Startseite und
  Starttext nennen RVZ und den Speicherbedarf.
- **Tests:** `rvz_check` (`tools/test_rvz.cpp`, Host-Build): gespeicherte und zstd-Gruppen (zstd-Rahmen aus
  Rohblöcken), gepackte Gruppen mit Füllbytes (gegen eine zweite, wörtliche Fassung von Dolphins Generator), eine
  Null-Gruppe, kurze letzte Gruppe, Disc-Kopf, Fortschritt, Abbruch, vier Ablehnungen: 0 Fehler. Echte EU-Disc 1 als
  RVZ verpackt (`tools/make_rvz.py`, zstd-Rahmen ohne Packen) und mit `rvz_check IN OUT` ausgepackt: SHA1 gleich der ISO,
  1,5 Minuten am PC. APK mit `libre4rvz.so` für ARM32 und ARM64 gebaut.
- **Nicht getestet:** eine RVZ, die Dolphin selbst geschrieben hat (echte zstd-Kompression, gepackte Füllbytes von
  der Disc), und der Import auf dem Gerät. Die Füllbytes liegen nur zwischen den Dateien; ein Fehler dort fiele bei der
  Dateiprüfung nicht auf, schadet aber auch nicht.

## Weiße Texturen mit 60 FPS und Version 0.95.0-beta (28.10.2026 abends, lokal, Branch `claude/determined-allen-36nj6r`)

- **Meldung:** Nutzer mit itel P661N (Mali-G57 MC2, Debug-Discs, 60 FPS an): Ganados mit weißen Waffen, manche ganz
  weiß. Seine Berichte (0.93.0 und 0.94.0): „missing“ in der GX-Zeile steigt nach 7–17 Minuten von 0 auf über 11.000
  (r101, r104) und fällt nicht mehr; der Bericht vom Xiaomi (0.93.0, 60 FPS) zeigt 3–7, die Retroid-Berichte ohne
  60 FPS 0. Eine fehlende Textur zeichnet der Renderer weiß (`BindTexture`, `whiteTex`).
- **Ursache:** Der Kern vergisst Texturen zu Beginn eines Chunks (`gx_decode.cpp` `forgetTextures`: 600 Chunks
  ungenutzt oder über dem Budget von 96 MB) und schickt eine davon im selben Chunk wieder mit, wenn das Spiel sie
  gleich braucht. Mit 60 FPS läuft vor dem Chunk der Zwischenbild-Durchgang: Er lädt die Textur schon hoch und lässt das
  Vergessen dem echten Durchgang. Der löschte sie dann und übersprang das Hochladen („schon vom Zwischenbild geladen“).
  Danach fehlte die Textur für immer, denn der Kern hält sie für vorhanden und schickt nur noch den Schlüssel.
- **Korrektur:** `gx_gl.cpp` (`RE4_GX_OP_FORGET`): nach dem Vergessen wird der Schlüssel aus `interpUploads`
  genommen, die Textur im echten Durchgang also wieder hochgeladen. Test `TestInterpolationForget` (`gx_gl_check`):
  vorher 3 fehlende Texturen und eine weiße Flamme, nachher 0 Fehler.
- **Version 0.95.0-beta** (versionCode 79; zuerst 0.94.1, auf Wunsch des Projektinhabers 0.95.0, weil RVZ und
  Touch-Zielen dazukommen): 0.94.0 ist schon bei diesem Nutzer. Releasetext
  `port/android-decomp/docs/releases/v0.95.0-beta.md`.
- **Nicht getestet:** auf dem Gerät (der Fehler tritt erst nach längerem Spiel auf; Nutzer bitten, mit 0.95.0 wieder
  lange zu spielen und den Bericht zu schicken: „missing“ muss 0 bleiben).

## Gerätetest 28.10.2026 12:20 (0.94.0, 2x MSAA) und Abschluss 0.94.0-beta (lokal, Branch `claude/determined-allen-36nj6r`)

Projektinhaber: großteils stabil 60 FPS (MSAA auf 2x gesenkt); vereinzelt flackern große Fackeln wie ein fehlerhaftes Bild.
Dynamische Auflösung ging wie gewollt 2x → 1.5x (15 % ohne Zwischenbild), danach 58 ausgelassen in ~90 s, 1 Pause.
Vier R3-Serien (r226): Lebenszähler der Teilchen korrekt (langlebige Fackel-Teilchen, kein Platz neu belegt). Ursache:
manche Teilchen-Objekte zeichnen von Bild zu Bild eine andere Zahl Vierecke (bis 5 je Serie), dann verschob „n-te
Zeichnung des Objekts“ die Zuordnung, Vierecke tauschten die Plätze. Jetzt zählt `Interpolate` die Zeichnungen je Objekt im
nächsten Chunk vorab; weicht die Zahl vom vorigen ab, wird das Objekt nicht über die Marke, sondern wie früher zugeordnet
(`interpOwnedRecount`). Jede Zeichen-Nachricht zählt dafür, auch eine verworfene. Test `TestInterpolationTrail`, `gx_gl_check`
0 Fehler, Serien weiter 11/11 Zwischenbilder gezeigt. **0.94.0-beta fertig** (Wunsch des Projektinhabers): Releasetext
`port/android-decomp/docs/releases/v0.94.0-beta.md`. Offen: Kapitelende auf dem Gerät, Hunk über einen Tester.
Nachtest des Projektinhabers mit dem fertigen Stand: große Fackeln flackern noch vereinzelt; bleibt so (60 FPS ist
„experimental“, Entscheidung des Projektinhabers 28.10.2026). Ansatz für später: Einzelbild-Animationen der Flammen
(`m_Ptn_no`) überblenden oder Teilchen mit wechselnder Viereck-Zahl einzeln über ihren Abstand zuordnen.

## Gerätetest 28.10.2026 12:03 (0.94.0): r226 mit 60 FPS (lokal, Branch `claude/determined-allen-36nj6r`)

Nach der Zähl-Korrektur etwa 25 Zwischenbilder/s (vorher ~14); Teilchen 1,6 %, Raum 0,9 % ohne Gegenstück, 1,33 Mio. über
das Objekt, 2,5 Mio. Texturmatrizen und 0,66 Mio. Lichtfarben gemischt; Zeichen-Thread 7–8 ms je Chunk. In r226 bei 2x mit
4x MSAA blieb die GPU ein Bild zurück (Zaun „1.0 Runs behind“), etwa **ein Fünftel** der Zwischenbilder fiel weg (60 und 30
im Wechsel), 2 Pausen; die dynamische Auflösung blieb bei 2x, weil sie erst ab einem Viertel herunterging. Jetzt ab einem
Zehntel (`AutoLevel::kDroppedDown` 0,10; hoch weiter erst unter 5 %); Test `TestAutoLevel` (neu: ein Fünftel → eine Stufe
tiefer).

## Gerätetest 28.10.2026 11:50 (0.94.0): 45 statt 60 Bilder/s (lokal, Branch `claude/determined-allen-36nj6r`)

Projektinhaber: mit 60 FPS oft stabil 45. Ursache: die 60-Hz-Erkennung (Film/Titel) zählte jeden Chunk mit Zwischenbild
zweimal (er kommt für sein eigenes Bild ein zweites Mal durch dieselbe Stelle, `deferred`); bei 30 Spielbildern sah das
wie 60 aus, jedes zweite Zwischenbild fiel weg (Protokoll: ~14 Zwischenbilder/s, nur 174 ausgelassen). Jetzt zählt
`ChunkRate` (`gx_auto_level.h`) jeden Chunk einmal; Test `TestChunkRate`. Sonst im Protokoll: Zeichen-Thread 2–7 ms je
Chunk, Teilchen 1,8 %, Raum 1 % ohne Gegenstück, 1,3 Mio. Texturmatrizen gemischt, 2 Pausen (Folge desselben Fehlers).

## Gerätetest 28.10.2026 11:33 (0.94.0): 30 FPS statt 60, App „beendet“ (lokal, Branch `claude/determined-allen-36nj6r`)

Projektinhaber: trotz 60 FPS nur 30, die App beendete sich nach 35 s. **Takt-Regel S4 zu empfindlich:** nur 34
Zwischenbilder ausgelassen, aber gehäuft – direkt nach jedem Wiedereinschalten ist der Kern ein Bild voraus, das gab
sofort wieder „3 binnen 2 s“ und die nächste Pause (10 Pausen in 60 s). Neu: Pause erst, wenn in den letzten 4 s
mindestens ein Viertel der Chunks (mindestens 60) ausgelassen wurde; nach einer Pause zählen die ersten 2 s nicht. Test
`TestSmoothPacer` neu (einzelne und Dreier-Schübe: bleibt an; ein Drittel über 4 s: aus; Aufholen nach der Pause zählt
nicht). **Das Beenden war kein Absturz:** Android vermerkt „USER REQUESTED / FORCE STOP“ von einem fremden Prozess
(`dumpsys activity exit-info`), das war `test-core-app.ps1`: ein einziges leeres `pidof` (ADB-Aussetzer) galt als „läuft
nicht mehr“. Jetzt fragt das Skript zweimal nach (je 1 s), bevor es aufgibt.

## Gerätetest 28.10.2026 10:56 (0.94.0) (lokal, Branch `claude/determined-allen-36nj6r`)

Projektinhaber: deutsche Untertitel im Intro sichtbar, weißes Aufblitzen bei START behoben; Kapitelende und Hunk folgen.
Protokoll: im Spiel 2 % der Teilchen ohne Gegenstück, 0,94 Mio. Zeichnungen über ihr Objekt, 0 neu belegte Plätze,
Texturmatrizen 1,8 Mio. gemischt (2879 am Rand gelassen). **Befund:** In Titel und Intro-Film liefert der Kern 60 Bilder/s
(Bildschirmtakt); jedes zählte als „Renderer hinten“: 4482 ausgelassen, 23 Pausen, die dynamische Auflösung fiel im Film
von 2x auf 1x. Jetzt erkennt `core_mode.cpp` 60 Chunks/s (≥ 45 in der letzten Sekunde) als eigenen Takt des Spiels: weder
ausgelassen noch Pause. Fünf R3-Serien (r226) nachgezeichnet: mit Marken 0 Raumteile ohne Gegenstück (ohne Marken 11 je
Serie), Teilchen ohne Gegenstück mit und ohne Marken gleich (echte neue). S3 ergänzt: Nach-Transformations-Matrizen (XF
0x500, Wasser/Nebel laufen dort) und indirekte Matrizen (BP 0x06–0x0E, bei gleichem Maßstab) bei Zuordnung über das
Objekt; in r226 je Zwischenbild 850–1200 Matrizen gemischt. Test `TestInterpolationExtras` erweitert (indirekte Matrix,
Gegenprobe), `gx_gl_check` 0 Fehler. Nutzerbericht (0.93.0, Xiaomi 25078RA3EL, EU): Zeichen-Thread 9–13 ms Prozessorzeit je
Bild bei 850–1000 MHz, GPU wartet – mit 60 FPS (jedes Bild zweimal) zu langsam; ein Frame-Skip-Zähler „636“ steht nicht in
der App (im Protokoll nur „636 texture binds“), Bildschirmfoto erbeten. Version bleibt 0.94.0-beta (versionCode 78; Projektinhaber: die APK ist noch bei keinem Tester).

## Gerätetest 28.10.2026 10:30 und Version 0.94.0-beta (lokal, Branch `claude/determined-allen-36nj6r`)

Bericht `cubeevil4-report-20261028-103732`, `senden.zip`, fünf R3-Serien. **Auf dem Retroid lief noch die alte APK
(0.93.0 vom 27.10. 20:48):** `test-core-app.ps1 -SkipApk` installierte die APK aus dem Build-Ordner, die nicht neu gebaut
war (Protokollzeile „60 FPS: …“ im alten Format, keine Marken in den Serien, `etc/movie.das` nie gelesen). Untertitel und
neues 60 FPS waren also nicht drauf; künftig `test-core-app.ps1` ohne `-SkipApk` (es baut die APK selbst).
Projektinhaber: mit 60 FPS blitzt das Bild beim Öffnen des START-Menüs kurz weiß. Ursache: der Optionsbildschirm (`gameOption`,
`Rno0 == 6`) zählte nicht als Menü-Bildschirm, zwischen Spielbild und erstem Menübild entstand ein Zwischenbild. Jetzt in
`re4_picture_still` (Test `letterbox`, neuer Fall; PC: „a menu screen: no pictures between (r100)“ nach START).
**Version 0.94.0-beta** (versionCode 78, Wunsch des Projektinhabers für weitere Tests), Releasetext
`port/android-decomp/docs/releases/v0.94.0-beta.md`; APK am PC gebaut (20,1 MB, Kern und App-Code geprüft). Hunk testet ein
Tester (kein Spielstand beim Projektinhaber).

## Lokale Sitzung 28.10.2026: Hunk, Kapitelende, Film-Untertitel, 60 FPS (Branch `claude/determined-allen-36nj6r`)

**Mercenaries mit Hunk (Patch 0134).** `archive_dump.py` mit den echten Discs: `pl06.drs` hat an `PL_ARC 0x40..0x45`
dieselben Nacken-Bewegungen wie `pl00.drs` – der Verdacht galt dem falschen Archiv. Wichtig: `PL_ARC(n)`/`WEP_ARC(n)`
ist Eintrag n−4 (die ersten vier Wörter sind der Kopf). Am PC nachgestellt (neue Testoption `RE4_TEST_PLAYER`, wählt im
Debug-Startmenü die Figur): Hunk stürzt in r402 genau wie beim Tester ab. Ursache: Hunks Maschinenpistole `wep29` hat
48 Einträge, der gemeinsame Code der Maschinenpistole (`objMachinegun.cpp setMotion`, auch Leons `wep11` mit 54) holt
die Nacken-Bewegungen von 0x34..0x39, also hinter der Tabelle (EU-Discs gleich). Der GameCube liest dort die
Kennungen big-endian als Abstand von etwa 1 GB (Zeiger ungültig, `VALID_PTR2` verwirft ihn), die App little-endian als
wenige MB. Behebung allgemein in `PL_ARC_PTR` (`re4_arc_entry`, Prelude): hinter der Tabelle wie der GameCube, mit
Protokollzeile. Prüfungen: `runtime-test` (neu `arctable`) 0 Fehler; Mercenaries-Karten r400/r402/r403/r404 mit Leon,
Ada, Hunk, Krauser, Wesker, Debug- und EU-Discs: 40/40 ohne Absturz, nur Hunk liest hinter seine Tabelle (6 Stellen);
Raum-Rundgang „set“ (27 Räume) „ALLES OK“, keine weiteren Lesestellen hinter Tabellen.

**Kapitelende/Spielende EU (Patch 0135).** EU-`ChapterEnd::move` (0x800422AC) gelesen: Gegnerzahlen vierstellig mit
Tausender in Baustein 0x1B/0x1C, führende Nullen aus, Fortsetzungen ≤ 999; EU-`GameResult::move`: Fortsetzungen ≤ 999,
Baustein 0 immer aus (dazu schließt es sich nach einer Zeit selbst – nicht übernommen). Mercenaries-Ergebnis gleich.
GX-Bild am PC (r106, Ende Kapitel 1-1, EU-Daten): „0“ ohne laufende Ziffer; mit Debug-Discs unverändert.

**Intro-Film-Untertitel (Patch 0136).** Nur der EU-`opening.sfd` hat im Kopf eine Untertitel-Tabelle (`CRITAGS/USRINFS`,
Kanäle 3–6 = Deutsch…Italienisch, 11 Einträge, Englisch keiner); Texte in `etc/movie.das`. Mit Debug-Discs gibt es
also keine. Nachgebaut wie EU-`cSofdec` (siehe Patch-Tabelle), Tabelle aus dem Filmkopf (`runtime/movie_status.cpp`),
`FNT` in Archiven umgewandelt. GX-Bilder am PC: Deutsch, Französisch, Spanisch, Italienisch richtig (mit Akzenten),
Englisch ohne (wie das Original); Lücken ohne Zeile; nach dem Film läuft das Spiel weiter. Test `runtime-test
moviesubtitles`.

**Händler-Koffer.** Dolphin nicht installiert (nur der Benutzerordner), Vergleich nicht möglich; offen für den
Projektinhaber.

**60 FPS ohne Flackern (S0–S4, `60fps-plan.md` „Umsetzung“).** Patch 0137 (Marken aus `ExecOt`), Renderer: Zuordnung nach
Objekt, neu belegte Teilchen-Plätze, Texturmatrizen (mit Umbruch), TEV- und Lichtfarben, gleichmäßiger Takt
(`SmoothPacer`), Zähler je Art im Fehlerprotokoll. Prüfungen: `runtime-test gx` ok, `gx_gl_check` 0 Fehler (neu
`TestInterpolationOwners`, `TestInterpolationExtras` je mit Gegenprobe ohne Marken, `TestSmoothPacer`), `core_mode.cpp`
mit dem NDK fehlerfrei übersetzt, Android-Kern beide ABIs 0 Fehler, Raum-Rundgang „set“ mit Marken 27/27 „ALLES OK“. Nicht getestet: auf dem Gerät (S5), R3-Serien.

## Testerberichte 27.10.2026: Kapitelende flackert, Händler zeigt den Koffer (28.10.2026, Cloud, Branch `claude/determined-allen-36nj6r`)

Zwei Videos des Kapitelendes (Spanisch, EU-Discs, 0.93.0-beta; Kapitel 1-1 ohne 60 FPS, Kapitel 4-2 mit 60 FPS), ein
Bericht (`cubeevil4-report-20261027-232444`, Xiaomi 2109119DG, EU-Discs, 64 bit) und ein Bildschirmfoto des Händlers
(anderer Tester, „BUY“, Spielbildformat 16:9, ohne 60 FPS).

**Kapitelende, zwei getrennte Fehler:**
1. **Die vorderste Ziffer bei „Enemigos eliminados“ läuft durch alle Zeichen** (beide Spalten gleich, mit und ohne
   60 FPS). Gemessen im Video ohne 60 FPS (10 Bilder/s ausgelesen): `-`, 2, 5, 8, `-` … und `%`, 1, 4, 7, `%` … – also
   eins weiter je Spielbild durch 12 Zeichen (0–9, `%`, `-`). Das ist die Texturanimation des IDSystem
   (`idSysMove04`, `id_sys.cpp:966`): Ein Baustein ohne `tex_flag` 0x2 zählt `texNo` jedes Bild weiter.
   `ChapterEnd::move` (`option.cpp:1086`, einmal beim Öffnen) setzt mit `num(…, 3, …)` je drei Stellen für die
   Gegnerzahlen (Bausteine 0x0E–0x10 und 0x11–0x13) und hält sie damit an; die EU-Vorlage (`SS/esp/chap01.dat`) hat
   offenbar eine **vierte Stelle** (Tausender), die der Debug-Code nicht kennt, also nie anhält. Welche Bausteinnummer und
   wie die EU-Fassung sie setzt, steht im EU-`main.dol` (nicht in der Cloud). Kopfzeile „1-1“ ist richtig (die
   umrandete Schrift lässt die 1 wie eine schmale 0 aussehen; in 4-2 stimmt sie sichtbar). Mit Debug-Discs nicht geprüft.
2. **Mit 60 FPS steht ein `%` zwischen zwei Zeilen** (Video 4-2, z. B. Bild 30: „84“, darunter halb versetzt „%“, dann
   „40“). Das Zwischenbild ordnet gleich aussehende 2D-Zeichnungen nach ihrer Reihenfolge zu; zeigt die laufende Stelle
   gerade `%`, gibt es ein `%` mehr, und ein `%` wird mit dem einer anderen Zeile gemischt. **Behoben** (Laufzeit, kein
   Patch): `re4_picture_still` (`runtime/picture.cpp`) zählt jetzt auch die Ergebnis-Bildschirme als Menü (in der
   Hauptschleife eine IDSystem-Tabelle der Klasse `IDC_TITLE`: Kapitelende, Spielende, Mercenaries- und
   Schießstand-Ergebnis); dort gibt es keine Zwischenbilder mehr, es bewegt sich ohnehin nichts. Test: `runtime-test
   letterbox` (`re4probe_picture_still`, neue Fälle: leere `IDC_TITLE`-Tabelle in der Hauptschleife ja, außerhalb nein,
   nach `kill` nein).
- **Nächster Schritt (PC):** `retail_code.py` liest jetzt auch Disc-Abbilder (`.gcm`/`.iso`: deren `main.dol`) und zeigt
  bei `dis` die ganze Funktion der anderen Fassung. Befehl:
  `retail_code.py dis <Debug-Disc1.gcm> <EU-Disc1> 0x80047B40 0x3A8` (`ChapterEnd::move`). Danach Patch mit
  Versionsweiche (EU: die vierte Stelle wie die EU-Fassung setzen).

**Händler („BUY“, Koffer sichtbar):** sehr wahrscheinlich das Original. `SsShopMain::move` (`ss_shop.cpp:564`) zeichnet
in jedem Bild den Koffer mit Inhalt (`caseModelMove(0)`, `pieceModelDisp`) an der Stelle, die der Hintergrund-Baustein
0xFE des Untermenüs vorgibt, nach einem Z-Löschen in OT 0xF (`shopClearZ`); die Händler-Tafeln liegen in OT 0x13 darüber,
das gezeigte Modell nach einem zweiten Z-Löschen in OT 0x14. Genau diese Schichten zeigt das Foto (Hintergrund, Koffer,
links die Tafeln darüber). `BuyItemNum::init` fährt die Liste weg (Baustein 0xFA rückwärts), darum ist die Tafel bei
„Buy? Yes/No“ leer. Ob Lage und Überdeckung genau wie auf dem GameCube sind, ist **nicht geprüft**: Vergleich mit
Dolphin (gleiche Stelle, gleicher Kauf) nötig. Kein Bericht dazu (der Bericht vom 27.10. enthält keinen Händler).

## 60 FPS ohne Flackern: Plan (27.10.2026, Cloud, Branch `claude/determined-allen-36nj6r`)

Wunsch des Projektinhabers: 60 FPS ohne flackernde Fackeln, Wasser und Texturen. Vorschlag in
`port/docs/60fps-plan.md`: Namensmarken aus `ExecOt` (ein Kern-Patch, neue GX-Nachricht `RE4_GX_OP_OWNER`), damit
das Zwischenbild jede Zeichnung sicher wiederfindet; dann auch Texturmatrizen, Farben und Licht mischen; gleichmäßiger
Takt statt einzelner Lücken. **Freigegeben am 28.10.2026** (Entscheidung des Projektinhabers: so umsetzen). Noch nichts gebaut
oder getestet.

## Testerbericht: Mercenaries mit Hunk stürzt ab (27.10.2026, Cloud, Branch `claude/determined-allen-36nj6r`)

Meldung eines Testers: „Mercenaries mode still finishes the game with the Hunk, on all maps.“ Gemeint ist: Mercenaries
mit Hunk **beendet die App** (Absturz), auf jeder Karte. Bericht `cubeevil4-report-20261026-172119` (Xiaomi 23129RA5FL,
Android 15, 0.91.0-beta-core, **64 bit**, Debug-Discs):
- Drei Abstürze am 26.10. (17:14, 17:15, 17:19), alle in `HermiteInterpolation` (Bewegungsdaten auswerten). Zwei schon
  beim Raumstart: `cPlHunk::cPlHunk` → `startUp` → `pl_R1_Footwork` → `cPlNeck::init` → `MotionSetCore` (Adresse
  0x7bfcde0d); einer 3 s nach dem Start in r402 (`pl06` „HUNK prolog Ok“, `wep29`) beim Gehen, direkt nach
  „MotionMoveCore(): Pl, Invalid parts 152.“ (Adresse 0x9800). Schon am 22.10. ein gleicher Absturz in `pl_R1_Footwork`
  (wohl ebenfalls Hunk, daher „still“).
- Gemeinsam: die **Nacken-Bewegungen** (Kopf zum Gegner drehen). Jede Mercenaries-Figur übernimmt Leons Tabelle;
  `pl_R1_Footwork`/`Walk`/`Run` (`player.cpp:423/472/568`) holen sie als `PL_ARC 0x40..0x45` aus dem Figurenarchiv, hier
  `em/pl06.drs`. Die Daten dort sind offenbar keine (umgewandelte) Bewegung: Gelenknummer 152 gibt es nicht
  (`pl00_mirror` hat 80 Plätze).
- Verdacht (nicht geprüft, Disc-Daten fehlen in der Cloud): `pl06.drs` hat an 0x40..0x45 keine FCV-Bewegungen oder weniger
  Einträge. Liest das Spiel hinter der Tabelle, nimmt es die Kennungen („FCV\0“ …) als Abstand: auf dem GameCube
  (big-endian, z. B. 0x46435600) zeigt das außerhalb des Speichers, `VALID_PTR2` verwirft es, der Nacken bleibt still; in der
  App (little-endian, 0x00564346) zeigt es in den Speicher und wird als Bewegung gelesen. Das wäre ein Fehler des Ports.
- Neues Werkzeug `port/tools/archive_dump.py` (nur lesen): listet die Einträge eines Archivs (Kennung, Abstand, Größe, erste
  Bytes, auch Plätze hinter der Tabelle mit beiden Byte-Ordnungen). Nächster Schritt am PC:
  `wsl python3 port/tools/archive_dump.py --data /mnt/e/Development/Android/RE4-Discdata --file em/pl06.drs --file em/pl00.drs --entries 0x3E-0x51`.
  Danach die Behebung (Patch oder Regel), Mercenaries mit allen Figuren prüfen. Nicht getestet: alles im Spiel.
- **Erster Lauf am PC (Projektinhaber, 28.10.2026):** „not an archive with a table (count 0x836e834a)“ für `pl06.drs`,
  „0xcab6be20“ für `pl00.drs` (beide Debug-Discs gleich). `.drs`-Dateien sind **Behälter mit Teile-Kopf**
  (`dvd.cpp cDvdQueue::readInit`: 1 KB vorn, ab +0x20 `DvdHeader`-Einträge zu 0x20 Byte, Typ 4 ein verschachtelter Kopf,
  -1 Ende); das Archiv ist „part 0.0“ (so auch im Kern-Protokoll), der Ton „part 1.x“. `archive_dump.py` zerlegt solche
  Dateien jetzt zuerst in ihre Teile und zeigt die Tabelle jedes MRAM-Teils (mit nachgebauter Datei geprüft, nicht mit
  echten Daten). Derselbe Befehl noch einmal nach `git pull`.

## Meilenstein 14: alles in `master`, Release 0.93.0-beta (27.10.2026 abends, lokal)

**Zusammenführen:** lokaler `master` zuerst auf `origin/master` nachgezogen (444 Commits zurück), dann
`retail-eu-testrun` (Stand `6469ba28`) mit `--no-ff` hineingeführt (`e706c541`). `beta-updates` und `retail-eu` hatten
nichts, was `retail-eu-testrun` nicht schon enthält; die drei README-Commits von `master` waren ebenfalls schon darin.
Der Baum von `master` ist danach **gleich dem von `retail-eu-testrun`** (`git diff` leer), also genau der in 10b
geprüfte Stand. Kein Branch gelöscht (Wunsch des Projektinhabers).

**Release 0.93.0-beta:** `versionCode` 77, Releasetext `port/android-decomp/docs/releases/v0.93.0-beta.md`
(Controller-Konfiguration, Auflösungs-Zwischenstufen, Stage 4 mit EU-Discs, keine „14-09LE“-Untertitel, Händler-Markierung,
dynamische Auflösung mit Warteregel, stärkere Vibration, aufleuchtende Cheat-Knöpfe). Bekanntes Problem nur noch
„Unendlich Gesundheit“ gegen Sofort-Tode; One-Hit-Kill wirkte im Gerätetest auch bei Bossen (10b).

**Prüfungen (PC, WSL2):** Debug-Discs i686 (Stand `e706c541`, das Zusammenführen) und EU-Discs mit `--room-info` (Stand
`de7fcfb8`, die Version) je „ALLES OK“; Android-Kern beide ABIs 0 Fehler (Stand `de7fcfb8`), `CoreMenuLayoutTest` 7/7, APK
`app-core-debug.apk` 19,3 MB, versionCode 77, `0.93.0-beta-core`. **Gerät (Retroid, 27.10.2026 21:13, 64 bit):** App
`0.93.0-beta-core`, Spielstand in r117 geladen, Leon frei, keine Abstürze, 0 Shader-Fehler; Projektinhaber: „Passt alles“.
Releasetext zeigt alle Änderungen seit 0.91.0 (Wunsch des Projektinhabers). Danach `master` gepusht, kein Branch gelöscht.


## Raum-Rundgang 10b mit beiden Disc-Arten (27.10.2026 nachmittags, lokal, Branch `retail-eu-testrun`)

**Abgeschlossen 27.10.2026 abends:** Rundgänge am PC mit beiden Disc-Arten je 126/126, die Punkte aus M13 nachgeholt, auf
dem Retroid zwei Kapitel, Bosse mit Cheats, Zwischenstufen der Auflösung, Controller-Konfiguration und Speichern/Laden mit
ARM32 bestätigt (Einzelheiten unten).

Auftrag des Projektinhabers: Rundgang über alle 126 Räume mit Debug- (G4BE08) und EU-Discs (G4BP08), Stage 4 mit EU-Daten
in die Prüfstrecke, die drei Spielmeldungen des EU-Rundgangs, das Ungetestete aus M13, gefundene Fehler beheben (mit den
Debug-Discs bleibt alles gleich).

**1. Rundgänge (i686, je 6000 Bilder ab der Hauptschleife, Stand `69510fa5`):** Debug-Discs **126 von 126** ohne Fehler;
EU-Discs **126 von 126** (vorher 108: Stage 4 scheiterte an „RelDataReadError! rel/st4_0.rel“, behoben mit Patch 0131, ein
echter Spielfehler: die EU-Fassung wechselt für Stage 3 und 4 auf Disc 2, diese nur für Stage 3). Vergleich Debug/EU: in
5 Räumen nur andere Todeszahlen (r113, r21d, r301, r332, r40f; Gegner verhalten sich mit den EU-Daten leicht anders).
- Prüfstrecke: `whole_r100_check.sh --room-info DATEI` gibt EU-Discs für den Debug-Start die `debug/roomInfo.dat` der
  Debug-Disc dazu (Ordner mit Verweisen `WORK/disc-ri`, im Inhaltsverzeichnis `sys/fst.bin` jeder Disc ein Ordner „Debug“
  angehängt; die Discs bleiben unverändert). Die Ergebnisse bleiben je Disc-Art getrennt (`rundgang-i686-O2.json`,
  `rundgang-i686-O2-eu.json`; vorher überschrieb der EU-Rundgang den Debug-Rundgang), ein EU-Rundgang wird mit dem
  Debug-Rundgang verglichen, dazu die Spielmeldungen, die nur eine Disc-Art gibt (`room_tour_report.py --compare
  --messages`). Ein Rundgang mit `RE4_CHEATS` bleibt ebenso getrennt (`-cheats7`) und wird mit dem ohne verglichen.
- Neue Testhilfen: `RE4_TEST_PLACE="free+N:area=A"` (Leon in die Mitte von Auslösebereich A des Raums),
  `RE4_TEST_SCF="free+N:F,F"` (Szenario-Merkmale setzen), `RE4_GAME_LOG_STACK=Text` (PC: bei einer Spielmeldung, die so
  beginnt, die Aufrufer ins Log).

**2. Die drei Spielmeldungen des EU-Rundgangs**
- „Message::init() Msg[04] Address Error“ (15 Räume, nur EU): die **Untertitel der Zwischensequenzen auf Englisch**. Mit den
  EU-Discs ist Englisch Sprache 2; die Debug-Version blendet Ereignis-Untertitel nur bei Sprache 1 aus, die EU-Discs haben
  für Englisch keine. Das Spiel zeigte ersatzweise Text 0 der Kerntabelle: „14-09LE“ unter Leon und Luis in r104 (GX-Bild).
  Patch 0132 wie die EU-Fassung (bei Sprache 2 keine). Danach die 15 Räume ohne Fehler und **keine Spielmeldung mehr, die
  nur mit EU-Discs kommt**; Deutsch zeigt seinen Untertitel („Vom Regen in die Traufe.“).
- „mapModelInit(): R105 flag SHAPE_MODEL clear“ (11 Räume, r315–r327): kommt mit den Debug-Discs in denselben Räumen. Ein
  Kartenmodell der Insel hat das Merkmal gesetzt; beide Fassungen löschen es, die EU-Fassung nur ohne Meldung. Kein Eingriff.
  Nebenbei gesehen: die EU-Fassung blendet in `mapModelInit` Räume mit Farbe 4 aus (weder offen noch betreten, oder versteckt),
  diese Version zeichnet sie in Farbe 4. Nicht weiter untersucht (Karten auf dem Retroid ohne Befund).
- „IDSystem::unitPtr(m[00],c[00]): Not found.“ (10 Räume): kam von der ersten Fassung von Patch 0126 (Funkspruch ohne
  Hauptmenü, aber `sscrnMainMenuInit` noch aufgerufen); mit der endgültigen Fassung im neuen Rundgang nicht mehr.

**3. Aus M13 nachgeholt (PC, GX-Bilder, EU-Discs auf Deutsch, Debug-Discs zum Vergleich)**
- **Funksprüche 13, 14, 15, 17** (r207, r204, r20b, r229; r204 über Bereich 2: Ereignis `r204s00` ohne den Lichtdatensatz der
  EU, Kapitelende 3-1 mit „Speichern? Ja/Nein“, dann der Funkspruch; r20b über Bereich 0x16): 13 – Hunnigans Bild wird zu
  Rauschen (Debug: bleibt); 14 – beginnt mit Rauschen, dann Salazar (Debug: sofort Salazar); 15 – Salazar legt auf, Rauschen;
  17 – Salazar geht, Rauschen (Debug: sein Bild gleitet hinaus, ein Rest bleibt am Rand). Alle wie der EU-Code (Patch 0126).
- **Achte Händler-Markierung:** die Debug-Fassung setzt `SCF_R204_MERCHANT_MOVE` nie (der Händler zieht in r204 trotzdem um),
  die EU-Fassung in `R204Init`: Patch 0133. Karte in r204 (Übersicht): ohne das Merkmal die alte Markierung, mit
  (`RE4_TEST_SCF=free+10:88,174`) die achte ein Stück daneben, die alte weg. Der Umzug selbst im Spiel nicht nachgespielt.
- **Cheats** (EU-Rundgang über alle 126 Räume mit `RE4_CHEATS=7`, 126 von 126): Leon 1-mal getroffen (ohne Cheats 1 854),
  Gegner bei 158 von 202 Treffern sofort besiegt (ohne: 65 von 257). Treffer mit Rest: Fässer/Kisten (0x46), Gegner, die sich
  verwandeln (Leben steigt), ein Treffer mit „mindestens 1 lassen“ und Schaden ohne Leons Waffe. Leon starb noch 12-mal in 9
  Räumen, jedes Mal ohne vorherigen Treffer: Fallen und Bereiche (r203, r208, r30f, r311), Sturz (r215), Szenen (r104, r227,
  r333) und ein Fang-Angriff im Söldner-Raum r404 (Leben 1200 → 0 in einem Zug) – die Grenzen aus `cheat-menu-plan.md`.
  **Gerät (Projektinhaber, Retroid, 27.10.2026 nachmittags, Stand `7f06f542`):** zwei weitere Kapitel ohne Probleme gespielt,
  Bosskämpfe enden mit One-Hit-Kill nach einem Schuss. Vereinzelt sanken die Bilder/s von 30 auf 22 (unten). **Nicht
  geprüft:** der Knopf bei offenem Menü.
- **Ruckler 30 → 22 Bilder/s** (Bericht `cubeevil4-report-20261027-144449.zip`, 64 bit, EU-Discs, Stufe 2 „Dynamisch“, 2× MSAA,
  FXAA, Anisotropie 16; dazu drei GX-Serien am See): nur an Orten mit vielen Zeichenaufträgen (r101, r108, r10a, r10b; 600–1000
  je Bild, ~8 Bildkopien). Dort wartete der Spielkern 18–22 ms je Bild auf den Renderer (`GX core: 42 ms each, of it 22 ms
  waiting for the renderer`), und der Renderer wartete im Treiber auf den Grafikchip: 30–43 ms Laufzeit bei 7–8 ms
  Rechenzeit je Bild (GPU-Zeit 18–29 ms). Die dynamische Auflösung blieb trotzdem auf Stufe 2: sie erkannte einen zu langsamen
  Grafikchip nur an Zäunen, die nach dem nächsten Bild noch offen sind, und die waren alle erreicht, weil der Renderer selbst
  wartete. **Neu (`gx_auto_level.h`):** ein langsames Fenster (unter 28,5 Bilder/s) zählt auch dann als „Grafikchip zu
  langsam“, wenn der Renderer im Schnitt mindestens 10 ms je Bild wartet (Laufzeit minus Rechenzeit, `core_mode.cpp`); wieder
  hoch nur unter 5 ms. In den 140 Zehn-Sekunden-Fenstern des Berichts warteten die ruckelnden 11–35 ms, alle flüssigen höchstens
  8,4 ms. Ohne Thread-Uhr (`CLOCK_THREAD_CPUTIME_ID`) zählt kein Warten. `gx_gl_check` mit neuen Fällen 0 Fehler (mit
  abgeschalteter Regel schlagen sie an), APK neu gebaut. **Nicht geprüft:** ob Stufe 1 am See wieder 30 Bilder/s schafft (Gerät).
- **Gerätetest der Regel (Retroid, 27.10.2026 16:25 und 16:29, Berichte `senden-arm64.zip`, `senden_arm32.zip`):** sie greift mit
  64 und 32 bit: `dynamic resolution: level 2 -> 1 (24.0 game pictures/s, the GPU 0.0 chunks behind, the render thread waiting
  31.2 ms a chunk …)`, danach 30 Bilder/s; nach 30 s wieder Stufe 2, kurz darauf wieder 1 (die Wartezeit verdoppelt sich wie
  vorgesehen). 32 bit: Spielstand in r117 geladen (`save loaded … room r117`), **gespeichert wurde in diesem Lauf nicht**.
  Projektinhaber: der Sprung von 2× auf 1× ist an der Bildqualität deutlich zu sehen.
- **Zwischenstufen der Auflösung (Wunsch des Projektinhabers, 27.10.2026):** 1,5×, 2,5×, 3,5× zusätzlich zu 1×–4×. Im Menü
  (Anzeige → Auflösung) wie eine Klaviertastatur: oben Dynamisch, 1×, 2×, 3×, 4×, darunter die drei Zwischenstufen, jede so
  breit wie die oberen und mittig auf der Linie zwischen den beiden, zwischen denen sie liegt (`MenuLayout.Keys`; ist die
  Zeile zu schmal, stehen alle acht in Zeilen gleicher Breite der Größe nach). Die Wahl steht in halben Stufen in
  `scaleHalves` (0 Dynamisch, 2 = 1× … 8 = 4×); eine Wahl der alten Einstellung `scale` wird übernommen. Die Zahl mit dem
  Dezimalzeichen der Sprache („1,5×“). Renderer (`gx_gl.cpp`): die Stufe in halben Schritten (`Impl::halves`), Bildspeicher
  bei 1,5× 960 × 792; Ausschnitte, Bildbereiche und Löschen von `Lo(x)` bis `Lo(x + w)` (abgerundet, Nachbarn ohne Lücke),
  eine Kopie hat `Hi(w)` Pixel (aufgerundet), der Kopier-Shader liest ihren Ausschnitt genau (`Sf`). Bei ganzen Stufen
  rechnet alles wie vorher. Neue Schnittstelle `InitHalves`/`RescaleHalves`/`Halves` (die ganzzahlige bleibt für Tests und
  Werkzeuge). **Dynamisch** geht jetzt in halben Schritten (2× → 1,5× → 1×; `AutoLevel::Start(…, bottom 2, halves)`, Log
  `level 2x -> 1.5x`). `CoreBridge.setGraphics` und `graphicsInfo()[2]` in halben Stufen, Fehlerbericht `level 1.5`.
- **Geprüft:** `gx_gl_check` 0 Fehler mit neuen Fällen (Stufenwechsel auf 1,5× und 2,5×: Bildgröße, mitgenommene Kopie,
  Löschfarbe; bei 1,5× eine Kopie an ungerader Stelle mit ungerader Größe; Dynamisch in halben Schritten bis 1× und nicht
  tiefer); `CoreMenuLayoutTest` 5/5 (neu: Klavier-Anordnung, Übernahme der alten Wahl 3× → `scaleHalves` 6, Druck auf 1,5×
  speichert 3; alle Seiten in 10 Sprachen, 3 Schriftgrößen, 5 Breiten ohne Überlappung); Texte 10 Sprachen ok
  (`ingame_resolution_line`: die Stufe als Text `%3$s`); APK gebaut. **Nicht geprüft:** Gerät (Bild bei 1,5×/2,5×/3,5×,
  Dynamisch in halben Schritten am See).
- **Gerätetest (Retroid, 64 bit, 27.10.2026 16:47, Bericht `senden.zip`):** Dynamisch ging im Regen (r117/r118, Nacht) von 2× auf
  1,5× (`22.0 game pictures/s … the render thread waiting 33.0 ms a chunk`) und blieb dort, solange es regnete (Projektinhaber).
  Im Regen 650–1 080 Zeichenaufträge je Bild statt ~180, ~2 200 Uniform-Blöcke (2,2 MB), 9 Bildkopien, mit 4× MSAA; der
  Renderer rechnet 7–8 ms und wartet den Rest auf den Grafikchip (die GPU-Zeitmessung ist mit 4× MSAA auf dem Adreno unbrauchbar:
  1 154 ms). **Offen:** wo genau die Zeit im Regen liegt (GX-Serie per R3 im Regen, GPU-Messreihe), mögliche Schritte:
  durchsichtige Partikel mit weniger Abtastpunkten, weniger Auflösungen des MSAA-Puffers für die Kopien, Zeichenaufträge
  zusammenfassen. Die Schrift der Auflösungs-Knöpfe ist überall gleich (16 sp fett; im Test nun geprüft), der Eindruck kommt
  von den längeren Texten.
- **Controller-Konfiguration (Wunsch des Projektinhabers, 27.10.2026):** Menü → Steuerung, ganz unten „Controller-Konfiguration ›“
  (nur bedienbar mit angeschlossenem Controller: `CoreActivity.gameDevice`). Die Seite listet 13 Funktionen (Aktion/Schießen A,
  Rennen/Abbrechen B, Ashley X, Inventar Y, Karte Z, Messer L, Zielen R, Optionen/Überspringen START, Steuerkreuz ↑↓←→,
  App-Menü SELECT), rechts je ein Knopf mit der Taste; ein Druck wartet auf die nächste Controller-Taste („Taste drücken …“),
  derselbe Knopf noch einmal lässt alles, wie es war. Hatte eine andere Funktion die Taste, tauschen die beiden
  (`CoreInput.assign`: jede Funktion behält eine Taste). „Standardbelegung“ stellt zurück. Gespeichert als `bindings` in
  `core_input` (fehlt bei der Standardbelegung). `CoreInput`: mit eigener Belegung ein eigener Weg (gehaltene Tasten je
  Gerät, Steuerkreuz der Hat-Achsen als Steuerkreuz-Tasten; L und R auf L2/R2 analog, andere Funktionen auf einem Trigger ab
  halbem Druck; „L und R / B einrasten“ wirken weiter; das App-Menü auf seiner Taste, R3 nimmt Grafikdaten nur, wenn keine
  Funktion darauf liegt), ohne eigene Belegung der bisherige Weg unverändert. Mit der alternativen Steuerung gilt deren feste
  Belegung (Hinweis auf der Seite). Protokoll: `layout: own bindings (controller configuration): A = BUTTON_A, …`. 19 neue
  Texte in 10 Sprachen. **Geprüft:** `CoreInputTests` 172 Prüfungen (neu: Tausch, Trigger mit Schwelle, L analog, Hat als
  Steuerkreuz, Menü auf L1, R3 mit Funktion, alternative Steuerung geht vor, Einrasten), `CoreMenuLayoutTest` 6/6 (neu:
  Knopf nur mit Controller, Zuweisen mit Tausch, Loslassen geschluckt, zweimal derselbe Knopf, Zurück zur Steuerung; die
  neue Seite in 10 Sprachen, 3 Schriftgrößen, 5 Breiten ohne Überlappung), Texte 10 Sprachen ok, APK gebaut. **Nicht
  geprüft:** Gerät.
- **Gerätetest der Controller-Konfiguration (Retroid, 27.10.2026 17:06 und 17:08, `senden-1.zip`, `senden-2.zip`) und
  Korrekturen (Wünsche des Projektinhabers):**
  - **B beendete das Spiel:** durch die Tausche war die Taste B keiner Funktion mehr zugeordnet (`A = BUTTON_R2, B = BUTTON_A,
    … Z = BUTTON_Y`), die App reichte sie an Android weiter (`KEYCODE_BUTTON_B … not mapped, passed on to Android`), Android
    machte daraus „Zurück“, die Frage „Spiel beenden?“ wurde bestätigt (`System.exit`). Neu: im Spiel geht keine Taste eines
    Controllers ohne Funktion mehr an Android (`CoreActivity.dispatchKeyEvent`, Protokoll `… -> no function, not passed on`).
  - **Eigene Belegung nur im Spiel:** in den Menüs des Spiels gilt die Standardbelegung. Der Kern meldet „das Spiel zeigt ein
    Menü“ in Bit 4 von `re4_core_rumble` (die App fragt es ohnehin jedes Bild, auch über den eigenen Kern-Prozess; keine neue
    Schnittstelle): `re4_game_menu_open` (`runtime/state_trace.cpp`) = keine Hauptschleife eines Raums (Titel und seine Menüs,
    Laden) oder ein Untermenü offen (`STA_SUB_SCRN`: Inventar, Karte, Akten, Händler, Optionen, Funk, Schreibmaschine).
    Zwischensequenzen, Reaktionstasten und das Kapitelende zählen als Spiel. `CoreInput.setMenu`: jedes Gerät merkt sich
    seine gehaltenen Tasten, beim Wechsel zählen sie in der neuen Belegung weiter (nichts bleibt hängen). Protokoll des Kerns
    `pad: game menu shown/closed at frame N` (höchstens 60 Zeilen).
  - **Abstand in „Steuerung“:** die Gyro-Zeile ist so hoch wie der Regler daneben; der Knopf steht jetzt senkrecht in ihrer
    Mitte, über und unter ihm gleich viel Abstand (`CoreMenuLayoutTest.gyroSwitchKeepsTheSameGapAboveAndBelow`, schlägt ohne
    die Korrektur an).
  - **Vibration stärker:** Stärke 210–255 statt 150–255, jeder Impuls 170 statt 120 ms (`RUMBLE_PULSE_MS`).
  - **Geprüft:** `CoreInputTests` 179 Prüfungen (neu: Menü mit Standardbelegung, Taste über den Wechsel gehalten, App-Menü in
    Menüs auf SELECT), `CoreMenuLayoutTest` 7/7; PC-Lauf r204 (EU): `game menu shown` am Titel, `closed` mit der Hauptschleife,
    `shown`/`closed` mit der Karte, `shown` mit dem START-Menü; Prüfstrecke Debug-Discs und EU (roomInfo), Stand `2212f1a1`,
    je „ALLES OK“; Android-Kern beide ABIs 0 Fehler, APK gebaut. **Nicht geprüft:** Gerät.
- **Zweiter Gerätetest (Retroid, 27.10.2026 17:32, `senden.zip`):** Abstände in „Steuerung“ passen (Projektinhaber). Mit A
  anders belegt ging die Navigation im Optionsmenü (START) nicht: im Protokoll fehlte jede Zeile `pad: game menu`, die
  Menü-Meldung kam auf dem Gerät nie an. Ursache: `pad.cpp` (libre4_platform) fragte `re4_game_menu_open` (libre4_main) über eine
  schwache Verknüpfung; Androids Linker sucht sie nur in den Bibliotheken, die libre4_platform braucht, und die braucht
  libre4_main nicht – der Zeiger blieb leer (am PC löst das Testprogramm ihn auf). Neu: `state_trace.cpp` übergibt seine
  Abfrage beim Laden der Bibliothek an `re4_pad_menu_probe` (libre4_main braucht libre4_platform, `llvm-readelf`: das Symbol
  aufgelöst, keine schwache Verknüpfung mehr). (Die schwache Verknüpfung `re4_idsys_report` in `log.cpp`, nur Diagnose bei
  „IDSystem work full“, hat dasselbe Problem; nicht geändert.) **Vibration:** noch zu schwach („Schrotflinte wie
  Steinschleuder“): Stärke 230–255, Dauer je Stoß nach Stärke 190–310 ms (`CoreActivity.rumblePulseMs`). Geprüft:
  Prüfstrecke Debug-Discs „ALLES OK“ (Stand `0f74de5f`), PC-Lauf r204 wie oben, Android-Kern beide ABIs 0 Fehler,
  `CoreMenuLayoutTest` 7/7, APK gebaut. **Nicht geprüft:** Gerät.
- **Dritter Gerätetest (Retroid, 27.10.2026 18:37, `senden.zip`):** die Menü-Meldung kommt jetzt an (`pad: game menu
  shown/closed` bei jedem Menü), Abstände und Vibration passen (Projektinhaber). Neu gewünscht: Hinweise wie „A Öffnen“ und die
  Reaktionstasten (A, A+B, L+R …) zeigen die GameCube-Tasten, mit eigener Belegung lagen diese aber woanders. **Entscheidung des
  Projektinhabers (27.10.2026):** solange ein Hinweis gezeigt wird, gilt die Standardbelegung (die gezeigte Taste ist die zu
  drückende; Türen, Gegenstände usw. dann mit A, auch wenn „Aktion“ sonst woanders liegt). Umgesetzt: `re4_game_menu_open`
  meldet auch den Hinweis des Aktionsknopfs (`Cckpt.m_ActBttn` ≠ `DISP_OFF` und `ActBtn.isActive()`, wie `qte_touch.cpp`;
  Rückgabe 2), Protokoll `pad: game button prompt shown`. Geprüft: Prüfstrecke Debug-Discs „ALLES OK“ (Stand `e263bd0d`),
  PC-Lauf r100 (Leon in den Tür-Bereich 0 gesetzt: `button prompt shown` bei Bild 2925), Android-Kern beide ABIs 0 Fehler,
  `CoreMenuLayoutTest` 7/7, APK gebaut. **Nicht geprüft:** Gerät.
- **Vierter Gerätetest (Retroid, 27.10.2026 19:25) und Neuausrichtung:** das Umschalten nach Menü/Hinweis machte die eigene
  Belegung unbrauchbar: bei jedem Hinweis, der kam und ging, wechselte dieselbe Taste ihre Bedeutung (in 13 s über 15 Wechsel;
  R2 mal „Zielen“, mal „A“); Leon blieb beim Rennen stehen, wenn er über einen Gegenstand lief (Rennen lag auf A); der
  Ja/Nein-Dialog beim Aufheben ist weder Untermenü noch Hinweis. **Entscheidung des Projektinhabers (27.10.2026): nur
  Zusatztasten.** Die Belegung (A, B, X, Y, L, R, Z, START, Steuerkreuz, SELECT; mit oder ohne alternative Steuerung) bleibt
  überall gleich, die Anzeigen des Spiels stimmen immer; die Konfiguration legt Funktionen nur zusätzlich auf freie Tasten
  (`CoreInput.freeKey`: alle außer Gesichtstasten, START, SELECT, R1, L2, R2, Steuerkreuz; L1 nicht mit der alternativen
  Steuerung; beim Retroid L1, L3, R3). Eine Funktion je Zusatztaste; dieselbe Taste noch einmal nimmt sie weg; L und R auf einer
  Zusatztaste drücken den Trigger ganz; „Einrasten“ wirkt; R3 nimmt Grafikdaten nur ohne Funktion; das App-Menü kann
  zusätzlich auf einer freien Taste liegen. `CoreInput` wieder auf dem Stand vor der eigenen Belegung, darauf die Zusatztasten
  (`setExtraKeys`, `assign`, `encode`/`decode`, `appFunctionOf`); die App schaltet nicht mehr nach Menü/Hinweis um (das Bit 4
  des Kerns bleibt als Protokoll `pad: game menu/button prompt …`). Gespeichert als `extraKeys` in `core_input`, der alte
  Eintrag `bindings` wird gelöscht. Seite: rechts die Zusatztaste („—“ ohne), „Freie Taste …“ beim Warten; eine belegte Taste
  beendet das Warten und tut ihr Normales (B zurück, A auf dem wartenden Knopf bricht ab); „Zusatztasten löschen“; Texte in
  10 Sprachen angepasst. Freie Tasten ohne Funktion gehen weiter nicht an Android. **Geprüft:** `CoreInputTests` 167 Prüfungen
  (neu: freie Tasten, Karte auf L1 und R1 bleibt Karte, A bleibt A, Wechsel und Wegnehmen, Rennen auf L1 mit Einrasten,
  Zielen auf L3, App-Menü und X auf R3, alternative Steuerung belegt L1), `CoreMenuLayoutTest` 7/7 (neu: belegte Taste
  abgelehnt, L1 angenommen, Wechsel zwischen Funktionen), Texte 10 Sprachen ok, APK gebaut. **Nicht geprüft:** Gerät.
- **Fünfter Gerätetest (Retroid, 27.10.2026) und dritte Fassung (Vorschlag des Projektinhabers):** mit Zusatztasten waren
  nur L1, L3, R3 belegbar. Neu: **A, B, L2, R2, START, SELECT und das Steuerkreuz sind fest** (die Hinweise und Dialoge des
  Spiels fragen A, B, L, R; seine Menüs bewegen sich mit dem Steuerkreuz), **X, Y, L1, R1, L3, R3 sind frei belegbar**
  (`CoreInput.MOVABLE`, eine Funktion oder keine je Taste; Standard X = Ashley, Y = Inventar, R1 = Karte, L1/L3/R3 keine).
  Der Controller meldet auch C, Z und MODE; beim Retroid sind das keine echten Tasten, sie bleiben außen vor. Wählt man
  Ashley, Inventar oder Karte für eine Taste, tauscht die Taste, die sie hatte (`CoreInput.assign`); fehlt eine davon (durch
  „Keine Funktion“), zeigt die Seite „Auf keiner Taste: …“. Andere Funktionen dürfen auf mehreren Tasten liegen (z. B. Zielen
  auch auf R1). L und R auf einer freien Taste drücken den Trigger ganz; „Einrasten“ wirkt; R3 nimmt Grafikdaten nur ohne
  Funktion; das App-Menü kann auf einer freien Taste liegen. Mit der alternativen Steuerung gilt deren Belegung. Seite: links
  die Taste, rechts ihre Funktion, ein Druck öffnet eine Unterseite mit „Keine Funktion“ und den 13 Funktionen (mit dem
  Controller bedienbar wie das übrige Menü), „Standardbelegung“. Gespeichert als `keyMap` in `core_input`; die Einträge der
  zwei ersten Fassungen (`bindings`, `extraKeys`) werden gelöscht. Protokoll `free keys (controller configuration): BUTTON_X
  = X, …`. Texte 10 Sprachen (neu „Fest: …“, „Auf keiner Taste: …“, „Keine Funktion“). **Geprüft:** `CoreInputTests` 170
  Prüfungen (neu: feste Tasten, Karte auf L1 mit Tausch, Zielen auch auf R1 und R2 weiter, A/Steuerkreuz unverändert, Tausch
  X/Y, Rennen auf L3 mit Einrasten, fehlende Funktion, R3 mit Aufnahme/Menü/Funktion, alternative Steuerung),
  `CoreMenuLayoutTest` 7/7 (neu: die Seite, die Auswahl, Tausch, Warnung, Standardbelegung; beide Seiten in 10 Sprachen, 3
  Schriftgrößen, 5 Breiten), Texte ok, APK gebaut. **Auf dem Retroid bestätigt** (Projektinhaber, 27.10.2026: „das klappt
  nun mit den Tasten, das lassen wir so“).
- **Bericht eines Testers (HONOR LGN-NX3, 0.91.0, `cubeevil4-report-20261026-231441.zip`, „r210 lädt nicht“):** r210 lädt
  einwandfrei; durch die Tür nach r222 liest das Spiel `st2/r222.das`, deren erste 0x400 Byte auf dem Gerät Nullen sind
  („DISC FILE DAMAGED: st2/r222.das“). Derselbe Bericht wie „Lesefehler `st2/r222.das`“ oben (M13): 0.92.0 prüft beim Import
  jede Datei gegen den Katalog, liest sie zurück und meldet eine unlesbare Datei im Spiel mit Ursache und Abhilfe. Kein neuer
  Eingriff; der Tester muss mit 0.92.0 neu importieren (bzw. ein unbeschädigtes Abbild nehmen, wenn die Prüfung das meldet).
  **Erledigt:** nach einer Neuinstallation läuft es beim Tester (Rückmeldung 27.10.2026).
- **Speichern/Laden mit ARM32 auf dem Retroid (Projektinhaber, 27.10.2026 20:11, APK 0.92.0-beta-core mit beiden ABIs,
  installiert als `armeabi-v7a`, Debug-Discs):** Lauf 1 lud zwei Spielstände, die die 64-bit-App gespeichert hatte (r117,
  12-mal gespeichert; r11b, 8-mal), und speicherte an der Schreibmaschine in r11b in den freien Platz FILE 05 (Karte danach:
  8 Dateien, alle Prüfsummen ok, Systemdatei ok, 194 von 251 Blöcken frei); Lauf 2 (32 bit, neu gestartet) lud FILE 05
  problemlos, Lauf 3 (wieder 64 bit, von Hand gestartet) ebenso. Damit lesen und schreiben beide Varianten dieselbe Karte.
  (Die Zeile „GPU 4295.0 ms“ im Bericht ist die Obergrenze der 32-bit-Zeitmessung des Grafikchips, wenn er Bilder
  vorausarbeitet; schon früher so gesehen, keine Auswirkung.)
- **Hinweis zum Testen:** „Run“ in Android Studio legt keine `app-core-debug.apk` in `app\build\outputs` ab; mit
  `-SkipApk` bricht `test-core-app.ps1` dann mit „APK fehlt“ ab. Ohne `-SkipApk` baut das Skript sie selbst (1–2 Minuten).

**Prüfungen (PC, WSL2):** Debug-Discs i686 (Stand `82403ab6`) „ALLES OK“; EU-Discs mit `--room-info` und unveränderte
EU-Discs i686 (Stand `b744b0d8`) je „ALLES OK“ (die Filmprüfung folgt jetzt den Verweisen des `disc-ri`-Ordners, ein erster
Lauf fand dort keine Filme); Rundgänge wie oben; ARM32-Kern unter qemu-arm, Speicherkarte mit den unveränderten EU-Discs (`--armhf --card-only`, Stand `e1b1fd9e`; die Kartenprüfung startet die ARM-Ziele jetzt unter qemu, vorher scheiterte schon das Formatieren): Systemdatei, Speichern in r100, Neustart, Laden, „Retry“ – „ALLES OK“.
Android-Kern beide ABIs 0 Fehler (Stand `152fdc2c`), APK `app-core-debug.apk` 19,2 MB mit beiden ABIs (0.92.0-beta-core,
App-Code unverändert). **Nicht geprüft auf dem Gerät:** Englisch ohne „14-09LE“, der Händler nach dem Umzug im Spiel (beide am PC geprüft);
Speichern/Laden mit ARM32 auf dem Retroid bestätigt (oben);
Bosskämpfe mit Cheats und zwei weitere Kapitel auf dem Retroid bestätigt (oben).

## M13 abgeschlossen (27.10.2026 nachmittags, Branch `retail-eu`)

**Gerätetest des Projektinhabers (Retroid, EU-Discs, Deutsch):** zwei Kapitel vom Start an ohne Befund durchgespielt;
Cheats funktionieren. Einziger Wunsch: die drei Cheat-Knöpfe gaben beim Drücken kein Zeichen – sie leuchten jetzt
1,5 Sekunden auf (`CoreMenu.CHEAT_FLASH_MS`, `CoreMenuLayoutTest` prüft An und Aus). Damit ist M13 erreicht.

**In den Raum-Rundgang 10b übernommen:** Stage 4 (r400–r411) mit EU-Daten in der Prüfstrecke (Raummodul `st4_0.rel`
nur auf Disc 2; R5 steht bei 108/126 Räumen), die drei Spielmeldungen ohne Absturz aus dem EU-Rundgang
(`retail-eu-plan.md`, „Noch offen“), Funksprüche 13/14/15/17 (Bildwechsel), die achte Händler-Markierung nach dem
Umzug (r204), Speichern/Laden mit ARM32, Schaden/Treffer mit den Cheats in Kämpfen und Bossräumen.

## Cheat-Menü im Ingame-Menü (27.10.2026 mittags, lokal, Branch `retail-eu`)

Wunsch und Entscheidungen des Projektinhabers: `port/docs/cheat-menu-plan.md`. Mit beiden Disc-Arten.

- **App:** Seite MANAGEMENT oben „Cheats“: drei Schalter (Unendlich Gesundheit, Unendlich Munition, One-Hit-Kill;
  gespeichert in `core_game`/`cheats`, beim Start an den Kern; ein `RE4_CHEATS` des Testskripts geht vor) und drei
  Knöpfe (Killer7 3/1/3/3, Aktenkoffer L, +1.000.000 PTAS) mit Antwortzeile darunter (erledigt, schon vorhanden,
  gerade nicht, kein Platz). Texte in 10 Sprachen (`check_translations.py` ok), `CoreMenuLayoutTest` mit neuer Prüfung
  (Cheats zuerst, Schalter senden ihre Bits).
- **Kern:** `runtime/cheats.cpp` (Bild-Haken: Bits `DBG_NO_DEATH`, `DBG_INF_BULLET`, `DBG_EM_WEAK` in jedem Bild, Leben
  von Leon und Partner voll, Magazin der ausgerüsteten Waffe voll; Knöpfe nur mit Leon, im Spiel, ohne Untermenü/Film),
  `re4_core_set_cheats` / `re4_core_cheat_action` (`core_api.cpp`: bei offenem Menü sofort auf dem Spiel-Thread in
  `pausePoint`, sonst im nächsten Bild), Nachrichten 31/32 für den eigenen Kern-Prozess, JNI `CoreBridge.setCheats` /
  `cheatAction`. Testhilfen `RE4_CHEATS=<bits>`, `RE4_TEST_CHEATS="free+N:1,2,3"`. Patch 0130: echte Magazinzahl
  statt „100“. Jede Aktion eine Zeile „cheat: …“ im Log.
- **Geprüft (PC, Stand `751426dc`):** Prüfstrecke EU (roomInfo) und Debug-Discs je „ALLES OK“. r100 mit `RE4_CHEATS=7`
  und den drei Knöpfen, beide Disc-Arten: Log „health on, ammo on, one-hit kill on“, Killer7 angelegt (Stufen 0x2022,
  14 Schuss), Koffer L, 2 000 000 PTAS. GX-Bilder (EU, Deutsch): nach etwa zwölf Schüssen zeigt die Anzeige weiter „10“;
  Koffer geöffnet: gewachsen, Killer7 mit 14, „1000000 PTAS“. Android-Kern beide ABIs, APK gebaut, alle Unit-Tests grün.
- **Nicht geprüft:** Schaden mit „Unendlich Gesundheit“ und Treffer mit „One-Hit-Kill“ in einem echten Kampf, die
  Bossräume, der Knopf bei offenem Menü auf dem Gerät (am PC nur der Weg „nächstes Bild“), Gerätetest.

## M13: Fehlerberichte, Gyro-Richtung, R3 und R6 (27.10.2026 vormittags, lokal, Branch `retail-eu`)

**R8 Gerät und Freigabe (27.10.2026 mittags):** ARM32 auf dem Retroid (`test-core-app.ps1 -SkipCore -Abi armeabi-v7a
-Seconds 600`, EU-Discs, Deutsch, APK 0.92.0-beta-core, ohne Speicherkarte): Vorfilm, Autofahrt, Funkspruch (Hunnigan,
dunkle Leiste, deutscher Untertitel) und r100 ohne Absturz, Kern im App-Prozess, 0 Voice Drops. ARM64 hat der Projektinhaber
selbst gespielt (Start, Funkspruch, Speichern/Laden an der Schreibmaschine, Menüs, Inventar, Karte, Spielstand in r226).
Version 0.92.0-beta (versionCode 76, Zählregel: mittlere Nummer je Release), Releasetext
`port/android-decomp/docs/releases/v0.92.0-beta.md`. **Offen für R8:** ein ganzes Kapitel mit EU-Discs, Speichern/Laden
mit ARM32; Veröffentlichung durch den Projektinhaber.

**Zweiter Gerätetest (27.10.2026 10:52, Video):** Knacken im Vorfilm nicht wieder aufgetreten (nur beim allerersten Start);
Speichern und Laden an der Schreibmaschine, alle Menüpunkte und Hilfetexte deutsch und richtig, Inventar ohne Befund – bis auf
ein „H“, das mitten auf der Karte stand und beim Verschieben stehen blieb: Patch 0129 (achte Händler-Markierung der EU-Daten).
GX-Bild r226 (EU, Deutsch) danach ohne das „H“; Prüfstrecke EU (roomInfo) und Debug-Discs, Stand `efb0a029`, je „ALLES OK“.

**Gerätetest des Projektinhabers (Retroid, EU-Discs, Deutsch, 64 bit, Stand `f2599d51`, 27.10.2026 10:37):** Spiel startet in
der gewählten Sprache, beide Vorfilme laufen; erster Funkspruch mit Hunnigan und dunkler Leiste hinter dem Untertitel (Patch
0126 bestätigt); Spielstand in r226 geladen, der Statuen-Ton ist zu hören (leise, das Sample selbst ist leise; bestätigt, dass
der frühere Bericht am Pegel lag, nicht an der Wiedergabe); Gyro-Schalter „Vertikal/Horizontal umkehren“ im Log wirksam.

**1. Lesefehler `st2/r222.das` (Bericht HONOR LGN-NX3, Android 16, 64 bit, Debug-Discs, 26.10.2026).** Kern-Log: die
Datei ist auf dem Gerät so groß wie auf der Disc (3 064 768 Byte), ihre ersten 0x400 Byte sind aber Nullen; Patch 0113
gab nach drei Versuchen auf, das Bild blieb stehen (die Meldung „DISC FILE DAMAGED“ ging nur ins Log). Auf unserer
Debug-Disc beginnt die Datei mit der Teile-Tabelle (SHA-256 wie `rooms.tsv`). Dieselbe Datei meldeten schon zwei andere
Geräte (25.10.2026): vermutlich ein verbreitetes beschädigtes Abbild, sonst ein Schreibfehler des Geräts – aus dem Bericht
allein nicht zu unterscheiden (der Import von 0.91 prüfte nur die Raumdateien und nur die Prüfsummen beim Lesen des
Abbilds, nicht was auf dem Gerät ankam; eine Warnung stand höchstens als Zeile auf dem Startbildschirm). Neu:
- **Import prüft jede Datei:** neue Kataloge `files.tsv` / `files-eu.tsv` (alle 2 248 / 2 608 Dateien beider Discs außer
  dem Disc-Kopf, Name, Größe, SHA-256; erzeugt mit `make_disc_catalogs.py --iso`). `RoomFileCheck` liest beide Formen;
  im Kernmodus vergleicht die App nach dem Import und bei jedem Start das ganze Manifest mit dem Katalog.
- **Zurücklesen:** `GameCubeDisc.extract` liest jede geschriebene Datei vom Gerät zurück und vergleicht die SHA-256; weicht
  sie ab, wird sie noch einmal geschrieben, sonst bricht der Import mit einer klaren Meldung ab (Datei, „Speicher frei
  machen, neu starten, neu importieren“).
- **Meldung im Spiel:** der Kern merkt sich die erste unlesbare Datei mit Grund (`re4_core_disc_problem`, `runtime/dvd.cpp`:
  Teile-Tabelle aus Nullen / beschädigt, Datei fehlt, Datei kürzer; auch über den eigenen Kern-Prozess, `kDiscProblem`).
  `CoreActivity` fragt zweimal pro Sekunde, prüft die Datei (`RoomFileCheck.diagnose`: Gerät gegen Manifest und Katalog)
  und zeigt einmal „A disc file cannot be read“ mit Datei und Disc und je nach Ursache: Abbild beschädigt (neu auslesen),
  Kopie auf dem Gerät beschädigt (neu importieren), Datei in Ordnung aber Lesen fehlgeschlagen (Bericht schicken),
  unbekannt (neu importieren); Knöpfe Bericht / Beenden / Weiter. 10 Sprachen (`check_translations.py` ok).
- **Fehlerbericht:** neue Datei `disc-check.txt` (jede Disc gegen den Katalog, der Lesefehler des Kerns, die Diagnose).
- Tests: `RoomFileCheckTests` (neu: Katalog aller Dateien, Diagnose Gerät/Abbild/Lesen/fehlt/unbekannt), `DiscTests` mit den
  echten Discs im Kernmodus (Debug 4 531, EU 5 251 Prüfungen; jede Datei wie im Katalog; Import mit Zurücklesen etwa 35 s
  je Disc am PC), Laufzeittest `dvdq` (der Kern meldet `zero.dat` mit „zeros“). **Nicht geprüft:** Gerät.

**2. Fehlender Ton bei Kamera-Fokus-Szenen (Bericht des Projektinhabers, Retroid, EU-Discs, Deutsch; laut Projektinhaber
auch an anderen Stellen).** Der Ton ist ein Stream der Musikdatei `bgm/bio4bgm.sbb`, den der Raumcode beim Kameraschwenk
startet: r226 `R226EventRoboWatchMain` → `SndStrReq(0, 0x16, 0x80000003)` (Stereo, 7,7 s); ebenso r104, r108, r11d, r200,
r201, r209, r20b, r21a, r221, r222, r22c u. a. Am Gerät lief der Stream (er endete nach seiner Länge, „SND: STREAM 0 STOP“),
war aber nicht zu hören. Am PC (neue Testhilfe `RE4_TEST_STREAM=free+N:B:S`, Mitschnitt `RE4_AUDIO_WAV`): Stream 0x16 in
r100 hörbar (−26 dB statt −56 dB ohne), die Statuen-Szene selbst (Leon per `RE4_TEST_PLACE` in Bereich 24 von r226 gesetzt)
mit Debug- und EU-Daten, i686 und x86_64 deutlich hörbar (−15 dB, 7 s, davor und danach Stille). Der Kern mischt den Ton am PC
also richtig. Nebenbei behoben: der PC-Mitschnitt endete nach 1 s (die Blöcke wurden verworfen, bevor sie in die WAV kamen,
weil am PC niemand den Ring liest). **Gerätelauf (Retroid, EU-Discs, 64 bit, 27.10.2026, `test-core-app.ps1 -AudioWav 560
-CoreSettings RE4_TEST_STREAM=free+11400:0:0x16`):** der Stream lief 7,8 s, und der Ton-Mitschnitt des Kerns auf dem Gerät
enthält ihn laut (−15 dB statt −35 dB, 7 s) – der Projektinhaber hörte zur selben Zeit aber nichts. Der Kern mischt den Ton also
auch auf ARM64; verloren geht er danach (Ring der App, AudioTrack) oder es ist nicht der gemeinte Ton (Ausschnitt
`retroid-stream-0x16.wav` an den Projektinhaber geschickt, Antwort offen). **Offen.** Nebenbei: `-CoreSettings` neu im
Geräteskript; das Skript bricht ab, wenn man seine Ausgabe mit `2>&1`/`*>` umleitet (die Meldung von `adb pull` gilt dann als
Fehler) – ohne Umleitung starten. Ein erster Gerätelauf (`free+60`) löste den Stream während des Vorfilms aus: die Erkennung
„Leon frei“ übersieht Filme (bleibt so, die EU-Prüfstrecke baut darauf auf).

**3. Gyro-Richtung:** Ingame-Menü, Seite Steuerung, zwei Schalter „Invert vertical“ / „Invert horizontal“ unter dem Regler
(gespeichert in `input` als `gyroInvertVertical` / `gyroInvertHorizontal`, wirken sofort: `GyroAim.setInvert`), nur
bedienbar mit Gyro an. 10 Sprachen. **Nicht geprüft:** `CoreMenuLayoutTest` und Gerät (siehe Prüfungen).

**4. R3 Menüs und Texte**
- **Dunkle Leiste hinter dem Funkspruch-Untertitel: behoben (Patch 0126).** Neue Diagnose: bei „IDSystem::set() work full“
  schreibt der Kern die Belegung von `IdSys`/`IdSub`/`IdNum` je Klasse ins Log (`re4_idsys_report`). Mit EU-Daten war `IdSub`
  beim Funkspruch 128 von 128 belegt. Drei erste Fassungen (vier Tabellen weglassen; 512 Plätze; nur das Hauptmenü weglassen)
  brachten die Leiste, aber Hunnigan verschwand (Projektinhaber am Retroid, GX-Bilder am PC). Ursache: die EU-Tabelle des
  Funkspruchs hat drei neue Bildgruppen (0x15–0x17) an der Stelle des Partnerbilds, die nur die EU-Fassung ausblendet und
  bei bestimmten Funksprüchen umschaltet (`retail_code.py` am EU-`Sscrn.rel`: `SsTermMain::init`, `OpeMesMove`). Patch 0126
  übernimmt beides. GX-Bild (EU, Deutsch, Bild 2401): Hunnigan sichtbar, Untertitelbereich so dunkel wie mit den
  Debug-Discs (Helligkeit 22 statt 45 ohne Leiste, Debug 21), kein „work full“; ebenso x86_64 (Italienisch). Prüfstrecke
  Stand `fa80ce1d`: EU (roomInfo, Deutsch) i686, EU (roomInfo, Italienisch) x86_64 und Debug-Discs i686 je „ALLES OK“;
  Android-Kern beide ABIs 0 Fehler, APK gebaut. **Nicht geprüft:** die Bildwechsel der
  Funksprüche 13, 14, 15, 17 (später im Spiel) und das Gerät.
- **Texttabellen** (alle Tabellen beider Fassungen Text für Text verglichen, neues Werkzeug `port/tools/retail_messages.py`,
  dazu die Textnummern aller `SceMesSet`-Aufrufe der Raummodule beider Fassungen): `etc/memcard.das` hat dieselben Nummern
  0–0x2C, neu nur 0x2D (Platzwarnung) und 0x2E (Sprachwahl beim Start, durch das App-Menü ersetzt) → nichts zu tun;
  `ss/eng/ss_cmmn.dat` dieselben Nummern (die Debug-Fassung hatte 0x22 doppelt, die EU den richtigen Kräutertext), neu nur
  am Ende → nichts zu tun; die Räume r100, r105, r111, r203, r214, r300, r308, r30b, r311, r31c, r333, r40e: nur überarbeitete
  Texte oder neue am Ende, der Code fragt dieselben Nummern → nichts zu tun. r22c: die Ausgangstür fragt mit EU-Daten
  „Leave target practice?“ statt „Quit…“ (gleiche Bedeutung), 0xA/0xB sind die Debug-Stufenwahl → bleibt so. **r221**: ab
  Text 6 verschoben → Patch 0128. **r101**: der Debug-Code nimmt nach dem Schlüssel den falschen Text (Prototyp-Fehler) →
  Patch 0127. Die übrigen Unterschiede der Aufruflisten sind neue oder entfernte Aufrufe der EU-Fassung, keine Verschiebungen.
- Werkzeuge aus der letzten Sitzung geordnet übernommen: `port/tools/retail_code.py` (DOL/REL-Funktion in der anderen
  Fassung finden, nebeneinander disassemblieren, Funktion um eine Adresse, Aufrufer mit Konstanten, Text-Verweise,
  Befehlswörter) und `port/tools/retail_messages.py` (Tabellen, nebeneinander, Aufruflisten).

**5. R6 Debug-Menü:** mit EU-Discs öffnet SELECT-Halten nichts (App: `CoreActivity.selectHold`; Kern: `re4_core_debug_menu`
ignoriert die Anfrage, auch für ältere Apps). Laufzeittest `disc release` prüft es für alle Disc-Arten.

**6. R5 Raum-Rundgang mit EU-Daten** (`--rooms all`, EU-Abbilder plus `debug/roomInfo.dat` nur für den Debug-Start, i686):
**108 von 126 Räumen ohne Fehler**. Die 18 übrigen sind alle Stage 4 (r400–r411): „RelDataReadError! rel/st4_0.rel“ – das
Raummodul liegt bei der EU nur auf Disc 2, der Debug-Start der Prüfstrecke beginnt auf Disc 1 (bei den Debug-Discs liegt es auf
beiden). Grenze der Prüfstrecke, im Spiel nicht geprüft. Spielmeldungen, die im Debug-Rundgang des 22.10. nicht notiert sind:
„Message::init() Msg[04] Address Error“ (14 Räume), „mapModelInit(): R105 flag SHAPE_MODEL clear“ (11), „IDSystem::unitPtr(m[00],
c[00]): Not found.“ (10) – ohne Absturz, noch nicht untersucht (ein Debug-Rundgang zum Vergleich fehlt, die alte Rundgang-Datei
wurde überschrieben).

**Prüfungen (PC, WSL2):** Stand `a88a61da` (Aufgaben 1–3, 0127, 0128, R6, erste Fassung von 0126): EU-Abbilder Deutsch i686
„ALLES OK“, Debug-Discs i686 „ALLES OK“, Funkspruch-Test EU (roomInfo, Französisch) „ALLES OK“; davor Debug-Discs `e72b0c04`
„ALLES OK“, EU 64 bit (x86_64) Stream-Test „ALLES OK“. Statuen-Szene r226 (Leon in Bereich 24): Debug und EU, i686 und x86_64
hörbar. App: `CoreMenuLayoutTest` 3/3, `RoomFileCheckTests`, `DiscTests` (synthetisch 19, echte Discs Debug 4 531 / EU 5 251),
Texte 10 Sprachen ok, Android-Kern beide ABIs 0 Fehler, APK gebaut und auf dem Retroid gelaufen (Vorfilm, Funkspruch, r100).
Endstand ohne 0126 (Stand `688d0371`): EU-Abbilder Deutsch i686 „ALLES OK“, Debug-Discs i686 „ALLES OK“, Android-Kern beide ABIs
0 Fehler, APK gebaut; der Projektinhaber erkannte den Ausschnitt als den gesuchten Ton; **nicht auf dem Gerät geprüft:** Meldung „Disc-Datei nicht lesbar“, Import mit
Zurücklesen, Gyro-Schalter, r101/r221-Texte, R6. Gemeldet vom Projektinhaber beim Gerätelauf: zweimal Knacken im Ton des
Vorfilms (noch nicht untersucht).

## M13 Retail-Discs (EU): Gerätetest 26.10.2026 und Korrekturen (27.10.2026 nachts, lokal, Branch `retail-eu`)

Gerätetest des Projektinhabers (Retroid, EU-Discs, Deutsch; Bildschirmfotos, Bericht, GX-Mitschnitt, Logcat):
Titel, Untertitel, Funkspruch und Laden alter Spielstände liefen. Gefunden und behoben:
- **Optionsmenü: Hilfetext fast immer „ZOOM“, bei „Messer“ der von „Helligkeit“.** Die EU-Kern-Texttabelle
  (`etc/core.das` CORE_MESSAGE) hat 173 statt 153 Texte, 15 neue ab 0x56 (beide englischen Blöcke Text für Text
  abgeglichen); der EU-Code fragt 0x90–0x94 und 0x9A/0x9B/0xA1/0x95, wo diese Version 0x81–0x85 und 0x8B/0x8C/0x92/0x86
  fragt. Patch 0123 verschiebt mit den EU-Discs jede Nummer ab 0x56 um 15. GX-Bilder: alle Hilfetexte deutsch und richtig.
- **Lebensanzeige „KiLeonr“.** Sechs Namensschilder (0x40–0x45) in beiden Fassungen, diese Version blendet nur drei aus;
  auf den Debug-Discs sind die drei übrigen leer, auf den EU-Discs tragen sie Namen. Patch 0125 wie die EU-Fassung
  (`LifeMeter::roomInit`, main.dol 0x800c05a8). GX-Bild: „Leon“.
- **Karte im Inventar leer** (Rahmen und Legende, keine Räume, kein Pfeil; nicht nur in Räumen: auch die Dorfkarte).
  Die EU-Bildschirmbeschreibung der Karte (`ss_map.dat` Eintrag 3) hat dieselben 48 Elemente, aber Element 253 (eine
  Kopie des Bildschirms) zuletzt; mit dieser Version deckt es die Karte zu. Der EU-Code (`SsMapMain::init`, Sscrn.rel)
  blendet es außer in r22c aus; Patch 0124 ebenso. PC-Lauf r226: Burgkarte mit Räumen, Pfeil, Schreibmaschine, Händler.
- **Sprachwechsel erst nach Neustart sichtbar.** Entscheidung des Projektinhabers (27.10.2026): nach der Wahl eine
  Frage „Neu starten“ / „Abbruch“; „Neu starten“ startet das Spiel neu, „Abbruch“ behält die bisherige Sprache.
  Umgesetzt: `CoreBridge.restartGame` → `re4_core_restart_game` (über den Kern-Prozess `kRestartGame`) → der Spiel-Thread
  setzt im nächsten Bild `SYS_SOFT_RESET` (der eigene Soft-Reset des Spiels; er wartet, solange die Speicherkarte
  beschrieben wird). Das Umschalten im laufenden Bild entfällt. Patch 0121 nimmt die Sprache der App auch nach dem
  Zurückholen aus dem Reset-Speicher. PC-Test (`RE4_TEST_LANGUAGE_RESTART=1000:fra`): Titel erst deutsch, nach dem
  Neustart französisch („COMMENCER, CHARGER, OPTIONS“), `SS/fra/option.dat` und `title.dat` gelesen.
- **Sprach-Schaltflächen:** drei pro Zeile (schmaler: weniger), weitere Zeilen stehen genau darunter.
- **Startbildschirm:** Bereich „Controls“ entfernt (mit ihm der Schalter „A/B und X/Y tauschen“; eine gespeicherte
  Einstellung gilt weiter), „November 25, 2004“ aus „Select ISO/GCM …“ entfernt.
- **Speicherkarte beim Start** (Rückfrage des Projektinhabers): mit beiden Fassungen gleich, keine Meldung; eine
  fehlende Karte legt die App formatiert an. Die Meldung auf einem Prüfbild kam von `RE4_MEMCARD=none`.

**Prüfungen (PC, WSL2, Stand `a272110b`):** EU-Abbilder Deutsch i686 „ALLES OK“, Funkspruch-Test Italienisch „ALLES OK“
(GX-Bild der Dorfkarte mit Räumen, Pfeil, Ziel, Schreibmaschine), Debug-Discs „ALLES OK“, EU-Abbilder Spanisch x86_64
„ALLES OK“; Optionsmenü, Lebensanzeige, Burgkarte und Neustart mit Sprachwechsel als eigene GX-Läufe (oben). App:
`CoreMenuLayoutTest` 3/3, Texte 10 Sprachen ok, Android-Kern beide ABIs 0 Fehler, APK gebaut. **Nicht geprüft:** Gerät.

**Noch offen (gefunden beim Abgleich aller Texttabellen, für R3/R5):** in mehreren Räumen hat die EU-Texttabelle
Texte mehr oder weniger (r100, r105, r111, r203, r214, r221, r22c, r300, r308, r30b, r311, r31c, r333, r40e), ebenso
`etc/memcard.das` (45 → 47) und `ss/eng/ss_cmmn.dat` (ein Text verschoben); der Raumcode dieser Version nennt dort
womöglich falsche Nummern. Nur raumweise am Code der EU-Fassung zu klären.

## M13 Retail-Discs (EU), Schritt R7: Spielsprache (26.10.2026 spätabends, lokal, Branch `retail-eu`)

Auftrag des Projektinhabers: R7 vorziehen (Sprachwahl im Ingame-Menü, „LANGUAGE SELECT“ aus dem Titelmenü
entfernen). Entscheidungen (26.10.2026): Standard ist die Handy-Sprache, ein Wechsel gilt sofort; nach dem Gerätetest
(27.10.2026): ein Wechsel startet das Spiel nach einer Frage neu.

**Wie die EU-Discs ihre Sprachen ablegen** (anders als im Plan angenommen)
- Kern, Räume, Speicherkarte und Funk: Sechs-Sprachen-Tabellen wie in der Debug-Version, aber in der Reihenfolge
  Japanisch, Englisch, Französisch, Deutsch, Italienisch, Spanisch (die Debug-Discs haben in den Plätzen 2–5 nur
  Platzhalter; die Debug-Version ordnete 3–6 den Plätzen 2–5 zu: im französischen Lauf kam der Funkspruch deutsch).
  Patch 0123 übernimmt die Zuordnung der EU-Fassung.
- Untermenüs, Titel, Kapitel, Optionen: ein Ordner je Sprache (`ss/Eng`, `ss/Ger`, `ss/Fra`, `ss/Esp`, `ss/Ita`), darin
  145 **einsprachige** Texttabellen (nur der Textblock, ohne den Sechs-Sprachen-Kopf). Die Debug-Version las sie als
  sechs Sprachen und nahm Text von falschen Stellen; die EU-Fassung von `getAddr`/`getMesNum` erkennt sie. Patch 0122
  und der Umwandler (`formats_mes.cpp`) lesen beide Formen; Kerntest `one-language mdt`.
- Sprachnummern (`pSys->language`): 1 US-Englisch (Debug, fest: ohne Untertitel in Filmsequenzen, zwei
  Schwierigkeitsgrade), 2 Englisch (EU), 3 Deutsch, 4 Französisch, 5 Spanisch, 6 Italienisch. Die EU-Fassung nimmt die
  Systemsprache der Konsole (`OSGetLanguage` + 2). Der Debug-Code kennt alle sieben (Ordnernamen, Speicherkarten-Layout
  `save_<e|g|f|s|i>.dat`, Textpositionen).
- Die deutschen, französischen, spanischen und italienischen `option.dat` sind größer als der feste Platz der
  Optionsdaten (0x44000 bis 0x4C2C0 statt höchstens 0x40000 Byte; die EU-Fassung liest sie nach 0x80784000 ohne
  Grenze). Sie liefen in Leons Daten: Absturz beim Start in r120 (Deutsch) bzw. hängender Speicherbildschirm
  (Französisch). Ein größeres `option.dat` kommt jetzt nach 0x81F00000 (`re4_option_data_place`, `runtime/dvd.cpp`;
  der zweite Platz des Kerns aus Patch 0117 ist dafür auf 3 MB begrenzt, der EU-Kern braucht 2,3 MB).

**Umsetzung** (Stand nach dem Gerätetest, 27.10.2026)
- Kern: `runtime/game_language.cpp` (Sprache der App beim Start, nach dem Lesen der Systemdatei und nach einem
  Soft-Reset; Neustart auf Wunsch der App), `re4_core_restart_game` (`core_api.cpp`, auch über den eigenen
  Kern-Prozess: `kRestartGame`), Startwert `RE4_GAME_LANGUAGE` (eng/ger/fra/esp/ita oder 2–6). Patch 0121: `setLanguage`,
  `systemWorkInit` (jeder Start und Soft-Reset), Reset-Speicher, Systemdatei, Optionsdaten-Platz, „LANGUAGE SELECT“
  (Einheiten 11 und 12 der EU-`title.dat`) ausgeblendet.
- Ein Wechsel startet das Spiel neu (Frage „Neu starten“ / „Abbruch“; der Soft-Reset des Spiels): danach liest der Titel
  `title.dat` und `option.dat` der neuen Sprache. Das erste Umschalten im laufenden Bild (26.10.2026) ließ Titel und
  geladene Bildschirme in der alten Sprache und ist entfallen.
- App: Eintrag „Game language“ auf der Startseite des Ingame-Menüs, nur mit EU-Discs: „Device language“ (Standard:
  de → Deutsch, fr → Französisch, es → Spanisch, it → Italienisch, sonst Englisch), English, Deutsch, Français, Español,
  Italiano, drei pro Zeile. Gespeichert in `core_game`/`gameLanguage`; beim Start als `RE4_GAME_LANGUAGE`, ein Wechsel
  über `CoreBridge.restartGame`. Texte in allen 10 Sprachen (`check_translations.py` ok).

**Prüfungen (PC, WSL2)**
- Stand `81d133db` (0121, 0122): EU-Abbilder Deutsch i686 „ALLES OK“, Italienisch x86_64 „ALLES OK“, Englisch
  (Kartenprüfung) „ALLES OK“, Debug-Discs „ALLES OK“; Systemdatei mit Sprache 3 bzw. 6 bzw. 2 gespeichert und wieder
  geladen. Vorher (Stand `f7a079b1`): Deutsch endete beim Start in r120 (Optionsdaten in Leons Daten), Französisch
  hing im Speicherbildschirm; beides behoben.
- Stand `b90d8c55` (mit 0123), Funkspruch-Test (EU-Daten plus `debug/roomInfo.dat`, nur für den Test): Französisch,
  Deutsch, Spanisch, Italienisch je „ALLES OK“; GX-Bilder: Funkspruch und Untertitel der Filmsequenz (Autofahrt) in
  der gewählten Sprache, scharf und richtig gesetzt. Gegenprobe Debug-Discs mit diesem Stand „ALLES OK“.
- Titelmenü (GX-Bild, Deutsch): START, LADEN, OPTIONEN, kein „LANGUAGE SELECT“; Warnhinweis und
  Speicherkarten-Meldungen deutsch. Mit neuer Karte keine Meldung beim Start (Karte formatiert angelegt, Systemdatei
  still erstellt), mit kaputter Karte dieselbe Startprüfung wie mit den Debug-Discs (Code -6, weiter zum Titel ohne
  Tastendruck; formatiert wird über die Abfrage des Spiels beim Speichern, Entscheidung vom 17.10.2026).
- Spielstände zwischen den Fassungen (Stand `868521ec`, Ladelauf wie Lauf B der Prüfstrecke): Spielstand der
  Debug-Discs mit den EU-Discs (Französisch) geladen, r100 läuft, „Retry“ geht; Spielstand der EU-Discs (Deutsch) mit
  den Debug-Discs ebenso. Dabei gefunden und behoben: die Systemdatei der EU-Discs trägt deren Sprache, die Debug-Version
  übernahm sie und suchte `ss/ger/title.dat` (die Debug-Discs haben nur `ss/Eng`), der Titel blieb stehen; mit den
  Debug-Discs bleibt die Sprache jetzt 1. Debug-Discs mit diesem Stand „ALLES OK“. Eine neue Disc-Installation ersetzt nur
  `disc1`/`disc2`, die Speicherkarte der App (`memcard/`) bleibt.
- App: `CoreMenuLayoutTest` 3/3 (alle 10 Sprachen, mit „Game language“), Android-Kern armeabi-v7a und arm64-v8a je
  0 Fehler, APK `app-core-debug.apk` gebaut. **Nicht geprüft:** Gerät (Retroid), ARM unter qemu, ein Sprachwechsel
  im laufenden Spiel (nur der Startwert lief am PC).

## Retail-Discs (EU), Schritte R0 und R1 (26.10.2026 abends, lokal, WSL2, Branch `retail-eu`)

Plan: `port/docs/retail-eu-plan.md` (Meilenstein „Retail-Discs (EU)“, Auftrag des Projektinhabers: R0 und R1;
GitHub-Arbeit auf einem neuen Branch von `beta-updates`). Grundsatzentscheidungen des Projektinhabers
(26.10.2026, nach R1): parallel zur Debug-Disc, Debug-Menü mit EU-Discs ausblenden (Cheat-Menü im Ingame-Menü
später gesondert), alle Sprachen der EU-Discs in R7, Aufnahme als **M13** in `CLAUDE.md`.
Nachtrag (26.10.2026, nach dem Bildschirmfoto des EU-Titelmenüs mit „LANGUAGE SELECT“): die Sprachwahl erfolgt im
Ingame-Menü der App, der Punkt wird aus dem Titelmenü entfernt (R7).

**R0 Prüfstrecke**
- `port/tools/disc_release.py`: welche Version ein Disc-Kopf ist. Debug-Build = G4BE08 mit der Dateitabelle bei
  0x28FE00, europäische Verkaufsversion = G4BP08, G4BE08 sonst = US-Verkaufsversion (ungetestet, abgelehnt).
- `whole_disc_folder.py` nimmt G4BP08 an, lehnt gemischte Versionen ab; `--check DIR --disc ...` erkennt einen
  Ordner, der aus anderen Abbildern entpackt wurde. `whole_r100_check.sh` nutzt das (Debug- und EU-Läufe teilen
  den Arbeitsordner: wechselt die Version, wird neu entpackt; vorher lief ein EU-Aufruf still mit den alten
  Debug-Dateien) und nennt die Version im Bericht („Version: retail-eu“). Tests: `test_whole_disc_folder.py`
  (4 Fälle, neu EU, Mischung, US).
- `port/tools/retail_compare.py` (Test `test_retail_compare.py`, beide in `whole_build.sh`): Dateilisten,
  REL-Modulnummern je Datei und der Aufbau jedes Archivs (same / appended / shifted / fewer / evd-missing).
  Mit den echten Discs: genau die Tabelle des Plans (4 verschobene Archive `pl0a.drs`, `map_obj4g.dat`,
  `ss_wep40.dat`, `ss_term.dat`; `r204s00.evd`/`s204s00.evd` ohne `s00_012.lit`; 127/124 Dateien mit Modulen,
  0 mit anderer Nummer).
- Kern: `runtime/dvd.cpp` erkennt die Version aus `sys/boot.bin` nach derselben Regel
  (`re4_disc_release()`, `RE4_DISC_*` in `re4_runtime.h`), schreibt sie ins Log („DVD: disc 0: …, European
  release G4BP08“) und warnt bei gemischten Discs. Laufzeittest `disc release`.

**R1 Starten** (Patches 0117–0119, Tabelle unten)
- 0117: größerer Kern (`etc/core.das`, EU 0x23AC40 Byte) nach 0x81C00000 statt über die Optionsdaten.
- 0118: ohne Debug-Schrift `etc/moji8.tpl` kein Absturz mehr, der Debug-Text bleibt aus.
- 0119: ohne `debug/roomInfo.dat` eine Ersatztabelle mit dem Raum des neuen Spiels (r120); ein anderer
  Debug-Startraum (`RE4_START_ROOM`) wird abgelehnt und gemeldet, das Spiel läuft wie beim Start in r120.
- `debug/config.txt` fehlt auf den EU-Discs: geprüft, ohne Folge. Alle Werte, die die Datei der Debug-Disc setzt
  (Sprache USA, Helligkeit 64, Spielmodus NORMAL, Startraum r120, Debug-Seite 1, Balken an), sind auch ohne sie
  so (`systemWorkInit`, `systemRestartInit`); nur `PlMode` (3 statt 0) und der Benutzername weichen ab, beide
  werden nirgends gelesen.

**Prüfungen (i686, Stand `da62a6e6`)**
- EU-Abbilder unverändert (`--data E:\…\RE4-DiscdataEuro`, entpackt durch die Prüfstrecke): Build 0 Fehler,
  alle Tests 0 Fehler, Formatvergleich 407 Dateien ohne Fehler. Spiellauf: **kein Absturz, Hauptschleife bei
  Bild 499 (r120), 10 000 Bilder**; über die Testhilfe nach r100, dort etwa 4 700 Bilder Spiel, Soft-Reset,
  Titel neu. Log: Kern bei 0x81C00000, Ersatztabelle, `RE4_START_ROOM=100` abgelehnt. Alle 12 Filme der EU-Discs ohne Fehler.
- **Gegenprobe Debug-Discs**, derselbe Build: „ALLES OK“ (Spiellauf r100, Speicherkarte, Filme); keine der
  neuen Stellen greift (Kern am alten Platz, `roomInfo.dat` gelesen).
- **Vergleichslauf** Debug-Discs mit Start in r120 gegen den EU-Lauf: über alle 10 499 Bilder gleiche Räume,
  Positionen, Gegner und Zufallswerte; Unterschiede nur in den GX-Zählern und der Heap-Belegung (andere Daten) und
  in 18 Bildern beim Titel-Timing nach dem Soft-Reset. Der Weg r120 → r100 ist das Verhalten der Prüfstrecke beim
  Start in r120, kein Unterschied der Daten.
- **64 bit (x86_64, steht für arm64-v8a)** mit den EU-Abbildern: Build 0 Fehler, alle Tests 0 Fehler,
  Formatvergleich ohne Fehler, Spiellauf wie bei i686 (kein Absturz, Hauptschleife bei Bild 499, 10 000 Bilder).
- **Offen:** der Speicherkarten-Teil der Prüfstrecke mit EU-Daten scheitert: er startet über `RE4_START_ROOM=0x100`
  (mit EU-Daten nicht möglich, das Spiel bleibt in r120) und der Speicherbildschirm blieb in der Dateiliste stehen
  (`card: save: file list`, bis Bild 30 000). Ob das an den EU-Menüdaten liegt (R3, z. B. „IDSystem work full“)
  oder am Start in r120, ist nicht geklärt. Nicht geprüft: ARM32/ARM64 unter qemu, Android-Build, Gerät.

## M13 Retail-Discs (EU), Schritt R4: App-Import (26.10.2026 abends, lokal, Branch `retail-eu`)

Auftrag des Projektinhabers: R4 vor R3 ziehen, damit die EU-Discs auf dem Retroid getestet werden können.

- **Import (`disc/GameCubeDisc.java`):** nimmt G4BE08 mit der Dateitabelle bei 0x28FE00 (Debug-Build) und G4BP08
  (europäische Verkaufsversion, nur im Kernmodus: `retailAllowed`, gesetzt in `Re4Application`); die US-Fassung
  (G4BE08, Tabelle woanders) wird mit eigener Meldung abgelehnt. `release` je Disc, `ready.properties` schreibt
  `id`, `release` und die Zahl der geprüften Code-Stücke.
- **Prüfkataloge je Version:** `references.tsv`/`rooms.tsv`/`blocks.tsv` (Debug) und neu `references-eu.tsv`
  (134 Code-Stücke: `main.dol`, `Rel/*.rel`, die REL-Module in `em/*.drs`), `rooms-eu.tsv` (128 Räume), `blocks-eu.tsv`
  (gleich den Debug-Blöcken). Erzeugt mit `port/android-decomp/tools/make_disc_catalogs.py` (nur Namen, Größen,
  Prüfsummen); das Werkzeug erzeugt die Debug-Raum- und Blockkataloge der App identisch, ihr Code-Katalog (116) ist eine
  Teilmenge dessen, was es findet (143). `ReferenceCatalog` verlangt nicht mehr genau 116 Zeilen.
- **Gemischte Discs:** `DiscInstaller.release`/`sameRelease`; der Import einer Disc der anderen Version wird nicht
  verweigert (sonst ginge kein Wechsel), aber die Startseite sagt „Disc 1 is the European release, Disc 2 the debug
  build: import both discs of one release.“ und „Start game“ bleibt aus, bis beide passen. Jede Disc zeigt ihre Version
  („Disc 1 Data is ready (European release)“). Importe vor diesem Stand gelten als Debug-Build.
- **Räume der Zustandsbrücke** (`CoreActivity.prepareRoom`) und die Raumprüfung nach dem Import nehmen den Katalog der
  importierten Version.
- **Kern (`runtime/dvd.cpp`):** mit den EU-Discs sieht das Spiel die Kennung G4BE08 der Debug-Version. Die Speicherkarte
  bindet jede Datei an die Kennung der Disc (`__CARDAccess`): mit G4BP08 wären die Spielstände der Debug-Discs (und die
  `.gci` des App-Menüs, `gci.cpp`) die eines anderen Spiels gewesen; der Spielcode und damit das Spielstand-Format sind
  dieselben. `cDvd::DiscChange` fragt ebenfalls nach „G4BE“. Laufzeittest `disc release` angepasst.

**Prüfungen**
- Java (Windows, JDK 11): `DiscTests` synthetisch 19 Prüfungen (neu: EU, US, Mischung, `ready`/`release`), mit den echten
  Debug-Discs 4 501 Prüfungen (Prüfsummen, Entpacken, Manifest), mit den echten EU-Discs im Kernmodus 5 249 Prüfungen,
  `RoomFileCheckTests` ok. Dabei behoben: der Test las `manifest.tsv` im Windows-Zeichensatz (eine Datei der Debug-Disc
  hat einen japanischen Namen) und zählte die zwei Systemdateien nicht mit; beides nur im Test.
- PC-Prüfung, Stand `2f711a61`: EU-Abbilder i686 „ALLES OK“ (Spielstände jetzt `bh4_system`/`bh4_data00` mit G4BE08),
  Debug-Discs i686 „ALLES OK“.
- Android-Build (`whole_build_android.sh --abi all` in `~/re4-build`, dann `gradlew.bat :app:clean :app:assembleCoreDebug`
  mit dem Java von Android Studio): armeabi-v7a und arm64-v8a je 0 Fehler, Platzhalter wie erwartet; APK
  `app-core-debug.apk` 18,9 MB, `0.91.0-beta-core` (Versionscode 75, unverändert), beide ABIs, die EU-Kataloge, signiert
  mit dem Debug-Schlüssel des PCs. **Gerätetest offen** (Anleitung an den Projektinhaber, 26.10.2026).

## M13 Retail-Discs (EU), Schritt R2: verschobene Archiv-Plätze (26.10.2026 abends, lokal, Branch `retail-eu`)

Vorgehen: für jedes der vier verschobenen Archive (Werkzeug `retail_compare.py`) den Code der Verkaufsversion
mit dem der Debug-Version verglichen. Die Funktionen wurden in den REL-Modulen beider Discs über ihre
Befehlsfolgen gefunden (Adressen der Debug-Symbole in `config/G4BE08`) und mit `llvm-objdump` (PowerPC)
nebeneinander gelesen; die Werkzeuge dafür lagen außerhalb des Repositorys.

- **Funkspruch (`ss/eng/ss_term.dat`, 10 statt 11 Teile):** Patch 0120 (Tabelle unten). Die fehlende Textur liegt
  in der EU an jeder `ss_ocNNN.dat` als Teil 16 (dort „appended“).
- **Krauser (`em/pl0a.drs`):** dieselben Teile, acht davon in anderer Reihenfolge (die Textur von Debug-Teil 7
  steht in der EU auf 12, die fünf Modelle dahinter rücken einen Platz vor). Die EU-Fassung liest in
  `cPlKlauser::setModel`/`setHead` und im Untermenü (`klauserModelInit`, `wep36Init`) entsprechend andere Plätze;
  Hände und Waffen sind gleich.
- **Adas Gewehr (`ss/cmn/ss_wep40.dat`):** die EU hat ein zweites Gewehrmodell auf Teil 2 (`wep40Init` wählt 1 oder 2),
  Hand und Bewegung folgen auf 3 und 4.
- Für Krauser und das Gewehr setzt der Umwandler die Tabelle beim Laden in die Reihenfolge der Debug-Version
  zurück (`runtime/formats/formats.cpp` `retailOrder`, nur mit EU-Discs und nur, wenn die EU-Reihenfolge erkannt
  wird; das zweite Gewehrmodell kommt ans Ende). Geprüft an den echten Dateien: danach ist die Teile-Liste gleich
  der der Debug-Version, jedes verschobene Teil hat genau die Größe seines Debug-Gegenstücks. Laufzeittest
  `retail order`. So passt auch Code, der nicht einzeln verglichen wurde (Krausers Angriffe, `wep07`).
- **Inselkarte (`ss/cmn/map_obj4g.dat`):** kein Handlungsbedarf. Die EU füllt leere Plätze; der Debug-Code nutzt dort
  nur Plätze mit derselben Art Teil oder nimmt Ersatzmodelle (`mark_model_tbl`, `map_door_tbl`).
- **Speicherbildschirm:** das Hängen in der Dateiliste liegt nicht an den EU-Daten. Mit den Debug-Discs hängt es in
  r120 genauso (eigener Lauf, `RE4_START_ROOM=0x120`). Die Kartenprüfung von `whole_r100_check.sh` setzt Leon ohne
  `debug/roomInfo.dat` jetzt wie der Spiellauf an die Tür nach r100 (B nach 300 Bildern) und speichert dort.

**Prüfungen (i686)**
- Funkspruch-Test: EU-Daten, nur für den Test ergänzt um `debug/roomInfo.dat` der Debug-Disc (Start direkt in r100,
  wie beim Debug-Start): Funkspruch öffnet bei Bild 1756, schließt bei 2620, Leon frei bei Bild 2923 (wie mit den
  Debug-Discs); die ganze Prüfstrecke „ALLES OK“, auch Speichern und Laden. Bild des Funkspruchs: Hunnigan, Leon
  und Text richtig; **die dunkle Leiste hinter dem Untertitel fehlt** („IDSystem::set() work full“, drei Bausteine,
  gehört zu R3).
- EU-Abbilder unverändert, Kartenprüfung (`--card-only`): „ALLES OK“ (Systemdatei, Speichern in r100, Laden, „Retry“).
- Prüfkette, Stand `6a9c8c25`: EU-Abbilder unverändert i686 **„ALLES OK“** (erstmals mit EU-Daten: Spiellauf,
  Filme, Speichern und Laden), Gegenprobe Debug-Discs i686 **„ALLES OK“**, EU-Abbilder x86_64 (steht für
  arm64-v8a) **„ALLES OK“**; Laufzeittest `retail order` grün auf beiden. Nicht geprüft: ARM unter qemu,
  Android-Build, Gerät.

**Offen / neu gefunden**
- Spielstände tragen die Kennung der eingelegten Disc (`bh4_data00` mit G4BP08 bei den EU-Discs, G4BE08 bei den
  Debug-Discs). Ob ein Spielstand der einen Fassung mit der anderen geladen werden kann, ist nicht geprüft (R4/R8).
- Krauser und Adas Gewehr laufen in keiner Prüfung (Mercenaries/Ada); geprüft ist die Umsortierung an den Dateien und
  im Laufzeittest, nicht im Spiel.

## Branches aufgeräumt (26.10.2026 abends)

Entscheidung des Projektinhabers: Arbeitsbranch ist jetzt **`beta-updates`** (Stand `a5eed0f`, gleich dem bisherigen
`claude/adoring-pasteur-h5s2fw`). Zum Löschen freigegeben: `claude/adoring-pasteur-h5s2fw`, `gui-optimisations`,
`beta-optimisations` (jeder ihrer Commits liegt in `beta-updates`). `master` bleibt unverändert (seine drei
README-Commits vom 24.10. heben sich auf). `ouya-discarded` und `re4-standalone` bleiben als Branches. Der Tag
`0.91.0-beta` soll auf `a5eed0f` zeigen (bisher auf `master`, `ac3c0e2`). Löschen und Tag verschieben verweigert
GitHub der Cloud-Sitzung (nur neue Branches erlaubt); der Projektinhaber erledigt beides per PowerShell vom PC.
Befehle: `port/docs/lokal-bauen.md`.

## Debug-Menü aufgeräumt (26.10.2026 abends, Cloud)

Wunsch des Projektinhabers: im Debug-Menü (SELECT lang) nur AREA JUMP und DEBUG OPTION, linksbündig, ohne die
flackernden senkrechten Balken.

- **Menü:** Patch 0116 (`db_menu.cpp` `move`): eine Spalte „TOOL MENU“, darunter die beiden Einträge bei x = 32,
  Zeiger bei x = 20, ab Zeile 72 (bleibt im 16:9-Ausschnitt, Zeilen 56–391); der C-Stick verschiebt nichts mehr.
  Hoch/runter wechselt (mit Umlauf), A startet, B/START schließt.
- **Balken:** die Zeitbalken und der Controller-Monitor (`DBG_PROC_BAR`, `DBG_PAD_INFO`) schaltete
  `state_trace.cpp` bisher nur einmal ab; im letzten Bericht stand diese Zeile, trotzdem sah der Projektinhaber
  die Balken wieder. Was sie wieder einschaltet, ist offen (im Spiel nur der Flag-Editor und das Untermenü, das
  den Zustand von vorher zurücksetzt). Jetzt hält die Laufzeit sie bei `RE4_DEBUG_HUD=0` jedes Bild aus und
  schreibt jedes Wiedereinschalten ins Fehlerprotokoll („run: the debug bars were on again, switched off
  (rXXX, tool menu open/closed)“, höchstens zehnmal).
- **60 FPS:** das Debug-Menü und seine Werkzeuge (`DBG_TEST_MODE`) bekommen keine Zwischenbilder mehr
  (`re4_picture_still`, wie Untermenü und Schreibmaschine): Listen aus Debug-Text über einem angehaltenen Spiel.
  Falls die Balken ein Mischfehler der Buchstaben waren, sind sie damit auch weg.
- **Tests:** Kerntest `letterbox` prüft den neuen Fall (Bit 8 in `re4probe_picture_still`). Das Menü selbst
  braucht die Schrift von der Disc, darum **nicht in der Cloud getestet**, nur gebaut.

## Sechster Nachtest 0.91.0 (26.10.2026 abends, Cloud-Korrektur)

Bericht des Projektinhabers (Retroid): der weiße Balken ist weg ✅. Flammen und Fackeln flackern beim schnellen
Drehen mit 60 FPS noch; vier R3-Serien an der blauen Feuerschale des Händlers (Kamera dreht sich).

- **Messung:** Im Zwischenbild (Anteil 0) stand der ganze Raum richtig, nur die Flamme und ihr Lichtschein standen
  schon an der Stelle des nächsten Bildes: beim Drehen hinkten sie der Schale hinterher. Drei Ursachen:
  1. **Flammen ohne Gegenstück:** jedes Flammen-Viereck zeigt pro Spielbild ein anderes Einzelbild seiner
     Animation, also eine andere Textur; die Textur gehört zur Signatur (`DrawSig`), es gab nie ein Gegenstück.
     Neu: Teilchen (eigene Matrix, höchstens 16 Ecken) ohne Gegenstück suchen eins mit gleichem Aussehen ohne
     Texturen (`DrawRec::look`, `recByLook`); die Textur bleibt die des nächsten Bildes.
  2. **Kamerabewegung falsch abgelesen:** zwei Teile eines bewegten Gegenstands mit derselben Matrix
     „bestätigten“ einander (Serie 01, Bild 5: 44 Flammen-Teilchen abgewiesen). Neu (`NoteCamera`): jedes große
     Teil eigenen Aussehens schlägt eine Bewegung vor oder stimmt für eine; eine Stimme zählt nur von einem
     Teil mit anderer Matrix; es gilt die Bewegung mit den meisten Stimmen (mindestens zwei), also der Raum.
  3. **Lichtschein als 2D-Viereck:** der Schein ist ein flaches Viereck (418 Einheiten breit), das das Spiel
     jedes Bild in Bildschirmkoordinaten über die Flamme legt; beim Drehen wanderte es 23 Einheiten je
     Spielbild, flache Teile kamen bisher immer aus dem nächsten Bild. Neu: ein flaches Teil mit eigener Matrix
     wird gemischt, wenn es sich nur verschoben hat, höchstens um ein Viertel des Bildes (`kFlatMove`, 128).
     Nicht, wenn es sich gedreht oder seine Größe geändert hat: r224 legt ein Abbild der Szene durch ein
     Vollbild-Viereck über sich, das sich 10 Grad je Spielbild dreht (Texturkoordinaten aus den Ecken); halb
     gedreht zeigte es den Raum doppelt (erster Versuch: r224 von 1,10 auf 13,29, behoben).
- **Messung über elf Serien** (mittlere Abweichung des Zwischenbilds mit Anteil 0, vor heute → 0.91-Kandidat
  `3077f24` → neu): Händler 13,1/12,8/10,1/3,4 → gleich → 1,8/0,9/1,3/0,9; r224 4,0 → 1,1 → 1,1; die übrigen
  1,6–3,5 → 1,6–3,5 → 0,1–3,5; stark abweichende Bildpunkte am Händler 64 353 → 9 031; echte Bilder byte-gleich.
- **Tests:** `TestInterpolationEffects` (neu: Flamme mit neuer Textur halbwegs; flacher Schein halbwegs; ein
  flaches Viereck mit Sprung und eins mit neuer Größe wie im nächsten Bild), `TestInterpolationParticles`
  erweitert (vor dem Raum ein fliegender Gegenstand aus zwei Teilen mit einer Matrix; die Raumteile mit eigenen
  Matrizen). Gegenproben je Regel (ohne Aussehen ohne Textur, ohne 2D, ohne Sprunggrenze, Stimmen gleicher
  Matrix, ohne Dreh-/Größenregel) jeweils rot. `gx_gl_check` 0 Fehler. Kern unverändert.
- **Nicht getestet:** auf dem Gerät.
- **Bekannter Fehler im Releasetext 0.91.0** (Wunsch des Projektinhabers, 26.10.2026): Fackeln und Feuer
  können mit 60 FPS ungleichmäßig flackern; ihre Animation (ein Einzelbild je Textur) hat kein Zwischenbild,
  sie wechselt weiter 30-mal je Sekunde. Die Zeile „Fixed“ nennt darum nur noch Rauch und Nebel.

## Fünfter Nachtest 0.91.0 (26.10.2026 abends, Cloud-Korrektur)

Bericht des Projektinhabers (Retroid, Bericht 15:03, Foto): Rauch ruckelt mit 60 FPS nicht mehr, Flammen und Fackeln
noch; bei schnellen Drehungen Leons ein weißer schräger Balken unten in der Bildmitte (nur mit 60 FPS). Die
R3-Serien des Berichts (r224, r22c, r220) kamen nicht mit.

- **Weißer Balken (von `607b4f5` eingeführt, nie in einem Release):** `MatchDraw` verglich seit dem Nachtest am Abend
  alle Zeichnungen mit der Kamerabewegung und nahm bei gleichem Aussehen das deutlich nähere statt des ersten. Die
  Kamerabewegung ist die des Raums; Leons Teile drehen sich mit der Kamera mit. Bei schnellen Drehungen sah daher ein
  anderes gleich aussehendes Teil Leons näher aus, die beiden tauschten die Plätze, und ihre gemischten Ecken (CPU-
  Skinning, `far <= 1500`) zogen einen langen Balken. Nachgewiesen an den älteren Serien: Zeichnungen mit 96 bis
  2281 Ecken wurden anders zugeordnet als vorher. Neu: Die neue Zuordnung (Kamerabewegung, das nähere, enge
  Grenze) gilt nur für **Teilchen** (eigene Matrix, höchstens 16 Ecken); alles andere wieder genau wie vorher (das
  erste innerhalb der Reichweite, sonst das nächste der ersten acht).
- **Flammen:** In r224 werden die Feuerteilchen richtig zugeordnet und bewegt (gemessen: Drehung und Größe bleiben
  gleich), aber 38 % von ihnen ändern von Bild zu Bild ihre **Materialfarbe** (XF 0x100C, vor allem die Deckkraft:
  sie blenden ein und aus, z. B. 0,14 → 0,02). Das Zwischenbild nahm die Farbe des nächsten Bildes; das Glimmen lief
  mit 30 Schritten. Neu: Material- und Umgebungsfarben (`xf.matAmb`) werden mit der Zeichnung notiert und im
  Zwischenbild gemischt; `FillXfRegs` setzt sie danach aus den Registern zurück.
- **Messung (Zwischenbild mit Anteil 0 gegen das Bild davor, sieben Serien):** r224 mittlere Abweichung 2,23 → 1,10
  (vor heute 4,01), Zaun-Serie r223 3,3 → 2,6; stark abweichende Bildpunkte überall wie vorher oder weniger; die
  echten Bilder byte-gleich.
- **Shader-Liste:** 9006 Programme (6 neu aus dem Bericht).
- **Tests:** `TestInterpolationLooks` (neu: zwei gleich aussehende Teile mit 20 Ecken, das eine liegt dem Platz des
  anderen näher, bleiben in der Reihenfolge; die Materialfarbe eines Teilchens geht von Rot nach Blau über Lila; das
  echte Bild danach wieder Blau). Gegenproben: alles als Teilchen behandelt → Reihenfolge-Prüfung rot; ohne
  Farbmischen → Farb-Prüfung rot. `gx_gl_check` 0 Fehler. Kern unverändert.
- **Nicht getestet:** auf dem Gerät; Fackeln anderer Räume (keine Aufnahme davon; die Farbmischung gilt für alle
  Teilchen).

## Vierter Nachtest 0.91.0 (26.10.2026 abends, Cloud-Korrektur)

Bericht des Projektinhabers (Retroid, Bericht 14:14, R3-Serie in r224, dem Lavaraum): 60 FPS läuft jetzt mit
knapp 60 Bildern/s und flüssig (Protokoll: 60,0/s gezeigt, 13 von rund 8500 Zwischenbildern ausgelassen), aber
Nebel, Rauch und Flammen ruckeln und flackern.

- **Ursache:** Rauch- und Feuerteilchen sind kleine Vierecke gleichen Aussehens mit je eigener Matrix, einige
  tausend Einheiten auseinander. `MatchDraw` nahm das erste noch freie Teilchen des Bildes davor, solange es
  innerhalb eines Viertels seiner Entfernung plus 400 Einheiten lag (bei 20 000 Einheiten: 5400). Stirbt ein
  Teilchen oder entsteht eines, verschiebt sich die Reihenfolge, und Nachbarn tauschten im Zwischenbild die Plätze
  (in der Serie: Paare mit 3000–4400 Einheiten Abstand statt 20–130).
- **Neu (`gx_gl.cpp` `MatchDraw`; seit dem fünften Nachtest nur noch für Teilchen):** Das erste freie gilt weiter
  (die Reihenfolge des Spiels), außer ein anderes der ersten 32 freien liegt deutlich näher (um mehr als 2 % der Entfernung plus 50 Einheiten). Dazu die
  **Kamerabewegung** zwischen den beiden Bildern (`NoteCamera`, `CameraMove`): abgelesen an zwei großen Zeichnungen
  eigenen Aussehens (mindestens 64 Ecken, je eine im Bild davor; der Raum), die sich gleich bewegt haben. Mit ihr
  werden die Positionen des Bildes davor erst um die Kamerabewegung verschoben, und ein kleines Teilchen (höchstens
  16 Ecken) darf sich nur 5 % seiner Entfernung plus 300 Einheiten selbst bewegen: ein neu entstandenes nimmt nicht
  den Platz eines anderen ein, es steht wie im nächsten Bild.
- **Messung an der Serie (Zwischenbild mit Anteil 0, muss dem Bild davor gleichen):** stark abweichende
  Bildpunkte im ersten Zwischenbild 1103 → 4, in den übrigen bis 834 → höchstens 31. Die echten Bilder bleiben
  byte-gleich (138 Vergleiche); die sechs älteren Serien (Dorf u. a.) unverändert gut (keine Verschlechterung,
  eine von 3 auf 0).
- **Geprüft und verworfen:** Texturmatrizen im Zwischenbild mitzumischen (fließende Lava, ziehender Nebel) zeigte in
  r224 keine messbare Wirkung (die Teilchen ändern dort nur eine ungenutzte dritte Zeile); nicht eingebaut. Was
  bleibt, sind Farbwechsel (Glühen der Lavaplatten, Ein- und Ausblenden der Teilchen): sie wechseln mit den
  30 Bildern des Spiels.
- **Shader-Liste:** 9000 Programme (64 neu aus dem Bericht).
- **Tests:** `TestInterpolationParticles` (neu: fünf Teilchen, Kamera 1500, eigene Bewegung 500, eines stirbt,
  eines entsteht; ohne Kamerabewegung 2 Prüfungen rot, mit der alten Zuordnung 1), `TestInterpolation` und alle
  übrigen: `gx_gl_check` 0 Fehler. Kern unverändert (kein Kern-Build nötig).
- **Nicht getestet:** auf dem Gerät.

## Dritter Nachtest 0.91.0 (26.10.2026 nachmittags, Cloud-Korrektur)

Bericht des Projektinhabers (Retroid, Video): C-Stick, Fackelschein hinter Wänden und Intro-Wellen ✅ bestätigt.
Offen: 60 FPS und Frame-Skip zusammen ruckeln hin und her; mit 60 FPS flackert die Liste des Ladebildschirms an der
Schreibmaschine beim Hochscrollen; 60 FPS bringt auf dem Retroid nur 2–3 Bilder/s mehr. Dazu die vom Projektinhaber
selbst gebaute `RetroTouch-1.0.0-beta.7.aar` (Commit `d987f4b`).

- **60 FPS und Frame-Skip schließen sich aus** (Wunsch des Projektinhabers; ersetzt „Frame-Skip auch mit 60 FPS“ vom
  Mittag): Einschalten des einen schaltet das andere aus, darunter der Hinweis `ingame_skip_or_smooth` (10 Sprachen,
  `check_translations.py` 122 Texte). Waren beim Start beide an (alte Einstellungen), bleibt 60 FPS und Frame-Skip
  geht aus (`CoreActivity`). `core_mode.cpp` lässt Bilder nur ohne 60 FPS aus.
- **Flackern an der Schreibmaschine:** Die Liste zeichnet jeden Buchstaben mit gleich aussehenden Zeichenbefehlen.
  Das Zwischenbild ordnet die Zeichnungen zweier Bilder nach ihrer Reihenfolge einander zu; scrollt die Liste,
  rutschen die Buchstaben an andere Plätze und das Zwischenbild mischt sie. Neu: Kennzeichen
  **`RE4_GX_CHUNK_STILL`** (`re4_gx_frame.h`). `runtime/picture.cpp` prüft jedes Bild (`re4_add_frame_hook`), ob ein
  Untermenü (`STA_SUB_SCRN`: Koffer, Karte, Händler, Akten) oder ein Speicherkarten-Bildschirm (`SYS_TYPEWRITER`,
  `CardMainTask`: Laden, Speichern, Startprüfung) offen ist, und meldet es `re4_gx_still` (`gx_decode.cpp`); die
  Bilder tragen dann das Kennzeichen, `GxRenderer::Interpolate` macht kein Zwischenbild hinein, darin und heraus.
  Die Menüs laufen mit 60 FPS also mit den 30 Bildern des Spiels. Wechsel ins Fehlerprotokoll
  („picture: a menu screen: no pictures between“). Tests: `TestInterpolation` (vier Fälle, ohne die Sperre alle vier
  rot), `runtime-test gx` (die Bilder bei offenem Menü tragen das Kennzeichen, danach nicht mehr), `runtime-test
  letterbox` (`re4probe_picture_still`: Untermenü, Kartenbildschirm, keins).
- **Tempo von 60 FPS:** Jedes überblendete Figurenmodell kostete im Zwischenbild ein Abbilden und Freigeben des
  Puffers beim Treiber (r104: 233 je Zwischenbild). Neu: mit `EXT_buffer_storage` ein Ring von 24 MB, einmal dauerhaft
  abgebildet, je Zwischenbild ein Viertel mit Zaun (`BlendRingInit`, `blendFence`): je Figur nur noch ein Kopieren,
  kein GL-Aufruf; ohne die Erweiterung der bisherige Weg (`RE4_GX_PERSISTENT=0` erzwingt ihn). Die r104-Aufnahmen
  vom Gerät zeichnen auf beiden Wegen dasselbe. **Grenze:** Ein Zwischenbild ist ein ganzes zusätzliches Bild für
  die GPU. Schafft das Gerät die 30 Bilder des Spiels nur knapp, bleibt keine Zeit dafür; dann entfallen die
  Zwischenbilder (siehe „Zwischenbilder nur, wenn Zeit ist“) und es kommen nur wenige Bilder/s dazu. 60 FPS wirkt
  voll erst, wenn das Spiel ohne es fest 30 Bilder/s schafft (kleinere Auflösung oder „Dynamisch“).
- **Nicht getestet:** alles auf dem Gerät (Tempo mit dem Ring, kein Flackern mehr an der Schreibmaschine, die
  Umschaltung im Menü).
- **Tests (Stand `68ecb61`):** `whole_build.sh` x86_64, i686, armhf, aarch64: 0 Fehler beim Bauen, `runtime-test`
  (auch mit `RE4_GX_THREADS=0`) und `core-test` überall 0 Fehler, `core_remote_test` grün (main, crash, missing).
  `gx_gl_check` 0 Fehler, alle Java-Tests (`test_host.sh`, Java-Teil) grün, `check_translations.py` 122 Texte in
  10 Sprachen.

## Zweiter Nachtest 0.91.0 (26.10.2026 mittags, Cloud-Korrektur)

Bericht des Projektinhabers (Retroid, vier Fehlerberichte, R3-Aufnahmen r223): 60 FPS mit „Dynamisch“ bleibt auf
Stufe 2 bei 17–19 Bildern/s, Frame-Skip wirkt nicht, mit beiden 3–5 Bilder/s; C-Stick soll ein gelber Analogstick
sein; Fackelschein blitzt kurz hinter Wänden auf und scheint nach einem Auflösungswechsel wieder durch Wände.

- **Fackelschein:** Der Kern verglich die Zahl seiner Marker mit der Zahl gelieferter Tiefenbilder. Jedes ausgelassene
  Tiefenbild (GPU im Rückstand, neuer Renderer beim Auflösungswechsel) vergrößerte den Abstand für immer; ab 8 hieß es
  nur noch „fern“. Neu: Der Marker BP 0x5B trägt die **Bildnummer** des Kerns (24 bit), der Renderer gibt seine Karte
  mit dieser Nummer zurück (`peekPicture`, `peekSlotPicture`). Karte höchstens 8 Bilder alt: Antwort aus der Karte;
  älter: „davor“ (0, der Schein wartet ein, zwei Bilder auf die neue Karte statt durch die Wand zu blitzen); 60 Bilder
  lang keine frische Karte oder nie eine: „fern“ wie früher (`gx_fifo.cpp`). Tests: `core-test peek depth` (neu
  geschrieben), `TestDepthMap` (Nummer kommt zurück), `core_remote_test` (die Karte jedes markierten Bildes mit seiner
  Nummer über den gemeinsamen Speicher).
- **60 FPS stockte:** Zwischenbilder schrieben die überblendeten Eckpunkte der Figuren mit `glBufferSubData` mitten im
  Bild in den Eckpunkt-Puffer, den die GPU für dieses Bild noch las: der Treiber kopierte oder wartete bei jeder Figur
  (Bericht: 74 ms je Bild, davon 57 ms Warten). Neu: eigener Puffer `blendVbo` je Zwischenbild, jede Figur an eigener
  Stelle, ohne Warten geschrieben (`GL_MAP_UNSYNCHRONIZED_BIT`); das echte Bild danach lädt seine Eckpunkte nicht noch
  einmal hoch (`uploadedBy`). Messung in der Cloud (llvmpipe, 60 Figuren mit je 4000 Eckpunkten): 31 ms statt 49–54 ms
  je Bildpaar.
- **Zwischenbilder nur, wenn Zeit ist:** Liegt beim Zeichnen schon das nächste Bild des Kerns bereit, entfällt das
  Zwischenbild (`gxBetweenDropped`, im Protokoll „left out“); das Spiel wartet nicht mehr auf den Renderer.
- **Frame-Skip auch mit 60 FPS:** `CanSkip` lehnt beim Mitschreiben nicht mehr ab; nach einem ausgelassenen Bild kein
  Zwischenbild (`Skip` setzt `recValid` zurück). Test in `TestInterpolation`.
- **Dynamische Auflösung mit 60 FPS:** Ein Fenster, in dem mehr als ein Viertel der Zwischenbilder entfiel, während die
  GPU zurückliegt, zählt als langsam; hoch erst wieder, wenn fast keines entfällt (`gx_auto_level.h`,
  `kDroppedDown`/`kDroppedUp`). Liegt nur der Prozessor zurück, bleibt die Stufe (eine kleinere Auflösung hilft dort
  nicht). Tests in `TestAutoLevel`.
- **C-Stick als Analogstick:** RetroTouch **1.0.0-beta.7** (aus dem beta.6-Quelltext im RetroTouch-Repository, in der
  Cloud gebaut, `docs/retrotouch-1.0.0-beta.7.patch`): Stick-Aktionen (`registerStickAction`), im Editor ADD und dann
  ACTION bis „C“ macht aus der Taste einen Stick, gemeldet über `RetroTouchStickListener.onStick`. Die App: `cstick`
  in GameCube-Gelb (0xF7C81E), wirkt wie die rechte Touch-Fläche (die weitere Neigung zählt); die vier C-Tasten vom
  Vormittag entfallen. Tests: `CoreTouchTests` (283), App-Java gegen beta.7 übersetzt. **Der RetroTouch-Quelltext muss
  noch in dessen Repository** (die Sitzung darf dort nicht schreiben).
- **Fehlende Texturen nach Grafik-Optionen:** Der neue Renderer übernimmt die EFB-Kopien des alten
  (`GxRenderer::HandOverCopies`, skaliert); Kopien, die das Spiel nur beim Raumstart macht, fehlten sonst bis zum
  nächsten Raum (Bericht: 306 fehlende in r223 nach drei Wechseln). Test `TestHandOverCopies` (ohne Übergabe 3 Fehler).
- **Shader-Liste:** 8936 Programme (868 neu aus den sieben Berichten des Tages).
- **Nicht getestet:** alles auf dem Gerät (Tempo von 60 FPS, Frame-Skip, Fackelschein, C-Stick im Editor). RetroTouch
  selbst lässt sich ohne Android nicht ausführen (nur übersetzt und Code-Durchsicht).
- **Unabhängige Durchsicht aller Änderungen des Tages** (zweiter Agent, nur lesend), acht Befunde, alle nachgeprüft:
  1. Stufenwechsel der dynamischen Auflösung warf ein vorab geholtes, ungezeichnetes Bild weg (`smoothDrop` gab
     `skipNext` frei, ohne dass der Kern neu sendet: fehlende Texturen, falscher GX-Zustand) – behoben (nur noch das
     Vorbild der Zwischenbilder geht).
  2. **Raumdatei-Prüfung meldete jede Raumdatei als beschädigt** (`manifest.tsv` nennt `files/St1/r100.das`,
     `rooms.tsv` `St1/r100.das`) – behoben (`RoomFileCheck.key`), Test mit echten Pfaden.
  3. Neue Tastenzählung (pad.cpp) + analoge Schwellen ohne Rastung: ein R2, das um 85 % zittert, hätte R ein Bild
     losgelassen (Waffe sinkt); alternatives Feuer bei 50 % gab Zusatzschüsse – behoben mit Rastung in `CoreInput`
     (L/R ab 85 %, los unter 70 %; Feuer ab 50 %, los unter 35 %), Tests (149).
  4. `Collection.removeIf` (ab Android 7) und `InputDevice.isExternal` (ab Android 10) im Controller-Protokoll bei
     minSdk 23 – ersetzt bzw. abgefragt.
  5. **Frame-Skip übersprang im Spiel fast nie** (jede Zeichnung vor der neuen Kopie galt als Lesen) – `CanSkip`
     verfolgt jetzt, welche Kopie an welchem Texturplatz hängt, und lehnt nur ab, wenn eine TEV-Stufe sie liest oder
     das nächste Bild die Kopie nicht neu macht. Drei neue Testfälle.
  6. Zähler der ausgelassenen Zwischenbilder fiel beim Abschalten von 60 FPS auf 0 (Unterlauf) – jetzt fortlaufend.
  7. Menü zeigte bei gesperrtem Speichern und leerem Platz „nicht verfügbar“ statt „hier nicht möglich“ – Reihenfolge.
  8. Wiederholungszähler der Part-Tabellen-Prüfung (dvd.cpp) zählt jetzt je Datei.
  Nicht geändert: der Text „Speichern hier nicht möglich“ nennt Mercenaries, dort erscheint aber „nicht verfügbar“
  (unschädlich, Übersetzungen bleiben).
- **Tests danach (Stand nach `a74301c`):** `whole_build.sh` x86_64, i686, armhf, aarch64: 0 Fehler beim Bauen, alle
  Prüfungen grün bis auf einen einmaligen Ausfall von `re4_host --runtime-test` mit `RE4_GX_THREADS=0` unter
  qemu-aarch64 („1 failed“, welcher Teiltest ist nicht festgehalten); neun Wiederholungen mit denselben Einstellungen
  alle grün (vermutlich der bekannte zeitabhängige Teiltest bei Überlast). `gx_gl_check` 0 Fehler, alle Java-Tests
  (`test_host.sh`, Java-Teil) grün, App-Java gegen RetroTouch beta.7 übersetzt.

## Nachtest 0.91.0 auf dem Retroid (26.10.2026, Cloud-Korrektur)

Bericht des Projektinhabers (Retroid Pocket Flip 2, 64 bit, Logcat, drei Fehlerberichte, R3-Aufnahmen in r22c, r21d, r10a):
durch Türen laufen, außerhalb des Spielbereichs landen, bergab in der Luft weiterlaufen; schwarze Wellen im Intro weiter da.

- **Durch Türen / in der Luft: Fehler in Patch 0115 (von mir).** Der Patch merkte sich `atari.m_flag` (16 bit) in einem
  `u8` und schrieb es nach jeder Prüfung zurück: Bits 0x100 (Kollision mit der Umgebung) und 0x200 (mit Figuren) waren
  danach bei jedem weg, der `EmAtCheck` ruft – Leon (`player.cpp:372`), Türen (`emdoor.cpp:1373`), Ashley/Luis,
  Kisten, Felsen. Im Logcat bleibt Leons Höhe in ganz r10a bei 7346 und in r21d bei -3959; die R3-Bilder zeigen ihn in
  Türen. Behoben (`u16`). Neuer Test `runtime-test em at flags` (`game_probe.cpp re4probe_em_at_flags`): Ganado in der
  Luft wird aus der Tür geschoben, jede Figur behält ihr ganzes `m_flag` (0xC308 bleibt 0xC308). Gegenprobe: mit dem
  alten `u8` schlägt der Test fehl (i686, 26.10.2026). **Instant-Spielstände aus dem fehlerhaften Testbau** tragen
  den Fehler im Speicherabbild (Leon ohne Kollision) und sollten gelöscht werden.
- **Schwarze Wellen im Intro:** Die Logos am Anfang flimmern über `IdShimmerTrans` (`id_sys.cpp:1314`): Kopie des
  ganzen Bildes (Zeilen 0–447, 256 × 224, ohne Kürzung gelesen), der Film füllt nur die Zeilen 56–391, die Scissors
  sind voll – die Regel vom Vormittag (`sceneBottom`, nur bei Letterbox-Scissors) griff nicht. Neu:
  `GxRenderer::SetShownRows` (in `core_mode.cpp` je Bild: 56–392 in 16:9 und „Gestreckt“, sonst alle Zeilen).
  Volle Textur-Kopien wiederholen dann oben und unten die Randzeilen des gezeigten Ausschnitts, statt die Balken zu
  lesen (auch Unterwasser und die Tiefenschärfe am Rand). In 4:3 bleibt es wie auf dem GameCube. Test
  `TestShownRowsCopy` (`gx_gl_check`, 16:9 und 4:3, auch mit MSAA). Die R3-Aufnahmen dieses Berichts zeigen das
  Intro nicht; Gerätetest offen.

## Umsetzung nach der Analyse: Version 0.91.0-beta (26.10.2026, Cloud, Branch `claude/adoring-pasteur-h5s2fw` auf `gui-optimisations`)

Entscheidungen des Projektinhabers (26.10.2026): `gui-optimisations` ist der Ausgangspunkt, der Grafikteil
meldet dem Kern die Tiefe zurück (neuer Rückweg Renderer → Kern), unsichere Stellen sperren das Speichern,
die Version heißt **0.91.0** (versionCode 75, `v0.91.0-beta.md`; zuerst 0.92.0 genannt, vom Projektinhaber am 26.10.2026 auf 0.91.0 berichtigt, da 0.90.1 die aktuelle Version ist). Cloud ✅ (siehe unten), **Gerätetest offen**.

- **Instant-Spielstand:** Laden schreibt die Systemdaten nicht mehr zurück (Optionen und Mercenaries-Werte
  bleiben); Speichern gesperrt in Mercenaries, Assignment Ada, Ashley-Abschnitten, in den Räumen
  r10b/r11b (Del Lago), r10f (Gondel), r11f (Mendez), r21b/r223 (Lore), r221, r226/r228 (Statuen-Jagd),
  r31c/r317 (Krauser), r332/r333 (Aufzug, Saddler, Jetski) und bei Boot, Schwimmen, Leiter, Gondel, Festhalten,
  Quick-Time, Händler, Zielfernrohr, Fernglas, Bulldozer (`unsafePlace()`, `instant_game.cpp`); Laden bleibt an
  diesen Stellen erlaubt, nicht in Mercenaries/Ada/Ashley. Die Raumliste ist aus den Quelltext-Namen
  geschätzt (Hinweis: Raum-Nummern der Bosse nicht gegen die Disc geprüft). Menü: Fokus bleibt nach Speichern/Laden,
  eigene Zeile „Speichern hier nicht möglich“ (`Re4InstantInfo.reserved`), Beschriftungen mittig. Patches 0111/0112 neu.
- **r222-Hänger:** Patch 0113 (siehe Patch-Tabelle), Protokollzeilen für stille Lesefehler und Null-Tabellen
  (`runtime/dvd.cpp`), Raumdateien gegen `rooms.tsv` prüfen (`RoomFileCheck`, nach dem Import und bei jedem Start,
  nur eine Warnung). Test: `runtime_tests` (Tabelle aus Nullen endet mit Fehler statt Stillstand).
- **32 bit:** Modul-Fenster über 0x80000000 (`re4_module_window_reserve`, `module_loader.cpp`), Patch 0114.
  Nur auf dem Papier geprüft (Syntax mit Ersatz-Header für 32 und 64 bit; der NDK fehlt in der Cloud). Die
  SIGBUS-Abstürze des Galaxy A02s nach dem Laden von r229 sind **nicht** behoben (Bericht ohne Protokoll).
- **Controller-Protokoll:** `ControllerLog` (Tasten und Achsen einmal je Gerät), `controllers.txt` im Bericht.
- **Dunkle Wellen unten:** `GxRenderer` merkt sich, bis zu welcher Zeile unter Vollbreiten-Scissors gezeichnet
  wurde (`sceneBottom`) und wiederholt bei halbgroßen Textur-Kopien die letzte gezeichnete Zeile (`limitY` im
  Kopier-Shader). Test `TestLetterboxCopy` (`gx_gl_check`).
- **Lichtschein durch Wände:** `GXPeekZ` (`re4_gx_efb_peek`) schreibt beim ersten Aufruf je Bild den Marker
  BP 0x5B in den Strom; der Renderer liest an der Anzeige-Kopie die Tiefe (320 × 264 Zellen, 24 Bit) in einen
  PBO und gibt sie ein Bild später über `re4_core_gx_depth` an den Kern zurück (im Kernprozess über den
  gemeinsamen Speicher, `Control.depthSerial`, `kVersion` 4). Ohne Karte oder bei mehr als 8 Bildern Rückstand
  antwortet er „fern“ wie bisher. `RE4_GX_NO_DEPTH_MAP=1` schaltet es ab. Tests: `TestDepthMap`, `core-test peek depth`,
  `core_remote_test` (i686). Die frühere Einschätzung „Fackel des Händlers leuchtet durch Wände: sehr wahrscheinlich
  Original“ (Abschnitte 24.10.) ist damit **widerlegt**.
- **FXAA und Frame-Skip:** FXAA-Durchgang nach der Anzeige-Kopie (`GxRenderer::SetFxaa`), Frame-Skip
  (`CanSkip`/`Skip`: liegen zwei Pakete bereit, wird das erste nur verarbeitet, nicht gezeichnet; nie bei RESET,
  HOLD, BLACK, MARK, 60 FPS, laufender Aufnahme, und nur wenn kein später gelesener Textur-Kopierauftrag
  übersprungen würde). Menü: Schalter unter MSAA und unter „Leistung“, 10 Sprachen. Tests `TestFxaa`, `TestFrameSkip`.
- **C-Stick per Touch:** vier optionale Tasten C↑ C↓ C← C→ im Editor (`CoreTouch`), mit dem Umsehen-Finger
  zusammen (der stärkere zählt). Ein echter zweiter Analog-Stick braucht eine neue RetroTouch (Callback mit Stick-Kennung).
- **Salazar-Statue r226, „A schnell drücken“ (Video des Testers, 26.10.2026):** `ButtonCount` (`r226.cpp`) füllt die
  Laufanzeige nur mit Druck-Flanken (`Key.trg`, A aus im Bild davor); ab Schwierigkeitsrang 8 (`Game_level`, dynamisch)
  ist das Fenster 3 Spielbilder (`r226_buttonAdj = -2`): nötig ist ein Druck mindestens jedes dritte Bild (10/s). Der
  Tester drückte im Mittel alle 3,9 Bilder; nachgerechnet mit seinen Drücken: Rang 8–10 Laufstufe 1,4 von 4 (Leon
  kommt nicht vom Fleck), Rang 1–7 Stufe 3,4. Dazu verlor der Port schnelle Taps: ein neuer Druck, während das Spiel
  den alten noch gehalten sah, wurde zu einem langen Druck (`pad.cpp`, `g_pressed`), Bildtipps bei Mash-Anzeigen hielten
  A zwei Bilder (`qte_touch.cpp`). Jetzt: jeder Druck eine eigene Flanke (ein Bild aus, dann an; Zähler je Taste,
  höchstens 4 offen), Mash-Tipps ein Bild an, ein Bild aus, aufgereiht. Tests `core-test pad`, `runtime-test qtetouch`.
  Rang 8+ bleibt so schwer wie im Original-Code (eine Hilfe, z. B. „A halten = schnell drücken“, wäre eine Entscheidung
  des Projektinhabers).
- **Tür-Tritt r100:** Patch 0115. **Nicht angefasst:** Zombie außerhalb von r100 (Eintrag unbekannt), Planken in der Hütte (wohl Absicht),
  Salazar-Statue (kein Fehler: L+R verlangt), rechteckiger Rauch und Flackern (Aufnahmen nötig).
- **Prüfungen (Cloud):** x86_64 ✅ (Bau 0 Fehler, `core-test`, `runtime-test`, `instant-store-test`, Formatvergleich);
  `gx_gl_check` auf Mesa ✅ (0 failed, mit allen neuen Tests); `re4decomp_link_check` (ganze native App-Bibliothek auf dem Host) ✅;
  Java: `CoreTouchTests` 297, `ControllerLogTests`, `RoomFileCheckTests`, `check_translations.py` (121 Texte, 10 Sprachen);
  die Java-Klassen kompilieren gegen android-all (15). **Nicht gelaufen:** Robolectric-Tests (`CoreMenuLayoutTest`),
  Gradle/APK-Bau, NDK-Bau, jeder Test auf einem Gerät.

## Analyse nach den Tester-Meldungen (26.10.2026, Cloud, Branch `claude/adoring-pasteur-h5s2fw` auf `gui-optimisations`)

Nur Analyse, nichts gebaut, nichts auf dem Gerät geprüft. Einzelheiten und Plan:
[`analyse-2026-10-26.md`](analyse-2026-10-26.md). Kurz:

- `gui-optimisations`: Bundle = GitHub `c4f79e9`, konfliktfrei mit `master` zusammenführbar; weder
  er noch `beta-optimisations` ist in `master`. Beschriftungen der Anzeige-Seite oben statt mittig
  (`CoreMenu.choices()` ohne senkrechte Ausrichtung). Instant Save: Laden überschreibt die
  Systemdaten, Mercenaries/Ada nicht gesperrt, Fokus springt, Patches 0111/0112 unscharf und nicht
  in der Patch-Tabelle, Version unverändert.
- **Hänger r222** (Infinix X6725, Galaxy S20 FE, „lava corridor“): die Teile-Tabelle von
  `st2/r222.das` ist leer, `readInit` zählt über das Puffer-Ende, `readMain` bleibt stehen.
  Vermutlich beschädigte Datei auf dem Gerät (Import prüft keine Raumdateien), sonst stiller
  Lesefehler im Port. Gleiches Muster wie r325.
- **Lichtschein durch Wände:** Ursache gefunden. `GXPeekZ` (Patch 0030 → `re4_gx_efb_peek`,
  `runtime/gx_fifo.cpp`) antwortet immer „fern“; die Leucht-Effekte (esp0e, espgen01, esp45)
  verstecken sich nur über diese Abfrage. Die Einschätzung „sehr wahrscheinlich Original“ in den
  Abschnitten vom 24.10. ist widerlegt (Dolphin-Video des Projektinhabers).
- **Schwarze Wellen unten:** Kopie der Zeilen 56–447 wird ganz gebunden, ungezeichnete Zeilen
  unter dem Balken sind schwarz.
- 32 bit: moto e6s bricht ab, weil ein Raum-Modul unter 0x80000000 geladen wird (kein
  Modul-Fenster auf 32 bit). Debug-Werkzeug „SCE ITEM“ stürzt ab (`LightToolPtr` leer).
- Salazar-Statue r226: kein Fehler (L+R verlangt). Planken r11c: wohl Absicht (nur Schrotflinte
  & Co.). Tür-Kick r100: Schiebe-Vorrang, Patch möglich.
- Gerätetabelle mit 22 Geräten (Benchmarks aus der Websuche) für den Projektinhaber erstellt.

## GUI-Optimierungen und Instant-Spielstand (24.10.2026)

Branch `gui-optimisations`, von `beta-optimisations` bei `19bd01c` abgezweigt.
[Umsetzung und Prüfungen](../../docs/gui-optimisations/IMPLEMENTATION.md),
[abgestimmte Screendesigns](../../docs/gui-optimisations/README.md).
Fünf Seiten in derselben pausierten Menüinstanz: Hauptseite mit Instant-Slot,
Display, Controls, Performance, Management. Buttons statt Radio-/Checkboxen,
alle bisherigen Funktionen/Präferenzschlüssel und alle zehn Sprachen erhalten.
Titel und Slot-Anordnung nach der Freigabe des Projektinhabers umgesetzt.

Instant-Datei `<game>/instant/slot-0.ce4save` außerhalb der Speicherkarte:
Original-GameSave-/Inventar-/Raum-/Händler-/Untermenü-/Systemdaten, aktuelle
Position/Blickrichtung, CRCs, feste Byte-Reihenfolge und atomarer Dateitausch.
Der normale Continue-Puffer bleibt beim Speichern erhalten. JNI und separater
Kernprozess angebunden. **Patch 0111** startet den Original-Ladeweg auf der
Spielaufgabe nach dem Schließen des Menüs; nicht aus PADRead oder Java.
`src/` und `include/` unverändert. Neue Dateien `instant_game.cpp`,
`instant_store.cpp`, `re4_instant.h`; neue gezielte Erfassungs-/Dateitests.

Cloud: Übersetzungen, x86_64-Kernprüfungen inklusive Original-Erfassung und
Dateifehlerfällen bestanden; ARM32-/ARM64-Kernbuilds (je 114 Module) fehlerfrei;
komplette coreDebug-APK und Android-Lint erfolgreich. i686 unter QEMU:
Kernprüfung, Instant-Dateitests mit Prozess-Neustart und separater Kernprozess
(inkl. neuer Instant-Befehle) mit 0 Fehlern. Fester Dateipuffer vermeidet eine
im Prozesstest gefundene STL-Bindung an den Spiel-Heap. Vorhandener Lint-Fehler
im geschützten Spatializer-Helfer mit `@TargetApi(32)` gekennzeichnet.
**Offen:** Spiel-/Gerätetest auf dem Retroid mit Disc-Daten, besonders Raum-/
Waffenwechsel, Ereignisfortschritt, Standort, Touch/Controller und 4:3.

### Menü-Layout-Nachkorrektur (24.10.2026)

Auf `gui-optimisations`: gesamtes Ingame-Menü mit 85 % Deckkraft (15 %
Transparenz). Tiefenschärfe in der Optionsspalte unter „4:3 (GC)“; 60 FPS und
FPS-Anzeige ebenfalls eingerückt, mit „Bildausgabe“ als linker Beschriftung.
Gyro-Regler mittig überschrieben, Untermenütitel mit 16 dp mehr Abstand nach
„Zurück“. Deutsch: „Anzeige“ und „Steuerung“; übrige Menüsprachen weiter über
Android-Ressourcen nach Geräte-/App-Sprache. Alle zehn Sprachen geprüft.
Aktualisierte Screendesigns, PDF und Offline-Vorschau. APK-Build und
`lintCoreDebug` bestanden; keine Kern-/Spielstandänderung. Visuelle Prüfung
auf dem Retroid für diese Nachkorrektur noch offen.

### Frame-Skip-Machbarkeit (24.10.2026)

[Quellcodeanalyse und Umsetzungsvorschlag](../../docs/gui-optimisations/FRAME-SKIP.md).
Grundsätzlich möglich: jedes zweite Spielbild beim Zeichnen auslassen, bei
normalem 30-Hz-Spiel rund 15 sichtbare Bilder; Spiellogik, Eingaben und Ton
behalten ihren Takt. Hilft vor allem bei langsamer Grafikberechnung. Der
GX-Befehlsstrom enthält bildübergreifende Zustands-/Texturänderungen und
Bildpufferkopien: ungeprüft ganze Bilder zu verwerfen ist nicht korrekt. Ein
eigener Renderpfad mit erhaltenen Abhängigkeiten ist nötig; der Nutzen hängt
von der tatsächlich auslassbaren Zeichenarbeit ab. **Nur geprüft, nicht
implementiert; kein Gerätevergleich und keine gemessene Beschleunigung.**
App und Übersetzungen bleiben gegenüber der Layout-Nachkorrektur unverändert.

### Lesbarkeit, kompakter Slot und Umbruch (24.10.2026)

Gesamtes Menü jetzt mit 90 % Deckkraft (10 % Transparenz). Hauptseiten-Buttons:
19-sp-Titel fett, 15-sp-Unterzeile normal; übrige Buttons 16 sp und Infotexte
überwiegend 15 sp. Im niedrigeren Speicherslot stehen Titel, Kapitel/Raum/
Spielzeit und Speicherzeit links untereinander, Speichern/Laden rechts
untereinander. Bei wenig Platz rücken die Aktionen unter die Informationen.
`MenuLayout` passt Auswahlspalten an reale verfügbare Breite und Textlänge an;
Einstellungszeilen, Gyro, Slot und Kopf berücksichtigen auch die Systemschrift.
Größenänderungen des App-Fensters passen das offene Menü an. Der größere
Navigationshinweis scrollt mit. Alle bisherigen Sprachressourcen erhalten.
Aktualisierte breite Screendesigns, PDF und Vorschau. Keine Änderung an
Instant-Dateiformat.

**Ergänzungen:** Patch 0112 veröffentlicht den ruhenden Titelzustand und
übernimmt Instant-Laden über den originalen Titel-Ladeweg (GameLoad,
titleExit, GameTask, gameInit). Laden ist dort auch ohne Spieler und zuvor
geöffneten normalen Spielstand erlaubt; Speichern bleibt gesperrt. Logos,
Demo-Filme, Karten-Zugriff und laufende Übergänge erlauben keine neue Ladung.
Frame-Skip ist nur als deaktivierter, übersetzter Button eingefügt: Reihenfolge
FPS-Anzeige → Frame-Skip → 60 FPS experimentell. Keine Frame-Skip-Funktion.

Prüfungen: drei Android-View-Tests bestanden (750 Layouts über fünf Seiten,
zehn Sprachen, vier Breiten und drei Schriftgrößen, inklusive RTL/Größenwechsel;
zusätzlich Schriftgewicht und Startmenü-/Platzhalterzustand). 117 Texte und
ein Plural je Sprache geprüft. x86_64- und i686/QEMU-Kernprüfung einschließlich
Original-Erfassung und Titel-Freigaben sowie Instant-Dateitests bestanden.
ARM32 und ARM64, je 114 Module, fehlerfrei gebaut. SDK-Ressourcen, alle Java-
Quellen, DEX und signierte Debug-APK erfolgreich gebaut und verifiziert.
Android-Lint direkt mit dem coreDebug-Projektmodell: 0 Fehler.
Finaler Gradle-Lauf durch gesperrte lokale Build-Verbindung in der Cloud
blockiert; View-Tests direkt mit JUnit ausgeführt. Gerätetest auf dem Retroid
mit echten Disc-Daten für den vollständigen Titel-Ladeablauf noch offen.

### Instant-Slot: Aktionen in einer Reihe (24.10.2026)

Speichern und Laden stehen jetzt nebeneinander in der rechten Hälfte der
Speicherslot-Kachel. Beide zusammen sind so breit wie die Steuerung-Kachel
in der zweispaltigen Navigation darunter. Links bleiben alle Metadaten erhalten.
Die Kachel ist niedriger; „Bereit.“ entfällt samt Platzbedarf. Fortschritt,
Speicherbestätigung und Fehler blenden ihre Statuszeile bei Bedarf weiterhin
sichtbar ein. Schmale Fenster und große Systemschrift behalten den Umbruch.
Keine Änderung am Spielkern, Speicherformat oder an Sprachressourcen.
Screendesign, Vorschau und PDF angepasst. Die vorhandenen Android-View-Tests
prüfen auch die gemeinsame Breite, die Anordnung und die Rückkehr von
Fortschritts-/Fehlermeldungen nach verstecktem Bereitschaftsstatus.
Alle drei View-Tests mit 750 Layoutmessungen und die Prüfung der zehn Sprachen
bestanden. Vollständige SDK-Debug-APK gebaut, Ausrichtung/Signatur verifiziert;
Android-Lint: 0 Fehler. Der Spielkern bleibt auf dem bereits gebauten Stand.
Die neue Anordnung ist noch nicht auf dem Retroid visuell geprüft.

### Instant-Slot: Innenrand und volle Buttonhöhe (25.10.2026)

Der rechte Außenrand der Instant-Aktionen beträgt nun 12 dp und entspricht
links dem Innenrand der Überschrift „Speicherslot“. Beide Buttons bleiben
nebeneinander in der rechten Hälfte und füllen die Kachelhöhe mit jeweils
12 dp Abstand oben und unten aus. Die Höhe folgt auch mehrzeiligen Metadaten.
`MenuLayout.Columns` kann dafür gezielt die letzte Spalte auf Innenhöhe
strecken; andere Reihen behalten ihr bisheriges Verhalten. Bei schmalem
Fenster/großer Schrift bleibt die Stapelung mit natürlicher Höhe erhalten.
Vorhandene Layoutprüfungen auf Randgleichheit und volle Buttonhöhe erweitert;
Screendesign, Vorschau und PDF aktualisiert. Keine Kern-/Speicherformat- oder
Sprachänderung. Alle drei View-Tests (750 Layoutmessungen), die Prüfung der
zehn Sprachen und der vollständige Gradle-Lauf mit `assembleCoreDebug`,
`testCoreDebugUnitTest` und `lintCoreDebug` bestanden (1 min 14 s).
Sichtprüfung auf dem Retroid noch offen.

## Kurzstand

| Meilenstein | Stand |
|---|---|
| M1 Probelauf | ✅ 13.10.2026 (`whole-program-probe.md`) |
| M2 Alle Spieldateien kompilieren (ARM32 + 32-bit-Linux) | ✅ 13.10.2026 |
| M3 Linken, lauffähiges 32-bit-Linux-Programm, Module als eigene Bibliotheken | ✅ 13.10.2026 |
| M4 Teil A: Plattformschicht ohne Disc-Daten (Cloud) | ✅ 13.10.2026 |
| M4 Teil B: Booten mit echten Disc-Daten bis `gameRoomInit` (lokal, WSL2) | ✅ 13.10.2026 (i686 und armhf unter qemu) |
| M4.5 Kern korrekt machen: C-Ersatz für Assembler, Byte-Aggregate, `__FILE__`-Länge | ✅ 14.10.2026 (Cloud, i686 und armhf) |
| M5 Erster Raum r100 | ✅ 14.10.2026: lokal mit Disc-Daten (i686 in WSL2) 10 000 Bilder ab der Hauptschleife ohne Absturz, Abdeckung ohne „nein“, Formatvergleich 152 Dateien ohne Fehler. armhf mit Disc-Daten nicht gelaufen (`-Arm`, optional) |
| K2 „Kern in die App“ (M6) | ✅ 16.10.2026 (Retroid): ARM32-App mit den Kern-Bibliotheken, Titel, r100, r101, Untermenüs und Karte spielbar; Abschluss und offene Punkte: „Abschluss M6/M6b“ |
| M6b GX nachbilden (`gx-plan.md`) | ✅ 16.10.2026 (Retroid): das Bild kommt aus der GX-Nachbildung (Logos, Titel, Räume, Figuren, Effekte, Menüs, Anzeigen); r100 und r101 flüssig bei etwa 1080p (`-Scale 3`) |
| M7 Steuerung | ✅ 16.10.2026 (Retroid, Branch `claude/re4-m7-steuerung`): alle Tasten, Sticks und L2/R2 kommen als GameCube-Controller an (Original-`PADClamp`, Belegung nach Beschriftung), Menüs bedienbar, Vibration spürbar; Touch später („M7 Steuerung“) |
| M8 Audio | ✅ 16.10.2026 (Retroid, Branch `claude/re4-m8-audio`): Originalton über die SDK-Bibliotheken auf nachgebautem DSP/AI, Hall/Chorus in C, Tonausgabe der App; klingt laut Projektinhaber sauber. Vereinzelt abgebrochene Schüsse (AX-Budget) behoben, Nachtest im ersten Gerätelauf von M9 („M8 Audio“) |
| M9 Videos | ✅ 16.10.2026 (Retroid, Branch `claude/re4-m9-videos`): die Filme laufen über die CRI-Middleware (ADX, Sofdec, MPEG) aus `src/lib`, Bild über die GX-Nachbildung, Ton synchron, START bricht ab; Vorfilm vor der Autofahrt und Demo-Filme des Titels auf dem Gerät, alle 13 Filme der Discs in der PC-Prüfung ohne Fehler. Dabei behoben: Ruckler (Takt an den 60-Hz-Bildschirm gekoppelt), abgebrochene Töne (SYN, Patch 0056, 0 Abbrüche), Helligkeit. **Nicht auf dem Gerät geprüft:** `VISetBlack` (der Titel blitzte vor dem Demo-Film kurz auf) („M9 Videos“) |
| M10 Speichern/Laden | ✅ 17.10.2026 (Retroid und PC-Prüfung, Branch `claude/re4-m10-speichern`): die CARD-Bibliothek aus `src/lib` läuft auf einer nachgebildeten Speicherkarte (Datei im Speicher der App, nur für den Port); neue Karte vorformatiert, Startprüfung echt, aber ohne Meldungen (Systemdatei still angelegt); Speichern an der Schreibmaschine, Laden im Titel und im Pausemenü, „Retry“, Optionen über einen Neustart erhalten, Spielstände mit Banner und Symbol. Dabei behoben: Hänger beim Neustart eines Raums mit laufender Musik (Patch 0059) („M10 Speichern/Laden“) |
| M11 Android-Feinschliff | ✅ 17.10.2026 (Retroid und PC-Prüfung, Branch `claude/nifty-feynman-jv729h`, Stand `093aa7a`): Menü der App über SELECT (Grafikoptionen, FPS-Anzeige, „Depth of field“ mit dem Autofokus der ersten App, Speicherkarte als `.gci` für Dolphin/GameCube, Fehlerprotokoll für Tester, alles auf Englisch), SELECT halten = Debug-Menü des Spiels, Diagnose-Anzeige ausgebaut, Spiel steht im Menü und im Hintergrund. Rundgang über alle Räume (PC mit Disc-Daten): 126 von 126 laufen (Patches 0062–0064). Super Magnum: kein Port-Fehler (Nachlade-Stufe Lv.5 aus dem Debug-Menü), mit Lv.1 bestätigt. Debug-Menü Waffenseite stürzte am Listenende ab (Patch 0066). Leistung: 60 Bilder/s, Kern 33,3 ms je Spielbild mit 13–15 ms Leerlauf, GPU um 20 ms („M11 Android-Feinschliff“) |
| M12 ARM64 | **✅ abgeschlossen** (Entscheidung des Projektinhabers, 27.10.2026: eine APK mit beiden ABIs seit 0.9.0-beta, ARM64 auf dem Retroid gespielt). Verlauf: (Branch `claude/re4-m12-arm64`, seit 18.10.2026 `claude/exciting-bohr-6akwfd`): A1 ✅ 64-bit-Linux (x86_64) baut und linkt; A2 ✅ Adressen unter 4 GB; A3 ✅ Strukturen, die auf 64 bit auseinanderlaufen: alle Prüfwerkzeuge 0 (Patches 0070–0073, Regel `lp64_sizes`); A4 ✅ alle Cloud-Tests grün auf i686, armhf, x86_64 und **aarch64 (neu, ARM64 unter qemu)**, auch Filme und Speicherkarte; A5 Android arm64 eingebaut, aber ungeprüft (kein NDK in der Cloud). 18.10.2026: PC-Prüfung `-Lp64` läuft r100 10.000 Bilder ohne Absturz, Speichern stürzte ab (Budget des Speicherbildschirms, behoben, unbestätigt); Neustart mit Bestandsaufnahme aller Prüfwerkzeuge (libclang statt Wörter, Selbsttest, `run_all.py` + `reviewed.json`), vier neue Werkzeuge, Patch 0082 (fünf echte Fehler), Vergleichslauf 32/64 bit (`-Compare`, in der Cloud mit dem synthetischen Film gleich; mit Disc-Daten offen). Offen: `-Compare` und `-Lp64` mit Disc-Daten, Gerätetest beider ABIs. Dabei gefunden: Puzzle-Untermenü las auch auf 32 bit falsche Felder (Patch 0073). Bestandsaufnahme `arm64-bestandsaufnahme.md`. **Entscheidung des Projektinhabers (17.10.2026): Weg A, nativ 64 bit** |
| 32-bit-Kernprozess | ✅ 20.10.2026 (Branch `claude/re4-32bit-kernprozess`, Release **0.40.0-alpha**): **Weg A, nur als Rückfall** (Entscheidungen des Projektinhabers): ist MEM1 im App-Prozess belegt (ART-Java-Speicher auf 32-bit-Geräten mit Android 14), läuft der Kern in `libre4_coreproc.so`; Bibliotheken werden dafür entpackt (`useLegacyPackaging = true`). Cloud ✅ (i686, armhf: `re4_core_remote_test`), Retroid ✅ (erzwungen mit `-CoreProcess`: Spiel, Filme, Ton, Laden/Speichern, Debug-Menü, 30 FPS), **Tester-Gerät ✅** (nur 32 bit, Android 14: das Spiel läuft, Projektinhaber 20.10.2026; ohne Bericht). Offen: Leistungszahlen vom Tester-Gerät, Raumklang mit Spatializer („32 bit: der Kern als eigener Prozess“) |
| Hotfix 0.40.1-alpha | 20.10.2026 (Branch `claude/re4-luis-hotfix`, vom Stand `548b0f24` des Kernprozess-Branches): Patch 0087, Absturz auf 64 bit, wenn ein Ganado Luis in der Hütte packt (Abschnitt „Absturz, wenn ein Ganado Luis in der Hütte packt“); Touch-Tasten nach einem Tippen auch bei einem „Geister-Controller“ (21.10.2026, Abschnitt „Touch-Bedienung bleibt ohne Controller versteckt“). Cloud ✅, **Gerätetest offen** |
| Menü mehrsprachig, 0.41.0-alpha | 21.10.2026 (Branch `claude/re4-menu-sprachen`, enthält den Hotfix): App-Menü im Spiel nach der Handy-Sprache, Englisch als Rückfall, Deutsch als erste Sprache (weitere liefert der Projektinhaber); Touchsteuerung Auto/Erzwingen/Unterdrücken, Komfortsteuerung. Cloud ✅, **Gerätetest offen** |
| Hauptbranch `master` | 23.10.2026: steht auf **0.9.0-beta** (versionCode 73, Beta 0.9.0, Entscheidung des Projektinhabers: „Beta jetzt“); davor 0.46.0-alpha (`f30b19c`, versionCode 72; Entscheidung des Projektinhabers: alles nach `master`): `claude/re4-start-controls` mit allem seit 0.41.0 (Menü in 10 Sprachen, 0.42–0.46, Branch `performance`) vorgespult; vorher kamen nur die README-Änderungen des Projektinhabers (Reviews, Controls) direkt auf `master`. Bis dahin (21.10.2026) 0.41.0-alpha (`0394c644`). `performance` und `claude/re4-start-controls` stehen auf demselben Stand |
| Menü in 10 Sprachen | 21.10.2026 (Branch `claude/re4-start-controls`, vom `master`-Stand `0394c644`): Spanisch, Französisch, Italienisch, Portugiesisch (Brasilien), Russisch, Arabisch (von rechts nach links), Chinesisch (vereinfacht), Hindi zu Englisch und Deutsch; Sprache auch je App wählbar (Android 13+). Cloud ✅, **Gerätetest offen** |
| Absturz Wegesuche r11c (64 bit) | 21.10.2026 (Branch `claude/re4-start-controls`): Patch 0088 fängt ihn ab und meldet, wessen Position außerhalb lag; die eigentliche Ursache zeigt erst ein Bericht mit Fehlerprotokoll. Cloud ✅, **Gerätetest offen** |
| 0.41.1-alpha | 21.10.2026 (Branch `claude/re4-start-controls`, versionCode 63): Fehlerprotokoll auf jedem Handy (der Kern schreibt `core-log.txt` selbst, `logcat-status.txt`, „Error log: on/off“ im Bericht), Patch 0088, Menü in 10 Sprachen. Cloud ✅, Retroid ✅ (Deutsch, Protokoll), Samsung-Tester offen |
| 0.41.2-alpha | 21.10.2026 (Branch `claude/re4-start-controls`, versionCode 64): Patch 0089, die Gondel in r10f setzte Leon und Ashley auf Plätze aus gesicherten Registern (Ursache der Wegsuche-Abstürze). Cloud ✅, **Gerätetest offen** |
| Todesschleife r21d (Insekten bei NaN) | 22.10.2026 (Branch `claude/re4-start-controls`): `acosf`/`asinf` des Spielcodes nehmen einen Wert knapp über ±1 als ±1 (Umschreibung `ARC_CALL`); vorher setzte der Wandschritt der Novistadore (em2d, auch em2c) sie auf etwa 30 % der schrägen Flächen auf NaN, die Stampfer in r21d töteten Leon sofort, auch nach jedem Weitermachen. Dazu Todes-, Raumstart- und NaN-Zeilen im Fehlerprotokoll (Abschnitt „Todesschleife in r21d“). Cloud ✅ (i686, armhf, aarch64), **Gerätetest offen** |
| 0.41.3-alpha | 22.10.2026 (Branch `claude/re4-start-controls`, versionCode 65, Wunsch des Projektinhabers nach dem r21d-Bericht): Todesschleife r21d (`ARC_CALL`), Schießbude (Patch 0090), Gegner-, Menü-, Raum- und Todeszeilen im Fehlerprotokoll; Releasetext `port/android-decomp/docs/releases/v0.41.3-alpha.md`. Cloud ✅, **Gerätetest offen** |
| Fehlersuche Teil 1 (Code) | 22.10.2026 (Branch `claude/re4-start-controls`): Werkzeuge `port/tools/scan/` (ungesetzte Werte, Byte-Reihenfolge, Lage lokaler Variablen), Patches 0091–0093 (r221, Ziel-Überblendung der Waffen, r104, Krauser, Debug-Editoren auf R1). Abschnitt „Fehlersuche mit Werkzeugen“. Cloud ✅ (i686, armhf, aarch64), **Gerätetest offen**; Teil 2 siehe nächste Zeile |
| Fehlersuche Teil 2 (Raum-Rundgang) | 22.10.2026 (lokal, WSL2, Branch `claude/re4-start-controls`): Rundgang über alle 126 Räume mit Eingaben, mehreren Räumen gleichzeitig und Bericht (`room_tour_report.py`), Prüf-Build mit Sanitizern (`RE4_SANITIZE`, `--sanitize`), aarch64 unter qemu (`--aarch64`). i686 126/126, i686 mit ASan+UBSan 126/126 (nach den Korrekturen), aarch64 unter qemu 126/126 (auch mit UBSan), gleich beurteilt wie i686; armhf unter qemu fand einen SIGBUS in r222 (Patch 0102) und die Ausrichtungs-Suche die CRI-Tabellen (Patch 0103), danach armhf 28/28. Behoben: `__rlwimi`-Nachbau (5-Bit-Felder), Patch 0096 (Mischart `blend_mode` 5), Patch 0097 (Effekt-Bereich 255 auf 64 bit), 0102 (`cCtrl::work` ausgerichtet, ARM32-Absturz r222), 0103 (CRI-Tabellen für ARM32), leere EVS-/ITM-Tabellen. Abschnitt „Fehlersuche mit Werkzeugen, Teil 2“. **Gerätetest offen** |
| 0.42.0-alpha | 22.10.2026 (Branch `claude/re4-start-controls`, versionCode 66, vorbereitet auf Wunsch des Projektinhabers, Veröffentlichung durch ihn): Patches 0091–0093 (Fehlersuche Teil 1), 0094 (Aufrüsten beim Händler), 0095 (Biss des Regenerators am Boden, Fehler des Prototyps), keine GPU-Zeitmessung auf Mali (Absturz im Treiber, moto g72); Releasetext `port/android-decomp/docs/releases/v0.42.0-alpha.md`. Cloud ✅, **Gerätetest offen** |
| 0.42.1-alpha | 22.10.2026 (Branch `claude/re4-start-controls`, versionCode 67, Wunsch des Projektinhabers: der A30s-Tester soll mit der neuen Messung noch einmal testen): Rechenzeit und Kern-Art von Kern- und Zeichen-Thread im Protokoll (`re4_cpu_info.h`, „Bericht Galaxy A30s“); dazu aus dem Raum-Rundgang (Fehlersuche Teil 2, gemischt in `889b40fb`) die Patches 0096 und 0097, `__rlwimi`/`__rlwinm` mit 5-Bit-Feldern und die leeren EVS/ITM-Tabellen; nach dem SCV40-Bericht 16:21 Patch 0098 (Bulldozer) und keine Formen mehr in der Zustandsbrücke (Absturz beim Laden); Releasetext `port/android-decomp/docs/releases/v0.42.1-alpha.md`. Cloud ✅ (gemischter Stand `4b97350d`: i686, armhf, aarch64 0 Baufehler, alle Tests 0 Fehler), **Gerätetest offen** |
| 0.43.0-alpha | 22.10.2026 (Branch `claude/re4-start-controls`, versionCode 68, Wunsch des Projektinhabers: alle Korrekturen aus diesem und dem zweiten Chat, Veröffentlichung durch ihn): Patches 0099 (Laser-Gang r318 auf 64 bit), 0100 (Gegenstände ohne Platz im Koffer, „E“-Marke in der Mitte), 0101 (Absturz am Aufzug r332), 0102 (`cCtrl::work` ausgerichtet: ARM32-Absturz in r222), 0103 (CRI-Tabellen ausgerichtet: Filmstart auf ARM32); App-Menü: Bildformat „Gestreckt“, mehr Zeilenabstand, „B einrasten (Touch)“, Gyro-Empfindlichkeit; GPU-Selbstprüfung der Filmfarben beim Start, die Zeilen der App auch im Protokoll des Kerns; Releasetext `port/android-decomp/docs/releases/v0.43.0-alpha.md`. Kern geprüft (lokal, WSL2, Stand mit 0100–0103): Builds i686, armhf, aarch64 mit allen Prüfungen 0 Fehler, Rundgang i686 126/126, armhf r222 und `set` 28/28. Cloud ✅ (Stand `844bdd25`: i686, armhf, aarch64 0 Baufehler, alle Tests 0 Fehler; `gx_gl_check` 0 Fehler, GPU-Prüfung ok). Releasetext nachgeschärft (Waffen aus dem Debug-Menü, Reihenfolge des Bildformats). APK nicht gebaut, **Gerätetest offen** |
| 0.43.1-alpha | **nicht veröffentlicht, in 0.44.0 aufgegangen** (Entscheidung des Projektinhabers). 22.10.2026 (Branch `claude/re4-start-controls`, versionCode 69, Wunsch des Projektinhabers): Patch 0104 (Absturz im Ton am Aufzug r332 → r331 nach Saddlers Ereignis), Shader der Brücke auf Mali (Uniform `refract` hieß wie die eingebaute GLSL-Funktion, Anbernic RG 477V), Gegner-Zeile beim Erscheinen mit Listeneintrag, `set` und `flag` (Dynamit-Frage); neues Werkzeug `tools/check_shaders_strict.sh` (alle Shader der Host-Prüfungen durch glslangValidator); Releasetext `port/android-decomp/docs/releases/v0.43.1-alpha.md`. Cloud ✅ (i686, armhf, aarch64 0 Baufehler, alle Tests 0 Fehler; strenge Shader-Prüfung 165 Shader, 0 abgelehnt). APK nicht gebaut, **Gerätetest offen** |
| Nach 0.43.1 (für 0.44.0) | 22.10.2026 (Branch `claude/re4-start-controls`, nach dem Stand `942a5c3`; **0.43.1 wird nicht veröffentlicht**, Entscheidung des Projektinhabers: alles zusammen mit 60 FPS als 0.44.0): Bildformat **„4:3 (ohne Balken)“** (Patch 0105, Anbernic RG 477V mit 4:3-Bildschirm), **Touch im Koffer** (Patch 0106, `runtime/menu_touch.cpp`: Reiter, Koffer-Cursor, Befehlsmenü, Ja/Nein-Frage per Fingertipp), Analyse 60 Bilder/s mit Zwischenbildern, Dynamit-Zeilen im Fehlerprotokoll (geworfen/explodiert aus dem zweiten Chat; Werfer am Posten, Zündschnur). Cloud ✅ (siehe die Abschnitte), **Gerätetest offen** |
| 0.44.0-alpha | 22.10.2026 (Branch `claude/re4-start-controls`, versionCode 69, Releasetext `port/android-decomp/docs/releases/v0.44.0-alpha.md`; **0.43.1 entfällt**, Entscheidung des Projektinhabers; Freigabe durch den Projektinhaber nach seinem Gerätetest 20:00): alles aus 0.43.1 (Patch 0104, Mali-Shader, Gegner- und Dynamit-Zeilen) plus **60 FPS (experimentell)** (Zwischenbilder im GX-Renderer, FPS-Anzeige mit Bildern auf dem Bildschirm), **„4:3 (ohne Balken)“** (Patch 0105 mit Schere, volle Bildhöhe), **Touch im Inventar** (Patch 0106: Koffer-Cursor, Befehlsmenü, Frage, Schlüssel/Schätze, Akten per Tipp, Reiter per Wischen ohne Ring mit richtiger Hervorhebung auch aus dem Koffer und von der Karte; Reiter per Tipp nach dem Gerätetest 20:09 ausgebaut, Karte per Finger verschieben nach dem Gerätetest 21:22), Bildformate kurz „4:3 (GC)“, „4:3“, „16:9“, „Gestreckt“, **Messer per Wischen** (`runtime/knife_swipe.cpp`), **Filmfarben auf Adreno 710** (Mischung ohne `a << 8`, Auswahl durch die GPU-Selbstprüfung). Cloud ✅ (i686, armhf, aarch64 0 Baufehler, alle Tests 0 Fehler; `gx_gl_check` 0 Fehler, strenge Shader-Prüfung 1119/0). Retroid ✅ (22.10.2026 19:40, 20:00 und 20:09): Koffer-Tipps, Messer per Wischen, 60 FPS (Stufe 1–2 ohne MSAA), das Spiel zeichnet ohne seine Balken (`picture: the game draws without its letterbox (r105)`). **Offen:** „4:3 (ohne Balken)“ auf einem echten 4:3-Gerät (Tester), Filmfarben auf dem Galaxy A36 |
| 0.45.0-alpha | 23.10.2026 (Branch `claude/re4-start-controls` mit dem Branch `performance` zusammengeführt, versionCode 70, Releasetext `port/android-decomp/docs/releases/v0.45.0-alpha.md`; Entscheidung des Projektinhabers: „Mach jetzt gleich 0.45.0 draus“, mit `performance`): Jetski-Abspann r333 (PTH-Daten), Tonanfragen-Bank 256 (Patch 0107, stumme Todesschreie), Touch im Inventar (Schlüssel, Akten, Karte, Reiter per Wischen, kein „Exit“-Aufblinken, Karte nicht mehr per Finger verschoben), schnellerer GX-Decoder, Grafik-Programme vor dem ersten Zeichnen, Eckpunkte auf bis zu 3 weiteren Kernen, XF-Uniforms in drei Teilen, Fehlerprotokoll mit Zeitaufteilung (Patch 0108) und Stay-Zeilen der Ganados. **Auf dem Gerät noch nicht getestet** (siehe Abschnitt unten) |
| 0.45.1-alpha | **nicht veröffentlicht, in 0.46.0 aufgegangen** (Entscheidung des Projektinhabers, 23.10.2026). 23.10.2026 (Branch `claude/re4-start-controls` mit `performance` zusammengeführt, versionCode 71, Releasetext `port/android-decomp/docs/releases/v0.45.1-alpha.md`; Entscheidung des Projektinhabers: die weiteren Performance-Optimierungen als 0.45.1): „Auto“ regelt die Auflösung nach dem Grafikchip (`GxRenderer::Rescale`), Spiel- und Zeichen-Thread weg von den langsamsten Kernen und Androids Leistungshinweis, Shader auf eigenem Thread mit Liste in der App (70 Programme, wächst mit den Schlüsseln der Berichte); -O3 geprüft und verworfen. **Auf dem Gerät noch nicht getestet** |
| 0.46.0-alpha | 23.10.2026 (Branch `claude/re4-start-controls`, versionCode 72, Releasetext `port/android-decomp/docs/releases/v0.46.0-alpha.md`; Entscheidung des Projektinhabers nach seinem Gerätetest 16:09: „Die Umschaltung klappt sauber“): „Dynamisch“ statt „Auto“ (erst nach zwei schlechten Fenstern tiefer), Auflösung in der FPS-Anzeige, Einrasten L/R und B in der Komfortsteuerung auch für den Controller, Menüzeilen ohne Grundlinien-Ausrichtung, Gyro mit der Harpune, Shader-Liste 627 Programme |
| 0.9.0-beta | 23.10.2026 (Branch `claude/re4-start-controls`, auch `performance` und `master`, versionCode 73, Releasetext `port/android-decomp/docs/releases/v0.9.0-beta.md`; Entscheidung des Projektinhabers: „Beta jetzt“, nachdem der SCV40-Tester das Spiel durchgespielt hat): erste Beta; seit 0.46.0 nur die Shader-Liste (1928 statt 627 Programme), Version, Releasetext und README (Status „Beta“). Alle 1928 Programme bauen auf Mesa ohne Fehler (mit und ohne MSAA). **Gerätetest der APK offen** |
| M13 Retail-Discs (EU) | 26.10.2026 (lokal, Branch `retail-eu`, Plan `retail-eu-plan.md`): R0 ✅ (Prüfstrecke und Vergleichswerkzeug für G4BP08, Versionserkennung im Kern), R1 ✅ (Patches 0117–0119: r100-Prüfung mit unveränderten EU-Discs ohne Absturz, Debug-Discs „ALLES OK“), R2 ✅ (Patch 0120 Funkspruch, Umsortierung Krauser/Adas Gewehr im Umwandler, Kartenprüfung mit EU-Discs „ALLES OK“), R4 (App-Import, vorgezogen) gebaut und am PC geprüft, Gerätetest offen. R7 (Sprachen, vorgezogen; Patches 0121–0123, „Game language“ im Ingame-Menü) gebaut und am PC mit allen fünf Sprachen geprüft, Gerätetest offen. Offen: R3, R5, R6, R8. Als **M13** in `CLAUDE.md` aufgenommen **✅ abgeschlossen 27.10.2026** (zwei Kapitel EU-Discs auf Deutsch auf dem Retroid, Cheat-Menü; Reste in 10b, Abschnitt „M13 abgeschlossen“) |
| Reihenfolge | Entscheidungen des Projektinhabers: erst M9 Videos und M10 Speichern/Laden (16.10.2026), dann **M11 vor dem Raum-Rundgang 10b** (17.10.2026: die APK soll an Tester gehen), dann **M12 ARM64 vor 10b** (17.10.2026: ein Tester hat ein Gerät nur für 64-bit-Apps). Gestrichen: Baum-Kollision in r100 (G17) |

**Entscheidung des Projektinhabers (14.10.2026):** Endprodukt ist
RE4-Android-Decomp (`port/android-decomp/`, Subtree von `re4-standalone`). Der
Kernmodus läuft vorerst als armeabi-v7a (ARM32); der bestehende ARM64-Modus der
App bleibt parallel. Die App bleibt Hülle (Disc-Import, GLES3-Grafik,
Controller, Tonausgabe, Menü); die Spiellogik kommt im Kernmodus aus dem
Originalcode, die eigenen Spielmodule der App werden dort nicht mehr
erweitert.

## Bauen und prüfen

```sh
# Werkzeuge (clang, lld, cmake, ninja, lib32-Bibliotheken, ARM32-Cross-Compiler,
# qemu-user): sudo port/tools/whole_setup_linux.sh. In der Cloud ruft das
# Startskript .claude/hooks/session-start.sh es automatisch auf; lokal in WSL2
# (Ubuntu 24.04) einmal von Hand. g++-multilib geht nicht: Ubuntu verweigert es
# neben dem ARM-Cross-Compiler.
port/tools/whole_build.sh          # baut und prüft i686 und armhf
RE4_JOBS=4 port/tools/whole_build.sh   # höchstens 4 Compiler gleichzeitig (Speicher)
RE4_OPT=2 port/tools/whole_build.sh    # mit -O2 wie der Android-Build (Ordner whole-<ziel>-O2)
```

Je Ziel: Build, dann drei Prüfungen von `re4_host` und der Formatvergleich
(armhf unter `qemu-arm`):

| Prüfung | Inhalt |
|---|---|
| `--self-test` | Weiterleitungen zwischen Modulen geben Argumente korrekt weiter |
| `--check-modules` | alle 114 Module laden, alle Querverweise lösen sich auf |
| `--core-test` | Einbettungs-Schnittstelle der App ohne Disc-Daten (`port/whole/tests/core_tests.cpp`, `core_probe.cpp`): `bridge` Schnappschuss aus einem nachgebauten Gegner (EmMgr, Teile, Modell-Info, Kamera, Raum), `thread` Kern-Thread mit Testeinstieg, Controller über `re4_core_pad`, Dreifachpuffer; seit M10 `card` die CARD-Bibliothek auf einer Kartendatei (`tests/card_probe.cpp`, siehe „M10 Speichern/Laden“) |
| `memcard_list.py` (M10) | liest die Karte des Kern-Tests zurück (`RE4_CARD_TEST_FILE`): Dateien, Banner/Symbol, die Prüfsummen des Spiels (`--expect-system --expect-save 0`) |
| `--runtime-test` | Plattformschicht ohne Disc-Daten (`port/whole/tests/runtime_tests.cpp`): `link` OSLink/OSUnlink mit synthetischem big-endian REL-Kopf und Testmodul; `dvd` DVD-API über einen synthetischen Zwei-Disc-Ordner; `yz2` Yz2 des Spiels + Kern mit den Testdaten `port/tests/yz2_fixtures.h`; `dvdq` die DVD-Warteschlange des Spiels liest eine synthetische mehrteilige Datei in den Spiel-Heap; `stream` der Stream-Code des Tontreibers spielt synthetische Stream-Dateien (Nachladen im Audio-Rahmen, Aufträge aus dem Interrupt enden erst danach); `math` PS*/C_*-Matrixfunktionen, die C-Fassungen der Gekko-Assembler-Funktionen (Tabelle mit Bitmustern aus einem Python-Modell der Paired-Single-Befehle), Skinning `CalcSk1` mit GQR-Quantisierung, `EmRoutineSetW`, die big-endian Sichten, die Länge von `__FILE__`; `trace` Zustands-Trace, GetDataExt-Abdeckung, Ende des Test-Tastenskripts; `tasks` der Task-Scheduler des Spiels (`TaskExec`, `TaskSleep`, `TaskExit`, `iTaskExec`) auf der Thread-Emulation, dazu VI-Retraces; `vsync` (seit M9) die Retraces folgen dem Vsync eines 60-Hz-Bildschirms |
| `--movie-test` (M9) | `port/tools/test_whole_movie.py`: ein synthetischer Sofdec-Film (ffmpeg: MPEG-1 mit B-Bildern, Stereo-ADX mit zwei Tönen; `port/tools/make_test_sfd.py`) durch die CRI-Middleware wie `cSofdec` (`tests/movie_probe.cpp`): Bilder gegen ffmpeg, Reihenfolge, Takt, Ton je Kanal, Ton/Bild-Abstand, Bild 10 durch den GX-Renderer der App (wenn `port/build/app-host-gl` gebaut ist), derselbe Film im Schnelllauf (`RE4_VI_FAST`) und mit 60,0-Hz-Halbbildern (Bildschirmtakt) |
| `re4_format_compare` | Formatumwandler gegen die geprüften big-endian Leser der App, mit synthetischen Beispielen jedes verglichenen Formats; danach `port/tools/test_whole_format_compare.py`: dieselben Beispiele als Disc-Ordner (Raumdatei Yz2-gepackt, `.drs`-Container), Modus `--data` (siehe „Formatvergleich mit der App“) |

Danach einmal `port/tools/test_whole_disc_folder.py` (Disc-Ordner-Werkzeug mit
synthetischen Disc-Abbildern). Rückgabewert 0 heißt: alles bestanden.

Nach Bedarf (seit M9): `port/tools/whole_ir_signatures.py port/build/whole-i686`
übersetzt jede Datei des Builds nach LLVM-IR und vergleicht jede Deklaration
einer C-Funktion mit ihrer Definition (Zahl der Parameter, Ergebnis ja/nein);
so fallen Aufrufe auf, die auf dem GameCube von Werten lebten, die zufällig in
einem Register standen (etwa 40 s).

`re4_host` ist seit M4 Teil B ein kleiner Vorlader (`host/loader.c`): Er hält
den Adressbereich unter `0x83000000` frei und lädt dann `libre4_host.so`
(Startprogramm und Laufzeittests, `host/main.cpp`) mit den Spielbibliotheken,
die deshalb oberhalb von `0x80000000` liegen wie auf Android (siehe
„Adressen der Spielbibliotheken“).

## M4 Teil B: Booten mit Disc-Daten (lokal, WSL2)

```sh
# einmal: Disc-Ordner außerhalb des Repositorys (Abbilder werden nur gelesen)
port/tools/whole_disc_folder.py --disc ".../RE4_2004-11-25_Disc1.gcm" \
    --disc ".../RE4_2004-11-25_Disc2.gcm" --out ~/re4-data
port/tools/whole_disc_folder.py --check ~/re4-data
# Spielstart mit Test-Tastenfolge (Speicherkarten-Dialog, Titel, Menüs)
RE4_PAD_SCRIPT="UP@100-3000/60 A@115-3000/60 START@130-3000/60" \
    port/build/whole-i686/re4_host --data ~/re4-data
# armhf: QEMU_LD_PREFIX=/usr/arm-linux-gnueabihf qemu-arm port/build/whole-armhf/re4_host ...
```

Disc-Ordner (13.10.2026): Disc 1 1154 Dateien, Disc 2 1090 Dateien, 0 Probleme.

**Nachweis (13.10.2026), i686 und armhf (qemu-arm) gleich:** Der Originalcode
läuft mit echten Disc-Daten durch `systemStartInit` (Ton-Tabellen, Schrift,
Größentabelle), `systemRestartInit` (`debug/roomInfo.dat`), `Title_task`
(Kernarchiv `etc/core.das` mit Tonbank, `SS/eng/option.dat`, Speicherkarten-
Prüfung „keine Karte“ → „ohne Speichern fortfahren“, `SS/eng/title.dat`,
Titel und Titelmenü), das Debug-Startmenü (`titleExit`, Raum r120 = Stufe 1,
Raum 0x20), `GameTask` → `gameInit` → `gameStageInit` (`rel/st1_0.rel` per
OSLink, `etc/emleon00.esl`) bis in **`gameRoomInit`**: `ReadPlayerData` lädt
`em/pl00.drs` mit Tonbank, `ReadAreaData` lädt `st1/r120.das`, der Yz2-Kern
entpackt 6 086 688 Byte, der Formatumwandler wandelt das Raumarchiv um. Danach
stürzt `ModInfoMgr.arrayAlloc` ab, weil die Raum-Zähler (`CNS`) noch
big-endian sind: Das ist der Einstieg in M5. Letzte Logzeilen:

```
[re4] OSReport: DVD: Read File: em/pl00.drs
[re4] formats: em/pl00.drs part 0.0: archive
[re4] OSReport: DVD: Read File: st1/r120.das
[re4] formats: st1/r120.das part 0.0: room archive (Yz2)
[re4] formats: st1/r120.das (unpacked): archive, 6086688 bytes
[re4] fatal signal 11 ... cModInfoMgr::memClear <- cManager<cModelInfo>::arrayAlloc <- gameRoomInit
```

`port/tools/whole_build.sh` am Ende der Sitzung (13.10.2026): i686 und armhf
je 0 fehlgeschlagene Schritte, 0 Fehler; `--self-test`, `--check-modules`
(114 Module, 0 offene Querverweise, 209 Platzhalter, alle Module oberhalb von
`0x80000000`) und `--runtime-test` (alle sechs Teile) bestanden, Disc-Ordner-
Werkzeug ok.

Die Tastenfolge drückt alle 60 Bilder OBEN, 15 Bilder später A, 15 Bilder
später START. OBEN+A wählt im Speicherkarten-Dialog „ohne Speichern
fortfahren“ (A allein wählt „erneut prüfen“). Die Bildnummer, ab der der
Dialog bereit ist, schwankt zwischen Läufen; die Wiederholung fängt das ab.

**M4-Teil-A-Nachweis (13.10.2026), frischer Build, 1:26 min:**

```
i686: 0 failed build steps, 0 distinct errors
  link plan: 234 function stubs, 1 data stubs, 893 cross-module calls, 1 cross-module data imports (private copies)
  re4_host --self-test: exit 0 | self-test: trampolines ok (code 0)
  re4_host --check-modules: exit 0 | check-modules: 114 modules, 0 failed to load, 0 unresolved imports, 234 stubs
  re4_host --runtime-test: exit 0 | runtime-test link: ok dvd: ok yz2: ok dvdq: ok math: ok tasks: ok, 0 failed
armhf: (gleiche Zeilen, ausgeführt unter qemu-arm)
disc folder tool: ok
```

Alle Bibliotheken linken mit `-Wl,--no-undefined`: Kein Symbol bleibt offen.

**Spielstart (`re4_host` ohne Disc-Daten), i686 und armhf gleich:** Der
Originalcode läuft als Boot-Thread der Thread-Emulation durch fast ganz
`systemStartInit`: OSInit (MEM1, Arena), `SystemMemInit` (Heap 0
`0x809740a0–0x817f4000`, `pRK`), DVD-Init, CARD, `TaskSchedulerInit`,
Bildschirm, GX-Init (Platzhalter), Lichter, Sound-Init (ARAM, liest
`bgm/*.hed`, `etc/memcard.das`), Sofdec, Id-System, Log, Debug-Modul, Größentabelle.
Jede Datei meldet „File not found“, weil die Cloud keine Disc-Daten hat. Der
letzte Schritt `cMes.init` → `loadSystemFont` → `setupFont` stürzt ab, weil
`Font/common_p.fnt` fehlt. Hier ist ohne Disc-Daten Schluss.

Mit einem synthetischen Disc-Abbild (Werkzeug-Test) liest die originale
DVD-Warteschlange `bgm/bio4str.hed` an die feste GameCube-Adresse `0x80370000`:
Die Kette Disc-Abbild → Ordner → `DVD*` → Spiel funktioniert.

**Nicht getestet:**
- Der GameCube-Matching-Build (`ninja` im Hauptverzeichnis). Original-Compiler
  fehlen in WSL2. `src/` und `include/` sind unverändert.
- Der Android-NDK-Build und das Reservieren von MEM1 in einer echten
  Port-App. Gemessen ist nur, dass der Bereich in einem 32-bit-App-Prozess frei
  ist (siehe „Adressbereich auf dem Retroid“).
- Spielinhalte jenseits von `gameRoomInit` (M5) und alles, was erst ein Bild
  zeigt (K2): Umgewandelte Texturköpfe, Effekte und Layouts sind nur so weit
  geprüft, wie der Spielcode sie beim Booten liest.
- Stand 14.10.2026 ausdrücklich ungetestet: der Lauf in r100 mit echten
  Disc-Daten (10 000 Bilder), das Abdeckungs-Log mit echten Daten, der
  Formatvergleich mit echten Disc-Dateien, `test-r100-core.ps1` unter echtem
  Windows/WSL (nur PowerShell 7 unter Linux mit nachgebautem `wsl.exe`), die
  ARM32-Ausführung auf echter Hardware (nur qemu-arm).

## M4.5: Kern korrekt machen (14.10.2026, Cloud)

**Assembler mit echten Befehlen** (Inline-`asm` entfällt sonst ersatzlos,
Prelude): jetzt C-Fassungen als Patches, und `whole_prepare.py` bricht ab,
sobald ein `asm` mit echten Befehlen ohne Ersatz entfallen würde
(`port/tools/whole_asm_check.py`, läuft bei jedem `cmake`). Bestandsaufnahme
nach den Patches: 195 Barrieren (`asm("")`, ohne Wirkung), 17 mit Befehlen (alle
ersetzt), 146 Sprungmarken/Kommentare, 155 Abschnitts-/Layout-Direktiven.
Leere `asm`-Kopien mit gebundenen Operanden (`asm("" : "=r"(x) : "0"(y))`)
sind echte Wertzuweisungen und werden zu `x = y` umgeschrieben.

| Funktion | Ersatz | Abweichung von der GameCube |
|---|---|---|
| `fabsf` (math_sub.h) | `re4_fabsf` (re4_ppc.h): Vorzeichenbit löschen | keine |
| `SQRTF` | `sqrtf` | Original: `frsqrte`-Schätzung + ein Newton-Schritt; Ergebnis kann in den letzten Bits abweichen (wie Bewegungs-Hilfswerkzeug und App) |
| `SINF`/`COSF` | Reihe der Paired-Single-Fassung in C (`PsSeries`), `ps_madd` als `fmaf` | bitgleich zum Python-Modell der Gekko-Befehle (`math`-Test, 24 Zeilen) |
| `LIMIT_ANGLE` | Schleife in C, jeder Schritt auf float gerundet | NaN/unendlich: Original hängt, der Port gibt den Wert zurück und loggt einmal |
| `PSQ_L/ST`-Makros (shape, trans, Espgen42, espgen45, dbmodule, pendulum) | `re4_psq_*` (re4_ppc.h): GQR-Skalierung, Sättigung, Abschneiden zur Null | keine (u8/s8/u16/s16, feste GQR2–5) |
| `CalcSk1_x`/`CalcSk1_x2` (trans.cpp, Skinning) | `Sk1Loop` in C mit GQR6 aus `setupGQR6` (Shift je Modell, Normalen 2^-14, s8 2^-6) | Locked Cache `0xE0000000` → Puffer `re4_locked_cache` (16 KB) |
| `dbg_tool.h` `li %0,0` | `z = 0` | keine |

**Byte-Aggregate** (`*(u32*) &bytes`): `port/tools/whole_byte_views.py`
findet mit `clang-query` alle Casts von der Adresse eines Byte-Aggregats auf
einen breiteren Zeiger (Schattenbaum ohne `#line`, sonst stimmen die Orte
nicht). 85 Stellen geprüft, Liste mit Entscheidung je Stelle:
`port/whole/byte_views.json`. 74 werden zu big-endian Sichten (`RE4_BE(T, E,
p)`, Prelude: Lesen/Schreiben/Verbundzuweisung in GameCube-Byte-Reihenfolge),
5 lesen einen Teil eines größeren Feldes (`RE4_BE_AT`), 4 bleiben (gleiche
Bedeutung auf beiden Plattformen), 2 per Patch: `EmRoutineSetW` (em.h, Patch
0023, setzt `r_no_0..3` byteweise) und das Spiegel-Bit in `pl_sub.cpp` (Patch
0024). `whole_prepare.py` setzt die Sichten anhand der Liste ein und bricht ab,
wenn eine Stelle nicht mehr gefunden wird.

**`__FILE__`:** `-fmacro-prefix-map` kürzt Pfade auf `src/game/x.cpp`
(`DvdReadN` kopiert `__FILE__` in 48 Byte). Geprüft mit dem Build in einem
langen Verzeichnispfad (`math`-Test prüft die Länge).

**i686:** `-msse2 -mfpmath=sse`, damit float-Ergebnisse wie auf ARM bitgleich
sind (x87 rechnet mit höherer Genauigkeit). Linux-Testbuilds seit 14.10.2026
mit `_FILE_OFFSET_BITS=64` (readdir/stat scheitern sonst an 64-bit-Inodes, z. B.
Windows-Laufwerke in WSL und qemu-arm).

## M5: Erster Raum r100

### Formatumwandler, Abdeckung, Trace (14.10.2026, Cloud)

Neue Umwandler (Tabelle unter „Formatumwandler“): Raumtabellen, Modelle,
Motionen, Lichter, Kameras, Ereignisse, Kollision, Gegnerlisten. Dazu:

- **Abdeckungs-Log** (Patch 0025): Jeder `GetDataExt`-Zugriff meldet Kürzel,
  Archiv und „umgewandelt ja/nein“ (`nein (kein Umwandler)`, `nein
  (unvollständig)`, `nein (unbekannter Eintrag)`), je Eintrag einmal;
  `RE4_COVERAGE_LOG=DATEI` schreibt zusätzlich tabgetrennt Archiv, Kürzel,
  Nummer, Adresse, Status.
- **Zustands-Trace** (`runtime/state_trace.cpp`, `RE4_TRACE=DATEI`): eine
  JSON-Zeile je Bild: `frame`, `Rno0` (Spielschritt), `room`, `pl`
  (Spielerposition), `rsf` (Raum-Speicherwort), `em` (lebende Gegner: Slot,
  Id, `r_no_0..3`, HP, Position).
- **Debug-Start** (Patch 0026, `RE4_START_ROOM=0x100`): wählt den Raum im
  originalen Debug-Startmenü vor; das Titelmenü steht dann auf „Neues Spiel“,
  so dass ein Test-Tastenskript nur START/A drücken muss.
- **Laufsteuerung:** `RE4_PAD_UNTIL_ROOM=1` beendet das Tastenskript, sobald
  der erste Raum lädt (Spielschritt 2); `RE4_STOP_AFTER=N` beendet den Lauf
  nach N Bildern ab dem ersten Bild der Hauptschleife (Schritt 3) mit
  Rückgabewert 0 und der Logzeile `run: stopped after …`.

### Formatvergleich mit der App (`re4_format_compare`)

Beide Seiten lesen dieselben big-endian Bytes:
- App-Seite (`tests/format_compare/app_side.cpp`, C++17 wie die App): die
  geprüften Leser `archive.cpp`, `smd.cpp` (mit `gx_stream.cpp`), `smx.cpp`,
  `sat.cpp`, `room_flow.cpp` (AEV und Raumsprungtabelle), `enemy_spawn.cpp`,
  `event_data.cpp` (EVD und Kurven), `model_skin.cpp`, `room_lighting.cpp` (LIT
  und Kamerabereiche aus CAM).
- Kern-Seite (`game_side.cpp`, Schalter und Header des Spielcodes): eine Kopie
  wird mit dem Formatumwandler gedreht und über die Originalstrukturen
  gelesen.
Jedes Feld wird als Schlüssel/Wert verglichen (Floats als Bitmuster, lange
Felder als Anzahl + Hash + die ersten 48 Einträge einzeln). Teile, die der
App-Leser ablehnt, erscheinen als „nicht vergleichbar“ mit seiner Meldung.

Synthetische Beispiele (Cloud, i686 und armhf): Raumarchiv mit SMD (mit BIN
und TPL), SMX (Typen 1, 2, 0), SAT, EAT (zwei Dateien), AEV (Tür, Leiter,
Schreibmaschine), LIT (Spot mit Schatten, Typ 4), CAM (B404), zwei BIN
(cWeightExt mit 256 Gewichten / cWeight, s8/s16-Normalen, farbig/nicht),
FCV, ein leerer FCV-Eintrag mit dem Offset des folgenden BIN; Gegnerliste;
EVD (Kamera-, Positions-, Blenden-, Dummykamera-, Effekt- und Endpaket,
Kurven- und LIT-Anhang); Raumsprungtabelle: 5129 Werte gleich.
Gegenprobe: sechs absichtlich eingebaute Umwandlerfehler (SMX `TexV`, SAT
`m_Status`, ESL `rot`, AEV `pExitParam`, CAM `Height`, LIT `Intensity`)
wurden alle als Abweichung gemeldet.

**Gefundener Fehler:** Die Teile-Datensätze der Modelle (`ModelDataHead`)
blieben big-endian; ihr Wort `x0` ist durch Patch 0016 eine Byte-Sicht für
Host-Wörter (`partsNo`, `parentNo`). Jetzt wird der ganze Datensatz
wortweise gedreht (`formats_model.cpp`).

Mit Disc-Daten: `re4_format_compare --data ORDNER [--room r100]` vergleicht
`st1/r100.das` (über den Yz2-Entpacker der App), `etc/*.esl`,
`debug/roominfo.dat`, die Ereignisse `*/r100*.evd`, `etc/core.das` und
`em/*.drs`. Bei Abweichungen nennt es die betroffenen Archiv-Einträge mit
Nummer, Offset, Größe und womit der Umwandler sie behandelt hat
(`Eintrag BIN0. #8 BIN @0xea0, 0xd20 bytes, umgewandelt als BIN`).
Erster Lauf mit echten Daten: siehe „Zweiter lokaler Lauf“.

**Ohne Vergleich mit einem App-Leser** (nur Strukturgrößen per
`static_assert` und die Läufe): CNS, ETS, EMI, SAR, EAR, FSE, ESE, DSE, STB,
BLK, RTP, ITA, SHD, ETM, ITM, EVS, SEQ, TPL, EFF/TEX/VIB, MDT, UWF, Ton.

### Erster lokaler Lauf (14.10.2026, i686 in WSL2)

Der Kern lief vom Titelbild über das Debug-Startmenü bis in **r100**: Raum-
archiv entpackt und umgewandelt, Abdeckung für RTP, MDT, OSD, EMI, CNS, SMX,
LIT, SAT, EAT, AEV, ITA „ja“, `em/wep02.drs` (Pistole) geladen und per OSLink
gebunden („Wep02 HANDGUN prolog Ok“). Danach blieb er in `gameRoomInit`
(Spielschritt 2) stehen; die Bilder liefen weiter (über 2,3 Millionen), es
kamen keine weiteren GetDataExt-Zugriffe (SHD, EFF, CAM … fehlen im Log): Die
Wartestelle liegt zwischen `EmMgr.create(0, 0)` (Spieler) und `ShdInit`.

- **Behoben:** Beide SMD des Raums (`#4`, `#5`) waren nur halb umgewandelt:
  ihr Motion-Tabellen-Offset zeigt genau auf das Blockende (leere Tabelle),
  `tableCount` wertete das als Fehler und brach vor den BIN/TPL-Modellen ab.
  Jetzt gilt ein Offset am Blockende als leere Tabelle; das synthetische SMD
  des Formatvergleichs hat dieselbe Form (Gegenprobe: mit dem alten Fehler
  313 Abweichungen und „Umwandlung unvollständig“).
- **Wahrscheinliche Ursache des Halts:** `SmdSetup` → `SmdSetParam` →
  `cModInfoMgr::create` prüft die BIN-Version; mit den nicht umgewandelten
  Modellen schlägt das fehl, und das Original wartet dann für immer in
  `notBinData()` (`eprintf("not bin data")` + `TaskSleep(1)`, nur auf dem
  Bildschirm, nicht im Log). Das passt zur Wartestelle. Bestätigt erst der
  nächste lokale Lauf.
- **Diagnose für den nächsten Lauf:** `RE4_WAIT_TRACE=1` merkt sich bei jedem
  Threadwechsel die Rückverfolgung (`os_thread.cpp`), `SIGUSR1` gibt danach
  jede Spielaufgabe mit ihrem Warteort aus (`re4_os_dump_threads`); der Wächter
  des Prüfskripts löst das aus, wenn die Hauptschleife nach 10 Minuten nicht
  erreicht ist.
- Archiv-Kürzel `DRA` in r100 hat keinen Umwandler (GetDataExt griff im Lauf
  bis zum Halt nicht darauf zu).

### Zweiter lokaler Lauf (14.10.2026, i686 in WSL2)

Mit der SMD-Korrektur kam der Kern über `gameRoomInit` hinaus bis
`ScenarioRoomInit` → `R100Init` → `EmSetEvent` und stürzte in `Em12Set` ab
(Gegnermodul em12, beim Setzen der ersten Gegner von r100).

- **Ursache des Absturzes:** `em.cpp` trägt `cEm::subArc` (Archiv des
  Gegners) ein und ruft dann die Init-Funktion des Moduls, die das Objekt mit
  `new (em) cEm10()` aufbaut. C++11 füllt bei `T()` eine Klasse ohne eigenen
  Konstruktor erst mit Nullen; der GameCube-Compiler hat bei solchen Klassen
  nur den Konstruktor aufgerufen. Damit war `subArc` wieder 0. **Behoben**
  mit der Umschreibung `PLACEMENT_VALUE_INIT` (Tabelle unten, 142 Stellen:
  Gegner, Objekte, Waffen, Lichter, Modelle). `port/tools/whole_value_init.py`
  fragt clang nach allen Stellen, die danach noch mit Nullen füllen: nur noch
  vier einfache Typen, die auch das Original nullt (`GXColor()` zweimal, die
  Flags `flg()`/`status()` von `cSubChar`).
- **Formatvergleich mit echten Daten:** 152 Dateien, 4 019 092 Werte gleich,
  75 Dateien mit Fehlern. Ursachen:
  - *Leere Archiv-Einträge* (fast alle `em/*.drs`, `pl*.drs`, `wep*.drs`):
    Ein Eintrag ohne eigene Daten hat denselben Offset wie der nächste
    Eintrag. Der Umwandler drehte die Daten mit dem Kürzel des ersten
    (leeren) Eintrags, z. B. ein BIN als FCV; die App-Seite las den leeren
    Eintrag als Daten des nächsten. Jetzt gehören die Daten auf beiden Seiten
    dem letzten Eintrag mit diesem Offset (`formats_arc.cpp`), leere
    Einträge werden übersprungen. Gegenprobe mit der alten Regel: genau das
    Bild aus dem Bericht („BIN0 … Kern (fehlt)“, „umgewandelt als FCV“).
    Die FCV-Meldungen der Waffen (`wep10/21/24/31/32/47`, `pl14`) haben
    vermutlich dieselbe Ursache; **bestätigt erst der nächste Lauf**.
  - *Kollision mit mehr als 512 Blöcken* (SAT/EAT von r100): Grenze nur im
    Vergleichswerkzeug, jetzt dynamisch.
  - *Kernarchiv `etc/core.das`*: CAM ohne Versionskennung (32 Byte) und zwei
    SAT mit unbekannter Version; das Spiel liest sie nicht über die
    Umwandlerpfade. Bleiben jetzt mit Logzeile unverändert statt als
    „unvollständig“ zu gelten.
  - `etc/emleon08/09.esl`: kürzer als 255 Datensätze, der App-Leser lehnt sie
    ab (nicht vergleichbar, kein Fehler).
- Der Zustands-Trace schreibt jetzt nach jedem Bild auf die Platte (vorher
  alle 64 Bilder; im Bericht stand deshalb Bild 575 statt der Absturzstelle).
- Weiterhin ohne Umwandler: Archiv-Kürzel `FNT` (`ss/eng/ss_cmmn.dat`), ein
  leeres Kürzel in `ss/eng/ss_pzzl.dat`, `DRA` in r100 (vielleicht ein leerer
  Eintrag; nächster Bericht zeigt es). Abdeckung im Lauf: 27 Zugriffe, kein
  „nein“.

### Dritter lokaler Lauf: M5 erreicht (14.10.2026, i686 in WSL2)

Stand 1c4f551. Bericht „ALLES OK“:
- **Spiellauf:** Hauptschleife in Bild 621 erreicht (Raum r100), danach
  10 000 Bilder in Spielschritt 3 ohne Absturz (25 s mit `RE4_VI_FAST`).
  Letzte Trace-Zeile: Spieler an (-99682, -484, -1340), 18 lebende Gegner
  (Ids 0x41/0x43/0x46, HP 1000). Tasten gab es nach dem Laden keine, der
  Spieler steht also; ob die Gegner sich wie im Original verhalten, ist nicht
  geprüft (erst mit Bild, M6).
- **Abdeckung:** 31 GetDataExt-Zugriffe, kein „nein“.
- **Formatvergleich:** 152 Dateien, 4 226 888 Werte gleich, 0 Fehler; die
  leeren Einträge waren die Ursache aller Abweichungen des zweiten Laufs.
- **Danach behoben (Cloud):** `op/opNN.das` (Funkgespräche, `ss_term.cpp`
  `opArc`) enthält das Archiv erst 0x400 Byte nach dem Anfang des Teils;
  eigener Umwandler `convertCodecArchive`, im Formatvergleich als
  „Funkarchiv“ (synthetisch mit Gegenprobe; mit echten Daten noch nicht
  gelaufen).
- **Offen, für r100 ohne Folgen:** FCV-Einträge, die auch der App-Leser
  ablehnt und bei denen der Umwandler abbricht (`wep09/10/21/24/31/32/47`
  #16/#28, `pl14` #8; `EVD-FCV: Key budget/Channel offset/Key order`);
  Archiv-Kürzel ohne Umwandler: `FNT` (ss_cmmn.dat), `PTH` (em36), `DMY`
  (em2f), `DRA` (r100). Werden wichtig, sobald diese Waffen, Gegner oder
  Menüs laufen.

### Lokale Prüfung mit Disc-Daten (`port/scripts/test-r100-core.ps1`)

Windows-PowerShell-Skript, startet in WSL2 `port/tools/whole_r100_check.sh`:
Werkzeuge einrichten (`whole_setup_linux.sh`), Quelltext nach `~/re4-kern`
klonen (Stand des Windows-Checkouts), `whole_build.sh i686` (mit `-Arm` auch
armhf), Formatvergleich mit den Disc-Dateien, dann das Spiel vom Titelbild:
`RE4_VI_FAST=1 RE4_START_ROOM=0x100 RE4_PAD_UNTIL_ROOM=1
RE4_PAD_SCRIPT="UP@100-60000/60 A@115-60000/60 START@130-60000/60"
RE4_STOP_AFTER=10000` mit Trace und Abdeckungs-Log. Die Tastenfolge hat das
Timing des Boot-Tests vom 13.10.2026: OBEN+A wählt im Speicherkarten-Dialog
„ohne Speichern fortfahren“, START überspringt Logos, A wählt „Neues Spiel“,
A/START verlässt das Debug-Startmenü. Dort verschiebt OBEN die Zeile (21
Zeilen, Zeile 1 lädt, 2 erzwingt r120, 0x12 schwer + Neustart); vor dem
nächsten A/START kommt höchstens ein OBEN an (Zeile 20 = normaler Start).
Wächter (seit 14.10.2026, nachdem der erste lokale Lauf ohne Anzeige hing):
alle 30 s Fortschritt (Bild, Spielschritt, Raum aus dem Trace); Abbruch mit
Rückverfolgung (`kill -USR1`, Ausgabe von `host/main.cpp`), wenn 180 s kein
neues Bild kommt, die Hauptschleife nach 600 s nicht erreicht ist oder nach
3600 s (Umgebungsvariablen `RE4_CHECK_STALL`, `RE4_CHECK_MAIN_LIMIT`,
`RE4_CHECK_LIMIT`; armhf 600/5400/14400 s). Bericht
`Desktop\RE4-Kern-Pruefung\bericht.txt`: Ergebnis (OK/Absturz/Zeitlimit),
erreichte Hauptschleife, Abdeckung mit allen „nein“-Zeilen, Umwandler-
Meldungen, aufgerufene Platzhalter, letzte Trace-Zeile. Disc-Daten als
Disc-Abbilder (.iso/.gcm, werden einmal nach `~/re4-kern/disc` entpackt),
Dolphin-Extraktion, Port-Ordner oder App-Import-Ordner. In der Cloud nur mit
einem künstlichen Disc-Ordner und einem nachgebauten `wsl.exe` unter
PowerShell 7 durchgespielt.

## K2 „Kern in die App“ (M6)

### Cloud-Teil (14.10.2026)

**Ablauf in der App (Variante „core“):**

1. `JNI_OnLoad` (`app/src/main/cpp/core_mode.cpp`) lädt `libre4_main.so` mit
   `dlopen(RTLD_NOW | RTLD_LOCAL)` (die Bibliotheken liegen unkomprimiert in
   der APK, `libre4_platform.so` und `libc++_shared.so` lädt der Linker mit)
   und ruft `re4_mem1_init`: MEM1 `0x80000000–0x83000000` ist reserviert,
   bevor die App GL oder Java-Threads startet. Fehlt eine Bibliothek, zeigt
   die App „core libraries missing (port/tools/whole_build_android.sh)“.
2. `CoreActivity` ruft `re4_core_reserve(<Datenordner>, "")`: Datenordner
   der App (`…/files/game` mit `disc1/`, `disc2/`), Module per bloßem Namen
   `libre4_<modul>.so` aus der APK; prüft mit `dladdr` die Lage von
   `libre4_main` (wie `re4_check_game_address`, ohne Abbruch, Meldung im
   Overlay). Danach `re4_core_start`: eigener Thread (8 MB Stack), darin
   `re4_os_run(re4_game_main)` als Boot-Thread der Thread-Emulation. Der
   Bildtakt kommt wie unter Linux aus den VI-Retraces der Plattformschicht;
   der GL-Thread wartet nie auf den Kern.
3. Pro Spielbild (in `PADRead`, Bildhaken `re4_add_frame_hook`) schreibt der
   Kern einen Schnappschuss (`re4_bridge_capture`, `runtime/core_bridge.cpp`)
   in einen Dreifachpuffer; der GL-Thread holt den neuesten mit
   `re4_core_acquire`/`re4_core_release`.
4. Der Controller geht über `re4_core_pad` an PAD-Kanal 0
   (`runtime/pad.cpp`, ersetzt das Test-Tastenskript, sobald es endet).
   Belegung (Meilenstein 7 verfeinert sie): A→A, B→B, X→X, Y→Y, L1→L, R1→Z,
   L2→L, R2→R, Start→START, Steuerkreuz/Hat→Steuerkreuz, linker/rechter
   Stick → Haupt-/C-Stick (±100), analoge Schultertasten 0–255 (ab 85 %
   zusätzlich digital L/R). SELECT blendet das Diagnose-Overlay ein/aus,
   Zurück fragt „End the game?“ und beendet den Prozess (der Kern lässt sich
   im selben Prozess nicht neu starten).

**Schnittstelle** `port/whole/runtime/re4_core.h` (Version 8, reines C, gleiche
Struktur-Größe auf 32 und 64 bit): `re4_core_reserve`, `re4_core_start`,
`re4_core_state` (idle/running/ended/failed), `re4_core_message`,
`re4_core_pad`, `re4_core_acquire`/`re4_core_release`, `re4_core_log_tail`
(letzte 24 Log-Zeilen für das Overlay), für Tests `re4_core_start_entry`.
Umgesetzt in `runtime/core_api.cpp` (in `libre4_main`).

**Schnappschuss** (`Re4CoreSnapshot`, 1 278 032 Byte auf 32 und 64 bit): Spielschritt
`Rno0..3`, Raum, Disc, Kamera aus `pG->Camera` (Augpunkt, Ziel, Rollwinkel,
Öffnungswinkel; bei offenem Untermenü die Weltkamera `CamCtrl.camera`, denn
`pG->Camera` ist dann die Kamera des Menüs), `screen` (Untermenü offen,
Ereignis, Debug-Kamera), bis 1024 Modelle aus `EmMgr` (Gegner) und `ObjMgr` (Objekte):
lebend (`be_flag & 0x201 == 1`), Zeichen-Bits `2|4`, `HIDDEN`, wenn das Spiel
das Modell in diesem Bild trotzdem nicht zeichnet (`trans.cpp`: Anzeige-
Schalter `Disp_flg` für Spieler, Partner, Gegner, Objekte, Kulisse; während
eines Ereignisses oder einer Nahaufnahme, `STA_SUSPEND`, nur Modelle mit
`be_flag & 0x800`; ganz ausgeblendete, `invisible_factor` 0), die
Zeichen-Einstellungen (Ausblenden `invisible_factor`, Tiefenmodus `z_mode`,
Flächenseite `CullMode`, Ordnungsklasse `ot_type`; je Info Mischmodus
`blend_mode`, Materialfarbe, eigener Ausblend-Anteil), Weltmatrix, je Teil die
Weltmatrix `cParts::mat` und die Bindung (−`lt_inv_mat`-Verschiebung), bis
8192 Teile. Für jede Modell-Info (`pModelInfo`; alle Infos aller Modelle in
einer gemeinsamen Liste, bis 2048, Leon allein hat neun) die Herkunft der BIN- und
TPL-Daten auf der Disc (Datei, Archiv-Offset, Eintrag) und wie das Spiel
sie zeichnet (`trans.cpp commonModelTrans`): nur mit `be_flag & 8`; ein
starres Modell (ein Teil, keine Gewichte) hängt an `pHead->partsNo` mit
seinen Punkten im Raum dieses Teils (z. B. Waffen in der Hand); dazu die eigene
Matrix der Modell-Info. Die Formatumwandler
merken sich beim Laden jedes Archiv und jeden Eintrag
(`re4_formats_locate`; bis 1024 Archive und 65 536 Einträge, vorher 64
Archive: r100 lädt mehr, ältere Herkunftsangaben gingen verloren). Liegt ein
Modell mitten in einem Eintrag (BIN in einem SMD, Modell in einer
Raumblock-Datei), meldet die Brücke den umgebenden Eintrag und `innerOffset`.
Angaben zu Archiven, über die später anderes geladen wurde, verfallen: lädt
das Spiel ein neues Archiv über einen Teil eines älteren, wird nur dieser
Teil gesperrt (über den Kopf oder denselben Bereich: das ganze ältere);
Daten ohne eigene Notiz erkennt eine Prüfsumme der Eintragsnamen; stimmt
sie nicht, ruhen die Angaben nur, bis die Daten wieder da sind (ein
Untermenü lagert Teile von MEM1 vorübergehend nach ARAM aus,
`SubScreenAramRead`). Die App merkt sich je Modell die letzte bekannte
Herkunft und zeichnet es so auch während solcher Pausen. Tauscht das Spiel
Speicher zwischen MEM1 und ARAM (`MemorySwap`, Patch 0028: Ereignisse
tauschen ihre Datei aus ARAM in den Speicher eines Gegners, Untermenüs Teile
des Heaps), wandern die Herkunftsangaben beider Seiten mit. Kopiert das Spiel
Daten aus ARAM in den Hauptspeicher (Ereignisse laden ihre Dateien nach ARAM
und holen sie beim Abspielen), folgt die Herkunft der Kopie
(`re4_formats_aram_copied`, `aram.cpp`). Ereignis-Dateien
(`Evd/*.evd`) werden als Ganzes gemerkt: Modelle darin haben `entry = -2`,
`innerOffset` = Position in der Datei. Jede Überlappung und jede Pause steht
im Log („formats: archive … overlaps …“, „… suspended“, im Bericht unter den
Umwandler-Meldungen). Yz2-gepackte Archive (Raumdateien) haben keinen
Datei-Offset (`0xFFFFFFFF`); solche Modelle (Gegenstände, Tiere des Raums)
liest die App aus der entpackten Raumdatei, die sie für den Raum ohnehin
vorbereitet. Die Raumkulisse (`cObjScr`, ObjMgr-Kennung 2: die SMD-Modelle
des Raums und seiner Blockdateien) zeichnet die App mit dem Raum; die Brücke
zählt sie nur („scenery“). Bewegt das Spiel ein Kulissenteil, sieht man das
deshalb noch nicht.

**Zeichnen** (`app/src/main/cpp/core_scene.cpp`, `CoreScene`): bis zum ersten
Raum (Schritt < 2) das Titelbild (größtes Bild der Titeldaten der App), danach
der Raum über den Raumlader der App (`RoomRepository` entpackt `r%03x.das` und
die Blockdateien, `core.das` für Himmel/Wasser) und der vorhandene
GLES3-Renderer (`DrawSceneGl`) mit der Kamera des Kerns (4:3 wie das Original,
auf den Bildschirm gestreckt wie bisher). Die Modelle des Kerns zeichnet ein
neuer Haken `SceneProbe::extraActors` mit dem `GenericEventActor` der App: BIN
und TPL liest die App selbst von der Disc (ihre geprüften big-endian Leser),
die Pose kommt aus den Teile-Matrizen des Kerns. Display-Listen und Texel aus
dem Spielspeicher werden dafür nicht gebraucht. Statusanzeige im Overlay:
„frame, step, room, disc, models (drawn, missing), room loaded“ und die
letzten Log-Zeilen.

**App-Variante** (`app/build.gradle`, `flavorDimensions 'engine'`):
`classic` = bisherige App (arm64-v8a, `com.ast.re4decomp.classic`, bis
17.10.2026 `com.ast.re4decomp`, Standard in Android Studio), `core` =
Kernmodus (armeabi-v7a und seit M12 arm64-v8a, `com.ast.re4decomp`, bis
17.10.2026 `com.ast.re4decomp.core`, Name „Resident Evil 4 Android“, bis
17.10.2026 „RE4 Kern“, ab Android 6.0, CMake `-DRE4_CORE_MODE=ON`). Im Kernmodus startet „Start
game“ die `CoreActivity` statt `StartupActivity`; Raumbrowser und
Diagnosen (eigene Spielmodule `r100_scenario`, `enemy_encounter`,
`event_playback` …) sind ausgeblendet und werden nie aufgerufen (sie bleiben
im Code für `classic`). Der Disc-Import übernimmt im Kernmodus alle Dateien
(auch andere Sprachen) und schreibt zusätzlich `sys/boot.bin` (0x440 Byte)
und `sys/fst.bin` (Layout wie `runtime/dvd.cpp`); eine Disc gilt im
Kernmodus nur mit `sys/fst.bin` als importiert. `runtime/dvd.cpp` nimmt
`disc1/`, `disc2/` (App) wenn `disc0/` fehlt.

**Android-Build des Kerns** `port/tools/whole_build_android.sh` (WSL/Linux,
NDK r28c = 28.2.13676358 wie die App, `sudo port/tools/whole_setup_linux.sh
--android` lädt es nach `/opt/android-ndk-r28c`): armeabi-v7a, API 21,
`c++_shared`, `-O2`, 16-KB-Seiten. Prüft vor dem Kopieren: alle Bibliotheken
gebaut, 114 Module, dieselben Platzhalter wie der Linux-Build (Patch 0027),
die `re4_core_*`-Exporte. Kopiert nach `app/src/core/jniLibs/armeabi-v7a/`
(nicht im Repository), Debug-Abschnitte entfernt.

**Testschalter für die App:** Das Intent-Extra `coreSettings`
(`"RE4_X=wert;…"`) setzt Umgebungsvariablen des Kerns vor dem Start
(`RE4_START_ROOM`, `RE4_PAD_SCRIPT`, `RE4_PAD_UNTIL_ROOM` …;
`state_trace.cpp` liest seine Schalter deshalb beim Kernstart neu).

**Prüfungen in der Cloud (14.10.2026):**

| Prüfung | Ergebnis |
|---|---|
| `port/tools/whole_build.sh` (i686, armhf unter qemu) inkl. `--core-test` | grün |
| `RE4_OPT=2 port/tools/whole_build.sh` (wie Android) | grün, 209 Platzhalter wie `-O0` |
| `core_scene_gl_check` (App-Renderer, EGL ohne Bildschirm, Mesa llvmpipe; synthetische Disc aus `tests/format_compare/samples.h`) | 31 Prüfungen: Pfade ohne Groß/Klein, Yz2-Quelle abgelehnt, Titel ohne Daten schwarz, Raum gebaut, Modell gezeichnet/versteckt, Teile-Matrizen bewegen das Modell |
| `re4decomp_link_check` (die native App-Bibliothek der Variante core auf dem Host mit `--no-undefined`) | linkt |
| Java der App (alle Klassen) gegen Android-API 34 mit `javac` | übersetzt; JNI-Namen (8) passen zu `core_mode.cpp` |
| App-Hosttests 32 bit (i686, `-msse2 -mfpmath=sse`) | wie 64 bit: nur die 2 schon vorher fehlschlagenden Tests |

**Nicht in der Cloud geprüft** (kein Android-NDK/SDK erreichbar,
`dl.google.com` gesperrt): NDK-Build des Kerns, Gradle-/APK-Build,
Gerätelauf, Bilder mit echten Disc-Daten.

### Erster lokaler Lauf (14.10.2026, WSL2 + Windows)

- `test-r100-core.ps1`: ALLES OK (Formatvergleich 155 Dateien ohne Fehler,
  10 000 Bilder ohne Absturz, Abdeckung ohne „nein“, 5 Bilder gezeichnet).
  Befunde zur Brücke: Kamera die ganze Zeit `(0, 0, 2000) → (0, 0, 0)`,
  Winkel 50 (nicht die Spielkamera); 128 Figuren = Obergrenze erreicht;
  112 von 126 sichtbaren Figuren ohne Disc-Herkunft (die Herkunftsliste hielt
  nur 64 Archive). App-Bilder: „drawn 16, missing 120“.
- `test-core-app.ps1`: NDK r28c geladen, Kern für armeabi-v7a gebaut
  (0 Fehler, 114 Module, 209 Platzhalter wie Linux, 117 Bibliotheken,
  21 MB). APK-Bau scheiterte: `JAVA_HOME` zeigte auf Java 11, Gradle braucht
  17–23.
- Behoben danach (Cloud): Das Skript sucht ein passendes JDK (JAVA_HOME,
  Android Studio `jbr`, JDKs unter `C:\Program Files`, `-JavaHome`) und setzt
  es nur für den Lauf; Ausgabe ohne „RemoteException“-Zeilen und ohne
  Treppeneffekt. Herkunftsliste 1024 Archive (Test `core-test bridge` mit
  101 Archiven), Obergrenze 256 Figuren (API-Version 2). Der Zustands-Trace
  schreibt jetzt pro Bild die Kamera mit ihren Schaltern (`cam`: Auge, Ziel,
  Winkel, `sw` = C STA_CAMERA, D Debug-Kamera, S SPF_CAMERA, E Ereignis,
  U Untermenü, X Sonderkamera, `r0` Zustand von CamCtrl, `ctrl` Auge von
  `CamCtrl.camera`); der Bericht zeigt den Kamera-Verlauf. Unbekannte
  Modell-Herkünfte stehen im Brücken-Protokoll mit ihrem Ort
  (`re4_formats_describe`: in welchem Archiv, oder in keinem).
- Ursache der Kamera noch offen: `CameraMove` übernimmt `CamCtrl.camera` nur
  mit STA_CAMERA und ohne Debug-Kamera (DBG_DBG_CAM, schaltet sich bei
  Tasten an Controller 2 ein); der nächste Lauf zeigt, welcher Fall vorliegt.

### Zweiter lokaler Lauf (14.10.2026, WSL2)

- Kamera-Verlauf: bis Bild 621 Titel/Laden, ab 622 Ereignis mit
  Ereigniskamera (Winkel 29), ab 1800 Kamera hinter Leon (`CamCtrl`, Zustand
  17), ab Bild 1871 ein Untermenü (`U`, vermutlich der Funkspruch, der auf
  eine Taste wartet; ohne Eingabe bleibt er offen): `pG->Camera` ist dann die
  Menükamera `(0, 0, 2000) → (0, 0, 0)`. Behoben: Die Brücke nimmt dann die
  Weltkamera und meldet das Menü (App-Overlay „MENU OPEN“).
- Brücke: 226 Figuren (Obergrenze 256 reicht), 15 mit Herkunft; die übrigen
  lagen mitten in Einträgen (Raumarchiv, Blockdateien) oder in Speicher, über
  den ältere Archive geladen worden waren (title.dat, save_e.dat: veraltete
  Angaben). Behoben wie oben (Version 3).
- Formatvergleich: 8 Dateien mit „FEHLER“ (FCV-Animationen in wep09, 10, 21,
  24, 31, 32, 47 und pl14). Kein neuer Fehler: Die größere Notiztabelle macht
  sichtbar, dass diese Einträge schon immer unvollständig umgewandelt wurden
  (vorher als „nicht vergleichbar“ durchgerutscht; die App-Leser lehnen sie
  auch ab). Für r100 ohne Bedeutung (Waffen, die dort nicht vorkommen). Der
  Umwandler meldet jetzt Gelenk, Kennung, Schlüsselformat und Grenze des
  Eintrags; die Ursache folgt aus dem nächsten Lauf.

### Dritter lokaler Lauf (14.10.2026, WSL2)

- Kamera bei offenem Untermenü jetzt die Weltkamera; 188 Kulissenteile
  erkannt; im ersten Bild zeichnet die App 43 Figuren (vorher 17).
- Nach dem Öffnen des Untermenüs fehlte die Herkunft der Modelle aus dem
  entpackten Raumarchiv (Gegner 0x41/0x43/0x46, Gegenstände 0x19) und von
  em12.drs („replaced archive“): Ein später geladenes Archiv überlappte sie,
  und die Regel verwarf das ganze ältere Archiv. Jetzt wird nur der
  überdeckte Teil gesperrt (Test `core-test bridge`), und das Log nennt jede
  Überlappung. Der Bericht wertet die Brücke jetzt zweimal aus (früh im Raum
  und am Ende).
- FCV: Die Diagnose-Zeile fehlte im Bericht (Filter). Der Umwandler prüft
  jetzt jedes Gelenk vor dem Umwandeln: Gelenke, deren Daten außerhalb des
  Datenbereichs liegen, bleiben unverändert; ein Fehler nur, wenn
  `MotionMoveCore` diese Gelenkart liest (Kamerafahrten lesen nach Nummer,
  daher werden alle passenden Gelenke weiter umgewandelt). Test mit einem
  unbenutzten Gelenk, das in den Kopf zeigt.

### Vierter lokaler Lauf mit Bildern (14.10.2026, WSL2)

- Erste Bilder der Brücke mit Disc-Daten: Raum r100 mit Brücke, Bäumen,
  Himmel und Nebel wie im Spiel; Leon mit Funkgerät am Ohr (Bilder 2500 bis
  10 000: der Funkspruch wartet auf eine Taste, das Funk-Menü selbst ist 2D
  und fehlt); Bild 651: Anfangsszene, zwei Polizisten auf der Brücke von
  hinten. Fehler: Leons Haare/Oberkopf fehlen, den Polizisten fehlen die
  Köpfe (nur die Mützen).
- Ursachen aus dem Bericht: Die Köpfe kommen aus den Ereignis-Dateien
  (`Evd/r100s00.evd`), die der Umwandler nicht als Herkunft merkte (jetzt
  `entry -2`). Nach dem Öffnen des Funk-Menüs meldeten r100.das und em12.drs
  „entry names changed“: Das Spiel lagert diesen Speicher vorübergehend aus;
  die Angaben wurden verworfen statt nur pausiert (jetzt Pause plus
  Gedächtnis der App). Leons Haare: Ursache offen; der Bericht zeigt jetzt im
  Abschnitt 4, welche Modelle die App nicht laden konnte und warum.
- FCV: Die 8 Fehler sind Kamera-Gelenke (Kanal 6) in Waffen-Animationen,
  deren Daten hinter ihrem Eintrag liegen (wep09/10/21/24/31/32/47), und ein
  32-Byte-FCV in pl14.drs, das mitten in seiner einzigen Kurve endet. Beides
  liegt an den Original-Daten, nicht am Umwandler: Kamera-Gelenke liest das
  Spiel nur mit angehängter Kamera, und was in den Eintrag passt, wird
  umgewandelt. Die Meldung nennt jetzt den Grund, gezählt als Fehler wird nur
  ein gelesenes Gelenk mit Daten im Eintrag.

### Fünfter lokaler Lauf (14.10.2026, WSL2)

- Während des Funkspruchs zeichnet die App jetzt 42 Figuren ohne Lücke
  (vorher 16 und 26 fehlende); alle 27 Modelle lassen sich laden. Formatvergleich:
  nur noch pl14 (FCV mit 32 Byte, keine Achse passt) – jetzt als Originaldaten
  eingeordnet.
- Leons Haare: kein Ladefehler, sondern die Regel aus `commonModelTrans`
  fehlte: Die Haare sind ein starres Teil am Kopf-Knochen, die App hängte sie
  an Knochen 0 (Hüfte). Jetzt mit Sichtbarkeit, Knochen und Info-Matrix
  (Version 4; Test in `core_scene_gl_check`, 52 Prüfungen).
- Köpfe der Polizisten: Ereignis-Modelle lagen „in em12.drs“ – die Ereignis-
  Datei kommt aus ARAM und überschreibt dabei vorübergehend em12. Jetzt folgt
  die Herkunft der ARAM-Kopie (Test `core-test bridge`).

### Sechster lokaler Lauf (14.10.2026, WSL2)

- PC-Prüfung **ALLES OK** (Formatvergleich 0 Fehler). Bilder unverändert.
- Leons vier Modellteile sind alle gewichtet (keins starr), sichtbar und
  geladen: Die Haare fehlen aus einem anderen Grund. Die Bild-Auswertung
  schreibt jetzt für jedes gezeichnete Teil die Lage seiner Punkte im Raum
  und die Zahl der Materialien mit Alpha-Test in den Bericht.
- Köpfe der Polizisten: Die Ereignis-Datei kommt nicht per Kopie, sondern
  per `MemorySwap` (Tausch MEM1/ARAM über Zwischenpuffer) in den Speicher von
  em12. Patch 0028 meldet den Tausch, die Herkunftsangaben wandern mit
  (Test `core-test bridge`).

### Siebter lokaler Lauf und erster Gerätelauf (14.10.2026)

- PC-Prüfung **ALLES OK**. Bild 651 (Anfangsszene im Polizeiauto): die
  Polizisten jetzt mit Köpfen (Patch 0028 wirkt).
- Leons Haare: Die Brücke gab je Figur nur die ersten **4** Modell-Infos
  weiter. Leon hat neun (`cPlLeon::setModel`: Körper, Kostüm, Messer,
  Gesicht, Kopf, Haare, dazu Gesicht/Hände aus `setFace`/`setRightHand`/
  `setLeftHand`); Kopf, Haare und Hände fielen weg. Jetzt liegen alle Infos
  aller Modelle in einer gemeinsamen Liste (`Re4CoreSnapshot::infos`, bis
  1024, Zähler `droppedInfos`; Version 5). Tests: `core-test bridge` mit einer
  Figur aus neun Infos, `core_scene_gl_check` (53 Prüfungen: alle Infos von
  zwei Modellen gezeichnet).
- Gerät (Retroid, App „RE4 Kern“): APK gebaut (Java 21 aus Android Studio),
  installiert, Kern geladen, MEM1 reserviert. Das Spiel startete nicht, weil
  Disc 1 in der neuen App noch nicht importiert war; der Import, den der
  Projektinhaber während des Laufs begann, wurde abgebrochen, weil das
  Skript die App nach 120 s beendet. `test-core-app.ps1` wartet jetzt auf
  den fertigen Import (Fortschritt in MB, höchstens `-ImportMinutes`,
  Standard 45) und beendet die App währenddessen nicht.

### Erster Gerätelauf mit Spiel (14.10.2026, Retroid)

- Gerät: Import von Disc 1 mit dem neuen Warten im Skript geklappt. Der Kern
  läuft auf dem Retroid: Titelbild (r120), Debug-Start r100, Anfangsszene im
  Polizeiauto mit Polizisten und Leon (Köpfe, Haare), 66 Figuren gezeichnet.
  PC-Prüfung **ALLES OK**, Leon mit Haaren (alle neun Modell-Infos).
- **Absturz** nach dem Funkspruch (Projektinhaber drückte START):
  `seRandomCheck` ← `emSeCheck` ← `SndCall` ← `emMove` (SIGSEGV bei
  0x10395040). Die Zufalls-Tabelle der Geräusche in jeder Tonbank (Kopfwort 1:
  Versatz der Tabelle, darin Versätze der Listen `{num, last, e[num]}`) blieb
  big-endian; außerdem lief die letzte Wavetable-Tabelle bis zum Blockende und
  hätte die Zufalls-Tabelle als 16-bit-Werte gedreht. Jetzt eigener Umwandler
  (nur die Gruppen, die die SITs des Blocks nennen; das Spiel liest keine
  anderen) und die Wavetable endet vor der Tabelle. Test `core-test sound`
  (schlägt mit dem alten Umwandler fehl). Mit echten Daten noch nicht geprüft.
- Warum die PC-Prüfung das nicht fand: Ihre Tastenfolge endet beim Laden des
  Raums, das Spiel wartet dann bis zum Ende im Funkspruch, Gegner laufen nicht.
  Neu: `RE4_PAD_ROOM_SCRIPT` (Bilder ab dem Raum gezählt) und Steuerknüppel
  in der Tastenfolge (`STICKUP` …); `whole_r100_check.sh` drückt 1500 Bilder
  nach dem Raum START (Funkspruch zu) und lässt Leon 10 s laufen. Test
  `runtime-test trace`.
- Offen: „Das Auto sieht man nur teilweise“ (Bild der Stelle fehlt noch);
  eine Figur ohne Herkunft ist eine Waffen-Figur (`cEmWep`, 0x42), nicht das
  Auto. Kein Ton: Meilenstein 8. `test-core-app.ps1` meldet jetzt einen
  Neustart der App (anderer Prozess) als „NEU GESTARTET … abgestürzt?“.

### Zweiter Gerätelauf (14.10.2026, Retroid) und Analyse

Gemeldet: Auto fehlt um Polizisten und Leon, Münder bewegen sich nicht, kein
Ton, der erste Funkspruch endet erst mit START, danach Kamera am Boden mit
Blick nach oben, Absturz beim Erreichen des Hauses. Ursachen und Korrekturen:

- **Kamera am Boden:** Ein Schulterkamera-Schnitt (CAM, `Id 8`,
  `CameraQuasiFPS::setAreaData`) liest immer 24 Kamerapositionen (nahe und
  ferne Punkte) und je 12 Ziele, Rollwinkel und Öffnungswinkel, unabhängig von
  `nPoint`. Der Umwandler drehte nur `nPoint` Werte: Die fernen Punkte blieben
  big-endian, fast 0, also die Kamera an Leons Füßen. Jetzt dreht er, was das
  Spiel liest, jedes Wort einmal (Test `core-test camera`, schlägt mit dem
  alten Umwandler genau bei den 12 fernen Punkten fehl).
- **Auto:** Das Polizeiauto ist Kulisse aus dem Raumskript (`SetObjSmd`,
  Einträge 0x1D/0x1E, 0x1F/0x20), nicht aus dem SMD. Die App übersprang alle
  Kulissen-Objekte. Patch 0029 + Brücke Version 6 (SMD-Eintrag je Objekt), die
  App folgt dem Kern: versteckte, nicht geladene und bewegte SMD-Kulisse, alles
  andere wie die Figuren (Tests `core-test bridge`, `core_scene_gl_check`).
  Dabei auch: r100 versteckt die SMD-Objekte 0x37/0x38 beim Start, die App
  zeichnete sie bisher trotzdem.
- **Münder / Blinzeln:** Formanimationen (`shape.cpp`) verformen die
  Gesichtspunkte im Speicher des Kerns; die App zeichnete die unverformten
  Disc-Daten. Brücke Version 7: Für Infos mit laufender Form rechnet die
  Brücke mit dem Originalcode (`ResetShape`, `CalculateShape_new`) und gibt
  die Punkte weiter; die App lädt das Gesicht dann neu hoch (Tests
  `core-test bridge` „shape“, `core_scene_gl_check`).
- **Funkspruch / Stimmen:** Der Tontreiber läuft im AX-Audiorahmen-Rückruf
  (5 ms); mit AX als Platzhalter lief er nie (Stimmen blieben belegt: „STR
  SND_VOICE L is not available“, Sprach-Stream nie bereit, `ss_term.cpp
  OpeMesMove` wartet). Jetzt `runtime/ax.cpp` ohne Tonausgabe (Plattformtabelle;
  Test `core-test ax`). Hörbarer Ton bleibt M8.
- **Absturz am Haus:** Kein Protokoll (nach Ende des 120-s-Laufs gespielt).
  Die PC-Prüfung stellt Leon jetzt nach dem Funkspruch vor das erste Haus
  (Testhilfe `RE4_TEST_PLACE`, die Stelle, an die `r100_Sce_look` Leon nach
  s03 setzt) und lässt ihn hineinlaufen; sie listet außerdem alle
  Auslöse-Bereiche des Raums (AEV). Ob s03 damit ausgelöst wird, zeigt der
  nächste Lauf; sonst Gerätelauf mit `-Seconds 600` bis zum Haus.
- App-Bildtest: Die Raum-Animationen (Himmel, Wasser, 30 Hz) liefen zwischen
  den Testbildern weiter; frühere Pixelvergleiche passten nur zufällig. Der
  Test zeichnet jetzt ohne Zeitfortschritt; die Prüfungen für starre Teile
  bewegen sichtbar (nach oben).

### Dritter PC-Lauf (14.10.2026, nach der Kamera-Korrektur)

- **Android-Kern baute nicht:** `formats_event.cpp` nutzte `std::vector`; die
  Datei wird mit den Spiel-Schaltern und der Prelude übersetzt, und die
  libc++ des NDK verträgt sie nicht (`__libcpp_allocate` …), libstdc++ der
  Linux-Builds schon. Jetzt einfacher C-Speicher. Neu im Cloud-Build:
  `whole_libcxx_check.py` übersetzt alle Laufzeitdateien zusätzlich gegen
  libc++ (fand den Fehler nachträglich, 150 Dateien ok); `whole_setup_linux.sh`
  installiert `libc++-dev`.
- **PC-Prüfung: Absturz in `CameraQuasiFPS::setAreaData` beim Raumaufbau:**
  Der Kamera-Umwandler brach bei r100 ab („camera keys at 0x818c3400 …
  conversion stopped“): ein Schnitt hat in `pFrame` einen veralteten Zeiger
  des Kamera-Werkzeugs (derselbe Wert steht in einem unbenutzten `pVer` mit
  0 Punkten; der alte Umwandler übersprang ihn darum zufällig über die
  gemeinsame „schon gesehen“-Liste). Bildzeiten liest das Spiel nur bei
  Kamerafahrten (Id 6/7, `HermiteExport`); jetzt nur dort umgewandelt, und ein
  Feld außerhalb des Blocks wird gemeldet und übersprungen statt die ganze
  Umwandlung zu stoppen. Test `core-test camera` nachgebaut wie r100 (fällt
  mit dem vorigen Umwandler mit genau der Meldung aus dem Lauf durch).

### Dritter Gerätelauf (14.10.2026, Retroid) und Analyse

Gemeldet: Nach dem Intro steht Leon mit richtiger Kamera da, aber keine Taste
wirkt, die Vögel stehen still; im Intro steht Leon kurz mit abgespreizten
Armen außerhalb des Autos; Dach, Seitenwände und Boden des Autos fehlen; ein
gelber Fleck im Intro. Lauf 600 s ohne Absturz, die Tasten kamen laut Log im
Kern an (`PADRead`), das Overlay zeigte durchgehend „event“.

- **Keine Steuerung, Vögel still:** Das Spiel hing im Funkspruch nach dem
  Intro (`sscrn.cpp OpeSetOpenTerm` → `event.cpp SndStrPlayBlock`: wartet,
  bis der Sprach-Stream bereit ist), also innerhalb von `SceEventStart`: Tasten
  gesperrt, alle Gegner und Tiere im Ereignis-Modus. Seit `ax.cpp` bekommt der
  Stream eine Stimme (vorher keine: der Funkspruch lief ohne Stream weiter,
  START schloss ihn). Der Stream-Code des Tontreibers (`snd_str2.cpp`) startet
  im Audio-Rahmen (einem Interrupt) einen DVD-Lese- oder ARAM-Kopierauftrag
  und setzt erst **danach** sein Kennzeichen `dvd_busy`/`dma_busy`; der
  Rückruf löscht es. Die Plattformschicht rief den Rückruf schon während des
  Auftrags, das Kennzeichen blieb für immer gesetzt, der Stream wurde nie
  bereit. Jetzt bleibt ein Auftrag aus einem Interrupt heraus „beschäftigt“,
  bis der Interrupt-Rückruf zurückgekehrt ist (wie auf der GameCube, deren
  Laufwerk Millisekunden braucht): `os_thread.cpp re4_os_defer` /
  `re4_os_run_deferred` (nach jedem Audio-Rahmen und am Ende des Interrupts),
  `dvd.cpp`, `aram.cpp`. Außerhalb von Interrupts bleibt alles sofort (die
  Lade-Warteschlangen des Spiels warten in Schleifen ohne Unterbrechung
  darauf). Tests: `runtime-test dvd` (Lesen im Interrupt, Abbruch → −3) und
  neu `runtime-test stream`: der Stream-Code des Spiels spielt zwei Dateien
  einer synthetischen Disc (eine passt in den ARAM-Ring, eine läuft
  11 Blöcke durch den Ring) bis zum Ende, dazu Stopp beim Spielen und beim
  Puffern (`tests/stream_probe.cpp`); mit der alten Plattformschicht schlägt
  er fehl. Der Funkspruch endet damit von selbst (er läuft nach Bildern, nicht
  nach Tonende, `ss_term.cpp OpeMesMove`).
- **Leon außerhalb des Autos (Arme abgespreizt):** Das war die Spielfigur
  Leon, die das Spiel während des Intros nicht zeichnet; das Intro zeigt
  seine eigene Leon-Figur im Auto. Die Brücke kannte die Sichtbarkeitsregeln
  von `trans.cpp` nicht: Anzeige-Schalter (`DPF_PL` …) und vor allem
  „während eines Ereignisses nur Modelle mit `be_flag & 0x800`“. Dadurch
  zeichnete die App im Intro zusätzlich Leon, das geparkte Polizeiauto des
  Raums (`R100Init`: `setNoSuspend(0)`, darin die Teile 11/13/15 auf 0
  skaliert) und die beiden Polizisten des Raums. Jetzt Kennzeichen `HIDDEN`
  (Version 8), die App hält sich daran. Tests: `core-test bridge` (Ereignis
  hält die Welt an, `setNoSuspend`, `DPF_PL`, `DPF_EM`, ganz ausgeblendet;
  schlägt ohne die Regel fehl), `core_scene_gl_check` (verstecktes Modell
  fehlt im Bild).
- **Zeichen-Einstellungen des Spiels:** Glas, Lichtschein und Strahlen
  zeichnet das Spiel mit eigenem Mischmodus (`blend_mode`: addieren …), ohne
  Tiefenschreiben (`z_mode`) oder ausgeblendet (`invisible_factor`); die App
  zeichnete alles undurchsichtig. Jetzt übernimmt sie Mischmodus, Tiefenmodus,
  Flächenseite (`CullMode`) und Ausblenden und zeichnet Durchscheinendes nach
  dem Undurchsichtigen (Test `core_scene_gl_check`, 83 Prüfungen).
- **Auto (Dach, Seiten, Boden):** Im Intro ist das Auto ein eigenes
  Ereignis-Modell `obm1a00` (24 Teile) plus sechs Scheiben-Modelle
  `obm3000a–f` (`r120.cpp EventCarInit`: Scheiben mit `z_mode` 1; im Auto
  versteckt das Spiel selbst die Teile 0xF und 0x12–0x17). Ursache noch nicht
  belegt; mit den beiden Korrekturen oben liegt das geparkte Auto nicht mehr
  im Intro, und die Scheiben werden durchscheinend gezeichnet. Diagnose für
  den nächsten PC-Lauf: Schnappschüsse während der ganzen Autofahrt (30 …
  750 Bilder nach dem Raumstart) und je Modell-Info im Bericht, wie viele
  Dreiecke auf vom Spiel auf 0 skalierten Teilen liegen, wie viele der
  Alpha-Test entfernt (Punkt-Alpha, fast durchsichtiges Alphabild), dazu
  Mischmodus, Tiefe, Seite, Ausblenden.
- **Gelber Fleck:** noch nicht belegt. Vermutung: ein Lichtschein- oder
  Glas-Modell, das die App bisher undurchsichtig zeichnete (jetzt gemischt
  wie im Spiel), oder ein Material mit Spiegel-/Wasser-Textur des Spiels
  (`setTexRender`, Rückspiegel `EvtTexRenderCamTrans`), das die App mit
  seiner Ersatz-Textur zeichnet. Der nächste Lauf zeigt, ob er bleibt.
- **PC-Prüfung:** drückt im Raum kein START mehr. Sie wartet, bis der Spieler
  frei ist (kein Ereignis, kein Untermenü seit 300 Bildern: „run: player
  free“), stellt Leon dann vor das erste Haus (`RE4_TEST_PLACE` mit
  `free+`) und lässt ihn 5 s laufen (`RE4_PAD_FREE_SCRIPT`). Der Bericht nennt
  „Spieler frei“ (sonst FEHLER: Funkspruch endet nicht) und „Steuerung“ (wie
  weit Leon lief; unter 500 Einheiten FEHLER). Das Tastenprotokoll nennt
  jetzt jede Änderung, auch vom Gerät, einmal.

### Rückfragen nach dem dritten Gerätelauf (14.10.2026)

- **Gelber Fleck = ovales Drahtgitter:** das Kollisions-Hilfsbild (Kapsel und
  Bodenfleck) der Bewegungsprüfung der früheren App (`scene_gl.cpp`,
  `SceneProbe::showBody`), im Kernmodus nie abgeschaltet und am Weltursprung
  gezeichnet. Jetzt aus (`core_scene.cpp`). Test `core_scene_gl_check`: Bild
  auf den Ursprung, vorher 163 Punkte in der Farbe der Kapsel, jetzt 0. Der
  Kulissen-Test sah den Boden bisher nur über diese Kapsel; er blickt jetzt
  von der Seite auf den Boden (verstecken: 32 229 Punkte ändern sich).
- **Vollständiges Intro:** Der Gerätetest startete im Debug-Menü direkt r100
  (nur die kurze Ankunft s40). Ein neues Spiel beginnt in r120
  (`r120.cpp R120Event`): Vorfilm `movie/opening.sfd`, dann die Autofahrt
  (Ereignisse r120s00/s01), dann Sprung nach r100. `test-core-app.ps1` startet
  jetzt standardmäßig r120 (`-StartRoom`); `whole_r100_check.sh --room 120`
  ebenso (mit `--frames 30000`).
- **Filme bis M9:** `runtime/movie.cpp` statt der erzeugten Platzhalter. Diese
  hätten r120 und die Demo-Filme des Titels abstürzen lassen: ohne Handle
  ruft `cSofdec::ThreadMove` trotzdem `mwPlyGetStat` über dessen
  Funktionstabelle, und `mwPlyGetCurFrm` ließ den Bildsatz ungeschrieben.
  Jetzt endet jeder Film sofort (Status PLAYEND, „movie: … skipped“ im Log),
  das Spiel geht danach weiter. Test `core-test movie`. Platzhalter: 173.
- **Ton:** `ax.cpp` lässt den Tontreiber laufen (Stimmen werden frei, Streams
  bereit, Funkspruch läuft), erzeugt aber noch keine Töne. Hörbarer Ton ist
  Meilenstein 8: Stimmen dekodieren und mischen (DSP-ADPCM, Abtastrate,
  Lautstärke), MIX/SYN/SEQ/AXART/AXFX aus `src/lib` mitkompilieren (Quellen
  liegen vor), Ausgabe über `GameAudio.java` (AudioTrack) der App.
- **Logos, Titelmenü, Untermenüs, Anzeigen:** Das Spiel zeichnet sie mit GX
  (2D); GX ist noch Platzhalter, die App zeigt bis zum ersten Raum nur das
  Titelbild. Entscheidung (15.10.2026): GX nachbilden (`gx-plan.md`, G1
  unten). Ton bleibt in der geplanten Reihenfolge (M7 Steuerung, M8 Ton, M9
  Filme).

### GX nachbilden, Schritt G1: FIFO einsammeln (15.10.2026, Cloud)

Entscheidung des Projektinhabers (15.10.2026): GX nachbilden statt die
Bildschirme in der App nachzubauen (Bauplan `port/docs/gx-plan.md`, Regel in
`CLAUDE.md`). G1 ist der erste Schritt: Alles, was das Spiel an den
Grafikprozessor schickt, kommt jetzt im Port an.

- **GX-Bibliothek des SDK im Build** (`RE4_SDK_UNITS` in
  `port/whole/CMakeLists.txt`: `GXAttr GXBump GXDisplayList GXDraw GXFifo
  GXFrameBuf GXGeometry GXInit GXLight GXMisc GXPerf GXPixel GXTev GXTexture
  GXTransform`), unverändert aus `src/lib` bis auf Patch 0030
  (Assembler-Stellen in C). Die rund 97 GX-Platzhalter entfallen; die
  Registerwerte stammen damit aus dem Original-SDK.
- **FIFO:** Jeder Schreibzugriff auf `GXWGFifo` (SDK `GXVert.h`, `__gx.h`,
  das Spiel in `gx.h`) wird beim Bauen zu `re4_gx_put_*` (neue Regel in
  `whole_prepare.py`). `runtime/gx_fifo.cpp` sammelt die Bytes big-endian wie
  die Hardware, zerlegt den Strom in Befehle (CP, XF, indiziertes XF, BP,
  Display-Listen-Aufruf, Zeichenbefehle mit der Punktgröße aus
  Punktbeschreibung und -format) und zählt sie; Unbekanntes wird gezählt
  (Zeichen für einen Strom, der seine Rahmung verloren hat). Display-Listen:
  Während `GXBeginDisplayList` schreibt der FIFO in den Speicher der Liste
  (PI-Register wie auf der Hardware, Überlauf-Bit), `GXCallDisplayList` liest
  sie über die physische Adresse (`0x80000000 + physisch`).
- **Register-Blöcke** CP, PE, PI, MEM sind Speicher des Ports
  (`re4_gx_register_block`). BP-Schreiben mit Maske (`0xFE`) werden
  gespeichert; `PE_DONE` (`0x45`) und Token (`0x47/0x48`) lösen die
  PE-Unterbrechungen 0x12 (Token) und 0x13 (fertig) aus, zugestellt beim
  nächsten `OSSleepThread` und bei jedem VI-Retrace über die
  Unterbrechungs-Rückrufe des SDK (`__OSSetInterruptHandler`, neu in
  `dolphin_os.cpp`). Damit kehrt `GXDrawDone` zurück und der
  Zeichen-Rückruf des Spiels bekommt sein Token.
- **EFB lesen** (`GXPeekARGB/Z`): antwortet „nichts gezeichnet“ (Farbe 0,
  Tiefe ganz hinten); echtes Lesen ist G5.
- Noch wird nichts dekodiert oder gezeichnet: Die App zeigt weiter die
  Zustandsbrücke. Nächster Schritt G2: Befehlsdecoder (Zustand aus
  CP/XF/BP, Punkte in Zeichenaufträge).

Test `re4_host --runtime-test gx` (`tests/gx_probe.cpp`, mit den Schaltern
und Headern des Spiels gebaut): `GXInit` schreibt alle Register (mehr als 30
BP, XF, nichts Unbekanntes); `GXSetBlendMode` ergibt das erwartete BP-Wort
`0x41`; ein Dreieck aus Float-Punkten und ein Viereck aus s16-Punkten mit
RGBA8-Farbe und Float-Texturkoordinaten ergeben 2 Zeichenbefehle mit 7
Punkten; eine Display-Liste wird in den Speicher geschrieben (Größe Vielfaches
von 32, enthält den Zeichenbefehl) und beim Aufruf genau einmal gezeichnet;
das Token `0xADEB` erreicht den Rückruf, `GXDrawDone` kehrt zurück.
Ergebnis: alle Laufzeittests grün in `whole_build.sh` (i686, armhf unter qemu,
beide auch mit `-O2`), libc++-Prüfung 152/152. Platzhalter: 76 Funktionen
(vorher 173), 0 Daten (vorher `GXWGFifo`). Nicht getestet: mit Disc-Daten
(lokal) und auf dem Gerät – das Spiel nutzt jetzt die echte GX-Bibliothek,
`test-r100-core.ps1` und `test-core-app.ps1` sollten zeigen, dass es so weit
läuft wie vorher.

### GX nachbilden, Schritt G2: Befehlsdecoder (15.10.2026, Cloud)

`runtime/gx_decode.cpp` arbeitet den Befehlsstrom sofort ab, sobald ein
Befehl vollständig ist (der Grafikprozessor liest die Daten also, solange das
Spiel sie noch so hat wie beim Senden). Er führt den Zustand von CP, XF
(Matrizen, Licht, Register), BP und dem Paletten-Teil des TMEM und schreibt,
solange ein Renderer Bildstücke abholt, die entschlüsselte Arbeit in
**Bildstücke** (`runtime/re4_gx_frame.h`, eigenes Format für die App):

- ein Stück endet mit jeder Bildkopie in den Bildspeicher (`GXCopyDisp`), also
  einmal je Spielbild; es enthält die geänderten BP-/XF-Register,
  Paletten-Ladungen, Zeichenaufträge (Punkte als Float/RGBA8 mit
  Matrix-Indizes, Dreieckslisten statt Vierecken/Streifen/Fächern) und
  Textur-Bindungen;
- Texturen werden über einen Inhalts-Schlüssel (Prüfsumme der Texel, bei
  Paletten-Texturen mit der Palette) nur einmal übertragen; was lange nicht
  gebraucht wurde, wird vergessen (`FORGET`);
- Kopien des Bildpuffers in eine Textur (`GXCopyTex`) hinterlassen eine Marke
  am Ziel; eine Textur dort ist dann diese Kopie (der Renderer hat die Pixel);
- Warteschlange mit 4 Plätzen: ist sie voll, wartet der Kern auf den Renderer
  (ein langsamer Renderer bremst das Spiel wie ein langsamer Grafikchip); holt
  er eine Sekunde lang nichts ab, wird verworfen und mit vollem Zustand neu
  begonnen. Ohne Renderer (Linux-Prüfungen, bisherige Brücke) wird nichts
  aufgezeichnet, nur der Zustand geführt.

**Byte-Reihenfolge** (Bestandsaufnahme aller Stellen, an denen das Spiel dem
Grafikprozessor Speicher übergibt): Befehlsströme (FIFO, aufgenommene und
Disc-Display-Listen) sind big-endian; Punktfelder und per Index geladene
Matrizen sind in Host-Reihenfolge (Formatumwandler, CPU-Puffer wie die
Skinning-Ausgabe); Texel und Paletten bleiben big-endian. Vier Display-Listen
baut das Spiel selbst mit CPU-Schreibzugriffen in Host-Reihenfolge (Sprite-Viereck
`eff_sys.cpp`, Wasser-/Sandgitter `Espgen42/43/45.cpp`): Patch 0032 meldet sie
an, der Decoder liest sie dann in Host-Reihenfolge. Zwei Felder liegen außerhalb
von MEM1 (`g_EspCommonDisplayList`, Palette `CLUTdata` in `filter02.cpp`):
Display-Listen-Aufrufe tragen den Zeiger selbst, für Felder, Texturen und
Paletten gibt Patch 0031 dem Grafikprozessor über `re4_gx_physical` ein
Adressfenster oberhalb von MEM1 (`0x03000000`).

Test `runtime-test gx` (zusätzlich zu G1, `tests/gx_probe.cpp
re4probe_gx_frame_test`): ein künstliches Bild über die SDK-Bibliothek –
Matrix direkt und per Index, texturiertes Viereck mit s16-Positionen (Index,
Host-Reihenfolge, Nachkommabits), RGBA8-Farben (16-bit-Index) und
Float-Texturkoordinaten, Dreiecksstreifen, CPU-gebaute Display-Liste in
Host-Reihenfolge, Palette und C4-Textur, Palette außerhalb von MEM1,
Bildpuffer-Kopie als Textur, Bildkopie; erwartet werden genau diese Punkte,
Indizes, Texel, Paletten und Zustände, beim zweiten Bild dieselbe Textur ohne
erneute Daten, ohne Renderer kein Stück. Ergebnis: grün in `whole_build.sh`
(i686, armhf, beide auch `-O2`), libc++ 153/153. Nicht getestet: mit
Disc-Daten (lokal) – das kommt mit G4.

### GX nachbilden, Schritte G3 und G4: Renderer in der App (15.10.2026, Cloud)

**G3 – GLES3-Renderer** (`port/android-decomp/app/src/main/cpp/gx_gl.cpp`,
`gx_gl_shader.cpp`, `gx_texture.cpp`): führt die Bildstücke des Kerns aus.

- eigener Bildpuffer (EFB) 640×528 mal Faktor (Farbe und Tiefe), GameCube-Zeile
  0 = GL-Zeile 0, dadurch ohne Spiegelungen bei Kopien, Schere und Bildausschnitt;
- Shader werden aus dem Zustand erzeugt und zwischengespeichert: Vertex-Teil mit
  XF-Transformation (Matrix-Indizes je Punkt oder Standard), Licht (8 Lichter,
  Richtung/Spot/Glanz, Diffus-Funktionen, Material/Umgebung aus Register oder
  Punktfarbe), Texturkoordinaten-Erzeugung (Matrizen 2×4/3×4, Quellen Position/
  Normale/Farbe/Texturkoordinaten, Prägung, zweite Transformation mit
  Normalisieren); Fragment-Teil mit bis zu 16 TEV-Stufen in 8-Bit-Ganzzahl-
  Rechnung wie die Hardware (Rundung, Vorzeichen-11-Bit-Register, Vergleichsmodi,
  Konstanten, Tauschtabellen), indirekte Texturen (Matrizen, dynamisch S/T,
  Umbruch, Bump-Alpha), Alpha-Test, Nebel (4 Formeln, perspektivisch und
  orthografisch);
- Mischen, Tiefe, Farb-/Alpha-Schreibmaske, Seiten-Kappen, Schere, Linien-
  und Punktgröße aus den BP-Registern; alle GX-Texturformate (I4 … CMPR,
  Paletten IA8/RGB565/RGB5A3), Mipmaps, Sampler aus den Texturmodi;
- Bildpuffer-Kopien: in Texturen (Farb-, Intensitäts-, Kanal- und
  Tiefenformate, 2×2-Filter, Löschen nach dem Kopieren) und in das Anzeigebild
  (XFB); Kopien, die als Paletten-Textur gelesen werden (Wärmesicht,
  `filter07`/`filter02`), laufen durch die Palette.
- Noch nicht nachgebildet (G5): Ziel-Alpha (`GXSetDstAlpha`), Z-Texturen,
  die meisten Logik-Operationen, `GXPeekZ`, Linienbreite über 1 Pixel hängt am
  Grafiktreiber.

**G4 – in der App und in der PC-Prüfung:**

- Kernmodus (`core_mode.cpp`): Die App holt jedes Bild alle fertigen
  Bildstücke, führt sie aus und zeigt die letzte Bildkopie im 4:3-Rahmen
  (640 breit, Zeilen aus 480). Bis zur ersten Bildkopie (und mit der
  Einstellung `RE4_VIEW=bridge`) zeichnet weiter die Zustandsbrücke.
  EFB-Faktor `RE4_GX_SCALE` (Standard 2). Neuer GL-Kontext: der Kern beginnt
  von vorn (voller Zustand, alle Texturen neu). `test-core-app.ps1 -Bridge`
  zeigt zum Vergleich die frühere Ansicht; der Bericht nennt Ansicht,
  GX-Zähler und Shader-Fehler.
- PC-Prüfung (`whole_r100_check.sh`, `test-r100-core.ps1`): Der Kern schreibt
  mit `RE4_GX_DUMP` bis zu 24 Bilder als Bildstück-Dateien
  (`re4_gx_frame_io.h`; früh: Bild 60 … 2400 für Logos/Titel/Menü, dann der
  Raum-Zeitplan der Schnappschüsse und alle 2500); das Werkzeug
  `gx_chunk_render` (App-Renderer auf Mesa) zeichnet sie, Bericht Abschnitt 5,
  Ordner `bilder-gx`.

Tests (Cloud, ohne Disc-Daten):
- `gx_gl_check` (`tools/test_gx_gl.cpp`): Texturdecoder gegen den geprüften
  TPL-Leser der App für alle 11 Formate (13×9 Texel, keine ganzen Kacheln) –
  gleich; gezeichnete Bildstücke: Löschen per Bildkopie, Punktfarben,
  Ausrichtung, Seiten-Kappen (im Uhrzeigersinn = Vorderseite), Tiefentest,
  Mischen, Alpha-Test, Textur über die TEV (Texel oben links, nächster
  Nachbar), Textur erneut nur per Schlüssel, Konstantfarbe, Lerp mit
  Hardware-Rundung, Vergleichsmodus, Subtraktion mit Faktor 2, Richtungslicht
  mit Umgebungslicht, Bildpuffer-Kopie als Textur, Paletten-Textur, linearer
  Nebel; 60 zufällige TEV-Aufbauten (Eingänge, Vergleiche, Faktoren,
  Tauschtabellen, Konstanten, indirekte Stufen): alle 70 Shader übersetzen.
  Ergebnis: 0 Fehler.
- Durchgehend: Das künstliche Bild von `runtime-test gx` (echter Decoder des
  Kerns) als Datei (`RE4_GX_TEST_DUMP`) mit `gx_chunk_render` gezeichnet:
  5 Zeichenaufträge, 2 Texturen, 1 Kopie, Shader fehlerfrei.
- `runtime-test gx` zusätzlich: Warteschlange zwischen Spiel- und
  Renderer-Thread (60 Bilder in Reihenfolge, keins verworfen; ein Renderer,
  der nichts mehr abholt, hält das Spiel höchstens etwa eine Sekunde auf;
  danach beginnt das nächste Stück von vorn).
- Bisherige App-Prüfungen unverändert grün (`core_scene_gl_check` 85,
  Bindungs-Prüfung `re4decomp_link_check` mit `core_mode.cpp`).

Nicht getestet: mit Disc-Daten (PC-Prüfung, Abschnitt 5) und auf dem Gerät
(`test-core-app.ps1`). Das sind die nächsten Schritte beim Projektinhaber.

### Vierter PC-Lauf mit GX-Bildern (15.10.2026, WSL2) und Analyse

Ergebnis des Laufs (`e4fcc0e2`): Formatvergleich 155 Dateien ohne Fehler;
Hauptschleife bei Bild 621 (r100); die Bilder der Brücke zeigen das Intro
jetzt richtig (Auto mit Dach, Polizisten, Leon). Zwei Fehler:

- **Stillstand bei Bild 1913, Funkspruch:** Der Funk-Bildschirm
  (`ss_term.cpp OpeMesMove`) beendet die Funk-Tonspur mit `SndStrStopBlock`:
  Stopp anfordern, dann `do SceSleep(1); while (spielt)`. `SceSleep` schläft
  nur in Szenario-Aufgaben; im Funk-Bildschirm kehrt es sofort zurück, die
  Schleife dreht. Auf der GameCube blendet derweil der Ton-Interrupt die
  Spur aus; im Port kommen Interrupts nur an Unterbrechungspunkten an, die
  Schleife hat keinen. **Patch 0035:** `SceSleep` außerhalb einer
  Szenario-Aufgabe wartet auf das nächste Halbbild (`re4_wait_interrupt`),
  dabei laufen die Audio-Rahmen. Test `runtime-test stream`, neuer Fall
  „stopped and waited for like the radio screen“: hängt ohne den Patch
  (2000 Durchläufe ohne Ende), endet mit ihm nach 26 Halbbildern. Der
  Zustands-Trace schreibt jetzt die Tonspuren mit (`"str"`, Bericht:
  „Tonspuren im letzten Bild“).
- **GX-Bilder fast ganz weiß:** Die GX-Bibliothek liest Farben (`GXColor`,
  vier Bytes R G B A) an mehreren Stellen als ein 32-Bit-Wort
  (`*(u32*)&color`, `GXCOLOR_AS_U32`); auf little-endian liegen die Bytes
  dann verkehrt herum (Schwarz deckend wird Rot durchsichtig).
  **Patch 0033** setzt das Wort aus den Bytes zusammen: TEV-Farben,
  Konstantfarben, S10-Farben, Material-/Umgebungsfarbe, Nebelfarbe, und die
  Lichtfarbe beim Laden des Lichtobjekts (Patch 0030). **Patch 0034:** sechs
  Stellen im Spiel geben Ein-/Ausblendfarben als `u32` an `FadeSet`
  (`card.cpp`, `tv_mode.cpp`, `event.cpp`); sie werden jetzt in
  GameCube-Byte-Reihenfolge geschrieben (`RE4_BE`). Test `runtime-test gx`
  prüft TEV-, Konstant-, Material-, Licht- und Nebelfarbe in den Registern:
  schlägt ohne Patch 0033 fehl, mit ihm grün.
- Damit die GX-Ausgabe künftig auch in der Cloud an echten Bildern geprüft
  werden kann, legt die PC-Prüfung einige Bildstücke als `gx-daten.zip` in
  den Bericht (nur zum Hochladen in die Sitzung, nie ins Repository).

Gerät: Die App stürzte sofort ab; Protokoll (`logcat.txt`) steht noch aus.

### GX-Daten in der Cloud: weiße Bilder und Zeigerprüfungen (15.10.2026)

Mit `gx-daten.zip` aus dem fünften PC-Lauf (Stand e035f750, mit `-Arm`;
i686 und armhf/qemu je 10.000 Bilder ohne Absturz) zeichnet
`gx_chunk_render` die Bilder 61, 301, 601 und 652 hier nach (die Dateien
bleiben im Sitzungsordner, nie im Repository). Ein Schrittwerkzeug (EFB
nach jedem Zeichenauftrag) zeigte: Schon der zweite Auftrag von Bild 301
(Warnbildschirm) färbt das ganze EFB weiß. Seine TEV-Stufe hatte Wörter wie
`0x8F10C0` (Register-ID im untersten Byte, Bias 3 = Vergleichsmodus): die
Tabellen von `GXSetTevOp` sind Bitfeld-Strukturen, auf Little-Endian falsch
gepackt (Patch 0036). Mit nachträglich korrigierten TEV-Wörtern zeigen die
aufgezeichneten Daten den Warnbildschirm und die Autofahrt des Intros
(einschließlich der Debug-Anzeigen der Entwicklerfassung); der Renderer
selbst war also richtig. Test: runtime-test gx prüft `GXSetTevOp`
(Modulate auf Stufe 0, Replace auf Stufe 1); ohne Patch 0036 schlägt er fehl.

Dabei fiel die Bildschirmmeldung `cLightInfo::init2() PTR ERROR` auf (24-mal
im PC-Lauf): `VALID_PTR` (127 Stellen in 31 Dateien) prüft gegen MEM1, die
Aufrufer übergeben aber Stapelvariablen, die im Port auf Host-Stapeln
liegen. Patch 0037 nimmt Host-Stapel und statische Daten der
Spielbibliotheken hinzu. Test: runtime-test tasks (Stapelvariable eines
OS-Threads, `Global`, MEM1 gültig; NULL, 0x1000, Host-Heap ungültig); ohne
Patch 0037 schlägt er fehl. Offen: 17 weitere Stellen prüfen den
MEM1-Bereich ohne `VALID_PTR` (eigene Ausdrücke); noch nicht durchgesehen.

Noch unklar: der Absturz auf dem Retroid in den ersten 15 s (auch mit
`-Bridge`, also im Kern), während armhf unter qemu durchläuft. Das
Geräteskript sichert jetzt den Absturzbericht (siehe unten).

### Erster Gerätelauf mit Absturzbericht (15.10.2026, Retroid) und Analyse

Stand `6e2288f1`. Das neue Geräteskript erkannte den Absturz nach 20 s
(die App startete einmal neu und stürzte wieder ab) und übersetzte die
Stapelzeilen: SIGBUS `BUS_ADRALN` (unausgerichteter Zugriff) in
`GXInitFifoBase` (`src/lib/GXFifo.c:111`) aus `GXInit` ← `Render_init` ←
`systemStartInit`, Adresse `…515`. `GXFifoObj` ist im SDK ein Byte-Feld
(`u8 pad[128]`, Ausrichtung 1); das statische `FifoObj` lag im
Android-Build an einer ungeraden Adresse, und clang fasst die vier
Wort-Speicherungen in `GXInitFifoBase` zu `strd` zusammen, die auf ARM eine
Wortausrichtung verlangen (der Kernel des Retroid korrigiert das für
32-bit-Prozesse nicht). Im armhf-Linux-Build lag das Objekt zufällig
ausgerichtet; qemu-arm meldet solche Zugriffe sonst ebenfalls (geprüft mit
einem kleinen Programm: SIGBUS). **Patch 0038** richtet `GXFifoObj` auf
32 Byte aus. Tests: `static_assert` in `tests/game_probe.cpp` (der Build
scheitert ohne den Patch) und `runtime-test gx` prüft die Adresse, die
`GXInit` zurückgibt. Durchsicht der übrigen SDK-Typen, die als Struktur
gelesen werden: `GXTexObj`, `GXTlutObj`, `GXLightObj`, `GXTexRegion` sind
`u32`-Felder (Ausrichtung 4), nur `GXFifoObj` war ein Byte-Feld.

Fünfter PC-Lauf (Stand `6e2288f1`, mit `-Arm`): i686 und armhf je 10.000
Bilder ohne Absturz; „Spieler frei: NEIN“: das Untermenü des Funkspruchs
öffnet bei Bild 1876 und bleibt bis zum Ende offen. Das Spiel-Log zeigt den
Ablauf: Funkarchiv `op/op01.das` geladen, „SND: STREAM 1 STOP“ (Klingelton
beendet, Patch 0035 wirkt), etwa 9 s später „SND: STREAM 0 STOP“ (die
Sprachaufnahme lief durch). Der Funk-Bildschirm (`ss_term.cpp OpeMesMove`)
schließt aber nur, wenn seine Ablauf-Tabelle (`OpeMesSeq`: Bild, Meldung,
Anzeigedauer; Ende bei Dauer −1) den letzten Eintrag erreicht. Diese Tabellen
stehen im Funkarchiv: `SS_ARC_PTR(arc, n)` liest das n-te Wort des
Archivkopfs, `OP_ARC_PTR(wk, 4 + i)` ist also Eintrag i; `OpeMesTblInit`
nimmt die erste Hälfte der Einträge als Meldungen, die zweite als
Ablauf-Tabellen (op01 13 + 13, op02 6 + 6, op03 5 + 5). Der Umwandler
wandelte die Einträge nur nach ihrem Kennzeichen; für die Ablauf-Tabellen
meldete er keinen fehlenden Umwandler, sie liefen also durch einen
vorhandenen (vermutlich `SEQ`, die Bewegungs-Ereignisliste: nur das erste
16-Bit-Wort und die Schlüssel gedreht), die Bildnummern blieben
big-endian und wurden riesig. **Jetzt:** `convertCodecArchive` wandelt die
zweite Hälfte als `OpeMesSeq` (`convertOpeMesSeq`, unabhängig vom
Kennzeichen) und schreibt die Kennzeichen beider Hälften ins Log (Bericht:
„Funkarchive“). Test `core-test codec`: ein Funkarchiv mit einer
Ablauf-Tabelle unter dem Kennzeichen `SEQ`; schlägt mit dem alten Umwandler
fehl. Noch nicht mit Disc-Daten geprüft. Der Bericht zeigt dazu den
Tonspuren-Verlauf (jedes Bild, in dem sich Tonspuren ändern; die Zeile
„Tonspuren im letzten Bild“ wird jetzt als JSON gelesen, das alte Suchmuster
schnitt sie an der ersten `]` ab).

### Sechster PC-Lauf (15.10.2026, WSL2): erstmals „ALLES OK“

Stand `38b0c6b9`. Formatvergleich 155 Dateien ohne Fehler, 10.000 Bilder
ohne Absturz. Der Funkspruch endet von selbst: Spieler frei bei Bild 3043
(2428 nach dem Raumstart), danach lief Leon mit der Test-Tastenfolge 10409
Einheiten (Steuerung OK). „Funkarchive“ bestätigt die Ursache: in
`op/op01.das` tragen die 13 Ablauf-Tabellen das Kennzeichen `SEQ` und
liefen vorher durch den Umwandler der Bewegungs-Ereignislisten.

### Zweiter Gerätelauf mit GX (15.10.2026, Retroid) und Analyse

Stand `38b0c6b`: Die App startet und zeigt das Intro über die
GX-Nachbildung (Capcom-Logo, Autofahrt r120s00 mit Leon). Der Lauf endete
nach den 120 s des Skripts (App lief bis zuletzt, Ereigniszähler bei
1856/1970) – kein Absturz. Gemeldet: Debug-Anzeigen, kein Vollbild, sehr
langsam (1–2 Bilder/s sichtbar), Renderfehler (Helligkeit, Leons Haare).

- **Geschwindigkeit:** Das Protokoll zeigt etwa 20 Bildstücke/s mit je
  rund 9000 Zeichenaufträgen; der Ereigniszähler lief mit etwa 20 statt 30
  Bildern/s. Sichtbar waren weniger, weil die App je Anzeige bis zu 8
  Bildstücke abarbeitete und nur das letzte zeigte. Jetzt: ein Bildstück je
  Anzeige (jedes Bild des Spiels wird gezeigt; ist der Renderer langsamer,
  wartet der Kern). Der Renderer überspringt, was GL schon hat:
  Zustand (Scissor, Tiefe, Mischung, Seite, Linienbreite) nur bei Änderung
  der Register, Shader-Schlüssel nur bei Änderung der formgebenden
  BP/XF-Register, PX-Block nur bei Änderung seiner Register oder
  Texturgrößen, Texturen und Sampler je Einheit, `uMtx` je Programm,
  XF-Block wortweise statt ganz neu umgerechnet, Vertex-Attribute einmal je
  Layout und Stride mit `glDrawElementsBaseVertex` (GLES 3.2/OES/EXT;
  `RE4_GX_BASEVERTEX=0` schaltet es ab). Uploads, Kopien, Paletten und die
  Anzeige setzen die Merker zurück (`Touched`). Prüfung: `gx_gl_check`
  grün mit und ohne Base-Vertex; vier aufgezeichnete Bilder (61, 301, 601,
  652) pixelgleich zum alten Renderer, beide Wege. Auf llvmpipe (rechnet in
  Software) nur 10–15 % schneller, dort dominiert das Rastern; auf dem
  Gerät zählt der Aufwand je GL-Aufruf (vorher etwa 50 je Zeichenauftrag,
  jetzt meist unter 10). Messwerte im Protokoll: App „GX: … run x ms
  (max y), shown z/s, base vertex yes/no“, Kern „GX core: … ms each, of it
  … ms waiting for the renderer“ (Bericht: „GX zuletzt“, „GX im Kern“).
- **Debug-Anzeigen:** `RE4_DEBUG_HUD=0` setzt `pG->debug_mode` auf 0, wie
  die Vorführfassung (`main.cpp`, `SYS_PUBLICITY_VER`): keine
  Ereignis-/Punktezeilen, Leistungsbalken, Heap-Zahlen, Debug-Texte
  (`EprintfDrawing`), kein grüner Fernnebel (`DBG_FOG_FAR_GREEN`). Die App
  setzt es als Standard (`-DebugHud` im Skript zeigt sie wieder), die
  PC-Prüfung behält sie (die Zähler helfen bei der Analyse). Das
  Diagnosefenster der App ist beim Start ausgeblendet (SELECT zeigt es).
- **Vollbild:** Das Spiel zeichnet überall mit Balken; die Zeilen 56–391
  der 448 ergeben auf einem 4:3-Fernseher genau 16:9. Die App zeigt nur
  diese Zeilen in einem 16:9-Feld (`Present(…, top, rows)`);
  `RE4_GX_ASPECT=4:3` zeigt das ganze Bild wie bisher.
- **Haare/Helligkeit:** Ein Teil der dunklen Flächen auf den Bildern war
  der halbdurchsichtige Hintergrund des Diagnosefensters (endet genau dort,
  wo die Haare wieder hell werden). Andere Stellen (Bild bei 95 s) bleiben
  offen; mit doppelter EFB-Auflösung zeichnet llvmpipe dieselben
  aufgezeichneten Bilder wie mit einfacher (unter 2 % Abweichung, nur
  Kanten), der Unterschied entsteht also auf dem Gerät. Zur Analyse
  speichert die App mit `RE4_GX_DUMP_DIR` alle `RE4_GX_DUMP_EVERY`
  Sekunden ein vollständiges Bildstück (sie meldet sich ab, das nächste
  Stück beginnt mit vollem Zustand), höchstens 6; das Skript holt sie mit
  `-GxDump` als `gx-geraet.zip` (nur hochladen, nie ins Repository).
- Skript: Beobachtung standardmäßig 240 s.

Nicht getestet: alles davon auf dem Gerät; `RE4_DEBUG_HUD` ohne eigenen
Test (Wirkung nur im laufenden Spiel sichtbar).

### Dritter Gerätelauf mit GX (15.10.2026, Retroid) und Analyse

Gemeldet (Skriptlauf mit `-GxDump` und ein Spiel von Hand, Intro mit
START übersprungen): leichter Grünton vor dem Menü, Filmintro fehlt,
Laderuckler, gröbere/flackernde Pixel in Leons Haaren, Autoscheiben von
außen zu dunkel, Absturz nach 153 s mitten im Intro (r120s01), kein Ton;
im Spiel ein kräftiger Orangeschleier über allem, flackernde Flächen,
Leon verschwindet kurz beim Wechsel Spiel → Filmszene, Steuerung
anpassen, Speicherkarten-Abfrage beim Start überflüssig.

- **Absturz 153 s (Patch 0040):** Stapel `re4gx::command` ←
  `callDisplayList` ← `drain` ← `re4_gx_put_f32` ← `Esp1b_SpTrans`, davor
  „GX: unknown command 0x3f …“ (Float-Bytes als Befehle): Der Befehlsstrom
  war aus dem Tritt, ein Zufallswert als `CALL_DL`-Adresse führte zum
  Zugriff ins Leere. Ursache: `Esp1b_SpTrans` findet die Daten des
  Unterobjekts über `&esp->pad_EC[0xC]`, also das Ende von `cEsp` unter
  MWCC, das den vptr ans Ende legt (0xF4). Die Compiler des Ports legen
  ihn an den Anfang; die Adresse lag 4 Bytes in den Daten, `div` war eine
  Gleitkommazahl als Ganzzahl, `div * 4 * div` passte nicht mehr in die
  16-Bit-Punktzahl von `GXBegin`. Jetzt `(u8*) esp + sizeof(cEsp)` (in
  beiden Layouts dieselbe Stelle); `game_probe.cpp` prüft
  `sizeof(cEsp) == 0xF8`. Andere Zugriffe dieser Art (`pad_…[]`,
  `(u8*) this + 0x…`) gibt es im Spielcode nicht.
- **Decoder abgesichert:** `listMemory` nimmt Zeiger außerhalb von MEM1
  nur noch an, wenn Anfang und Ende in Spielspeicher liegen
  (`re4_valid_port_ptr`: Bibliotheken, Thread-Stapel); sonst meldet der
  Decoder die Liste als „not in memory“ statt abzustürzen. Beim ersten
  unbekannten Befehl schreibt er die 16 letzten Befehle (Draws mit Anzahl,
  Punktgröße, VCD, VAT) und die Bytes danach ins Protokoll („GX: the
  command stream lost its place …“).
- **Orangeschleier:** Am Bildende legt das Spiel ein Leuchten über das
  Bild: Kopie des EFB-Alphakanals (A8, halbe Größe, dreimal verkleinert,
  verwischt) und additiv mit Eckpunktfarbe (0x28, 0x19, 0x0C) mal 2
  zurück. Das Leuchten soll nur dort entstehen, wo Lichtquellen Alpha
  schreiben; fast alle anderen Zeichenaufträge setzen mit `GXSetDstAlpha`
  (BP 0x42) Alpha 0. Das kannte der Renderer nicht, Alpha blieb 1 und das
  ganze Bild bekam +(80, 50, 24). Jetzt: Mit `GXSetDstAlpha` schreibt der
  Shader die Konstante (PX `alphaRef.z`), wenn die Mischung die TEV-Alpha
  nicht braucht; braucht sie sie (SRC_ALPHA-Faktoren), setzen die
  Alpha-Mischfaktoren den Wert 0 (der Fall des Spiels). Andere Werte dann
  zählt `Stats().dstAlphaApprox` (Protokoll „inexact dst alpha“). Geprüft
  mit den PC-Daten (Bild 652): Bildpunkt (320,100) bleibt nach dem Filter
  `1a1a16` statt `6a4c2e`. Der leichte Grünton von Wolken und Logo vor dem
  Menü gehört zum Original (Titelbild), die Geräteaufnahmen 00–05 haben
  keinen Schleier mehr.
- **Leons Haare:** Die Geräteaufnahmen zeigen die Fehler auch auf
  llvmpipe (also Renderer-Logik, nicht Adreno), aber nur klein: einzelne
  dunkle Strähnen der Unterschicht (`00823100`) liegen über hellen.
  Geprüft und richtig: Texturen (CMPR, I4-Maske), Texturkoordinaten,
  Sampler, Koordinatenskalen, Tiefenformat, Projektion der Baumschatten.
  Die großen schwarzen Rechtecke vom Gerätefoto (70 s) sind in keiner
  Aufnahme; offen. Dafür speichert die App jetzt auf **R3** (rechter Stick
  gedrückt) jederzeit das nächste Bildstück (`CoreBridge.gxDump`, bis zu
  40, Ordner `files/gx-dumps`); `test-core-app.ps1` holt sie immer mit,
  nach einem Spiel ohne Skript `port/scripts/pull-gx-dumps.ps1`.
- **Ruckler:** Neue Shader kosten auf dem Gerät bis zu einige hundert ms.
  Der Renderer legt gelinkte Programme jetzt als Binärdatei ab
  (`SetProgramCache`, Ordner `cache/gx-programs`, Name = Hash beider
  Quelltexte und des Treibers, `RE4_GX_PROGRAM_CACHE`) und lädt sie beim
  nächsten Mal mit `glProgramBinary` statt zu übersetzen; ein abgelehntes
  Binary wird neu gelinkt. Geprüft mit llvmpipe: zweiter Lauf 30 von 30
  Programmen aus dem Speicher, Bild bytegleich. Protokoll: „programs from
  the cache“. Der erste Lauf ruckelt weiterhin.
- **Speicherkarte (Patch 0039):** `CardFirstCheck` überspringt die
  Startprüfung wie nach einem Reset (`re4_skip_card_check`, runtime/
  card.cpp) – derselbe Ausgang wie „ohne Speichern fortfahren“ auf dem
  Bildschirm „keine Speicherkarte“. `RE4_CARD_CHECK=1` behält sie; die
  Test-Tastenfolgen (`whole_r100_check.sh`, `test-core-app.ps1` ohne
  `-Manual`) setzen es, weil ihr Zeitplan darauf abgestimmt ist.
- Filmintro (Meilenstein 9) und Ton (Meilenstein 8) fehlen erwartungsgemäß.
  Steuerung: Meilenstein 7. Offen ohne Daten: Leon verschwindet kurz beim
  Szenenwechsel, flackernde Flächen, dunkle Autoscheiben (die Aufnahmen mit
  R3 an genau diesen Stellen helfen).

Getestet (Cloud): Kern-Build i686/armhf normal und `-O2` mit allen
Prüfungen (siehe unten), `gx_gl_check`, Renderer mit den Geräte- und
PC-Aufnahmen, App-Bibliothek gegen JNI gelinkt (`re4decomp_link_check`),
Java der App übersetzt, beide PowerShell-Skripte mit einem nachgebildeten
ADB. Nicht getestet: alles auf dem Gerät, die PC-Prüfung mit Disc-Daten.

### Vierter Gerätelauf mit GX (15.10.2026, Retroid) und Analyse

Stand `45205aa`: Der Skriptlauf lief die vollen 240 s ohne Absturz durch
das Intro bis r100 (der Absturz nach 153 s ist weg). Gemeldet aus einem
Spiel von Hand: Pixelflecken in Leons Haar (heller Hinterkopf), zu dunkle
Glasscheiben, „CLIMB DOWN“ an Geländern und Fenstern (danach kurz keine
Steuerung), kein Sprung durch Fenster, durchlaufbare dünne Bäume, ein
Ganado springt durch die Hüttenwand, Ganados sterben „wie ins Wasser“,
falsche Ganado-Reaktionen, Logo im Hauptmenü verblasst, START im Menü
zweimal nötig, Schatten am Fenster in einer Zwischenszene fehlt, Leon
verschwindet kurz vor Filmszenen.

- **Kollision (Patch 0041):** `Get_poly_attr` (at_sub.cpp) liest das
  Attributwort einer Kollisionsfläche als zwei Halbwörter, das obere zuerst.
  Im Port liegt `SAT_POLY::m_Status` als Host-Wort vor (umgewandelte SAT/EAT,
  `createSat`), die Hälften kamen vertauscht an: aus Zaun (0x20) wurde „hoch“
  (0x200000), aus „Spieler kollidiert nicht“ (0x400000) „Route“ (0x40),
  „Fallkante“ (0x100000) und „runter“ (0x10) tauschten, bei EAT die
  Effektbits (Wasser-Spritzer beim Tod). `Get_poly_attr` liest jetzt das Wort
  (auf dem GameCube derselbe Wert), ebenso die Debug-Anzeige in atari.cpp.
  Das erklärt „CLIMB DOWN“ statt „JUMP OVER“, durchlaufbare Bäume, Ganados
  durch Wände und den Wasser-Tod; nicht auf dem Gerät geprüft.
- **Weitere schmale Sichten** über Zeiger statt `&x` (das Suchskript von
  M4.5 fand nur `&x`): alle 21 Stellen durchgesehen. Richtig waren
  `motion.cpp` (Byte 2 ist das u8-Flag der Sequenzdatei), die EMI-Bytes
  (`EmiEntry::type`), `id_sys.cpp` (`FUNC_PATH_PTR::nPoint` ist ein Byte),
  die Debug-Werkzeuge `t_flag`/`t_emlist`. Korrigiert: `card.cpp`
  `CardID::save` (Byte 3 von `HERMITE_1_PTR::nPoint`, Patch 0041). Offen für
  später: `r117.cpp` liest Byte 0xF9 eines Effekts (Raum r117).
- **Alphakanal ohne Farbe:** Rund 1100–1900 Zeichenaufträge je Bild
  schreiben mit `GXSetDstAlpha` nur Alpha (Farbe aus, Werte 0x33–0x96, eine
  Maske für spätere Filter). Bisher galten sie als „ungenau“ und schrieben 0
  (Protokoll: 4709 „inexact dst alpha“); ohne Farbschreiben schreibt jetzt
  der Shader den Wert genau.
- **Leons Haare im Spiel (Patch 0042):** Leons Haar besteht aus
  Doppelschichten: jede Strähne zweimal mit denselben Punkten, die Innenseite
  mit nach innen zeigenden Normalen und umgekehrtem Umlauf (Bild 4744: Haar
  16451–16602, z. B. 16517 innen / 16536 außen). Auf dem GameCube lässt
  `GX_CULL_FRONT` jeweils die abgewandte Schicht weg. Im Port hatte **kein**
  Zeichenauftrag Rückseiten-Weglassen (BP 0x00 Bits 14–15 überall 0):
  `GXSetCullMode` verwarf das Ergebnis von `__rlwimi`. Beide Schichten wurden
  gezeichnet; wo die Strähnen nicht ganz eben liegen, gewann die dunkle
  Innenseite die Tiefenprüfung (dunkle Rechtecke, flackernd bei Bewegung).
  Geprüft mit der Geräteaufnahme: mit Rückseiten-Weglassen (Hardwarewert 2,
  wie `GX_CULL_FRONT`) ist das Haar glatt und hell wie im Original, die
  Szene sonst unverändert; mit Wert 1 stünde alles „von innen“ da (die
  Richtung im Renderer stimmt also). Ebenso verschwinden die Blöcke in den
  Stirnsträhnen der Autoszene (Bild 1160). Der Test `gx` prüft jetzt alle
  vier Werte. Betrifft vermutlich auch zu dunkle Scheiben und flackernde
  Flächen (doppelt gezeichnete Rückseiten), Schatten auf Modellen
  (`shadow.cpp` nutzt ebenfalls `GX_CULL_FRONT`) und Spiegel; auf dem Gerät
  zu prüfen. Die Stoff-Simulation (`pendulum.cpp`) ist nicht die Ursache
  (Mathe-Ersatz, Strukturlage `cParts`/`PEN_INFO` geprüft).
- **Titel:** „START zweimal“ und das verblassende Logo sind ohne Aufnahme
  des Titelbilds offen.

Getestet (Cloud): Kern-Build i686/armhf normal und `-O2`, alle Prüfungen
grün (Laufzeittest `gx` mit der neuen Prüfung der Rückseiten-Werte,
Formatvergleich, Modul- und Kerntests); Geräteaufnahmen 4744 und 1160 mit
erzwungenem Rückseiten-Weglassen im Renderer nachgezeichnet. Nicht
getestet: Gerät und PC-Prüfung mit Disc-Daten.

### Fünfter Gerätelauf mit GX (15.10.2026, Retroid) und Analyse

Stand `880ef9a` (Patch 0042). Bestätigt vom Projektinhaber: Haare und
Glasscheiben jetzt richtig, Gegner verhalten sich wie im Original. Der Lauf
reichte von Hand bis zum Übergang nach r101 (Dorf) und stürzte dort nach
579 s ab. Gemeldet: kurzes Flackern zwischen Szenen der Autofahrt,
„Spiegelungen“ rechts neben den Gesichtern im Funkgerät, Leon in einem
Baumstamm, Leon verschwindet vor einer Zwischensequenz und danach steht kurz
der leere Raum da (Video), Logo im Titel verblasst.

- **Absturz 579 s:** in der Zustandsbrücke (`core_bridge.cpp`
  `captureModel`, Diagnose-Teil des Ports, nicht Spielcode) beim Raumwechsel
  nach r101: `live()` ließ jede Adresse ab 0x80000000 gelten, eine
  Modell-Info zeigte beim Abbau des Raums auf 0xabc28685. Jetzt nur MEM1 oder
  Spielbibliothek (`re4_valid_port_ptr`).
- **Leon verschwindet / leerer Raum (Video, je 4 Bilder):** Beim Start und
  Ende einer Zwischensequenz setzt das Spiel `SYS_SCREEN_STOP`
  (`event.cpp`, Raumskripte). `Render_swap` (`main_sub.cpp`) zeichnet dann
  weiter, ruft aber `VISetNextFrameBuffer` nicht auf: Der Fernseher zeigt das
  letzte Bild weiter, während das Spiel Figuren ausblendet und die Kamera
  umsetzt. Der Port zeigte jede Display-Kopie sofort. Jetzt wartet ein
  Bildstück mit Display-Kopie bis `VIFlush` (bzw. bis zur nächsten Kopie);
  schaltet der VI nicht auf seinen XFB um, trägt es `RE4_GX_CHUNK_HOLD`: Die
  App führt es aus (EFB, Löschen, Texturkopien), behält aber das sichtbare
  Bild (`GxStats::heldCopies`, Protokoll „held pictures“). Prüfungen: Test
  `gx` (gehaltenes und danach gezeigtes Bild), `gx_gl_check`. Erklärt
  vermutlich auch das Flackern zwischen den Szenen der Autofahrt (jede
  Ereignis-Szene beginnt und endet so); nicht auf dem Gerät geprüft.
- **Logo im Titel:** Aufnahmen 787/846 (Titelmenü) gegen 599: Alle Teile
  des Logos tragen dieselbe Alpha-Kurve der Id-Animation; die Werte springen
  (0xC3 → 0x8D → 0x3A, ein Teil 0x9C → 0x71 → 0xFC). Das passt zu
  überlaufenden Umwandlungen `(u8)(f32)` in `idSysMove02`
  (`u->col[3] = (f32)(u8)(...)`) bei Kurvenwerten über 255, die das Original
  bei 255 abschneidet (siehe Compiler-Schalter oben). Behoben durch
  `-fno-strict-float-cast-overflow`; nicht auf dem Gerät geprüft. „START
  zweimal“: Das Menü wählt mit A (`titleMenuSelect`); offen bis zur
  Tastenbelegung (Meilenstein 7).
- **Funkgerät:** Die Aufnahme 7483 zeigt die Gesichter im Renderer wie auf
  dem Gerät, mit waagerechten Schlieren rechts neben den Köpfen
  (Bildstörungs-Effekt der Übertragung). Ob das Original sie so stark zeigt,
  ist offen; nach der Umwandlungs-Korrektur neu ansehen.
- **Baum:** Kollision läuft über die SAT-Polygone (`atari.cpp`
  `polySphereCk`, Blockbaum, `At_poly_sphere_ck`); Umwandler und Strukturen
  geprüft, ohne Befund. Die App schreibt bei **R3** jetzt zusätzlich Raum,
  Spielerposition und Kamera ins Protokoll („R3: room …, player at …“); mit
  der Position lässt sich die Stelle am PC nachstellen (`RE4_TEST_PLACE`).

Getestet (Cloud): Kern-Build i686/armhf normal und `-O2` mit allen Prüfungen
(neu: Begrenzung der Umwandlung im Test `math`, gehaltenes Bild im Test `gx`),
App-Renderer `gx_gl_check`. Nicht getestet: Gerät, PC-Prüfung mit Disc-Daten.

### Sechster Gerätelauf mit GX (15.10.2026, Retroid) und Analyse

Stand `1c16ab6`. Bestätigt vom Projektinhaber: Logo im Titel jetzt richtig,
r101 (Dorf) erreichbar. Gemeldet: im Titel zweimal START nötig, nach dem
zweiten Druck ruckt das Bild kurz nach rechts; manchmal ein leicht
abgedunkelter Balken unten (Intro am Fenster, oft wenn Leon getroffen wird);
Karte (Inventar oder R1) bleibt schwarz; r101 ruckelt stark; nach dem Tod und
„nicht weiterspielen“ bleibt das Bild schwarz; `test-r100-core.ps1` fragte
nach `DataDir`.

- **Zweimal START = Raumauswahl (Patch 0043):** Nach „New Game“ zeigt die
  Debug-Fassung ein Startmenü (`title.cpp` `titleExit`/`titleDebugMenu`:
  Stage, Raum, Sprungpunkt, Gegnerliste …) und wartet auf A oder START.
  Seine Zeilen sind Debug-Text (`eprintf`), den die App mit
  `RE4_DEBUG_HUD=0` ausblendet: Das Menü war unsichtbar, der zweite Druck
  bestätigte es. Auf Wunsch des Projektinhabers bleibt es (zum Testen
  einzelner Räume) und ist jetzt sichtbar: Patch 0043 meldet das offene Menü
  an die Laufzeit (`re4_title_room_menu`, runtime/dolphin_os.cpp), die
  Debug-Anzeigen-Sperre (`state_trace.cpp`) lässt `debug_mode` dann
  mindestens 1. `RE4_ROOM_MENU=0` überspringt das Menü wie ein sofortiges A
  (Zeile 0 „DEBUG GAME“ mit dem voreingestellten Raum). Bedienung:
  Steuerkreuz hoch/runter wählt die Zeile, links/rechts ändert den Wert
  (STAGE, ROOM, JUMP_POINT), A oder START startet; Zeile 1 lädt, Zeile 2
  startet r120, Zeile 18 (Wert 0x12) schaltet „hard mode“ mit Neustart; der
  rechte Stick (C-Stick) verschiebt das Menü (die Kopfzeile mit dem
  Raumnamen liegt bei y=44, über dem 16:9-Ausschnitt der App).
  Das Bild-Rucken nach dem Bestätigen: Dort beginnt im Original das
  Eröffnungsvideo (Meilenstein 9); nicht weiter untersucht.
  **Seit 17.10.2026 (Wunsch des Projektinhabers) überspringt die App das
  Startmenü** (`core_mode.cpp`: Voreinstellung `RE4_ROOM_MENU=0`): „New
  Game“ startet das Spiel in r100 wie ein sofortiges A; Räume wählt man im
  Debug-Menü im Spiel (SELECT halten). `test-core-app.ps1 -StartRoom` und
  die PC-Prüfung setzen `RE4_ROOM_MENU=1` weiter ausdrücklich.
- **Kurze Tastendrücke:** Die App gab dem Spiel den Tastenstand im Moment von
  `PADRead`; ein Druck kürzer als ein Spielbild (bei Rucklern länger) ging
  verloren. `pad.cpp` merkt sich jetzt Drücke seit dem letzten `PADRead` und
  meldet sie einmal (Test `core-test` Teil `thread`).
- **Tod → schwarz:** „nicht weiterspielen“ setzt `SYS_SOFT_RESET`
  (`game.cpp`), `systemSoftReset` (`main_sub.cpp`) schaltet das Bild schwarz,
  bricht Lade- und ARAM-Aufträge ab, räumt die Heaps und kehrt zum Titel
  zurück (liest `SS/eng/title.dat` neu). Die PC-Prüfung löst diesen Neustart
  jetzt aus (`RE4_TEST_SOFT_RESET="free+5000"`, state_trace.cpp) und meldet,
  ob der Titel neu geladen wird („Neustart nach Spielende“). Ergebnis steht
  aus (PC-Lauf mit Disc-Daten).
- **Karte schwarz:** Die PC-Prüfung öffnet die Karte mit Z (`RE4_PAD_FREE_SCRIPT`
  `Z@30 B@300`) und schreibt die GX-Daten 200 Bilder nach dem Spielstart
  (`RE4_GX_DUMP_FREE`, core_api.cpp); der Bericht zeichnet sie
  („Karte offen“). Ergebnis steht aus.
- **Dunkler Balken unten:** Verdacht auf die Filter 00/01 (Kontrast,
  Tiefenschärfe), die das EFB in halber Größe kopieren
  (`GXSetTexCopyDst(SCR_W/2, SCR_H/2, …)`) und zurückzeichnen. Braucht eine
  R3-Aufnahme in dem Moment.
- **r101 ruckelt / Zeichenaufträge zusammengefasst:** Das Gerätebild 9131
  (r100, Wald, `gx-geraet-03.bin` aus dem fünften Lauf) hat 15 931
  Zeichenaufträge mit im Schnitt 7 Dreiecken: Der Decoder machte aus jedem
  `GXBegin` einen eigenen GL-Aufruf. 15 465 davon folgen direkt auf einen
  anderen mit gleichem Aufbau (keine Zustandsänderung dazwischen).
  `gx_decode.cpp` `draw()` hängt so einen Auftrag jetzt an den vorigen an
  (gleiche Primitivklasse, gleicher Punktaufbau und Standardmatrizen,
  höchstens 65 536 Punkte, Reihenfolge der Dreiecke bleibt): 466 Aufträge
  statt 15 931, im Autofahrt-Bild 7483 140 statt 2 214. Die vier Gerätebilder,
  so zusammengefasst und mit dem App-Renderer gezeichnet, sind pixelgleich
  mit den ursprünglichen. Auf llvmpipe (Software) kaum schneller, dort
  zählt das Rastern; auf dem Gerät zählt der Aufwand je GL-Aufruf. Wirkung
  auf dem Gerät nicht gemessen. Für r101 selbst fehlt noch das Protokoll;
  die App-Prüfung lässt sich jetzt mit Q beenden und sammelt trotzdem alles
  ein.
- **Werkzeugmenü (L + START im Spiel):** Die Debug-Fassung öffnet mit
  gehaltenem L und START das „TOOL MENU“ (`db_menu.cpp`: AREA JUMP, FLAG
  EDIT, DEBUG OPTION → PLAYER mit INF BULLET, NO DEATH, WEAPON, LIFE …) und
  hält das Spiel an. Auch das war Debug-Text und damit unsichtbar (das
  Spiel stand still, ohne dass etwas zu sehen war). Die Debug-Anzeigen-Sperre
  lässt `debug_mode` jetzt in Ruhe, solange `DBG_TEST_MODE` gesetzt ist; das
  Menü setzt beim Schließen die alte Seite (0) selbst zurück. Nicht getestet.
- **Skripte:** `test-r100-core.ps1` nimmt `DataDir` standardmäßig aus dem
  Nachbarordner `RE4-Discdata`; `test-core-app.ps1` beendet die Beobachtung
  mit Q oder Strg+C und sammelt danach Protokolle und Aufnahmen ein, der
  Bericht zeigt die R3-Zeilen; `-Scale 1..4` wählt die interne Auflösung
  (`RE4_GX_SCALE`; 2 = Standard, sichtbar etwa 720p, 3 = etwa 1080p).

Getestet (Cloud): Kern-Build i686/armhf normal und `-O2` mit allen Prüfungen
(neu: kurzer Tastendruck im Test `core-test`, Debug-Seite bei offenem
Startmenü im Test `trace`, zusammengefasste Aufträge im Test `gx`),
Bildvergleich der zusammengefassten Gerätebilder. Nicht getestet: Gerät,
PC-Prüfung mit Disc-Daten (Karte, Neustart nach Spielende), das sichtbare
Raumauswahl- und Werkzeugmenü.

### Siebter Gerätelauf mit GX (16.10.2026, Retroid) und Analyse

Stand `06dead6` (zusammengefasste Zeichenaufträge, sichtbares
Raumauswahl-Menü), interne Auflösung `-Scale 3` (etwa 1080p). Start über das
Raumauswahl-Menü direkt nach r100. Absturz nach 105 s beim Untersuchen der
Totenköpfe unter der Treppe. Laut Projektinhaber funktioniert dasselbe beim
normalen Start (über r120); die Karte (L1/R1) bleibt weiterhin schwarz und
lässt sich nicht schließen (nicht in diesem Protokoll).

- **Leistung in r100 bei 1080p:** Kern 33,9 ms je Bild (0,7 ms Warten auf
  den Renderer), Renderer 6,1 ms je Bild bei 332 Aufträgen (vorher 10–15 ms
  bei 8 000–15 000 Aufträgen und doppelter statt dreifacher Auflösung);
  einzelne Spitzen bis 216 ms beim ersten Auftreten neuer Shader.
- **Absturz (Patch 0044):** `r100_mes_gaikotu` → `SceUpCut` → `CutCall` →
  `switchCamera` → `delete m_pProc` mit Fehleradresse 0x0909090d: der
  vtable-Zeiger des Kamera-Objekts war 0x09090909, das Füllmuster aus
  `CameraControl::roomInit` (`memset(m_Free, 9, …)`). `roomInit` setzt
  `m_pProc = NULL`, ruft dann `Check()` – das bei `d.y <= 500` (Hüfte nicht
  über den Füßen, beim Direktstart noch ohne berechnete Teile) die
  Fall-Kamera `CameraLookAt` in `m_Free` baut – und füllt erst danach
  `m_Free`. Damit zeigte `m_pProc` auf ein überschriebenes Objekt; der
  nächste Kamera-Schnitt mit Kamerafahrt (Typ 6/7) löschte es. Auf dem
  GameCube wäre das genauso abgestürzt; dort trat es offenbar nicht auf
  (beim normalen Start läuft `roomInit` nicht in diesem Zustand). Patch 0044
  füllt `m_Free` vor `Check()`. Diagnose: „run: camera after the room start
  …“ (state_trace.cpp), im PC-Bericht „Kamera:“, im Gerätebericht „Kamera
  nach dem Raumstart“.
- **Karte schwarz, nicht schließbar:** Die Karte liest beim Öffnen
  `SS/<Sprache>/ss_map.dat` (`SsMapInit::move`, Zustand 2/3) und wartet auf
  das Ende des Lesens; erst danach reagiert sie auf B/Z. Ein Lesen, das nie
  fertig wird, erklärt beides. Neu im Protokoll: „run: sub screen open/closed
  at frame …“; die PC-Prüfung meldet „Untermenüs“, „Kartendatei“ und ein am
  Ende noch offenes Untermenü, der Gerätebericht ebenso. Ursache noch offen.

Getestet (Cloud): Kern-Build i686/armhf normal und `-O2` mit allen
Prüfungen; Berichtszeilen der PC-Prüfung mit einem nachgebauten Protokoll.
Nicht getestet: Gerät, PC-Prüfung mit Disc-Daten.

### Achter Gerätelauf (16.10.2026, Retroid): Karte

Stand `9f41b98` (Patch 0044). Intro übersprungen, Karte sofort geöffnet
(auf dem Gerät kam die Taste als GameCube-Z an). Kurz erscheinen die Anzeigen
und der gelbe Kartenrahmen, dann bleibt das Bild schwarz; B schließt nicht.

- **Protokoll:** Untermenü offen ab Bild 1469 und nie geschlossen. Gelesen
  wurden `SS/eng/ss_map.dat` und `SS/cmn/map_obj1.dat`; dazu „archive entry
  "MAP" … no converter, left big-endian“. Danach zeichnete das Spiel nur noch
  5 Aufträge je Bild (ein schwarzes Vollbild, Aufnahme Bild 1769), der Kern
  lief weiter mit 33,9 ms je Bild. Kamera nach dem Raumstart: „none“ (Patch
  0044 nicht berührt).
- **Ursache:** Die Einträge „MAP“ sind die Räume der Karte (Sscrn
  `ss_map.cpp` `mapRoomNum` / `mapBinAddr` / `mapHitAddr`): u32 Anzahl
  (Modelle + 2), drei Wörter, die die Karte nicht liest, dann die Offsets der
  Modelle (BIN) und der zwei Bodenkollisionen (`cSatHeader`). Unumgewandelt
  liest `mapModelInit` eine Modellanzahl in Millionen und Modelle aus
  big-endian-Daten. Neuer Umwandler `convertMapRoom` (formats_scene.cpp,
  Regel „MAP“ in formats_arc.cpp): Kopf und Offsets tauschen, Modelle mit
  `convertBin`, Böden mit `convertSat`; unplausible Anzahl/Offsets bleiben
  unverändert und werden protokolliert.
- **Prüfung:** Formatvergleich mit einem synthetischen Kartenraum (ein Modell,
  zwei Böden; App-Leser gegen Kern-Umwandler, 154 Werte gleich) und als
  Datei `SS/cmn/map_obj1.dat` im synthetischen Disc-Ordner; mit echten
  Disc-Daten vergleicht `re4_format_compare --data` jetzt auch alle
  `SS/cmn/map_obj*.dat` (lokal, steht aus). Ob die Karte damit erscheint und
  sich schließen lässt, zeigt erst der Gerätelauf oder die PC-Prüfung
  („Untermenüs“, „Karte offen“).

### Neunter PC-Lauf (16.10.2026, WSL2, Stand `c6cd51f`)

- **Karte:** geöffnet mit Z bei Bild 3074, mit B bei 3355 wieder
  geschlossen; das GX-Bild der offenen Karte (Bild 3244) hat 135 Aufträge,
  4 286 Dreiecke und 28 Texturen (vorher auf dem Gerät: 5 Aufträge, schwarz).
  Mit dem Umwandler `convertMapRoom` öffnet und schließt die Karte also am
  PC; auf dem Gerät nicht geprüft.
- **Formatvergleich:** 155 Dateien ohne Fehler; die 18 `SS/cmn/map_obj*.dat`
  fehlten („kein DVD-Container mit Archiv an 0x400“): ihr Archiv ist der
  erste Teil der Kopftabelle, nicht der zweite wie bei `.drs`. Der Vergleich
  nimmt jetzt ersatzweise den ersten Hauptspeicher-Teil (`drsBody`), geprüft
  mit einem synthetischen Container dieses Aufbaus.
- **Soft-Reset (Tod → „nicht weiterspielen“) hängt (Patch 0045):** Stillstand
  nach `--SOFT_RESET START!!`; der laufende Kontext stand in
  `SndSoftReset` → `Snd_iss_control` → `Snd_dev_voice_ck`. `SndSoftReset`
  wartet in einer leeren Schleife, bis die Ton-Abspieler im Audio-Interrupt
  den Reset bestätigen (`Snd_soft_reset_ck`). Der Port liefert Interrupts
  nur an Wartepunkten aus; in dieser Schleife gab es keinen. Patch 0045 ruft
  darin `re4_wait_interrupt()` auf (wie Patch 0012 für die VSync-Schleife).
  Das erklärt den schwarzen Bildschirm auf dem Gerät nach dem Tod
  (`systemSoftReset` schaltet vorher auf Schwarz). Ob danach der Titel
  zurückkommt, zeigt der nächste PC-Lauf („Titel nach dem Soft-Reset“).

### Zehnter PC-Lauf (16.10.2026, WSL2, Stand `a9b1f20`)

- **Soft-Reset:** mit Patch 0045 läuft er durch („--SOFT_RESET END!!“ ein
  Bild später), das Spiel liest `title.dat` neu und steht am Ende im Titel
  (Raum 0x120, Schritt 0). Die Prüfung meldete trotzdem „Ziel nicht
  erreicht“, weil sie das Ende in r100 erwartete; nach dem angeforderten
  Soft-Reset gilt jetzt das Ende im Titel (die Rückkehr prüft der
  Soft-Reset-Abschnitt).
- **Kartendateien im Formatvergleich:** wieder „FEHLT“; die Karte liest
  `SS/cmn/map_objNN.dat` ohne Teile-Tabelle (`DvdReadN` ohne Kopf-Bit), die
  Datei ist selbst das Archiv. Der Vergleich nimmt sie jetzt so (die
  Container-Annahme vom neunten Lauf ist zurückgenommen).
- **Dunkle Fläche am Boden (Projektinhaber, GX-Bilder 901 und 922):** In der
  Draufsicht auf das Auto (Ereignis r100s40, Schnitt 1) liegt eine
  scharfkantige, gleichmäßig dunkle Raute auf dem Boden. Diese Bilder haben
  13 statt 9 EFB-Kopien: der weiche Schatten (`shadow.cpp`
  `make_shadow_texture`: Umriss mit konstantem Ziel-Alpha ins EFB, Kopie als
  A8 → I8, `MakeSoftShadow` mit weiteren Kopien) wird auf den Boden
  projiziert. Eine gleichmäßig dunkle Fläche heißt, die Schattentextur ist
  überall „Schatten“ statt nur im Umriss. Ursache offen; die PC-Prüfung
  legt Bild 901 jetzt als Daten bei (`RE4_GX_KEEP`, Standard 901).

### Elfter PC-Lauf (16.10.2026, WSL2, Stand `352677d`): „ALLES OK“

- Formatvergleich 173 Dateien ohne Fehler, darunter alle 18
  `SS/cmn/map_obj*.dat` (die Räume der Karte, Umwandler `convertMapRoom`);
  Soft-Reset kehrt zum Titel zurück; Karte öffnet und schließt.
- **Dunkle Fläche am Boden (Bild 901/922) geklärt:** Mit den Daten von Bild
  901 schrittweise nachgezeichnet. Der Schattendurchgang (`shadow.cpp`
  `make_shadow_texture`) schreibt nur das Ziel-Alpha der Werfer (hier 0xAD
  unter dem Auto) in einen 256×256-Bereich des EFB und verlässt sich darauf,
  dass dort sonst Alpha 0 steht: Jede Display-Kopie des Spiels löscht das EFB
  mit der Löschfarbe Schwarz, Alpha 0 (`bio4_GXSetCopyClear`), bei
  eingeschaltetem Alpha-Schreiben. Der Renderer legte das EFB beim Start aber
  mit Alpha 255 an; ein einzeln gezeichnetes Bild (PC-Prüfung,
  `gx_chunk_render`) hatte deshalb im ganzen Lichtkegel „Schatten“. Jetzt
  beginnt das EFB mit Alpha 0 (`gx_gl.cpp` `Init`). Bild 901 zeigt den Boden
  danach normal; die übrigen Bilder sind gleich bis auf die Randzeilen des
  Auto-Innenraums (Bild 652, 1 %, ebenfalls Alpha-abhängig). In der App
  laufen die Bilder hintereinander, dort löscht schon die Display-Kopie des
  Vorbilds das Alpha; auf dem Gerät war der Fleck daher vermutlich nicht zu
  sehen (nicht geprüft).

### Neunter Gerätelauf (16.10.2026, Retroid, Stand `490b78d`)

Normaler Start (Raumauswahl-Menü nicht benutzt), `-Scale 3`. Karte (Z) öffnet
und schließt jetzt auch auf dem Gerät. Im Menü „FILES“ flackert es laut
Projektinhaber kurz beim Öffnen und danach im Hintergrund. Absturz nach 139 s
beim Sprung aus dem Fenster.

- **Absturz (Patch 0046):** `CameraMove` → `CameraControl::Check` →
  `checkAttachCamera` → `delete m_pProc`, Fehleradresse 0x0909090d. Kamera nach
  dem Raumstart „none“, also nicht der Fall von Patch 0044. Allgemeine Ursache:
  Die Kamera-Unterklassen füllen ihr Objekt im Destruktor mit
  `memset(this, 9, 0x200)`; `CameraControl` löscht eine beendete Kamera, lässt
  `m_pProc` aber gesetzt und löscht sie beim nächsten Kamerawechsel noch einmal.
  Mit GCC 2.95 war das harmlos: `~cCamera() {}` schreibt den vtable-Zeiger von
  `cCamera` zurück, das zweite `delete` landet im löschenden Destruktor von
  `cCamera` (leerer `operator delete`). Mit clang scheitern beide Schritte:
  Bei leerem Rumpf lässt clang den Schreibbefehl weg (das zweite `delete`
  sprang über 0x09090909), und der löschende Destruktor einer abstrakten Klasse
  ist bei clang ein Haltebefehl (SIGILL). Patch 0046: `~cCamera` übergibt das
  Objekt an `re4_keep_vtable` (runtime/dolphin_os.cpp, tut nichts, für den
  Compiler undurchsichtig), `move()` hat einen leeren Rumpf statt rein virtuell
  zu sein (`cCamera` selbst wird nie angelegt). Prüfung
  `re4probe_camera_double_delete` im Kern-Test (zweimal `delete` wie im Spiel):
  ohne Patch „check failed“, mit Patch bestanden (i686/armhf, normal und
  `-O2`). Das erklärt auch den Absturz beim Untersuchen der Totenköpfe
  (siebter Gerätelauf); Patch 0044 bleibt (schadet nicht). Das
  Debug-Startmenü war nicht die Ursache.
- **Kern-Build auf dem PC abgebrochen (Stand `972a857`):** Die erste Fassung
  von Patch 0046 nutzte eine leere `asm`-Anweisung; `whole_prepare.py` lässt
  Inline-Assembler ohne C-Ersatz nicht zu, CMake brach ab. Das Android-Skript
  meldete „0 failed build steps“ ohne Grund; `whole_build.sh` in der Cloud
  wertete den Abbruch gar nicht aus und prüfte die Programme des vorigen
  Stands. Beide Skripte werten jetzt den Rückgabewert von CMake/ninja aus und
  zeigen in diesem Fall das Ende des Protokolls.
- **FILES-Flackern:** Die R3-Aufnahme (Bild 3423, FILES offen, 161 Aufträge)
  zeichnet einzeln richtig; die Zahl der gehaltenen Bilder blieb während des
  Untermenüs gleich (37). Ein Flackern zeigt sich erst über mehrere Bilder:
  R3 nimmt jetzt eine Folge auf, das Bildstück mit vollem Zustand und die
  11 direkt danach (`core_mode.cpp`, `gx-geraet-NN-KK.bin`; bricht ab, wenn
  ein Bildstück fehlt). `gx_chunk_render` zeichnet ein Bildstück, das direkt
  auf das vorige folgt (nächste Folgenummer und nächstes Bild), mit demselben
  Renderer weiter wie die App (EFB, Kopien und Texturen bleiben, ein
  angehaltenes Bild lässt das vorige stehen); ein Folgebild ohne Vorgänger
  wird übersprungen und gemeldet. Geprüft mit einer nachgebauten Folge aus
  der Aufnahme von Bild 3423 (Folgebilder pixelgleich, Einzelbilder wie
  vorher) und dem Link-Test der App.

### Zehnter Gerätelauf (16.10.2026, Retroid, Stand `9313333`)

Normaler Start, `-Scale 3`, 388 s, am Ende Tod und Soft-Reset zurück zum Titel.
Sprung aus dem Fenster ohne Absturz (Patch 0046 auf dem Gerät bestätigt).

- **FILES-Flackern:** vier R3-Folgen zu je 12 Bildern (Titel, zweimal FILES,
  r101), alle mit `gx_chunk_render` als Folge nachgezeichnet. In den
  FILES-Folgen ändern sich von Bild zu Bild nur Leons Animation und das
  pulsierende „Village Files“; kein gehaltenes Bild, keine Kopien. Das
  Bildschirmfoto des Geräts (Bild 145 s) zeigt dasselbe. Ein Flackern ist in
  den Daten nicht zu sehen; Ursache offen (eine Beschreibung oder ein Video
  vom Projektinhaber hilft).
- **Leistung r101 (Dorf):** Kern 34–38,6 ms je Bild (r100: 33,4, also an
  der 30-Bilder-Grenze), Renderer 21–36 ms je Bild bei 650–920 Aufträgen
  (r100: 7–9 ms bei 400–550), gezeigt 27–41 Bilder/s. Die R3-Folge in r101:
  946 Aufträge, 138 000 Dreiecke, 7,9 MB Punktdaten je Bild und rund 1 200
  Uploads der Uniform-Blöcke (XF/PX) mit `glBufferSubData` (1,7 MB). Mobile
  Treiber kopieren einen Puffer, den die GPU noch liest, bei jedem solchen
  Update.
- **Uniform-Ringpuffer (`gx_gl.cpp`):** Jeder geänderte XF- oder PX-Block
  kommt an eine neue Stelle eines 16-MB-Puffers und wird dort mit
  `glBindBufferRange` gebunden (wie bei Dolphin). Dauerhaft eingeblendet
  (`EXT_buffer_storage`), sonst eine unsynchronisierte Einblendung je Block;
  ein Fence schützt jedes Viertel vor dem Überschreiben.
  `RE4_GX_UNIFORM_STREAM=0` schaltet zurück, `RE4_GX_PERSISTENT=0` nimmt die
  Einblendung je Block. Alle 48 Bilder der Folgen sind in beiden Arten
  pixelgleich zu vorher (llvmpipe).
- **Messungen:** Die App protokolliert je 300 Bildstücke „GX work per chunk“
  (Uniform-Uploads, Programmwechsel, Textur-Bindungen, Zustandswechsel,
  Wartezeiten des Ringpuffers, GPU-Zeit über `EXT_disjoint_timer_query`), der
  Kern in „GX core“ zusätzlich die Leerlaufzeit (Warten auf den nächsten
  Retrace, `re4_idle_ns`), FIFO- und Display-List-Bytes und Punkte je Bild.
  Im Gerätetest misst Taste P 15 s lang mit simpleperf aus dem NDK, wo die
  Rechenzeit hingeht; `port/tools/android_profile.py` ordnet die Proben in
  WSL mit den Bibliotheken des Builds Funktionen zu (`profil-N.txt`).
  `gx_chunk_render` zeigt mit `RE4_GX_WORK=1` die GL-Arbeit je Bild.

### Elfter Gerätelauf (16.10.2026, Retroid, Stand `a30abf2`): Messung in r101

Gerät: Snapdragon mit Adreno-GPU (`libGLESv2_adreno.so`). Normaler Start,
`-Scale 3`, zwei Leistungsmessungen mit Taste P im Dorf (r101).

- **Uniform-Ringpuffer wirkt:** Renderer 4–5,7 ms je Bild bei 500–900
  Aufträgen (vorher 21–36 ms), gezeigt 60 Bilder/s, GPU 8–15,6 ms je Bild
  (Timer-Abfrage), 0 Wartezeiten im Ring, dauerhafte Einblendung aktiv.
- **Der Kern ist die Bremse:** 33,4–37,9 ms je Bild (30 Bilder/s brauchen
  33,3 ms), 4–6 ms Leerlauf. simpleperf: Der Kern-Thread rechnet 80–97 %
  eines Prozessorkerns, davon 68–71 % aller Proben in `libre4_platform.so`,
  nur 4–6 % in der Spiellogik (`libre4_main.so`). Größte Posten: der
  GX-Decoder `draw()` (Punkte umrechnen) 31–35 %, `re4gx::memory` 10–11 %
  (für jedes Attribut jedes Punkts), `bindTextures` 4 %, `describe` 3,5 %
  (Punktformat für jeden Zeichenbefehl neu, für die Größenprüfung sogar
  doppelt), `command`/`drain`/`feed` zusammen gut 7 %, Divisionen 3 %.
  Je Bild: 216–246 KB FIFO, 1 MB Display-Listen, 120 000–148 000 Punkte.
- **Decoder schneller (`gx_decode.cpp`), Ausgabe unverändert:**
  Punktformate je VAT zwischengespeichert (neu nach CP-Schreiben auf VCD,
  VAT, MATINDEX); die Arrays eines Zeichenbefehls einmal in Speicher
  übersetzt (`memorySpan`, gx_fifo.cpp) statt `memory()` je Attribut;
  Komponenten durch je Zeichenbefehl gewählte, spezialisierte Leser
  (Format, Anzahl, Byte-Reihenfolge) direkt in den Punkt geschrieben;
  Indizes und XF-Daten in einem Stück angehängt; `bindTextures` nur nach
  einer Änderung der Textur-Register, einer Palette, einer EFB-Kopie oder
  am Anfang eines Bildstücks. Neuer Test „gx fingerprint“
  (`tests/gx_probe.cpp`): ein zufälliger, fester Befehlsstrom mit allen
  Punktformaten (direkt und indiziert, alle Komponenten- und Farbformate,
  Normalen mit NBT/NBT3, Matrixindizes), allen Primitiven, sofort und in
  Display-Listen, mit Registern, Texturen, Paletten, EFB-Kopien und
  indizierten Matrixladungen; der Hash aller Bildstücke ist der des alten
  Decoders (`02524012c6419af2`, auf i686 und armhf gleich) und wird geprüft.
  Er fand beim Umbau einen Fehler (direkte Normalen mit NBT3).
  `RE4_GX_BENCH=N` misst den Decoder mit einem dorfähnlichen Bild
  (Display-Listen, 120 000 Punkte): i686 `-O2` 2,9 ms statt 8,3–9 ms.
- **Absturzmeldung des Host-Programms:** nennt jetzt Fehleradresse sowie
  `pc` und `lr` mit Bibliothek und Versatz (auf ARM entrollt `backtrace()`
  den Signalrahmen nicht).

### Zwölfter Gerätelauf (16.10.2026, Retroid, Stand `26450c1`)

Laut Projektinhaber läuft r101 jetzt gefühlt so schnell wie r100, für 1080p
in Ordnung. Der dunkle, halbtransparente Balken unten erscheint offenbar nur,
wenn Leon getroffen wird, zu kurz für R3. Das Ergebnis-Archiv war mit den
R3-Folgen zu groß zum Senden (höchstens 5 Dateien).

- **Aufnahme nach einem Treffer:** Der Kern zählt Treffer (`pl_life` sinkt im
  Spiel, state_trace.cpp, Protokoll „run: player hit, life A -> B“,
  `re4_core_player_hits`); die App speichert danach drei einzelne Bilder
  (2, 8 und 16 Bildstücke später, `RE4_GX_DUMP_ON_HIT`, Ordner
  `RE4_GX_DUMP_FOLDER`). Das Testskript schaltet das für einen Treffer ein
  (`-HitDumps`, 0 = aus); der Bericht nennt „Treffer auf Leon“. Nicht auf dem
  Gerät getestet.
- **Kleines Archiv zum Senden:** Das Testskript packt `senden.zip` (Bericht,
  Protokoll, Profile, Absturzberichte, Bilder als JPEG mit 960 Pixel Breite)
  und löscht den entpackten GX-Ordner neben `gx-geraet.zip`; dieses kommt nur
  bis 25 MB dazu.

### Dreizehnter Gerätelauf (16.10.2026, Retroid, Stand `07543f0`): Treffer

Leon wurde gepackt und gewürgt: 38 „Treffer“ (Leben −10 je Bild, ab Bild
1675), danach Tod und „Continue“. Die App speicherte drei Einzelbilder 2, 8
und 16 Bildstücke nach dem ersten Treffer (`gx-geraet-00..02`).

- **Kein Balken in den Aufnahmen:** Nachgezeichnet (auch zweimal
  hintereinander, damit die Kopie des Vorbilds existiert, und mit
  `RE4_GX_SCALE=3` wie auf dem Gerät) zeigen alle drei Bilder keinen dunklen
  Balken. Die Filterkette am Bildende (EFB halb kopiert, auf 128×112 und
  64×56 verkleinert und weichgezeichnet, über das Bild gelegt; das Vorbild
  als Kopie 256×224 zurückgezeichnet) ist auch in Dorf-Bildern ohne Treffer
  da, ist also nicht der Treffer-Effekt. Der Balken blitzt vermutlich im
  Moment des Treffers auf, die Aufnahme kam zu spät.
- **Genauer aufnehmen:** Bei einem neuen Treffer (Leben sinkt nach mehr als
  60 Bildern ohne Treffer) markiert der Kern das gerade entstehende
  Bildstück (`re4_gx_mark`, `RE4_GX_CHUNK_MARK`, gx_decode.cpp) und lässt
  das nächste neu beginnen (RESET). Die App speichert ab dem markierten Bild
  24 Bilder so, wie der Bildschirm sie zeigt (`treffer-NN-KK.bmp`, aus dem
  XFB in Spielgröße zurückgelesen), und ab dem nächsten Bildstück eine Folge
  von 4 Bildstücken als GX-Daten. Einstellungen `RE4_GX_SHOTS_ON_HIT`
  (Kern: wie viele Treffer markieren) und `RE4_GX_DUMP_ON_HIT`; Testskript
  `-HitShots` (Standard 3) und `-HitDumps` (Standard 1). Prüfung im
  Laufzeittest „gx“: markiertes Bildstück, danach eines mit RESET, dann ein
  normales. Auf dem Gerät nicht getestet.
- `gx_chunk_render` zeichnet mit `RE4_GX_SCALE=N` in der Auflösung der App
  und liest das XFB in voller Größe zurück.

### Vierzehnter Gerätelauf (16.10.2026, Retroid, Stand `623afd6`): Balken gefunden

Drei markierte Treffer (zwei Würgegriffe, ein Axthieb), je 24 Bilder vom
Bildschirm (`treffer-NN-KK`), GX-Daten zum ersten Treffer und eine R3-Folge.
Laut Projektinhaber zeigen `treffer-03-00` bis `-03` den Balken und rechts
einen dunkleren Bereich: Nach dem Axthieb liegt ein Nachbild über dem Bild,
ab Zeile 345 (von 448) ist das Bild deutlich dunkler.

- **Ursache:** Mehrere Effekte des Spiels kopieren die Bildzeilen 56–447 halb
  (`GXCopyTex`, 256×196) und lesen davon als Textur 256×168, also genau die
  sichtbaren Zeilen 56–391 (auf dem GameCube liegen die Texel der Kopie
  zeilenweise im Speicher, die kleinere Textur liest die ersten 168 Zeilen):
  das Nachbild bei Treffern, das Hitzeflimmern über den Feuern in r101, die
  Überblendung im Titelbild (Ashleys Gesicht) und bei den Totenköpfen. Der
  Renderer rechnete Texelkoordinaten auf die Texturgröße (168) um, die
  GL-Kopie hat aber 196 Zeilen: Das Bild war um 196/168 gestreckt, und ab
  Zeile 56 + 336·168/196 = 344 kamen die schwarzen Zeilen unter dem Bild
  dazu (gemessen auf dem Gerät: 345).
- **Behoben (`gx_gl.cpp` `BindTexture`):** Hat eine Textur dieselbe Breite
  wie die Kopie an ihrer Adresse, gehen ihre Texelkoordinaten nach der Höhe
  der Kopie. Nachgezeichnet: Titelbild ohne dunklen Streifen, Gesicht an der
  richtigen Stelle; Hitzeflimmern unten heller. Vergleich aller 51
  bisherigen Geräteaufnahmen vorher/nachher: Änderungen nur in Bildern mit
  diesem Kopiermuster. Auf dem Gerät nicht getestet. Vermutlich ist das auch
  das „dunkle Glas“ im Titel (G14).
- Die Treffer-Aufnahme ist im Testskript wieder aus (`-HitShots`,
  `-HitDumps`, Standard 0), weil sie das Spiel kurz ruckeln lässt.

### Abschluss M6/M6b (16.10.2026)

Erreicht und auf dem Retroid geprüft: die App „RE4 Kern“ (armeabi-v7a) startet
den Originalkern, zeigt über die GX-Nachbildung Logos, Titel, das Intro im
Auto, r100 und r101 mit Figuren, Effekten, Anzeigen, Untertiteln,
Untermenüs (Inventar, Dateien) und der Karte; Raumauswahl-Menü, Soft-Reset
nach dem Tod, Leistung bei etwa 1080p (Renderer 4–6 ms, GPU 8–16 ms je Bild,
Kern unter 33 ms nach dem schnelleren Decoder).

Offen, mitgenommen in die nächsten Meilensteine:

- Dunkler halbtransparenter Balken unten bei Treffern: Ursache gefunden und
  behoben (vierzehnter Gerätelauf), Gerätetest steht aus.
- Flackern im Untermenü „FILES“ laut Projektinhaber; in den R3-Folgen und
  Bildschirmfotos nicht zu sehen (Beschreibung oder Video fehlt).
- Titel: START zweimal / Logo verblasst (G14), Leon kurz unsichtbar beim
  Szenenwechsel (G12): beim nächsten Gerätelauf prüfen, ob noch vorhanden.
- Baum-Kollision in r100 (G17, Leon läuft in einen dünnen Baumstamm):
  **gestrichen**, wird nach Entscheidung des Projektinhabers (17.10.2026)
  nicht weiter verfolgt.
- 60 Bilder/s und Grafikoptionen: Meilenstein 11.

### Werkzeuge zur Fehlersuche auf dem Gerät (Stand 16.10.2026)

- **Testlauf:** `port/scripts/test-core-app.ps1 -Manual -Seconds 900 -Scale 3`;
  Ergebnis zum Schicken: `senden.zip` (Bericht, Protokoll, Profile, Bilder als
  JPEG), dazu `gx-geraet.zip` bis 25 MB.
- **Im Spiel:** SELECT = Diagnose-Anzeige (erste Zeile seit M7: zuletzt
  gedrückte Taste mit ihrer GameCube-Taste, gehaltene Tasten, Rohwerte von
  Sticks und Triggern); R3 = 12 aufeinanderfolgende Bilder als GX-Daten; Taste P im
  PowerShell-Fenster = 15 s CPU-Profil (simpleperf, `profil-N.txt`,
  `port/tools/android_profile.py`); `-HitShots N`/`-HitDumps N` = Bilder vom
  Bildschirm und GX-Daten ab den ersten N Treffern auf Leon.
- **In der Cloud:** `gx_chunk_render` (Bau: `port/build/app-host-gl`, siehe
  `whole_r100_check.sh`) zeichnet GX-Daten mit dem App-Renderer, Folgen
  hintereinander, `RE4_GX_SCALE=N` in App-Auflösung, `RE4_GX_WORK=1` mit
  GL-Arbeit und GPU-Zeit; `port/tools/gx_chunk_inspect.py` listet EFB-Kopien
  und Kopie-Texturen (`copies`) oder Zeichenaufträge mit Punkten (`draws`).
- **Kern-Prüfungen:** Laufzeittest „gx“ mit Decoder-Fingerabdruck
  (`02524012c6419af2`) und Markierung; `RE4_GX_BENCH=N` misst den Decoder.

### Lokale Prüfungen für K2

1. `port/scripts/test-r100-core.ps1` (wie M5) zeichnet jetzt zusätzlich
   Bilder: Der Kern schreibt mit `RE4_BRIDGE_SNAPSHOT` bis zu 14
   Schnappschüsse (30, 150, 300, 450, 600 und 750 Bilder nach Beginn der
   Hauptschleife: die Autofahrt des Intros; dann alle 2500),
   `core_snapshot_render` (`port/android-decomp/tools/`) zeichnet sie mit dem
   App-Renderer und den Disc-Daten (Bericht Abschnitt 4, Ordner `bilder`).
   Braucht die Mesa-Pakete aus `whole_setup_linux.sh`.
2. `port/scripts/test-core-app.ps1`: (1) WSL: NDK einrichten, Kern bauen
   (`whole_build_android.sh --source <Windows-Checkout> --into <App>`),
   (2) Windows: `gradlew.bat :app:assembleCoreDebug`, (3) Retroid unter der
   Sperre `Global\Retroid_ADB_Exclusive`: installieren (prüft
   `primaryCpuAbi=armeabi-v7a`), mit Debug-Start r100 und der
   Test-Tastenfolge starten (`-Manual`: Titelbild, selbst spielen),
   Bildschirmfotos bei 15, 40, 70, 95 … s, Protokoll (`logcat` der App ab
   Startzeit), App beenden. Bericht `Desktop\RE4-Kern-App\bericht.txt`.
   Fehlt Disc 1 in der App „RE4 Kern“ (eigener Datenordner, ein Import der
   bisherigen App zählt nicht), startet das Skript die App, bittet um den
   Import und wartet darauf, ohne die App zu beenden.
   Seit 15.10.2026: Das Skript fragt jede Sekunde, ob die App noch läuft.
   Stürzt sie ab, endet die Beobachtung sofort (Bild, Protokoll mit den
   Puffern main/system/crash und allen Zeilen der App-Prozesse, der
   Absturzbericht des Systems aus `dumpsys dropbox` als
   `absturz-system.txt`), und `port/tools/android_symbolize.sh` übersetzt
   die Stapelzeilen in WSL mit den Bibliotheken des Kern-Builds (über die
   Build-ID zugeordnet) in Funktion, Datei und Zeile. Ergebnisse früherer
   Läufe (`logcat.txt`, `absturz-*.txt`) löscht es am Anfang. Anlass: Bei den
   ersten Geräteläufen nach G1–G4 stürzte die App vor 15 s ab, das Skript
   wurde vor dem Protokollschritt beendet, und im Ordner lag noch die
   `logcat.txt` vom Vortag. Geprüft mit nachgebildetem `adb`/`wsl.exe` und
   PowerShell 7 (Absturz nach 4 s erkannt, alter Dropbox-Eintrag
   ausgelassen, Stapelzeile übersetzt).

Ziel des Gerätetests: Titelbild sichtbar, r100 mit Originalablauf (Ereignis
s03, Tor, Hinterhalt). Grenzen dieses Stands: Effekte, Partikel, Schrift,
Menüs und 2D-Anzeigen des Spiels zeichnet die Brücke noch nicht (das braucht
den GX-Ersatz); Modelle ohne Disc-Herkunft (aus gepackten Raumdateien)
erscheinen als „missing“. Ton fehlt (M8).

## M7 Steuerung

### Bestandsaufnahme (16.10.2026, Cloud)

Weg einer Taste bis ins Spiel, Stand M6b:

1. **App, Java** (`CoreActivity`): Android-Tasten und -Achsen → GameCube-Bits,
   Sticks ×100, Trigger 0..255; belegt waren A/B/X/Y nach Android-Code,
   L1 und L2 = L, R1 = Z, R2 = R, SELECT = Overlay, R3 = GX-Aufnahme. Ein
   Zustand für alle Geräte zusammen (ein Gerät ohne Sticks setzte die Sticks
   des anderen auf 0), Achsen ohne Bereichsnormierung, kein Loslassen beim
   Fokusverlust.
2. **JNI** `CoreBridge.pad` → `core_mode.cpp` → `re4_core_pad` (`Re4CorePad`:
   Tasten, Hauptstick, C-Stick, Trigger) → `pad.cpp re4_pad_live`.
3. **Plattform** `pad.cpp PADRead`: Kanal 0 aus Testskript oder App, Drücke
   kürzer als ein Bild bleiben erhalten, `analogA/B` voll bei A/B,
   Kanäle 1–3 „kein Controller“. `PADClamp` und `PADControlMotor` waren
   leer.
4. **Spiel** `src/game/pad.cpp PadRead` (einmal je Bild): `PADRead`, dann
   `PADClamp`; `Joy[0]` mit `on/trg/rel/rep/rep2` (Wiederholung 24/6 und
   18/3 Bilder); **jeder Triggerwert über 0 gilt als L bzw. R**;
   Stick-Richtungsbits ab 30 Einheiten (Winkelsektoren), C-Stick-Bits ab 10;
   über `Key_type_tbl[pSys->pad_type]` (immer Tabelle 0) in das 64-Bit-Wort
   `Key`; analoge Werte nach `Key.stickX…`, `Key.triggerLeft/Right`.
   Verwendung im Spiel: R = Waffe anlegen (`Key.on & 0x10`), L = Messer
   (`0x800`), A = Aktion/Schießen, B = Rennen/Abbrechen (Unten+B =
   Schnellwende, `keyConfigTypeA`), X = Ashley warten/folgen
   (`pl_leon.cpp`), Y = Koffer, Z = Karte (`sscrn.cpp`), START =
   Optionen/Szene überspringen. Analoge Trigger nur in Entwicklerwerkzeugen;
   der C-Stick steuert den Blick (`cam_qfps.cpp` teilt durch 59, den Bereich
   nach `PADClamp`).
5. **Rüttelmotor:** `VibControl` schaltet den Motor je Bild an/aus (schwache
   Stufen als Pulse), über die Spieloption „Vibration“ abschaltbar.

**Befund:** Ohne `PADClamp` kamen die Werte ungeklemmt an: C-Stick bis 100
statt 59 (Blick schwenkte zu weit), Hauptstick bis 100 statt 72, keine
Totzone, und schon ein Triggerrauschen von 1 galt als gedrücktes L/R.

### Entscheidung des Projektinhabers (16.10.2026)

- Gesichtstasten **nach Beschriftung**: Retroid-A = A, B = B, X = X, Y = Y
  (die Buchstaben der Spielhinweise stimmen; B liegt wie beim GameCube links
  unterhalb von A). Schalter im Startmenü der App „Swap A/B and X/Y“ für
  Controller, die die Codes der anderen Anordnung schicken.
- **R2 = R** (Waffe anlegen, analog), **L2 = L** (Messer, analog),
  **R1 = Z** (Karte), **L1 frei**.
- **R3 allein** bleibt die Grafik-Aufnahme (keine Tastenkombination), Taste P
  im Testskript unverändert; SELECT = Diagnose-Anzeige.
- **Vibration** schon in M7.

### Umsetzung (16.10.2026, Cloud)

- **Kern:** `src/lib/Padclamp.c` wird unverändert mitkompiliert (SDK-Einheit
  in `CMakeLists.txt`), das No-op in `pad.cpp` ist entfernt. Totzonen und
  Bereiche damit wie im Original: Sticks 15 je Achse, Hauptstick höchstens 72
  (Achteck, diagonal 40/40), C-Stick 59, Trigger ab 30 (≈12 % Weg), höchstens
  150. `PADControlMotor` merkt den Motor; `PADRead` schreibt je Bild ein Bit
  (`re4_core_rumble`: Bits 0–3 die letzten vier Bilder, Bits 8–31 Bildzahl),
  Protokoll „pad: rumble from frame N“ (höchstens 16). API-Version 9.
- **App:** `CoreInput.java` (ohne Android, Hosttest
  `tests/CoreInputTests.java`, 69 Prüfungen mit einer Java-Kopie von
  `PADClamp`): Belegung wie oben, Zustand je Eingabegerät (Tasten aller
  Geräte zählen, je Stick und Trigger das Gerät mit dem größten Ausschlag),
  Rohwerte wie ein echter Controller (Hauptstick ±101, C-Stick ±92 am
  Anschlag: Mittel echter GameCube-Controller, 0,794 bzw. 0,722 von 127;
  Trigger 0..255, L2/R2 als Taste = voll; digitales L/R erst ab 85 % wie der
  Klick des GameCube-Triggers). `CoreActivity`: Achsen nach ihrem Bereich
  normiert (rechter Stick Z/RZ, sonst RX/RY; Trigger LTRIGGER/BRAKE,
  RTRIGGER/GAS; Steuerkreuz auch als HAT), alles loslassen bei Fokusverlust,
  Pause und abgezogenem Gerät; Vibration alle 16 ms aus den letzten vier
  Spielbildern (Stärke 1–4 von 4 Bildern, mit Amplitudensteuerung 116–255),
  aus, sobald das Spiel steht; Berechtigung `VIBRATE`. Overlay (SELECT):
  zuletzt gedrückte Taste mit GameCube-Taste, Tasten, Rohwerte der Sticks und
  Trigger. Protokoll `RE4Input`: Belegung, jedes Eingabegerät (Name,
  Hersteller-/Produktnummer, Quellen, Achsen mit Bereich und `flat`), jede
  Taste (Android-Code, Scancode → GameCube-Taste), Vibrator.
- **Testskript:** Abschnitt im Bericht mit Belegung, Eingabegeräten, allen
  gedrückten Tasten (Android → GC) und der Vibration.

### Prüfungen (Cloud, 16.10.2026)

| Prüfung | Ergebnis |
|---|---|
| `port/tools/whole_build.sh` (i686, armhf unter qemu), normal und `RE4_OPT=2` | grün; Kern-Test „pad“ (Totzonen, Achteck, C-Stick, Trigger-Schwelle, Motor je Bild) bestanden; 76 Platzhalter wie vorher |
| `CoreInputTests` + alle Java-Hosttests von `tools/test_host.sh` | bestanden |
| `re4decomp_link_check` (native App-Bibliothek, `--no-undefined`) | linkt; 10 JNI-Namen passen zu `CoreBridge` |
| `CoreActivity` mit `javac` gegen nachgebildete Android-Klassen (kein SDK erreichbar) | übersetzt |

Nicht geprüft: echtes Android-SDK/Gradle (nur auf dem PC), PowerShell-Skript
(kein PowerShell in der Cloud), alles auf dem Gerät.

### Gerätetest (16.10.2026, Retroid, Stand `d1794f1`): M7 erreicht

`test-core-app.ps1 -Manual -Seconds 900 -Scale 3`, 200 s gespielt (Titel,
Intro, r100, r101, Würgegriff, Tod). Laut Projektinhaber alle Tasten erkannt,
Vibration funktioniert.

- **Eingabegerät:** „Retroid Pocket Controller“ (Hersteller 0x2022, Produkt
  0x3001, Quellen Gamepad/Joystick/Tastatur), ein einziges Gerät. Achsen:
  X/Y linker Stick, Z/RZ rechter Stick, BRAKE/GAS die analogen L2/R2
  (0..1), HAT_X/HAT_Y das Steuerkreuz (kommt nur als Achse, nicht als
  Taste); L2/R2 schicken zusätzlich Tastencodes. `flat` überall 0 (Totzone
  macht allein `PADClamp`).
- **Tasten:** Die Codes passen zur Beschriftung (BUTTON_A = A usw.), der
  Tausch-Schalter wird nicht gebraucht. Im Bericht: SELECT → Overlay, A, B,
  X, Y, START, R2 → R, L2 → L, R1 → Z, L1 und L3 ohne Funktion.
- **Im Kern angekommen** (174 Tastenwechsel): alle 13 GameCube-Bits, auch in
  Kombination, z. B. L (Messer) und L+A (Stich), R (Zielen, Laser im Bild)
  und R+A (Schuss, Munition 17 → 9), B, Steuerkreuz in alle vier
  Richtungen, Y (Koffer), Z (Karte), X, START. Untermenüs viermal geöffnet
  und geschlossen; „Shake off“ im Würgegriff.
- **Vibration:** Vibrator mit Amplitudensteuerung; das Spiel rüttelte 5×
  (Treffer, Tod).
- **Leistung unverändert:** Kern 33,4 ms je Bild, Renderer 5,7–7,3 ms,
  gezeigt 58–60 Bilder/s.
- Nicht eigens geprüft: wie weit L2/R2 gedrückt werden müssen (das
  Protokoll zeigt nur die digitalen Bits), Gefühl der Totzone (keine
  Beanstandung), R3 in diesem Lauf (unverändert), der Tausch-Schalter.
- „Räume aufgebaut: keiner“ im Bericht betrifft nur die frühere Brücken-Ansicht
  (`-Bridge`), nicht die GX-Ansicht.

## M8 Audio

### Bestandsaufnahme (16.10.2026, Cloud)

- **Vorher:** `runtime/ax.cpp` (K2) bildete die AX-Bibliothek selbst nach
  (Stimmen-Vergabe, Setter, Abspielposition je Audio-Rahmen) und erzeugte
  keine Töne; MIX, SYN, SEQ, AXART, AXFX, AI und ADX waren Platzhalter
  (Gerätelauf M7: 30 Platzhalter aufgerufen, u. a. `AXARTInit`, `MIXInit`,
  `SYNInit`, `SEQRunAudioFrame`, `AXFXReverbHiInit`, `AIInit`).
- **Tontreiber des Spiels** (`snd_*.cpp`, Capcom): `AIInit`, `AXInitEx(1)`
  (Dreifachpuffer), `MIXInit`, `AXARTInit`, `SYNInit`, `SEQInit`,
  `AXSetCompressor(1)`, `AXSetMode` 0 (Stereo) oder 2 (DPL2) je nach Option;
  Stimmen über AX und MIX (Lautstärke, Pan, Surround-Pan, Aux A/B), Musik über
  SYN/SEQ (MIDI mit Wavetables im ARAM), Streams (Sprache, Funk, Musik) als
  ADPCM-Stimmen über einen ARAM-Ring, den `snd_str*.cpp` per DVD/ARQ nachlädt;
  Effekte auf den Aux-Bussen (`snd_efx.cpp`: ReverbHi, ReverbStd, Chorus,
  Delay, ReverbHiDpl2). Scheitert ein `AXFX…Init`, hängt das Spiel keinen
  Effekt ein (trockener Ton).
- **ADX** (CRI) nutzt das Spiel nur für Filme (`sofdec.cpp`, ADXT mono) und
  die DVD-Hilfe `ADXM_ExecMain`: gehört zu M9.
- **SDK-Quellen** in `src/lib`: AX, MIX, SYN, SEQ, AXART reines C; die
  Effekte haben Gekko-Assembler (`reverb_hi.c` DoCrossTalk/HandleReverb,
  `reverb_std.c` HandleReverb, `reverb_hi_4ch.c` HandleReverbDpl2,
  `chorus.c` do_src1/do_src2). Der DSP-Microcode (`DSPCode.c`) ist
  DSP-Maschinencode.
- **App:** Die bisherige App (classic) spielt eigene Mischungen über
  `GameAudio.java` (AudioTrack 48 kHz); im Kernmodus gab es keine
  Tonausgabe.

### Umsetzung (16.10.2026, Cloud)

- **SDK unverändert mitkompiliert:** AX, AXAlloc, AXAux, AXCL, AXComp,
  AXOut, AXProf, AXSPB, AXVPB, DSPCode, mix, syn*, seq, axart*, axfx, delay,
  chorus, reverb_hi, reverb_hi_4ch, reverb_std (`CMakeLists.txt`,
  `RE4_SDK_UNITS`). `runtime/ax.cpp` entfällt. Platzhalter 76 → 27 (Ton nur
  noch ADX, M9).
- **`runtime/dsp.cpp` – der DSP mit dem AX-Microcode:** `DSPAddTask` bootet
  sofort (init-Rückruf), Post 0xBABE0180 + Adresse der Befehlsliste
  (`AXCL.c __AXNextFrame`) führt die Liste aus, danach der resume-Rückruf wie
  beim DSP-Interrupt. Befehle: 0x00 Studio (Startwerte der neun Mischpuffer
  mit den Entknack-Rampen aus `AXSPB.c`; das SDK schreibt jeden Startwert als
  ein 32-bit-Wort über Hi/Lo, so wird er gelesen), 0x07/0x11 Surround des
  letzten Blocks in links/rechts (gleich-/gegenphasig), 0x02/0x03 alle
  Stimmen der PB-Liste je Millisekunde (PB-Updates, Lesen aus dem ARAM:
  DSP-ADPCM mit Rahmenkopf, PCM16 big-endian, PCM8; Ende: Schleife mit dem
  ADPCM-Schleifenkontext, Streams (`type` 1) behalten ihre Historie, sonst
  Stopp am Endpunkt; Abtastraten-Umsetzung: keine / linear / statt der
  ROM-Polyphasenfilter eine 4-Punkt-Kubik; Hüllkurve `ve`, Tiefpass,
  Mischen in L/R/S, Aux A L/R/S, Aux B L/R/S mit Rampen nach `mixerCtrl`,
  Entknack-Werte `dpop`), 0x04/0x05 Aux-Bus zum CPU-Effekt und dessen
  Ergebnis von vor zwei Blöcken in die Hauptmischung, 0x13/0x10 DPL2 auf
  Stereo heruntergemischt, 0x12 Kompressor (nicht nachgebildet, wie bei
  Dolphin), 0x0E Ausgabe (Surround an die CPU, links/rechts 16 bit,
  rechts zuerst). Nicht nachgebildet: ITD (`AXSetVoiceItdOn`), Kompressor,
  die exakten Polyphasen-Koeffizienten (liegen im DSP-ROM, nicht auf der
  Disc). Eine endende Stimme verliert die letzten 1–2 Abtastwerte, die noch
  in der Umsetzung stecken.
- **`runtime/ai.cpp` – AI und Tonausgabe:** 200 Blöcke je Sekunde Spielzeit
  mit den VI-Halbbildern (vorher `re4_ax_field`, jetzt `re4_ai_field`), je
  Block der gerade gestartete in einen Ring (512 ms), dann der
  DMA-Rückruf (AXOut: der DSP mischt den nächsten, der Audio-Rahmen des
  Spiels). `AIReset` setzt wie im SDK nur das Init-Kennzeichen. Leser für die
  App (`re4_audio_read`): hält etwa 60 ms im Ring, passt die Rate um höchstens
  0,5 % an (Drift zwischen Spiel- und Geräteuhr, Schübe der
  Halbbild-Zustellung), springt nach einer Pause der App vor. Protokoll alle
  10 s „audio: … blocks, … ms waiting, … lost, … ran dry, … skips; DSP …
  lists, … voices“ („ran dry“ zählt Aussetzer, nicht das Warten auf den
  ersten Ton). `RE4_AUDIO_WAV=datei` schreibt den Ton zusätzlich als WAV
  (32 kHz stereo), `RE4_AUDIO_WAV_SECONDS=N` begrenzt auf N s.
- **Patch 0047 `axfx-asm-in-c`:** C-Fassungen der fünf Assembler-Funktionen
  (gleiche Schritte, gleiche Reihenfolge, einfache Genauigkeit; `fctiwz` =
  abschneiden, `fctiw` = runden) und das fehlende `return` in
  `AXFXDelayInit` (der Gekko gab in r3 das Ergebnis von `AXFXDelaySettings`
  zurück, das Spiel prüft es). Gefunden beim Übersetzen: Im Original speichert
  `HandleReverb` des hohen Halls (auch der DPL2-Fassung) den weitergerückten
  Zeiger der Vorverzögerung nicht in `preDelayPtr`, sondern in die
  Verzögerungsleitung selbst (Byte 0x1B8); die Vorverzögerung wirkt deshalb
  nur einen Abtastwert lang. Die C-Fassung tut dasselbe (ohne den
  Streu-Schreibzugriff), damit es klingt wie auf dem GameCube.
- **App:** `CoreAudio.java` (eigener Thread, AudioTrack 32 kHz stereo,
  256 Frames je Schreibvorgang; Lautstärketasten = Spiellautstärke),
  gestartet in `onResume`, gestoppt in `onPause`; `CoreBridge.audio` →
  `re4_core_audio` (API-Version 10); Overlay-Zeile „sound: …“; Protokoll
  `RE4Audio`.
- **Prüfskripte:** `whole_r100_check.sh` (PC mit Disc-Daten) nimmt die ersten
  240 s Spielzeit als `ton-<ziel>.wav` auf und zeigt Pegel je 10 s
  (`port/tools/audio_wav_report.py`); `test-core-app.ps1` meldet die
  Tonzähler und die Tonausgabe, `-AudioWav N` nimmt N s auf dem Gerät auf
  (`ton.wav`, bis 12 MB in `senden.zip`).

### Prüfungen (Cloud, 16.10.2026)

| Prüfung | Ergebnis |
|---|---|
| Kern-Test „ax“: SDK-Stimmen-Vergabe; PCM16 1:1 nach links voll, rechts halb, Stopp am Ende; DSP-ADPCM (drei Rahmen, eigene Köpfe) bitgenau gegen die dokumentierte Formel; Schleife; Rate 0,5 linear = 80 Abtastwerte je Block; Hüllkurve; Aux A über einen CPU-Effekt zurück in die Hauptmischung; Leser der App; AI-Takt 200 Blöcke je 60 Halbbilder | bestanden |
| Kern-Test „axfx“: hoher Hall (Übersprechen 8500/1500, trockener Anteil sofort, kein Hall vor dem kürzesten Kamm, abklingender Nachhall, Surround still), normaler Hall mit 10 ms Vorverzögerung, Chorus auf Sinus, Delay 50 ms = 9 Blöcke | bestanden |
| Laufzeittest „stream“ (Streamcode des Spiels über das echte AX und den DSP-Nachbau) | bestanden |
| `whole_build.sh` i686 und armhf (qemu), normal und `RE4_OPT=2` | grün, 27 Platzhalter |
| native App-Bibliothek (`re4decomp_link_check`), Java gegen nachgebildete Android-Klassen | linkt, 11 JNI-Namen passen; übersetzt |

Nicht geprüft (Stand dieses Abschnitts): Ton mit echten Disc-Daten, Klang,
Lautstärke, Latenz, Rechenzeit auf dem Retroid; siehe Gerätelauf unten.

### Erster Gerätelauf mit Ton (16.10.2026, Retroid, Stand `34b60a3`)

`test-core-app.ps1 -Manual -Seconds 900 -Scale 3 -AudioWav 60`, etwa 360 s
(Titel, Intro, r100, r101, Untermenüs, Treffer, Soft-Reset). Laut
Projektinhaber klingt alles sauber und plausibel; nur der Schuss von Leons
Waffe bricht vereinzelt ab.

- **Ausgabe:** 70 000 Blöcke (350 s Spielzeit), 0 verloren, 12 Aussetzer,
  40–76 ms im Ring; AudioTrack 32 kHz mit 2568 Frames Puffer (80 ms), also
  etwa 120–160 ms vom Mischen bis zum Lautsprecher (nicht beanstandet; kürzer
  machen: M11). WAV der ersten 60 s (Titel, Intro): Pegel bis −0,5 dBFS,
  nichts übersteuert. Kern weiter 33,4 ms je Bild.
- **Abbrüche:** 116 × „ISS Voice Drop“ (der Rückruf des Tontreibers, wenn AX
  eine Stimme wegnimmt), gehäuft im Kampf. Ursache: das Budget von AX für den
  DSP (`__AXSyncPBs`): AX schätzt die DSP-Takte jeder laufenden Stimme
  (Umsetzung, Busse, Rampen) und stoppt alles über `OS_BUS_CLOCK / 400`
  (405 000 Takte, was der DSP des GameCube in 5 ms schafft), innerhalb einer
  Priorität zuerst die älteren Stimmen, also z. B. den ausklingenden Schuss
  neben neuen Geräuschen. Auf dem GameCube ist das dieselbe Grenze; der
  nachgebaute DSP hat sie nicht.
- **Behoben:** `dsp.cpp` gibt AX beim Start des DSP-Programms das Vierfache
  (1 620 000 Takte, mehr als 64 Stimmen auf allen Bussen brauchen);
  `RE4_DSP_ORIGINAL_LIMIT=1` behält die Grenze des GameCube. Kern-Test „ax“:
  40 Stimmen auf allen Bussen mit Rampen bleiben, mit 405 000 Takten stoppt
  AX welche. Die Protokollzeile „audio: …“ nennt jetzt auch die meisten
  gleichzeitig laufenden Stimmen, die höchste Schätzung von AX und die Blöcke
  über dem Budget; der Bericht von `test-core-app.ps1` zählt „ISS Voice
  Drop“. Auf dem Gerät nicht getestet.

## M9 Videos

### Bestandsaufnahme (16.10.2026, Cloud)

- **Vorher:** `runtime/movie.cpp` (K2) beendete jeden Film sofort (kein Bild,
  kein Ton), das Spiel machte danach weiter.
- **Filme des Spiels** (alle über `cSofdec`, `sofdec.cpp`): auf dem Titel nach
  10 s ohne Taste abwechselnd `movie/demo0.sfd` und `demo1.sfd`
  (`title.cpp`), `e3_jpn.sfd` (Messe-Zweig des Titels), `opening.sfd` vor der
  Autofahrt (`r120.cpp R120Event`), `r214_ev.sfd` (r217), `r229_ev.sfd`
  (r221), `r333_ev.sfd` (st3), `ending.sfd` (r333), die Ada-Enden
  `adaend_m.sfd`/`adaend_c.sfd` (r40e); fehlt eine Datei, nimmt das Spiel
  `dmy.sfd`. Dazu das Debug-Menü „MOVIE TEST“ (`t_movie.rel`).
- **Ablauf im Spiel:** Der Film läuft in Scheduler-Platz 1 (Platz 0 hält an),
  der Heap des Raums wandert für die Dauer ins ARAM (5 MB Film-Heap). Je Bild
  `cSofdec::appMain`: START oder B (`Joy.trg & 0x1200`) bricht ab, sonst
  `ADXM_ExecMain` (der CRI-Server: Lesen, MPEG-Dekodieren, ADX), das neueste
  Bild als Y8- und C4:4-Textur (`mwPlyFxCnvFrmY84C44`), gezeichnet mit vier
  TEV-Stufen (YUV → RGB über Konstantfarben, `drawTex`). In r120 überspringt
  ein Abbruch laut Originalcode auch die Autofahrt (`SCF_R120_EVENT_CANCEL`).
- **CRI-Quellen in `src/lib`:** 138 Dateien (ADX, ADXF, SJ, SVM, Sofdec
  SFD/SFH/SFX, MWPLY, MPEG MPV/MPS), C bis auf Gekko-Assembler: IDCT
  (`dct_fsri.c`) und Halbpixel-Mischen (`mpv_umc.c`) mit Paired Singles und
  GQR-Quantisierung, die Zeitbasis (`uty_tmr.c`), GQR-Sichern
  (`uty_ppc.c`), die Textur-Umwandlung (`cftyp422_ppc.c`); dazu viele
  Registerbindungen des Decompilats ohne Wirkung und Bewegungsausgleich, der
  Bildpunkte als Wörter in GameCube-Reihenfolge packt (`mpv_mc*.c`).
- **Uhr:** Sofdec taktet das Bild nach der Spielzeit des ADX-Tons, geglättet
  mit der Zeitbasis (`sfd_adxt.c sfadxt_UpdateTime`). Läuft das Spiel
  schneller als die Zeitbasis, bleibt die Uhr am Ende des Tons stehen und der
  Film endet nie (im Test gesehen).

### Umsetzung (16.10.2026, Cloud)

- **CRI unverändert mitkompiliert:** alle 138 Dateien (`RE4_CRI_UNITS`,
  Ziel `re4_cri_obj`, `-fsigned-char` wie CRIs `-char signed`), gelinkt in
  `libre4_main` (im Original Teil von `main.dol`). `runtime/movie.cpp`
  entfällt.
- **Registerbindungen** (`asm { mr r3, r31 }` ohne Wirkung auf C-Variablen)
  entfernt eine neue Regel von `whole_prepare.py` (Tabelle „Mechanische
  Umschreibungen“).
- **`port/whole/include/re4_cri_port.h`:** Gekko-Befehle als C (Intrinsics,
  big-endian Lesen, Paired Singles `re4_ps`, GQR-Quantisierung
  `re4_psq_l`/`re4_psq_st` mit den GQR-Werten der CRI-Kerne in
  `re4_cri_gqr`).
- **`port/tools/whole_ppc_to_c.py`:** übersetzt Gekko-Assembler Befehl für
  Befehl nach C (Register als Variablen, Sprünge als `goto`); damit entstand
  Patch 0050.
- **Patches 0048–0055** (Tabelle „Patches“): Plattformteile, Bewegungsausgleich
  als Byte-Schleifen, Assembler in C, Byte-Reihenfolge der Stromleser und
  ADX-Köpfe, Bildgröße aus `mwPlyGetHdrInf`, Werte, die im Original in
  Registern lagen, Fortschritt im Log und Konstantfarben in `sofdec.cpp`,
  Blendfarben in `title.cpp`.
- **Werte in Registern** (Patch 0053): gefunden durch Vergleich jeder
  Deklaration mit ihrer Definition in LLVM-IR (jetzt als Werkzeug
  `port/tools/whole_ir_signatures.py`): die Zahl übersprungener Makroblöcke
  (fehlte: Absturz im Bewegungsausgleich), die Bildrate für den
  Sofdec-Zeitgeber, der Player für die Nachschub-Prüfung, die Größe für den
  ADX-Kopf, zwei Ergebnisse, die Aufrufer prüfen.
- **Plattform:** `DVDCancel` (`dvd.cpp`, Abbruch über den verzögerten
  Rückruf), `__cvt_fp2unsigned` (`sn_runtime.cpp`, wie `fctiwz`),
  `re4_cri_timer` (`vi.cpp`: die Zeitbasis der CRI-Uhr; normal `OSGetTime`,
  mit `RE4_VI_FAST=1` zählt sie die zugestellten Halbbilder, damit Ton und Uhr
  im Schnelllauf zusammenpassen).
- **Farben:** `setTevPrm` schreibt die vier Konstantfarben als Wort über
  ein `GXColor` (Patch 0054, sonst grünstichig), der Titel ebenso seine
  Blendfarben (Patch 0055, sonst ist Schwarz ein durchsichtiges Rot).
- **Helligkeit** (Hinweis des Projektinhabers: der Regler wirkte nicht): Das
  Spiel stellt sie über den Kopierfilter ein (`Bg_brightness_set` →
  `GXSetCopyFilter`, Summe der sieben senkrechten Gewichte, 64 = normal). Der
  GX-Renderer der App (`gx_gl.cpp`) multipliziert das kopierte Bild jetzt mit
  Summe/64; das Weichzeichnen des Filters bleibt weg. Test `gx_gl_check`
  (`TestCopyFilter`).
- **Texturen:** Jeder Film lädt je Bild zwei neue Texturen hoch; der
  Texturspeicher von `gx_decode.cpp` begrenzt jetzt auch die entpackten Bytes
  (256 MB), damit sich Filmbilder nicht ansammeln.
- **Log:** `runtime/movie_status.cpp` (aus `sofdec.cpp`, Patch 0054): „movie:
  NAME started“, alle 5 s „… playing, Schritte, Bilder, Zeit, übersprungen
  Decode/Anzeige, Status“, am Ende „ended“ oder „skipped (START)“.
- **Prüfwerkzeuge:** `port/tools/make_test_sfd.py` (Sofdec-Muxer für
  Testfilme), `test_whole_movie.py` (Cloud-Test, in `whole_build.sh`),
  `movie_report.py` (Standbilder als PNG), `whole_r100_check.sh` Abschnitt 6
  (jeder Film der Disc ganz im Schnelllauf, Standbild alle 30 s und das letzte Bild in
  `bilder-film/`, Ton des Vorfilms als `film-d1-opening.wav`),
  `test-core-app.ps1` (Bericht: Filme gestartet/beendet, je Film Zeit, Bilder,
  ausgelassene Bilder, Abbruch mit START, fehlende Dateien, CRI-Fehler).

### Prüfungen (Cloud, 16.10.2026)

| Prüfung | Ergebnis |
|---|---|
| Kern-Test „ax“ mit SYN-Stimmen (Patch 0056), Laufzeittest „vsync“, Filmtest mit 60,0-Hz-Halbbildern (alle 120 Bilder genau zwei Halbbilder) | bestanden (i686; armhf: unter qemu zu langsam für Echtzeit, dort zählt nur das Ende) |
| Kern-Test „movie“: drei synthetische Packs, `mwPlyGetHdrInf` 320 × 240, Arbeitsspeicher ≥ 320 · 240 · 3 | bestanden |
| Filmtest (4 s, 320 × 240, 29,97 Bilder/s, MPEG-1 mit B-Bildern, ADX stereo 48 kHz) in Echtzeit | 120 von 120 Bildern in Reihenfolge, schlechtestes Bild 75,9 dB gegen ffmpeg, 2,03 Halbbilder je Bild, Ende (PLAYEND) nach 249 Halbbildern, 0 ausgelassen, 0 CRI-Fehler; Ton: beide Töne auf ihrem Kanal (0,97/0,93, Übersprechen 0,000), Ton 40 ms nach dem ersten Bild |
| Filmbild über die GX-Nachbildung (`drawTex` des Spiels durch `gx_chunk_render`) | 24,0 dB gegen die BT.601-Umrechnung, mittlere Abweichung R +0,3 G +0,5 B +0,8 (Kanten weichen ab: die Farbanteile filtert die Textureinheit bilinear) |
| derselbe Film im Schnelllauf (`RE4_VI_FAST`) | 120 Bilder, 2,03 Halbbilder je Bild, Ende nach 249 Halbbildern, Ton +40 ms |
| `whole_build.sh` i686 und armhf (qemu), normal und `RE4_OPT=2` | grün, 45 Platzhalter |
| `gx_gl_check` (mit `TestCopyFilter`), `re4decomp_link_check` | 0 Fehler; linkt |
| `whole_ir_signatures.py` (alle 1335 Dateien des i686-Builds) | 10 Stellen „liest Unsinn“, alle geprüft und ohne Wirkung: tote Funktionen (Z-Puffer-Umwandlungen prüfen das Ergebnis von `mwPlyGetCurFrm`), die nie aufgerufene Tabelle `mpvcmc_oneref`, die Sprungfunktionen `MPVUMC_*picSkipped` (Aufruf über den in Patch 0053 korrigierten Zeiger), Ergebnisse von `ADXT_SetOutVol/Pan`, die `mwsfdrna.c` verwirft; 26 harmlose (zusätzliche Argumente, ungelesene Ergebnisse) |

**Nicht geprüft (Stand dieses Abschnitts):** die echten Filme der Disc
(PC-Prüfung), alles auf dem Gerät (Rechenzeit des Dekodierens, Speicher,
START, Lippensynchronität, Demo-Filme des Titels). Der ADX-Pegel liegt im
Test etwas über der Quelle (Spitze 4836 bei etwa 4100 im Testton; Rundung des
ADX-Kodierers oder Mischung, nicht untersucht). Nicht übernommen, weil das
Spiel sie nicht nutzt: die ARGB8888-Umwandler (melden sich einmal im Log),
die WAV/AIFF/AU-Wege von ADX (ohne Byte-Reihenfolge-Korrektur). Aufrufe über
Funktionszeiger mit falscher Argumentzahl findet `whole_ir_signatures.py`
nicht.

### Erster Gerätelauf und PC-Prüfung mit Filmen (16.10.2026, Stand `af34ae0`)

- **Erster Versuch:** Der Android-Build brach nach fehlerfreiem Bauen bei der
  Platzhalter-Prüfung ab (alter Vergleichs-Ordner, siehe „Platzhalter“);
  behoben mit `port/whole/stubs-expected.txt`.
- **Gerätetest A** (`-Seconds 480 -Scale 3 -AudioWav 60`, r120): Vorfilm
  `opening.sfd` ganz gelaufen (76,0 s, 2278 Bilder, 4567 Schritte, 0
  ausgelassen, Status 3), danach Autofahrt und r100. Während des Films Kern
  16,7 ms je Bildstück (10 ms frei), Renderer 2–3 ms (höchstens 13), 60,0
  gezeigt je Sekunde; Ton des Films in `ton.wav` sauber (−18 dBFS, 3
  übersteuerte Werte). Laut Projektinhaber vereinzelte kleine Ruckler, auch
  außerhalb der Bildschirmfotos. **6 × „ISS Voice Drop“** in r100.
- **Gerätetest B** (`-Manual`): Demo-Film nach etwa 15 s auf dem Titel mit
  Ton (`demo0.sfd` ganz, 61,4 s, 1839 Bilder, 0 ausgelassen), der zweite
  (`demo1.sfd`) mit START abgebrochen („skipped (START)“), 0 Voice Drops.
  Helligkeit nicht geprüft.
- **PC-Prüfung** (`test-r100-core.ps1`, „ALLES OK“): alle 13 Filme beider
  Discs ganz im Schnelllauf (Disc 1: `adaend_c`, `adaend_m`, `demo0`,
  `demo1`, `opening`, `r214_ev`, `r229_ev`; Disc 2: `adaend_c`, `adaend_m`,
  `demo0`, `demo1`, `ending` mit 6475 Bildern, `r333_ev`), jeder mit Status 3,
  0 ausgelassen, 0 CRI-Fehler, 512 × 336; ein Schritt 0,5–0,8 ms im Mittel
  (i686). Standbilder richtig (Farben, Untertitel, Abspann). `e3_jpn.sfd` und
  `dmy.sfd` gibt es auf der Disc nicht; `demo0` und `demo1` sind gleich.

**Abgebrochene Töne: Ursache gefunden (Patch 0056).** Die SYN-Bibliothek
(Synthesizer für die Sequenz-Klänge des Spiels, `snd_seq*.cpp`) schrieb
32-bit-Werte als ein Wort über zwei u16-Felder des AX-Parameterblocks
(`synsample.c`: Schleifen-Kennung und Format, Abspiel-, Schleifen- und
Endadresse, Verstärkung; `synpitch.c __SYNUpdateSrc`: das
Abtastraten-Verhältnis). Auf little-endian waren die Hälften vertauscht: die
Adressen lagen weit außerhalb des ARAM (SYN-Stimmen blieben stumm), und der
Bruchteil des Verhältnisses wurde zum ganzzahligen Teil. AX liest damit
`__AXSrcCycles[ratioHi]` weit hinter der 5-Einträge-Tabelle (auf dem GameCube
hätte ein Index ≥ 5 die folgenden Mischtabellen getroffen) und schätzte über
3 Milliarden DSP-Takte („AX estimate at most 3678218915“ im Protokoll),
also stoppte AX Stimmen. Das erklärt auch die abgebrochenen Schüsse aus M8
(das vierfache Budget hatte die Folgen nur verringert). Prüfung: Kern-Test
„ax“ setzt SYN-Stimmen auf (ADPCM mit Schleife, PCM16, PCM8) und ändert die
Tonhöhe; ohne Patch 11 Fehler, mit Patch bestanden. Gefunden hat das nicht
der IR-Vergleich (die Typen stimmen), sondern die Schätzung im Protokoll.
Für M10: `CARDFormat.c` und `CARDUnlock.c` schreiben ebenso Wörter in
Byte-Felder.

**Ruckler: Bildtakt an den Bildschirm gekoppelt.** Die Protokolle zeigen
keinen Engpass (Kern und Renderer mit Reserve, 0 ausgelassene Filmbilder,
gleichmäßige Abstände). Der Kern lieferte aber 59,94 Bildstücke je Sekunde
(NTSC), der Bildschirm zeigt 60: Alle 16,7 s fehlte dem Renderer ein neues
Bild, ein Filmbild stand drei statt zwei Bildwechsel lang (im 76-s-Vorfilm
etwa viermal, im Spiel ebenso). Jetzt meldet die App jeden Vsync
(`Choreographer`, `CoreBridge.vsync` → `re4_core_vsync`), und `vi.cpp` legt
die Halbbilder auf den Bildschirmtakt, wenn er höchstens 0,5 % von 59,94 Hz
abweicht (60 Hz; 120 Hz mit zwei Vsyncs je Halbbild): eine Retrace je Vsync,
1 ms danach, die Lage wird sanft nachgeführt (höchstens 1/50 Halbbild je
Retrace). Das Spiel läuft dann 0,1 % schneller; die Tonausgabe der App gleicht
das aus, und die Filmuhr folgt dem Ton, also den Halbbildern (Filmtest mit
60,0-Hz-Halbbildern: jedes Bild genau zwei Halbbilder). Ohne Vsync für
250 ms (App pausiert, Linux) oder mit `RE4_VI_VSYNC=0` wieder der eigene
NTSC-Takt. Laufzeittest „vsync“: Rechnung (60/59,94/120 Hz angenommen, 50/90
Hz abgelehnt, Lage wandert zu 1 ms) und echter Takt (Vsync-Gitter 8 ms
neben den Retraces: die Retraces wandern auf 1 ms nach dem Vsync; mit
`RE4_VI_VSYNC=0` schlägt der Test fehl). Auf dem Gerät nicht getestet.

**FILES-Flackern** (Video des Projektinhabers, 37 Bilder bei 30 Bildern/s):
Ausgewertet zeigt sich ein Wechsel von Bild zu Bild genau im Rahmen der
Dateiliste (Holzrand oben, links, rechts, unten und die Leiste unter
„Village Files“): dieselben Bildpunkte springen jedes zweite Videobild um
etwa 15–25 Helligkeitsstufen hin und her (z. B. 85 ↔ 103). Leons Animation
und das pulsierende „Village Files“ sind gewollt. In den R3-Folgen aus dem
zehnten Gerätelauf (in der Cloud nachgezeichnet) war nichts davon zu sehen,
die Ursache liegt also vermutlich im Zeichnen auf dem Gerät (Treiber). Neu:
Eine R3-Folge speichert jetzt zusätzlich jedes Bild so, wie die GPU des
Geräts es gezeichnet hat (`gx-geraet-NN-KK.bmp` in `gx-geraet.zip`, auch über
`pull-gx-dumps.ps1`); der Vergleich mit dem Cloud-Bild derselben Daten zeigt
dann, welche Zeichnung abweicht.

### Zweiter Gerätelauf mit Filmen (16.10.2026, Stand `6c2e7d9`): M9 erreicht

- **Test A** (r120, `-Seconds 480 -Scale 3`): Vorfilm ganz (76,0 s, 2278
  Bilder, 0 ausgelassen), laut Projektinhaber **ohne Ruckler**. Je 5 s genau
  300 Schritte (der Takt folgt dem 60-Hz-Bildschirm), Ton 2000 Blöcke je
  9,99 s statt 10,0 s (0,1 % schneller, wie erwartet), „ran dry“ 19 statt 49.
  **Abgebrochene Töne: 0**, höchste AX-Schätzung 108 746 von 1 620 000 Takten
  (vorher 3,7 Milliarden). In r100 Kern 33,3 ms je Bild, 0 ms Warten auf den
  Renderer, 60 gezeigt je Sekunde; die R3-Folge im FILES-Menü kostete kurz
  Zeit (Renderer höchstens 843 ms beim Speichern der Bilder).
- **Test B** (`-Manual`): Demo-Film nach etwa 15 s mit Ton, Abbruch mit START,
  **Helligkeitsregler wirkt** (heller und dunkler). Gemeldet: Vor dem
  Demo-Film blendet der Titel aus, erscheint für einen Bruchteil einer Sekunde
  noch einmal, dann kommt der Film.
- **Titel blitzt vor dem Film auf → `VISetBlack`:** `title.cpp` schaltet vor
  dem Film mit `systemVISetBlack(1)` den Videoausgang schwarz und nimmt die
  Blende weg (`FadeKill`); der Film hebt das Schwarz mit seinem ersten Bild auf
  (`cSofdec::draw`). Im Port tat `VISetBlack` nichts, also war das eine Bild
  des Titels ohne Blende zu sehen. Jetzt gilt es ab dem nächsten `VIFlush`
  wie auf dem GameCube (`vi.cpp`); die Bildstücke tragen
  `RE4_GX_CHUNK_BLACK`, und der Renderer zeigt dann Schwarz (`gx_gl.cpp
  Present`), das Bild selbst wird weiter gezeichnet. Betrifft auch Raumwechsel
  und Filmende. Tests: `gx_probe` (Kennung gesetzt und wieder weg),
  `gx_gl_check` (schwarz gezeigt, danach wieder das Bild; ohne die Änderung
  schlägt der Test fehl). Auf dem Gerät noch nicht geprüft.
- **FILES-Flackern ist Filmkorn des Originals:** Die R3-Folge (12 Bilder,
  gx-geraet-00-00 bis -11) zeigt das Flackern auf dem Gerät **und** in der
  Cloud gleich (Bildpunkte im Rahmen der Dateiliste wechseln mit Periode 2,
  z. B. 93 ↔ 86; Gerät gegen Cloud im Mittel 0,2 Stufen Abweichung). Die
  Bildstücke unterscheiden sich in Zeichnung 7 und 15: Textur 0 ist abwechselnd
  0x721c60 und 0x719060, zwei I4-Texturen 320 × 224 mit Zufallsrauschen (zwei
  Bilder einer Texturanimation hintereinander in einer Datei). Die
  Texturanimation des ID-Systems (`id_sys.cpp idSysMove04`) schaltet jedes Bild
  eine Textur weiter, hier zwischen den beiden. Das ist der Körnungseffekt des
  Menüs, auf dem GameCube genauso; auf dem Röhrenfernseher (480i, unscharf)
  fiel er weniger auf. Die frühere Aussage „in den Daten nicht zu sehen“
  (zehnter Gerätelauf) war falsch. Kein Fehler des Ports; eine Option zum
  Beruhigen (die beiden Rauschbilder mischen) passt zu M11
  (Grafikoptionen).

## M10 Speichern/Laden

### Bestandsaufnahme (16.10.2026, Cloud)

- **Vorher:** `runtime/card.cpp` meldete beide Schächte leer, Patch 0039
  übersprang die Prüfung beim Start; Speichern ging nicht.
- **Dateien des Spiels** (`card.cpp`): Spielstände `bh4_data00` bis
  `bh4_data19` (je 8 Blöcke = 0x10000 Bytes, benutzt 0xEAFC) und die
  Systemdatei `bh4_system` (1 Block, benutzt 0x1E7C). Aufbau beider: Kommentar
  bei 0x0/0x20 („resident evil 4“, „FILE NN“ bzw. „Systemfile“), Banner 96 × 32
  RGB5A3 bei 0x40, ein C8-Symbol bei 0x1840 mit Palette bei 0x1C40 (aus der
  TPL des Untermenü-Archivs; die Bildpunkte bleiben big-endian wie auf der
  Disc, die Formatumwandler drehen nur die Köpfe). Spielstand: bei 0x2000 der
  Kopf `SaveInfo` (CRC über die folgenden 0x1FC Bytes, Kennung 0x116,
  Kalenderzeit, Art, Raum, Kapitel), ab 0x2034 der Spielzustand (aus `pG`
  0x36F8 Bytes), Gegenstände 0x572C, Räume 0x6930, Untermenü 0xE7D0, Händler
  0xE7D4, CRC-32 der Datei bei 0xEAF8. Systemdatei: `SYSTEM_SAVE_WORK` bei
  0x1E40 (Sprache, Land, Helligkeit, Steuerungsbelegung, Tonart, Söldner-
  Wertungen), CRC bei 0x1E78. CRC: MSB zuerst, Polynom 0x04C11DB7, Start 0
  (`CRCCalc`).
- **Byte-Reihenfolge:** Das Spiel kopiert seinen Speicher, wie er ist (im Port
  Host-Reihenfolge), und rechnet die CRC über die Bytes. Innerhalb des Ports
  passt das immer zusammen; ein GameCube-Spielstand sähe anders aus.
- **Wann:** Speichern an der Schreibmaschine (AT-Typ 8, `sce_at.cpp` →
  `CardSave`), am Kapitelende (`sce_com.cpp`), Söldner (`CardSysSave`).
  **Die Systemdatei (Optionen) schreibt das Spiel nur beim Start (wenn sie
  fehlt) und nach jedem Spielstand; gelesen wird sie nur beim Start.** Wer nur
  die Optionen ändert und neu startet, verliert sie also, wie auf dem
  GameCube. Laden: Titel („Load“), Pausemenü (`option.cpp`), Debug-Startmenü
  Zeile 1.
- **SDK:** 15 Dateien `src/lib/CARD*.c`; das Spiel nutzt nur Schacht A
  (Arbeitsbereich 0xA000) und verlangt 8-KB-Blöcke. `CARDFormat.c` und
  `CARDUnlock.c` schreiben 32-bit-Wörter in Byte-Felder (Hinweis aus M9): beim
  Formatieren landen Zähler, Sprache und Zeit in Host-Reihenfolge im
  Kartenkopf, und `CARDCheck.c` liest sie genauso zurück; `CARDUnlock.c` läuft
  nicht (siehe unten).

### Entscheidung des Projektinhabers (16.10.2026)

Kartenformat **nur für den Port**: Die Kartendatei hat die Byte-Reihenfolge
des Geräts und ist nicht mit GameCube/Dolphin austauschbar. Ein Umwandler für
`.gci` (Import/Export) kann später als eigener Schritt kommen.

### Umsetzung (16.10.2026, Cloud)

- **CARD-Bibliothek unverändert mitkompiliert** (`RE4_CARD_UNITS`, Ziel
  `re4_card_obj`, gelinkt in `libre4_main`) mit `re4_card_port.h` (die
  OS-Bytes 0x800030E3/0x800030C0 in MEM1, ein leerer VI-Registerblock für
  `CARDFormat`) und Patch 0057 (Befehlsbytes, siehe „Patches“).
- **Hardware darunter:** `runtime/exi.cpp` (EXI wie `EXIBios.c`),
  `runtime/memcard.cpp` (die Karte mit den Befehlen einer Memory Card 251, so
  wie Dolphin sie nachbildet), `runtime/alarm.cpp` (`OSAlarm` für die
  Zeitüberwachung der Befehle). Übertragungen geschehen sofort; was die
  Hardware per Interrupt meldet (DMA-Ende, „Karte fertig“), kommt an den
  Interrupt-Punkten (Leerlauf in `vi.cpp`, `OSSleepThread`) im
  Interrupt-Kontext, so lange, bis nichts mehr ansteht (ein Block lesen sind 16
  Übertragungen, schreiben 64 Seiten). Die Sperre gegen verschachtelte
  Zustellung gilt nur während des Rückrufs: am Ende des Interrupts darf auf
  eine andere Aufgabe umgeschaltet werden, die den Rest zustellt (sonst Hänger,
  wenn die schlafende Aufgabe gerade zustellte).
- **Die Karte:** Kennung 0x10 (16 Mbit, 8-KB-Blöcke), Status „entsperrt“ von
  Anfang an, also kein DSP-Handshake (`CARDUnlock.c` läuft nicht, wie bei
  Dolphin). Die Flash-ID im SRAM, die `CARDCheck` gegen den Kartenkopf prüft,
  wird beim Einstecken aus dem Kopf abgeleitet (wie Dolphins
  `SetCardFlashID`); eine unformatierte Karte bekommt eine feste. Jede
  geschriebene Seite und jeder gelöschte Block geht sofort in die Datei
  (`pwrite64`), eine Sekunde nach dem letzten Schreiben `fsync`.
  `RE4_MEMCARD` nicht gesetzt: `<Datenordner>/memcard/card-a.bin` (fehlt sie,
  entsteht eine neue Karte, seit 17.10.2026 formatiert und leer, mit
  `RE4_MEMCARD_NEW=blank` unformatiert); `RE4_MEMCARD=none`: Schacht A leer;
  sonst der angegebene Pfad. In der App ist das
  `Android/data/com.ast.re4decomp/files/game/memcard/card-a.bin` (bis
  17.10.2026 `com.ast.re4decomp.core`; ein neuer Disc-Import lässt sie stehen). Schacht B bleibt leer.
- **Startprüfung echt:** Patch 0039 und `runtime/card.cpp` entfallen. Die
  automatischen Läufe (`whole_r100_check.sh` Hauptlauf und Filme,
  `test-core-app.ps1` ohne `-Manual`) setzen `RE4_MEMCARD=none`: ihr Zeitplan
  ist auf den „keine Karte“-Dialog abgestimmt, und es entsteht keine
  Kartendatei im Disc-Ordner bzw. die Spielstände der App bleiben unberührt.
- **Uhr:** `OSGetTime` zählt jetzt in Ortszeit wie die Uhr der Konsole (die
  Spielstände zeigen das Speicherdatum).
- **Dateifunktionen im Spielcode:** Der Prelude macht `__asm__` leer; damit
  fallen in Dateien mit den Schaltern des Spiels auch die Umleitungen der
  C-Bibliothek auf die 64-bit-Dateifunktionen weg, `off_t` bleibt aber 64 Bit
  breit (`lseek` sah im Test die Datei als 0 Bytes groß). `memcard.cpp`
  nimmt deshalb `pread64`/`pwrite64` beim Namen. Die übrigen Dateien mit
  Spielschaltern nutzen nur `fopen`/`fseek` (unbetroffen).
- **Protokoll:** „memcard: …“ (Kartendatei, Dateien auf der Karte bei jeder
  Änderung), „card: …“ (Schritte des Kartenbildschirms, Patch 0058, mit
  Fehlercodes), „run: memory card check done … options …“ (Ende der
  Startprüfung mit den Optionen) und „run: save loaded from the memory card“
  (`state_trace.cpp`, immer an).
- **Testhilfen:** `RE4_TEST_CARD_YES=1` (Pfeil der Kartenfragen auf „Ja“),
  `RE4_STOP_AT_FRAME=N` (Ende bei Bild N), `RE4_TEST_SAVE="free+N[,B][,stop]"` öffnet N Bilder nach
  „Spieler frei“ den Speicherbildschirm wie eine Schreibmaschine (vorher
  Helligkeit = B; `stop`: Ende, wenn er wieder zu ist); `re4_host
  --memcard-format DATEI` legt eine formatierte, leere Karte an;
  `port/tools/memcard_list.py` listet eine Kartendatei (Dateien, Banner,
  Symbol, Kommentar, die CRCs und Inhalte von Systemdatei und Spielständen).
- **PC-Prüfung** (`whole_r100_check.sh` Abschnitt 7): Lauf A mit einer
  frisch formatierten Karte: Startprüfung legt die Systemdatei an (A = ja),
  neues Spiel in r100, dort speichern (Datei 1, Helligkeit vorher 72), Ende;
  Lauf B mit derselben Karte: Startprüfung liest die Systemdatei (Helligkeit
  72), Titel „Load“, Datei 1 laden, r100 läuft weiter; danach
  `memcard_list.py` mit `--expect-system --expect-save 0`.
- **Gerätetest** (`test-core-app.ps1`): holt nach jedem Lauf die Karte
  (`speicherkarte-a.bin`, auch in `senden.zip`) und listet sie im Bericht
  (über WSL), dazu die „card:“-Schritte, das Ende der Startprüfung mit den
  Optionen und geladene Spielstände; `-NewCard` startet mit einer neuen,
  unformatierten Karte (die alte wird vorher gesichert); `-SaveTest` öffnet
  den Speicherbildschirm von selbst kurz nach „Spieler frei“
  (`RE4_TEST_SAVE=free+120`).

### Prüfungen (Cloud, 16.10.2026)

| Prüfung | Ergebnis |
|---|---|
| Kern-Test „card“ (`tests/card_probe.cpp`): neue Kartendatei (2 MB, unformatiert, Mount meldet „kaputt“), Formatieren, `CARDCheck`, 251 Blöcke und Kodierung ANSI frei, Seriennummer; `bh4_system` (1 Block) und `bh4_data00` (8 Blöcke) anlegen, schreiben, Status wie `makeCardStatus` (Banner RGB5A3, C8-Symbol: Lagen 0x40/0x1840/0x1C40/0x1E40), zurücklesen; „existiert“, „keine Datei“; löschen und neu anlegen; Neustart (Datei neu gelesen, Flash-ID neu abgeleitet): Mount bereit, gleiche Seriennummer, Inhalte gleich; die synchronen Aufrufe (`CARDMount`, `CARDFormat`, `CARDCreate`, `CARDWrite`, `CARDRead`, `CARDDelete`) auf einem OS-Thread; Schacht leer: „keine Karte“ | bestanden |
| `memcard_list.py` auf der Karte des Kern-Tests (die Testdateien tragen CRCs aus `CRCCalc` des Spiels) | 2 Dateien, CRCs von Systemdatei, Spielstandkopf und Spielstand stimmen |
| `re4_host --memcard-format` | formatiert, 251 Blöcke frei; `memcard_list.py` liest sie |
| `whole_ir_signatures.py` für die CARD-Dateien und die Aufrufer des Spiels | 0 Abweichungen |
| Platzhalter | 47 Funktionen, 1 Datenobjekt (2 neue aus `CARDNet.c`), wie `stubs-expected.txt` |
| `whole_build.sh` i686 und armhf (qemu), normal und `RE4_OPT=2` | grün (alle Prüfungen, Kern-Test „card“, `memcard_list.py`) |

### Erste PC-Prüfung mit Karte (16.10.2026, Stand `6752926`): Lauf A ohne Spielstand, Lauf B hing

- **Gemeldet:** „Ergebnis A: FEHLER (Exit-Code 0)“, danach hing Lauf B und
  ließ sich weder mit Q noch mit Strg+C beenden; es entstanden keine Logs auf
  dem Desktop (die Prüfung kopiert sie erst am Ende).
- **Ursache (aus dem Code):** Die Ja/Nein-Fragen des Kartenbildschirms
  stellen den Auswahlpfeil auf die **zweite** Möglichkeit (`card.cpp`:
  `cMes.SetCursor(0, 1)`; `mes.cpp code08`: A wählt `m_cur + 1`), das ist
  meist „Nein“. Die Tastenfolge drückte nur A: beim Start „Systemdatei
  anlegen? → Nein“, beim Speichern „In diese Datei speichern? → Nein“ (zurück
  zur Liste, immer wieder), bis `RE4_STOP_AFTER` den Lauf mit Exit-Code 0
  beendete. In Lauf B war die Liste leer, und der Ladebildschirm wartete auf
  B (die Folge drückt nur A/START), bis zur Zeitgrenze von 30 Minuten.
  `timeout` ließ das Spiel in einer eigenen Prozessgruppe laufen, darum kam
  Strg+C nicht an.
- **Behoben:** `RE4_TEST_CARD_YES=1` (state_trace.cpp) hält den Pfeil in den
  Kartenbildschirmen auf der ersten Möglichkeit, A allein sagt „Ja“;
  `RE4_STOP_AT_FRAME=N` beendet einen Lauf bei Bild N, was das Spiel auch
  gerade tut (mit Schritt, Raum, Kartenbildschirm offen/zu). Die Kartenläufe
  laufen im Hintergrund mit Wachhund (alle 30 s der letzte Schritt, nach
  `RE4_CHECK_CARD_LIMIT` Sekunden, Standard 900, Abbruch), Lauf B entfällt,
  wenn A keinen Spielstand schrieb. Strg+C beendet jetzt die ganze Prüfung,
  beendet das laufende Spiel und kopiert Bericht und Logs bis dahin nach
  `RE4-Kern-Pruefung`. Die Logik der Kartenläufe ist mit einem nachgemachten
  `re4_host` geprüft (Erfolg, Hänger mit Zeitgrenze, Strg+C). Neu:
  `test-r100-core.ps1 -OnlyCard` (`whole_r100_check.sh --card-only`) baut
  und prüft nur den Speicherkarten-Teil.
- **Für das Gerät:** Auch dort steht der Pfeil zuerst auf der zweiten Antwort;
  vor A nach oben (bzw. links) auf „Ja“ gehen.

### Zweite PC-Prüfung mit Karte (16.10.2026, Stand `3fdd5a6`, `-OnlyCard`): „ALLES OK“

- Build (i686 `-O2`) und Cloud-Prüfungen grün (der synthetische Filmtest
  übersprungen: ffmpeg fehlte in WSL; `whole_setup_linux.sh` installiert es
  jetzt mit).
- **Lauf A:** Startprüfung mit leerer, formatierter Karte: Kartenprüfung,
  Ergebnis „Steckplatz A 0x0“, Systemdatei angelegt (Schritte 0–10, 320 Seiten
  geschrieben, 5 Blöcke gelöscht), Titel, Debug-Start r100. Spieler frei bei
  Bild 3043; Speicherbildschirm bei 3103 (Helligkeit 72): Kartenprüfung,
  Liste, Datei 1 gespeichert (Schritte 0–9), danach die Systemdatei (Schritte
  0–10), geschlossen bei Bild 3267. Karte danach: `bh4_system` (1 Block),
  `bh4_data00` (8 Blöcke), 242 Blöcke frei.
- **Lauf B (Neustart):** Startprüfung liest die Systemdatei („0x300 saves
  system file“), **Optionen: Helligkeit 72** (vorher 64). Titel „Load“,
  Datei 1 geladen (Schritte 0–5) bei Bild 768: Raum r100, 1-mal gespeichert;
  Hauptschleife ab Bild 794 in r100, Spieler frei, 600 Bilder ohne Fehler.
- **Karte** (`memcard_list.py`, auch die geschickte `speicherkarte-a.bin` in
  der Cloud gelesen): Kopf, Verzeichnis und FAT gültig; Systemdatei
  „resident evil 4 / Systemfile“, CRC richtig, Sprache 1, Land 1 (USA),
  Helligkeit 72; Spielstand „resident evil 4 / FILE 01“, beide CRCs richtig,
  Version 0x116, Datum 2026-10-16 23:55:57 (Ortszeit), Raum r100, Banner
  RGB5A3 und C8-Symbol an den erwarteten Stellen.
- Damit ist auf dem PC (i686) geprüft: Systemdatei anlegen, Speichern,
  Neustart, Optionen erhalten, Laden im Titel. Formatieren über den Dialog
  des Spiels und alles auf dem Gerät stehen aus.

### Erster Android-Build mit Karte (16.10.2026, Stand `3fdd5a6`): Linkfehler

- **Gemeldet:** Der Kern-Build für den Gerätetest brach ab:
  `ld.lld: undefined symbol: __open_real`, `__pread64_real`,
  `__pwrite64_real` (Plan: 50 statt 47 Platzhalter).
- **Ursache:** Das NDK baut mit `_FORTIFY_SOURCE` (Prüfung von Pufferlängen
  in der C-Bibliothek). Bionic ersetzt dann `open`, `pread64`, `pwrite64`
  durch geprüfte Inline-Fassungen, die die echten Funktionen über
  umbenannte Deklarationen (`__open_real` = `open` per `__asm__`) rufen.
  `memcard.cpp` wird mit den Schaltern des Spiels gebaut, deren Prelude
  `__asm__` leer macht; die Umbenennung fiel weg, und in `libre4_platform`
  (gelinkt mit `--no-undefined`) fehlten die Namen. Unter Linux tritt das
  nicht auf (glibc ruft dort echte `__*_chk`-Funktionen), darum blieb es in
  der Cloud unbemerkt.
- **Behoben:** Die Dateizugriffe der Karte (Öffnen mit Ordnern, Lesen,
  Schreiben, `fsync`, Schließen) liegen in `runtime/host_file.cpp`, gebaut
  mit den Schaltern der Plattform; `memcard.cpp` ruft sie über
  `re4_host_file_*` (`re4_runtime.h`) und nutzt nur noch Speicher-, Text-
  und Uhrfunktionen, die auf Android schon immer gelinkt haben.
  **Neue Prüfung in `whole_build.sh`:** Die Objekte von `re4_formats_obj`
  dürfen keine der von Bionic so umgeleiteten Funktionen rufen (`open`,
  `read`, `write`, `pread*`, `pwrite*`, `readlink`, `getcwd`, `umask`,
  `poll`, `recv*`, `send*`, `strlcpy/strlcat`); mit dem alten `memcard.cpp`
  meldet sie `open pread64 pwrite64`, mit dem neuen nichts.
  `whole_build.sh` i686 und armhf grün; der Android-Build läuft erst wieder
  beim Projektinhaber.

### Erster Gerätetest mit Karte (17.10.2026, Retroid, Stand `a43953a`)

- **Test A** (r120, Nachtest M9, ohne Karte): laut Projektinhaber keine Ruckler;
  „Abgebrochene Töne (Voice Drop): 0“. Der Android-Build lief nach der
  Verlegung der Dateizugriffe durch.
- **Test B** (`-Manual -NewCard -SaveTest`, damals unformatierte Karte):
  Formatieren über den Dialog des Spiels, Systemdatei angelegt (Schritte
  0–10), Vorfilm mit START übersprungen, im Titel „Load“ ohne Spielstand
  (Meldung, Code 0), neues Spiel; der Speicherbildschirm der Testhilfe
  speicherte Datei 1 und die Systemdatei. **Gemeldet:** Formatieren und die
  Lademeldung beim Start stören (Punkte 2 und 3); „Retry from a continue
  point“ führt zu einem schwarzen Bild (Punkt 4).
- **Test C** (`-Manual`, Neustart): Startprüfung las die Systemdatei (Ton
  „2“, Steuerung wie gespeichert), Titel „Load“, Datei 1 geladen (Bild 1593,
  r100), weitergespielt, an der Testhilfe erneut gespeichert. **Gemeldet:**
  nach dem Laden fehlte beim Betreten des Hauses die Szene mit dem ersten
  Ganado (Punkt 5); Laden aus dem Pausemenü führt zu einem schwarzen Bild
  (Punkt 6).
- **Punkte 4 und 6: Hänger, Patch 0059.** Beide Wege rufen `GameContinue`
  (`option.cpp`): der Raum startet neu, während seine Musik noch läuft. Die
  Protokolle enden nach `DataCtrl::setData(…r100_04…)`, auch die Tonzähler
  (der Kern stand). `SndRoomBgmLoad` wartet in einer Schleife, bis die Musik
  des Raums davor ausgeblendet ist (`stop_req`); das Ausblenden läuft im
  AX-Interrupt, den der Port nur an Unterbrechungspunkten zustellt. Die
  Schleife ruft jetzt `re4_wait_interrupt()` wie Patch 0045. Neue Testhilfe
  `RE4_TEST_CONTINUE=free+N` (state_trace.cpp: `GameContinue(1)` wie das
  Pausemenü, Log „run: after the retry: room … runs again“); die PC-Prüfung
  nutzt sie in Lauf B nach dem Laden. Die Kartenläufe erkennen jetzt einen
  Stillstand (2 Minuten ohne neue Logzeile) und schreiben die Rückverfolgung
  in den Bericht.
- **Punkt 5: kein Fehler des Ports.** r100 lädt das Ereignis der Szene
  (`evd/r100s03.evd`) vorab, sobald Leon Bereich 6 am Weg betritt, und setzt
  dabei das Raum-Speicher-Flag 0 (`R100Main`). Das Flag kommt in den
  Spielstand, das Vorladen nicht: Nach dem Laden ist Flag 0 gesetzt, also lädt
  `R100Main` nicht mehr vor, und `r100_Sce_look` findet beim Betreten des
  Hauses kein geladenes Ereignis (`readEvent(0, 1)`: `waitLoadOk` schlägt
  fehl, im Protokoll nur `setData("evd/r100s03.evd")` ohne Lesen). Die
  Testhilfe `-SaveTest` speicherte vor dem Haus; die Schreibmaschine von r100
  (Bereich 0x14, Typ 8, bei etwa x −53500, z −15100) steht dahinter, im
  Original ist dieser Spielstand also nicht möglich. Im Geräteskript
  vermerkt.
- **Punkte 2 und 3 (Entscheidung des Projektinhabers, 17.10.2026):** Eine
  neue Karte ist formatiert und leer wie eine aus dem Laden: `memcard.cpp`
  schreibt beim Anlegen die fünf Systemblöcke genau wie `CARDFormat.c`
  (Kopf mit Seriennummer aus der Flash-ID und der Uhrzeit, zwei leere
  Verzeichnisse, zwei FATs mit 251 freien Blöcken); die Frage zum Formatieren
  kommt nur noch bei einer kaputten Karte. `RE4_MEMCARD_NEW=blank` (Gerät:
  `-BlankCard`) legt eine unformatierte an. Die Meldung „Systemdatei wird
  geladen“ bei der Startprüfung entfällt, und eine fehlende Systemdatei legt
  das Spiel ohne Frage und ohne Meldungen an (Patch 0060, auf Nachfrage
  ergänzt; `RE4_CARD_BOOT_MESSAGE=1` verhält sich wie das Original). Kern-Test „card“:
  die neue Karte bindet ohne Formatieren ein, ihre Systemblöcke sind bis auf
  Zeit und Seriennummer gleich mit denen nach `CARDFormat`.

### Nachtests (17.10.2026, Stand `78aa0b9`): M10 erreicht

- **PC-Prüfung** (`-OnlyCard`, „ALLES OK“): Lauf A legt die Systemdatei
  jetzt ohne Frage an (Schritte 0, 2–10), speichert in r100; Lauf B liest
  die Systemdatei (Helligkeit 72), lädt im Titel und bringt r100 nach
  „Retry from a continue point“ nach 31 Bildern wieder in Gang (vor Patch
  0059 hing das Spiel dort), 2400 Bilder ohne Fehler.
- **Gerätetest 1** (`-Manual -NewCard`): laut Projektinhaber alles in
  Ordnung (Start ohne Kartenmeldung, Szene mit dem ersten Ganado, Speichern
  an der Schreibmaschine, „Retry“ und Laden im Pausemenü ohne schwarzes
  Bild); `senden.zip` entstand nicht, weil die umbenannte Datei schon
  existierte, kein neuer Lauf nötig.
- **Gerätetest 2** (Neustart): Startprüfung liest die Systemdatei
  (Helligkeit 63 wie eingestellt), im Titel auf 64 verstellt, „Load“ →
  Datei 1 geladen, r100 läuft; 0 abgebrochene Töne. Laut Projektinhaber
  Laden und Einstellungen in Ordnung.
- **Damit erreicht:** Speicherkarte in Schacht A als Datei, Startprüfung,
  Formatieren (nur noch für kaputte Karten), Speichern an der
  Schreibmaschine, Laden im Titel und im Pausemenü, Systemdaten über einen
  Neustart, Spielstände mit Banner und Symbol.
- **Offen / nicht nachgebildet:** `.gci`-Import/Export (geplant für M11,
  siehe unten), Schacht B, Karte während des Spiels wechseln, `OSResetSystem`
  (Neustart-Angebot bei „kein Platz“; bei 251 freien Blöcken nicht zu
  erreichen).

### Entscheidung des Projektinhabers (17.10.2026): Karten-Import/Export im SELECT-Menü

(Überholt durch die Entscheidungen unter „M11 Android-Feinschliff“: SELECT
kurz öffnet das Menü, SELECT gehalten das Debug-Menü; Austausch mit Dolphin
und GameCube.)

Der Import und Export von Spielständen (`.gci`, eine Datei je Spielstand
wie bei Dolphin) kommt in ein **Menü der App, das sich öffnet, wenn man
SELECT eine Sekunde lang hält** (wie im früheren Android-Port des
Projektinhabers). Eingeplant für **M11 Android-Feinschliff**; das Menü kann
dort auch die Grafikoptionen aufnehmen. Hinweise für die Umsetzung:

- SELECT ist keine GameCube-Taste, das Spiel braucht sie nicht. Im
  Kernmodus schaltet SELECT heute die Diagnose-Anzeige
  (`CoreInput.java`, `APP_OVERLAY`); kurzes Drücken kann das weiter tun,
  langes Halten öffnet das Menü. Vorbild für Menüs mit Controller-Bedienung:
  `CheatDialog.java`/`GameScreenDialog.java` (ARM64-Modus, dort öffnet
  SELECT das Cheat-Menü sofort, `ControllerActivity.java` „1 << 7“).
- Die Kartendatei liegt in der Byte-Reihenfolge des Geräts (Entscheidung
  16.10.2026). Für den Austausch mit Dolphin oder einer echten GameCube-Karte
  muss der Umwandler die Karten-Strukturen (Verzeichniseintrag) **und** den
  Inhalt der Spielstände (`bh4_system`, `bh4_dataNN`, Aufbau in
  `src/game/card.cpp`, CRCs neu rechnen) in GameCube-Reihenfolge bringen.
  `port/tools/memcard_list.py` kennt die Felder schon. Zu Beginn von M11
  klären: nur Sicherung zwischen Geräten mit dem Port oder auch Austausch mit
  Dolphin/GameCube.
- Vorschlag (noch nicht gebaut): Während das Menü offen ist, hält der Kern
  an; ein Import schreibt nur, wenn kein Kartenzugriff des Spiels läuft, und
  das Spiel liest die Karte bei der nächsten Prüfung neu (Lade-Bildschirm).

**Nicht geprüft (Stand des Abschnitts „Prüfungen (Cloud)“):** die PC-Prüfung mit Disc-Daten
(Abschnitt 7: die Bildschirme des Spiels beim Speichern und Laden, das
Formatieren über den Dialog), der Android-Build (das NDK ist in der Cloud
nicht erreichbar) und alles auf dem Gerät. Nicht nachgebildet: Karte während
des Spiels herausziehen/einstecken, Schacht B, `OSResetSystem` (bei „kein
Platz“ bietet das Spiel einen Neustart an; der Platzhalter kehrt zurück),
`.gci`-Import/Export.

## M11 Android-Feinschliff

### Entscheidungen des Projektinhabers (17.10.2026)

- **Reihenfolge:** M11 kommt vor dem Raum-Rundgang (10b); danach soll die
  APK an einige Tester gehen.
- **Diagnose-Anzeige** (SELECT, Text oben links) ausgebaut.
- **SELECT kurz** öffnet das Menü der App: Grafikoptionen, Import/Export der
  Speicherkarte, (Antwort auf die Frage nach einer Offline-Protokollfunktion)
  das Fehlerprotokoll.
- **SELECT eine Sekunde halten** öffnet das Debug-Menü des Spiels (bisher
  L + START, L2 + START auf dem Retroid).
- **`.gci`-Import/Export** soll mit Dolphin und einem echten GameCube
  gehen.

### Bestandsaufnahme (17.10.2026, Cloud)

- Das „alte“ SELECT-Menü ist das „Ingame Options“ des ARM64-Modus
  (`CheatDialog.java`: Cheats, Waffe hinzufügen, MSAA, AF, Mipmapping,
  Raum-FPS, Tiefenunschärfe, Kreiselsensor). Im Kernmodus übernehmen die
  Cheats das Debug-Menü des Spiels; Raum-FPS, Kreisel und Tiefenunschärfe
  gehören zu den eigenen Spielmodulen der App bzw. bräuchten einen Fokus aus
  dem Spielzustand (nicht in M11).
- Der GX-Renderer der App kannte nur die EFB-Stufe (`RE4_GX_SCALE`) und das
  4:3-Bild (`RE4_GX_ASPECT`), beides nur als Testschalter.
- Ein Spielstand ist eine Kopie von Spielvariablen (`card.cpp`
  `makeSaveData`): GLOBAL_WK 0x4F80..0x8678, Gegenstände, Räume, Menü-Wort,
  Händler, dazu Banner/Symbol (Texel bleiben im Port big-endian) und zwei
  CRCs. Für den GameCube muss jedes Feld nach seiner Größe umgedreht werden.
  Die Typen kommen vollständig aus den Headern; unbekannt sind nur
  Platzhalter (`pad_*`, 884 Bytes in GLOBAL_WK, je 2–4 Bytes in Gegenständen
  und Gegnerliste). In allen fünf Spielständen des Projektinhabers (PC-Prüfung
  und Retroid) sind diese Bytes 0.
- Der Kern lief im Hintergrund weiter (die App hielt nur den Ton an).

### Umsetzung (17.10.2026, Cloud)

- **Byte-Reihenfolge der Spielstände** aus den Headern statt von Hand:
  `port/tools/save_layout.py` übersetzt eine Probe mit den vorbereiteten
  Headern (Schalter wie `card.cpp`, also mit den umgedrehten Unions von Patch
  0016), liest die Debug-Information (`llvm-dwarfdump`) und schreibt
  `runtime/save_layout.inc`: je Struktur die Läufe der Felder breiter als ein
  Byte (Unions: das breiteste Mitglied) und die Platzhalter. `whole_build.sh`
  prüft mit `--check`, dass die Tabelle zu den Headern passt.
- **`runtime/gci.cpp`:** dreht `bh4_system` und `bh4_dataNN` Feld für Feld
  um, prüft vorher die CRCs in der Reihenfolge der Quelle (und die Version
  0x116) und rechnet sie danach neu (Kopf-CRC zuerst, sie liegt im Bereich
  der Datei-CRC). Der Verzeichniseintrag (`CARDDir`) wird ebenso umgedreht;
  eine `.gci` ist Eintrag + Blöcke wie bei Dolphin, Dateiname wie Dolphins
  GCI-Ordner (`08-G4BE-bh4_data00.gci`). Abgewiesen: andere Spiele, andere
  Version, falsche CRC, zu kurz. Bytes der Platzhalter, die nicht 0 sind,
  gehen als Bytes mit und werden geloggt.
- **`runtime/card_files.cpp`:** Dateien auf dem Kartenabbild von
  `memcard.cpp` wie die Bibliothek: aktuelle Verzeichnis-/FAT-Kopie
  (höherer Prüfcode), Blöcke wie `__CARDAllocBlock`, neue Kopie mit Prüfcode
  + 1 in die andere Hälfte (erst die Blöcke, dann FAT, dann Verzeichnis),
  Ersetzen gibt die alten Blöcke frei.
- **Kern (`core_api.cpp`):** `re4_core_pause` hält das Spiel in seinem
  nächsten `PADRead` an (Frame-Hook) und lässt es wieder laufen; die
  Kartenfunktionen des Menüs nur, wenn das Spiel steht **und** die
  Karten-Bibliothek die Karte nicht eingebunden hat (`__CARDBlock[0].attached`,
  also zwischen den Kartenbildschirmen des Spiels); sonst `RE4_CARD_BUSY`.
  `re4_core_debug_menu` + Patch 0061: das Debug-Menü auf Wunsch der App.
- **Renderer (`gx_gl.cpp`):** `GxOptions` – Mehrfachabtastung des EFB
  (MSAA 2×/4×; vor jeder Kopie aus dem EFB wird der Bereich in die Texturen
  aufgelöst, die die Kopien lesen) und anisotrope Filterung (nur Texturen mit
  Mipmaps und linearem Filter; Effekte mit Punktfilter bleiben). Grenzen des
  Geräts (`Limits`). `core_mode.cpp`: eine Änderung baut den Renderer beim
  nächsten Bild neu, sobald das Spiel wieder läuft (der Kern schickt dann
  den ganzen Zustand); Stufe „Auto“ = 336 Bildzeilen auf etwa die
  Bildschirmhöhe, höchstens 3×; 4:3 zur Laufzeit.
- **App:** `CoreMenu.java` (Grafik: Auflösung Auto/1×–4×, Kantenglättung,
  Texturfilter, Bildformat 16:9/4:3; Speicherkarte: Dateien mit Raum,
  Spielzeit, Datum, einzeln oder alle exportieren, importieren mit Frage vor
  dem Ersetzen; Fehlerprotokoll; Spiel beenden). Deutsch auf deutschen
  Geräten, sonst Englisch. `CoreActivity.java`: SELECT kurz (beim Loslassen)
  = Menü, 1 s gehalten = Debug-Menü (kurze Vibration); Spiel steht im Menü
  und solange die App nicht vorne ist; R3-Grafikaufnahme nur mit
  `RE4_APP_TOOLS=1` (Testskript) oder dem Schalter im Menü.
- **Fehlerprotokoll (`CoreLog.java`, `ReportProvider.java`):** zeichnet
  ab dem Start auf (Standard an): `logcat` der App in
  `Android/data/<app>/files/logs/log.txt` (2 × 4 MB; bis 17.10.2026 `protokoll.txt`); ein Absturz
  schreibt Signal, Stelle (Bibliothek + Versatz) und mögliche Rücksprünge in
  `crash.txt` (`core_mode.cpp`, danach der Absturzbericht des Systems wie
  bisher). Beim nächsten Start wandert alles nach `logs/previous/`, dazu was
  Android über das Ende sagt (`ApplicationExitInfo`, ab Android 11) und die
  Absturzzeilen des Systems. **Hänger:** stehen die Spielbilder 8 s lang
  still, ohne dass das Spiel angehalten ist (Menü, Hintergrund), schreibt
  die App dreimal im Abstand von 200 ms, wo der Spiel-Thread steckt
  (`re4_core_frame_count`, `re4_core_game_thread`, SIGUSR2 an diesen Thread,
  pc/lr/Stapel mit den Funktionsnamen aus `dladdr`), ebenfalls in
  `crash.txt`. „Bericht senden“ packt beides mit Gerätedaten
  und der Dateiliste der Karte (ohne Spielstände) in eine ZIP und gibt sie
  an Mail/Messenger; „Bericht speichern“ legt sie in einen Ordner.
- **Debug-Menüs nur mit Text** (Wunsch des Projektinhabers, 17.10.2026): die
  senkrechten, flackernden Balken links und rechts im Debug-Startmenü (und im
  Debug-Menü im Spiel) sind die Zeitbalken der Debug-Fassung (`debug.cpp`
  `DebugControl`: Prozess-Zeitbalken, Primitivpuffer, Heap, dazu der
  Controller-Monitor). `ConfigSet` schaltet `DBG_PROC_BAR` und `DBG_PAD_INFO`
  beim Start ein, gezeichnet werden sie, sobald `debug_mode` an ist – also
  genau, wenn ein Debug-Menü offen ist. `state_trace.cpp` schaltet beide
  Flags bei `RE4_DEBUG_HUD=0` (Standard der App) einmal ab, sobald sie das
  erste Mal an sind (Log: „run: the debug bars … switched off“); der
  Flag-Editor des Debug-Menüs kann sie wieder einschalten, `-DebugHud` des
  Testskripts lässt sie an.
- **Werkzeuge:** `re4_host --gci-export KARTE ORDNER`, `--gci-import KARTE
  DATEI.gci [--replace]`; `memcard_list.py DATEI.gci` liest eine `.gci` für
  sich in GameCube-Reihenfolge (ohne den Umwandler des Ports).

### Prüfungen (Cloud, 17.10.2026)

- Kern-Build i686 und armhf (unter qemu) mit allen Prüfungen grün. Neu im Kerntest „card“: ein
  Spielstand und die Systemdatei, mit der Bibliothek angelegt, als `.gci`
  exportiert – Felder gegen die Strukturen geprüft (Spielzeit, Geld, Raum
  mit dem Stage-Byte zuerst, Position als float, Statuswort, Gegenstände,
  Räume, Händler), Banner unverändert, CRCs wie ein GameCube sie rechnet –,
  wieder importiert (ohne Ersetzen abgewiesen, mit Ersetzen und als neue
  Datei angenommen); danach findet die Bibliothek Verzeichnis und FAT in
  Ordnung und liest genau ihre Bytes zurück, auch nach einem Neustart;
  andere Spiele, zu kurze Dateien, falsche CRC abgewiesen. Neuer Kerntest
  „pause“: das Spiel steht im `PADRead`, Kartenfunktionen nur dann, das
  Debug-Menü einmal je Wunsch. `memcard_list.py` liest die exportierte
  `.gci` unabhängig: CRCs in Ordnung, Version 0x116, Raum r100.
  `save_layout.py --check` passt.
- Die echten Karten des Projektinhabers (PC-Prüfung, Retroid): Export →
  `memcard_list.py` liest Optionen, Raum, Datum richtig in
  GameCube-Reihenfolge; Export → Import auf eine neue Karte ergibt
  byte-gleiche Dateien und Einträge.
- App-Renderer (`gx_gl_check`): alle bisherigen Prüfungen, neu mit 4× MSAA
  und 8× AF: Bildkopie, schräge Kante geglättet, EFB-Kopie als Textur und
  Palette über den aufgelösten EFB. `libre4decomp_link_check` (core_mode.cpp
  auf dem Host) gebaut.
- Java der App gegen die Android-Klassen (Robolectric `android-all`, API 35,
  mit einer erzeugten `R`-Klasse) mit `javac` übersetzt: fehlerfrei.
  `CoreInputTests` 69 Prüfungen bestanden.

**Nicht geprüft:** der Android-Build (NDK, Gradle, Lint), alles auf dem
Gerät (Menü, SELECT kurz/lang, Debug-Menü, Debug-Menüs ohne Balken, Grafikoptionen und ihre
Leistung, Import/Export mit der Dateiauswahl, Fehlerprotokoll und Bericht,
Absturzmelder), die PC-Prüfung mit Disc-Daten nach Patch 0061, und **ob
Dolphin und ein GameCube die exportierten Spielstände laden** (dafür
braucht es Dolphin mit der Debug-Disc auf dem PC).

### Erster Gerätelauf mit dem Menü (17.10.2026, Retroid, Stand `d637611`)

`test-core-app.ps1 -Manual`: Android-Build (NDK, Gradle, Lint) fehlerfrei.
Laut Projektinhaber und Protokoll: SELECT öffnet das Menü, das Spiel steht
(„core: paused“ … „running again“); SELECT gehalten öffnet das Debug-Menü;
die Debug-Balken sind weg; Grafikwechsel bauen den Renderer neu (Stufe 2/3/4,
4× MSAA, 16× AF, 4:3, „GX renderer ready … 4 samples (of 4), anisotropy 16
(of 16)“), r100 danach weiter flüssig (gezeigt 56,7 Bilder/s, Renderer
4,8 ms je Bild); „Alle exportieren“ schrieb `bh4_system`, `bh4_data00`,
`bh4_data18`. Die drei `.gci` liest `memcard_list.py` unabhängig in
GameCube-Reihenfolge richtig, auch Datei 19 (r200, Kapitel 6, Modus 2);
die Platzhalter-Bytes sind auch dort 0. Nicht geprüft: Import auf dem
Gerät, Bericht senden, Dolphin.

Aufgefallen: die Zahlen „GX-Arbeit je Bild“ im Protokoll liefen nach einem
Neubau des Renderers über (Zähler des alten Renderers abgezogen); jetzt
beginnen sie mit dem neuen Renderer bei 0.

**Wünsche des Projektinhabers zum Menü (umgesetzt, nicht auf dem Gerät
geprüft):** die Auflösung steht sofort bei der Wahl da (Bildpunkte, Stufe,
bei Auto wie `core_mode.cpp` aus der Bildschirmgröße berechnet, dazu die
Bildschirmgröße); kein „Weiterspielen“ (B schließt), stattdessen eine Linie
unter dem Einleitungstext; Überschriften mittig in Kapitälchen mit Linien
links und rechts; die beiden Schalter des Fehlerprotokolls nebeneinander;
neuer Schalter **FPS-Anzeige unten links** (Spielbilder je Sekunde, aus dem
Bildzähler des Kerns, jede Sekunde neu; steht während Menü/Hintergrund);
„Spiel“ und „Spiel beenden“ entfernt (Beenden weiter mit der Zurück-Taste).

### Absturz in r206 (PC-Prüfung 17.10.2026, Stand `d637611`): REL-Kopf

`test-r100-core.ps1 -Room 206`: Absturz in Bild 613 beim Stagewechsel
(`gameStageInit` → `StageSet` → `cRoomData::linkRelData` → `mem_alloc` →
`OSAllocFromHeap`, Fehleradresse 0x5cae18c8), kurz nach „Read File:
rel/st2_1.rel“. Ursache: Die Raumprogramme liegen als eigene Dateien auf der
Disc (`rel/st1_0.rel`, `rel/st2_1.rel` …). Der Formatumwandler ließ ihren
Kopf big-endian („OSLink converts its header“), aber `linkRelData` (und
`sscrn.cpp`) holt **vor** `OSLink` Speicher für `bssSize`. Big-endian gelesen
wird der Wert riesig; bei `st2_1.rel` ist er negativ, `OSAllocFromHeap`
legte dann den Rest-Block an einer Adresse weit unter dem Heap an
(0x80AE18.. − 0x24000000 ≈ 0x5CAE18..) und stürzte ab. Bei `st1_0.rel`
fiel es nicht auf. Die Raumprogramme in Archiven bekamen ihren Kopf schon
immer beim Laden umgedreht (`convertArchive`).

Korrektur: `rel/*.rel` hat einen eigenen Umwandler (`convertRel`,
`formats.cpp`), der den Kopf gleich beim Laden in Host-Reihenfolge bringt
(`re4_rel_header_to_host`; `OSLink` lässt einen umgedrehten Kopf in Ruhe).
Neuer Kerntest „rel“: ein Kopf mit einer `bssSize`, die big-endian negativ
wäre, kommt in Host-Reihenfolge an, ein zweiter Lauf ändert nichts, ein
späterer Teil der Datei bleibt unberührt; ohne die Korrektur schlägt der
Test fehl.

**Andere Räume** (Frage des Projektinhabers): Fast jeder Raum lädt eines von
15 Raumprogrammen als eigene Datei (`src/game/roomdata.cpp`, `rel_file`):
`st1_0` (r100, r120), `st1_1` (r101–r10b), `st1_2` (r10c–r10e, r119–r11b),
`st1_3` (r10f, r111–r113, r117, r118, r11c–r11f), `st2_0` (r200–r203, r207,
r208, r210, r222), `st2_1` (r204–r206, r209–r20f), `st2_2` (r211–r219),
`st2_3` (r21a–r22b), `st2_4` (r22c), `st3_0` (r300–r30e), `st3_1` (r30d,
r30f–r312), `st3_2` (r315–r31d), `st3_3` (r320–r333), `st4_0` (r400–r411),
`st0` (Testräume r000–r042). Vor der Korrektur hing es vom Speicherwert der
einzelnen Datei ab: niedrigstes Byte ≥ 0x80 → negativ → Absturz (sicher bei
`st2_1`), sonst eine zu große Anforderung, die still scheiterte (`st1_0`,
`st1_1`, `st2_0` liefen: r100, r101, Spielstand in r200). Dasselbe Lesen vor
`OSLink` machen auch das Untermenü-Programm (`sscrn.cpp`, `rel/Sscrn.rel`
über ARAM) und die Werkzeuge des Debug-Menüs (`db_menu.cpp`, `rel/<werkzeug>`);
die Korrektur deckt alle ab, weil jede `rel/*.rel` beim Laden umgedreht wird.
Der Datenblock „DRA“ ohne Umwandler (gemeldet in r206) gehört nur zum
Entwickler-Werkzeug `t_dr.cpp`.

**Raum-Rundgang in der PC-Prüfung** (neu, für die Nachprüfung und für 10b):
`test-r100-core.ps1 -Rooms LISTE` (`whole_r100_check.sh --rooms`, Abschnitt
8): nach dem Build jeder Raum über den Debug-Start, `-TourFrames` Bilder der
Hauptschleife (Standard 1500), je Raum eine Zeile (läuft / Absturz mit
Bibliothek, Versatz und Funktion / Stillstand mit Rückverfolgung), die
Einzelheiten in `rundgang-einzelheiten.txt`. `-Rooms set`: zwei Räume je
Raumprogramm (27), `-Rooms all`: jeder Raum der Tabellen (etwa 150). In der
Cloud nur mit Platzhalter-Daten geprüft (Ablauf und Bericht).

**Erster Rundgang mit Disc-Daten** (PC-Prüfung 17.10.2026, Stand `1af1916`,
`-Rooms set`, i686, je 1500 Bilder): 26 von 27 Räumen laufen, **r206
läuft** (die Korrektur oben wirkt). Absturz nur in **r311** (unten).

### Absturz in r311 (Rundgang 17.10.2026): Division durch null in `SeAtCheck`

Bild 619, gleich nach dem Raumstart: Signal 8 (`SIGFPE`) in `SeAtCheck`
(`src/game/se_at.cpp`, aus `SndWatcher`). Die Raumgeräusche (Datei „ESE“
im Raumarchiv) spielen ihr Geräusch und warten dann entweder eine feste
Zeit (`interval`) oder `rnd_base + Rnd() % rnd_interval` Bilder. r311 hat
ein Geräusch mit `interval` 0 **und** `rnd_interval` 0: eine Division durch
null. Der GameCube rechnet einfach weiter (`divw` durch 0 ergibt den
Quotienten 0, der Rest ist damit `Rnd()`, also 0–255), ein x86-Prozessor
bricht das Programm ab, ARM bricht nicht ab, die Divisionsroutine des
Compilers liefert aber keinen festgelegten Rest. Die Umwandlung der Datei
ist richtig (alle zehn 16-Bit-Felder ab 0x10 werden gedreht; ein Fehler dort
hätte schon die Versionsprüfung von `SeAtInit` scheitern lassen); die Null
steht wirklich in den Daten.

Korrektur: **Patch 0062** (`se_at.cpp`): bei `rnd_interval` 0 nimmt der
Port das Ergebnis des GameCube (`Rnd()`); `Rnd()` wird weiterhin genau
einmal aufgerufen, die Zufallsfolge bleibt gleich. Geprüft: Build i686 und
armhf in der Cloud. **Nicht geprüft:** r311 mit Disc-Daten (nächster
Rundgang). Ein Test ohne Disc-Daten fehlt bewusst: `SeAtCheck` erreicht die
Stelle nur, wenn `SndCall` wirklich spielt (Tonbank, Treiber).

Weitere Divisionen durch null, die der GameCube still übergeht, sind
möglich; der Rundgang über alle Räume (`-Rooms all`) findet sie auf dem PC
als Absturz mit Funktionsnamen.

### Rundgang über alle Räume (PC-Prüfung 17.10.2026, Stand `69bbc10`)

`-Rooms all`, i686 -O2, je 1500 Bilder: **122 von 126 Räumen laufen**, r311
läuft (Patch 0062 wirkt). Vier Fundstellen; die Adressen im Bericht passen
genau zum gleichen Build in der Cloud, die Befehle an den Stellen sind
nachgesehen:

| Raum | Was | Ursache | Stand |
|---|---|---|---|
| r404 | Stillstand beim Raumstart (`SINF` ← `cLightInfo::updateMatrix` ← `cModel::setAng` ← `r404_initEmSet`) | Der Söldner-Raum wählt einen von drei Startpunkten und holt den Winkel über `&pos + n * 12 + 0x28`: für den Prüfsummen-Build geschrieben, setzt voraus, dass CodeWarrior `rot` 0x28 Bytes über `pos` in den Stapel legt. Clang ordnet anders, Leon bekam einen Winkel von irgendwo im Stapel, das Zurechtrücken des Winkels (`LIMIT_ANGLE`) endete nie. Im übrigen Spielcode (ohne `src/lib`) gibt es diese Art Adressrechnung sonst nicht | **behoben** (Patch 0063); zweiter Lauf mit Disc-Daten (Stand `4042c81`): läuft |
| r215, r227 | Absturz in `cLightMgr::setEnv` (Lesen bei Adresse 8: kein Lichtschnitt), aus dem Licht-Paket einer Zwischensequenz (`Event::ExePacket_Lit` → `LightMgr.update` → `registCut`) | Erste Vermutung (gleichnamige Lichtdaten mehrerer Zwischensequenzen, `DatTbl::SetDat` behält die erste) war nicht die Ursache; die Änderung dafür bleibt (richtig, Kerntest „event“). **Ursache laut zweitem Lauf:** `evd/r215s02.evd` und `evd/r227s02.evd` (nachgeladene Zwischensequenzen) haben eine 32-Byte-Texttabelle (MDT), deren Block nach dem ersten Wort aufhört (Zwischensequenz ohne Text). Der MDT-Umwandler brach ab, und damit blieb **jede weitere Datei** der Zwischensequenz big-endian, auch ihre Lichtdaten. Dieselbe Tabelle steht als Eintrag 17 in `st2/r215.das` | **behoben:** MDT-Umwandler nimmt den abgeschnittenen Block hin (dreht das eine Wort, lässt den Rest); EVD und EVS wandeln nach einer abgebrochenen Datei die übrigen weiter um. Kerntest „event“ um diesen Fall erweitert (schlägt mit dem alten Umwandler genau so fehl). **Dritter Lauf mit Disc-Daten (Stand `11c4154`): beide laufen** |
| r229 | Absturz in `IdGeneralTrans` beim Funkgespräch (Codec, Bild 789, 96 Bilder nach dem Öffnen) | Die Zeichenschleife (`ExecOt`) ruft die Funktion eines Eintrags auf, deren Adresse **0x74 höher** war als `IdGeneralTrans`: die Gesichts-Animation (`CalculateShape_new`, `*vtx += delta`) schrieb in die Zeichenliste (gleicher Zwischenspeicher `GetPrimBuff`). **Ursache laut drittem Lauf** (Hinweise von Patch 0064): Ein Kanal des Partnergesichts (Modell mit 1576 Eckpunkten in `ss_oc205.dat`) nennt Eintrag 9 einer Formen-Tabelle mit weniger Einträgen. Das Spiel liest dann die Eckpunkt-Listen hinter der Tabelle als Eintrag. Auf dem GameCube ist dessen Anzahl big-endian 0xFFFF0000, also negativ, und es wird nichts addiert; in Host-Reihenfolge sind dieselben Bytes 65535, und die Werte gingen weit hinaus. (Geteilte Eckpunkt-Listen, die erste Vermutung, waren es nicht; die Änderung dafür bleibt, Kerntest „morph“.) | **behoben:** Patch 0064 gibt einem Kanal hinter der Tabelle keine Werte, wie der GameCube, und überspringt Eckpunkt-Nummern außerhalb des Modells; beides wird einmal gemeldet. Brücken-Test um den Fall erweitert (schlägt ohne die Korrektur fehl). Der dritte Lauf (nur mit der Nummernprüfung) lief schon durch; **vierter Lauf (Stand `0aa2337`): läuft**, nur noch die erwarteten Hinweise (Kanäle 5 und 6 nennen die Einträge 9 und 10 einer Tabelle mit 9 Einträgen), keine falschen Eckpunkt-Nummern mehr |

**Vierter Rundgang über alle Räume** (PC-Prüfung 17.10.2026, Stand
`0aa2337`, i686 -O2, je 1500 Bilder ab der Hauptschleife): **126 von 126
Räumen laufen**, ALLES OK. Damit ist jeder Raum der Raumtabellen einmal
gestartet und 1500 Bilder gelaufen (Debug-Start, ohne Spielen; was erst beim
Spielen passiert, prüft 10b). Auf dem Retroid nicht wiederholt.

Der Rundgang-Bericht zeigt bei Problemen jetzt zusätzlich die letzten 30
Zeilen **vor** dem Absturz (vorher nur die Rückverfolgung), die letzten
Meldungen des Formatumwandlers, **alle** abgebrochenen Umwandlungen und die
Hinweise der Port-Prüfungen (`check: …`, auch bei Räumen, die laufen). Das
PC-Prüfprogramm schreibt beim Absturz die Register und den Speicher um jedes
Register, das in den GameCube-Speicher zeigt.

Noch offen aus dem Lauf: `st2/r215.das` Eintrag 9 (`ITM`, 0x40 Bytes) wird
nicht vollständig umgewandelt; der Grund stand nicht im Bericht (der nächste
zeigt ihn). r215 lief trotzdem 750 Bilder.

### Vom Projektinhaber gemeldet (17.10.2026, ohne Protokoll) – für 10b

- Vereinzelt lässt sich ein Gegenstand nicht aufnehmen: das „A“-Symbol
  erscheint, danach reagiert das Spiel nicht mehr.
- Super Magnum: lädt nur nach, solange R2 gehalten wird; lässt man los,
  senkt Leon die linke Hand und die Patronen fliegen von selbst in die
  Waffe.
- Raum r206 springt zurück ins Startmenü der App (Disc-Import): ein Absturz
  des Kerns, gefunden und behoben (oben, „Absturz in r206“); läuft in der
  PC-Prüfung mit Disc-Daten und **auf dem Retroid** (17.10.2026).

### Zweiter Gerätelauf M11 (Retroid, 17.10.2026, Stand `3a9a8a6`, `-Manual`)

- Dritter Gerätelauf (17.10.2026): Zwischentext unter dem Titel auch
  zentriert; **alle Texte, die wir einbauen, nur Englisch** (Menü,
  Meldungen, Fehlerbericht und seine Dateinamen: `logs/log.txt`, `crash.txt`,
  `previous/`, `exit.txt`, `device.txt`, `re4-report-….zip`); neuer Schalter
  **„Depth of field“** (Tiefenschärfe, wie im ersten Android-Port; Standard
  an = das Spiel wie es ist; Patch 0065, `re4_core_set_dof`). Java gegen die
  Android-Klassen und die JNI-Bibliothek auf dem Host gebaut, Kerntest
  „pause“ prüft den Schalter; **auf dem Gerät nicht geprüft**.
- Vierter Gerätelauf (17.10.2026, Stand `b778140`): der Schalter kommt im
  Kern an (`core: depth of field off/on`), macht aber in r100 keinen
  sichtbaren Unterschied. Grund: das Original zeichnet Tiefenschärfe nur, wo
  ein Lichtschnitt des Raums (`FocusLevel`) oder eine Zwischensequenz
  (Fokuskurven) sie verlangt; die sichtbare Unschärfe der ersten App
  (RE4-Decomp, `depth_of_field_gl.cpp`) war ein **eigener Autofokus** der
  App, kein GameCube-Effekt. Wunsch des Projektinhabers (17.10.2026): bei
  „an“ der Hintergrund unscharf wie in RE4-Decomp. Umgesetzt als
  **Autofokus im Kernmodus** (Patch 0065 erweitert, `runtime/dof.cpp`,
  `gx_gl.cpp Autofocus`): wenn der Spieler frei ist (Hauptschleife, keine
  Zwischensequenz, kein Untermenü) und das Spiel selbst keinen Fokus
  anlegt, legt `Filter01Trans` einen Eintrag in die Filtertabelle (OT
  0x12, wo auch die Tiefenschärfe des Spiels läuft: nach der Szene, vor den
  Tabellen der Anzeigen 0x13–0x15). Er schreibt BP-Register 0x5A (gibt es
  auf dem GameCube nicht) in den GX-Strom; der GX-Nachbau der App zeichnet
  dort den Weichzeichner der ersten App: Entfernung in der Bildmitte
  (Median aus 9 Tiefen) = Fokus; was deutlich dahinter liegt, wird bis zu
  60 % weich, Näheres bleibt scharf, nur Punkte hinter dem Fokus werden
  gemischt; Fokus weiter als 6 m (Blick in die Ferne) = keine Unschärfe.
  Entfernungen aus der Perspektive der Szene und dem XF-Viewport. „Aus“
  schaltet beides ab (Spiel und App). `RE4_DOF_AUTOFOCUS=0` lässt den
  Autofokus weg (nur das Bild des Spiels). Das Protokoll schreibt, welche
  Tiefenschärfe läuft (`dof: from frame N: light cut …`, `event near/far`,
  `autofocus (app)`, `none`), die Zeile „GX:“ der App zählt die
  Autofokus-Durchgänge. Geprüft: `gx_gl_check` (echtes GLES3, mit und ohne
  4-fach-MSAA: ferne Kante weich 209/46, nahe Kante scharf, Alpha bleibt,
  Blick in die Ferne scharf), Kern-Laufzeittest „gx“ (die Markierung kommt
  jedes Mal im Strom an, Decoder-Prüfsumme unverändert), i686 und armhf.
- Fünfter Gerätelauf: Absturz im Debug-Menü (SELECT halten), Waffenseite,
  beim Navigieren über den ersten bzw. letzten Eintrag hinaus
  (`eprintf_main`, Fehleradresse 0, aus `tp_pl_weapon`): Patch 0066. Die
  anderen Seiten des Debug-Menüs (`t_option.cpp`, `t_util.cpp`
  `ToolMenu`, `t_flag.cpp`, `t_log.cpp`, `t_bugcheck.cpp`) laufen mit
  richtiger Anzahl im Kreis bzw. begrenzen.
- Fünfter Gerätelauf (17.10.2026, Stand `d929d4b`): der Autofokus läuft
  (941 Durchgänge im Protokoll), aber **im Stand ist alles unscharf**: die
  Bildmitte liegt dann auf Leon bzw. seiner Waffe, er wird zum Fokus und
  alles hinter ihm weich (in RE4-Decomp dasselbe Problem; dort wurde der
  Fokus vor dem Zeichnen Leons gemessen). Behoben: der Kern rechnet je Bild
  aus, wie tief Leon und seine Waffe ins Bild reichen (Tiefe ihrer Gelenke
  in Blickrichtung der Kamera, `cParts::mat`, plus 60 cm für Körper, Hand
  und Lauf; `re4_dof_player_depth`, `dof.cpp`) und schickt das als Wert der
  Markierung (BP 0x5A). Der GX-Nachbau nimmt den Fokus nur aus Punkten
  dahinter: die 9 Tiefen der Mitte, oder, wenn weniger als 5 davon hinter
  Leon liegen, 35 um die Mitte (15 % breit, 12 % hoch); keiner dahinter =
  keine Unschärfe. Geprüft: `gx_gl_check` mit einer Szene wie im Stand
  (Leon 1 m in der Mitte, Wand 3 m, ferne Kante 20 m): ohne Leons Tiefe wird
  die Wand weich (36/219, der gemeldete Fehler), mit ihr bleibt sie scharf
  (0/255) und die ferne Kante wird weich (209/46), Leon bleibt scharf;
  nichts hinter Leon = scharf; mit und ohne MSAA. **Auf dem Gerät nicht
  geprüft.**
- Menü (SELECT kurz): passt laut Projektinhaber („wunderbar“); Wünsche:
  die Linie unter dem Titel weg (die Linien der Überschrift „Grafik“ reichen),
  Titel „Resident Evil 4 Options“ zentriert, ohne Linien. Umgesetzt
  (`CoreMenu.java`), Java-Übersetzung gegen die Android-Klassen geprüft,
  **auf dem Gerät nicht geprüft**.
- r206 läuft auf dem Gerät.
- Super Magnum: die Nachlade-Animation ist weiter falsch (die Patronen
  fliegen von selbst in die Waffe, Leon lädt nicht). Wird vor den Testern
  angegangen (Entscheidung des Projektinhabers, 17.10.2026), siehe unten.

### Super Magnum: Nachladen (M11, geklärt)

Ablauf im Spiel: `wep15` (Broken Butterfly, im Debug-Menü „SUPER MAGNUM“)
lässt die Pistolen-Steuerung `wep/pl_handgun.cpp` laufen. Beim Nachladen
spielt Leon die Bewegung `0x2D/0x2E/0x2F` des Waffenarchivs (nach Stufe), die
Waffe selbst `0x33/0x36/0x35` (Trommel und Patronen), beide ab Bild 0.
`wep02_r2_reload` Schritt 1 lässt Leons Bewegung bis `PlReloadEndTbl[15]`
(96/80/52 Bilder) laufen, auch wenn das Zielen losgelassen wird; erst danach
senkt er die Waffe. Gemeldet: mit gehaltenem R lädt Leon, lässt man R los,
senkt er sofort die linke Hand und die Patronen fliegen allein in die Waffe.
Im Code (Tabelle, Waffennummer, Bildzähler, `joyKamae`, `keyReload`,
`EmRoutineSetW`/Patch 0023) ist ohne Lauf keine Port-Abweichung zu sehen.

Werkzeug dafür: **Nachlade-Prüfung** der PC-Prüfung
(`test-r100-core.ps1 -Reload 15`, `whole_r100_check.sh --reload W`):
Debug-Start in r100, sobald Leon frei ist, gibt `RE4_TEST_WEAPON`
(`state_trace.cpp`) ihm die Waffe wie das Debug-Werkzeug `t_option.cpp`
(in einer eigenen Aufgabe auf Platz 2, wie das Werkzeug-Menü; endlose
Munition), dann zweimal Nachladen (einmal mit gehaltenem R, einmal mit R
25 Bilder nach dem Beginn losgelassen). Je Bild eine `wep:`-Zeile (Leons
Zustand und Bewegung, Zielen, Neigung, Zustand und Bewegung der Waffe,
Abstand der linken Hand zur rechten), im Bericht je Nachladen eine
Zusammenfassung, ein Verlaufsauszug und GX-Bilder. Simulierte R/L-Tasten
setzen jetzt auch den Analogwert (`pad.cpp`, wie ein echter Controller).

**Ergebnis der Nachlade-Prüfung (PC mit Disc-Daten, Stand `d732568`):** Der
Kern lädt richtig. Leons Nachlade-Bewegung (Eintrag `0x2C/0x2D` des
Waffenarchivs, 111 Bilder) und die der Waffe (`0x33`) laufen gleichzeitig;
mit gehaltenem R bis zum Ende, mit losgelassenem R (Bild 27) weiter bis
Bild 96 (`PlReloadEndTbl`), dann senkt Leon die Waffe – wie der Code es
sagt. Die linke Hand bewegt sich in beiden Läufen Bild für Bild gleich
(zur Tasche, zurück an die Trommel), die GX-Bilder zeigen das Nachladen,
die Munitionsanzeige zählt hoch.

**Auf dem Retroid** (Projektinhaber, 17.10.2026, Sichtprüfung): sobald R2
losgelassen wird, lädt Leon nicht mehr, die Patronen fliegen in die Waffe,
werden aber nicht geladen. Im Geräteprotokoll begann das Nachladen mit B
**ohne R-Klick** (`0x0200`): R2 war nur halb gedrückt, das Spiel zählt den
Analogwert als R (`game/pad.cpp`, wie der GameCube-Controller); ein zweites
B kam 40 Bilder später. Unterschiede zum PC-Lauf: ARM-Kern statt i686, R nur
analog, das zweite B, die Nachlade-Stufe der Waffe (`weapon_lv_reload`;
nach einem Waffenwechsel im Debug-Menü die des Spielstands). Werkzeug für
den Gerätelauf: `test-core-app.ps1 -WeaponTrace` (`RE4_TRACE_WEAPON=1`): je
Bild beim Nachladen und 40 Bilder danach eine `wep:`-Zeile, dazu Munition,
Waffennummer/Nachlade-Stufe, Tasten (`Joy[0].on`) und R-Analogwert.

**Gerätelauf mit `-WeaponTrace` (Retroid, 17.10.2026, Stand `b778140`):
Ursache gefunden, kein Port-Fehler.** Die Magnum des Spielstands hat die
Stufen aus der Waffenseite des Debug-Menüs (`t_option.cpp
tp_pl_weapon`): Kraft Lv.4, Tempo Lv.3, Magazin Lv.4, **Nachladen Lv.5**
(`weapon_lv_reload` 4; der Spielstand speichert `pG` 0x4FA4..0x500C mit,
die Bytes stehen so auf der Speicherkarte des Laufs). `PlReloadEndTbl` hat
je Waffe nur 3 Stufen; bei Stufe 4 liest `wep02_r2_reload`
`PlReloadEndTbl[15][4]`, also den Eintrag der nächsten Waffe (`[16][1]`,
Messer: **1 Bild**). Damit darf Leon das Nachladen ab Bild 1 abbrechen:
im Protokoll senkt er die Waffe 8 Bilder nach dem Beginn (`pl 0/0/2/1`),
die Waffe geht in ihren Zustand „gesenkt“ (`obj 3/0`), aber
`cObjWep::move` spielt ihre Bewegung weiter (`moveDown` ist bei der Magnum
leer): Bild 8 → 110 der Nachlade-Bewegung `0x33` – die Patronen „fliegen
allein“. Die Munition kommt nur bei Waffen-Bild 34 in `moveReload`, im
gesenkten Zustand nie. Der GameCube rechnet mit derselben Tabelle und
denselben Bytes genauso (der Build trifft die SHA1). Mit einer echten Stufe
(Lv.1–3, wie beim Händler) lädt Leon immer fertig, auch wenn R2 losgelassen
wird, und senkt die Waffe erst ab Bild 96/80/52 (PC-Prüfung oben). Keine
Änderung am Spiel; neu ist der Hinweis `check: weapon … has reload level …`
(`state_trace.cpp`) im Protokoll, sobald eine Waffe eine Stufe über 3 hat.
**Auf dem Retroid bestätigt (Projektinhaber, 17.10.2026):** mit RELOAD
Lv.1 lädt Leon richtig („Reload passt mit Lvl. 1“).
- „Bei einem Gegner abgebrochen“: kein Absturz. Das Testskript beendet die
  App nach der Beobachtungszeit (240 s, Protokoll ohne Fehler). Mit
  `-Manual` beobachtet `test-core-app.ps1` jetzt 30 Minuten (Q beendet
  früher, `-Seconds` legt fest, Bilder alle 2 Minuten) und schreibt beim
  Ablauf „Beobachtungszeit abgelaufen … kein Absturz“.
- Leistung im Lauf: 60 Bilder/s angezeigt, Kern 33,3 ms je Spielbild (30 Hz
  wie das Original) mit 12,9 ms Leerlauf, GPU 17,7–19,7 ms je Bild, Ton ohne
  Verluste.
- **Abschluss M11 (Retroid, 17.10.2026, Stand `093aa7a`, `-Manual`,
  222 s von Hand beendet):** im Stand nicht mehr alles unscharf, der
  Autofokus läuft (`dof: … autofocus (app)`), das Debug-Menü (Waffenseite
  über das Listenende hinaus) stürzt nicht mehr ab, kein Absturz im Lauf
  („Sieht gut aus. Klappt auch das DEBUG Menü“, Projektinhaber). M11 ✅.
- Ein Tester hat ein Gerät, das nur 64-bit-Apps ausführt (kein
  `armeabi-v7a`): der Kernmodus läuft dort nicht. **Entscheidung des
  Projektinhabers (17.10.2026):** erst M11 abschließen (dazu jetzt die
  Nachlade-Animation der Super Magnum, vor den Testern), dann **M12 ARM64
  vor 10b** (eine APK für 32- und 64-bit-Geräte). Der Weg für 64 bit wird
  in der Bestandsaufnahme von M12 mit Zahlen vorgelegt und dann entschieden.

## M12 ARM64

Bestandsaufnahme und Vergleich der Wege: `port/docs/arm64-bestandsaufnahme.md`.
**Entscheidung des Projektinhabers (17.10.2026): Weg A, nativ 64 bit.** Alles,
was das Spiel adressiert, liegt unter 4 GB (wegen der Bit-31-Prüfungen, siehe
„Adressen der Spielbibliotheken“, genauer zwischen `0x80000000` und
`0xFFFFFFFF`); Strukturen mit fester Form (Disc-Daten, Spielstand, feste Plätze)
behalten ihre 32-bit-Form.

Schritte: A1 64-bit-Linux-Ziel baut und linkt → A2 Adressen unter 4 GB →
A3 feste Formen und feste Plätze → A4 Laufzeit- und Kerntests auf x86_64 →
PC-Prüfung mit Disc-Daten → arm64-Build der App, eine APK mit beiden ABIs →
Gerätetest.

### Schritt A1: 64-bit-Linux-Ziel (17.10.2026, Cloud)

`port/tools/whole_build.sh x86_64` (Ziel `x86_64-linux-gnu`,
`cmake/linux-x86_64.cmake`, steht für `arm64-v8a`): **0 Fehler, alle 600
Spieldateien, 114 Module gelinkt, 47 Funktions- und 1 Daten-Stub wie auf
ILP32** (`stubs-expected.txt` unverändert). `--self-test` (Sprungstellen
zwischen Modulen, neue x86_64-Vorlage) und `--check-modules` (114 Module, 0
offene Importe) laufen. Neu:

- Umschreibungen für LP64 (Tabelle „Mechanische Umschreibungen“): genaue
  Breiten in den Original-`types.h`, Alias-Namen mit `RE4_ASM_U32`/`RE4_ASM_NEW`,
  `size_t` in eigenen Deklarationen, Füllfelder, Strukturen mit fester Form
  (`fixed_form.json`, 42 Zeiger-Glieder in 20 Strukturen), `TaskFunc` 4 Bytes.
- Patch 0067 (variadische Aufrufe mit festen Zeigern, `SEQGetVolume`).
- Sprungstellen für x86_64 und arm64 in `whole_link_plan.py`;
  `libre4_main_lp64.ver` (Spiel-`new` heißt dort `_Znwm`), `game_heap.cpp`.
- Größenprüfungen der SDK-Nachbauten (`dvd.cpp`, `os_thread.cpp`) je
  Zeigerbreite; `OSModuleLink`/`ModuleInfo` mit 4-Byte-Verweisen.

Prüfung ILP32 nach den Änderungen: i686 und armhf (qemu) komplett grün
(Sprungstellen, Module, Laufzeit-, Kern-, Karten-, Spielstand-, Format- und
Filmtests). Auf x86_64 bricht
`--runtime-test` wie erwartet beim ersten Modul ab: die Bibliotheken liegen
über 4 GB (`OSLink` springt an eine gekürzte Adresse) → Schritt A2.
`save_layout.py --check` meldet auf x86_64 eine andere Form des Spielstands →
Schritt A3.

### Schritt A2: Adressen unter 4 GB (17.10.2026, Cloud, Linux)

Alles, was das Spiel sieht, liegt im **Fenster `0x80000000–0xFFFFFFFF`**: MEM1
wie bisher am Anfang, darüber Bibliotheken, Stacks und Speicher der
Plattformschicht.

- `runtime/game_window.cpp`: `re4_window_map` holt Adressraum ab `0x83000000`
  (mit `MAP_FIXED_NOREPLACE`, sucht freie Lücken in `/proc/self/maps`);
  `re4_check_window` bricht mit Logmeldung ab, wenn eine Adresse außerhalb
  liegt; `re4_check_game_address` prüft auf LP64 auch die Obergrenze.
- Stacks der Spiel-Threads (`port/src/stack_context.cpp`) kommen aus dem
  Fenster (schwacher Verweis auf `re4_window_map`, sonst wie bisher).
- Linux-Vorlader (`host/loader.c`): belegt auf 64 bit zuerst jede freie Lücke
  zwischen 4 GB und dem Stack (vor dem ersten `malloc`, damit dessen
  brk-Bereich nicht über 4 GB anfängt; `/proc/self/maps` ohne stdio gelesen),
  dann wie bisher alles unter `0x83000000`. Bibliotheken, Thread-Stacks und
  große `malloc`-Blöcke landen danach von selbst im Fenster (Bibliotheken
  oben bei `0xFE…–0xFF…`). Prüft `malloc` auf eine Adresse unter 4 GB; der
  Starter läuft auf einem eigenen Thread mit Stack im Fenster; eine
  `malloc`-Arena. Auf Android legt die App selbst fest, wohin Bibliotheken und
  Stacks kommen (späterer Schritt).
- Patch 0068: Adressen über vorzeichenbehaftete 32-bit-Werte (auf LP64 wird
  daraus `0xFFFFFFFF8…`): `OS_BASE_CACHED` (Absturz in `__AXVPBInit`),
  `cModel::modelInit` (zehn Aufrufer), Formtabelle in `shape.cpp`. Gefunden
  mit `-Wint-to-pointer-cast`: von 1049 Umwandlungen 32-bit-Zahl → Zeiger
  kommen 14 aus vorzeichenbehafteten Typen, davon sind vier Nullen.
- Patch 0069: `GXTexObj` hat im Spiel 32 Bytes; die SDK-Innensicht wuchs auf
  LP64 auf 40 und überschrieb Nachbarspeicher (Division durch null in
  `__GXGetNumXfbLines`). `userData` 4 Bytes, Größenprüfungen für die vier
  Innensichten.
- Testspiegel `TestFileInfo` je Zeigerbreite (`runtime_tests.cpp`).

Ergebnis x86_64: `--self-test` ✅, `--check-modules` ✅ (114 Module, 0
offen), **`--runtime-test` 10 von 10 ✅** (link, dvd, yz2, dvdq, stream, gx,
math, trace, tasks, vsync). `--core-test` bis `axfx` ✅, dann hängt die
Film-Bibliothek: `SFMPS_Init` bleibt absichtlich stehen, weil ihr
Arbeitsblock auf LP64 größer ist als die feste Grenze (CRI, nächster Punkt).

Prüfwerkzeug `port/tools/m12/fixed_layout_check.py`: vergleicht für jede
Struktur, die die Formatumwandler nennen, Größe und Lage aller Glieder
zwischen armhf (Vorbild, wie GameCube) und x86_64. Ergebnis: **83 von 83
gleich**.

### Schritt A3: Strukturen, die auf 64 bit auseinanderlaufen (17.10.2026, Cloud)

Der dekompilierte Code beschreibt manche Objekte mehrfach: als vollständige
Struktur, als Teilsicht mit Platzhaltern (`Uint8 pad[0x40]`, die
GameCube-Abstände festhalten), unter anderem Namen in einer anderen Datei,
als Byte-Tabelle fester Größe. Auf ILP32 stimmen alle Sichten Byte für Byte
überein, auf LP64 verschiebt jeder Zeiger die Glieder dahinter. Dafür gibt es
jetzt Prüfwerkzeuge in `port/tools/m12/` (vergleichen den armhf-Bau als
Vorbild mit dem x86_64-Bau; die mit libclang brauchen
`pip install libclang==18.1.1`, nur in der Cloud):

| Werkzeug | prüft | Ergebnis nach den Patches |
|---|---|---|
| `fixed_layout_check.py` | Strukturen, die die Formatumwandler nennen (Disc-Daten): Größe und Lage aller Glieder | 83 von 83 gleich |
| `view_check.py` | Strukturen gleichen Namens in mehreren Dateien: gleicher GameCube-Abstand → gleicher LP64-Abstand (mit Code-Erzeugung, damit jede benutzte Struktur angelegt wird) | 794 Strukturen mit mehreren Sichten, 0 auseinander (vorher 11) |
| `decl_size_check.py` | globale Variablen: Größe der Definition gegen die `extern`-Deklarationen (Byte-Tabellen) | 1351 Variablen, 0 (vorher 2) |
| `proto_check.py` | C-Funktionen, deren Dateien einen Parameter mit verschiedenen Strukturen beschreiben | 2112 Struktur-Parameter, 0 (vorher 7) |
| `cast_check.py` | Umwandlung eines Strukturzeigers in einen anderen (`SFD_TIM(sfd)` legt die Zeitarbeit über `SFD_OBJ.con`) | 44 Umwandlungen, 0 (vorher 3) |
| `raw_offset_check.py` | feste Byte-Abstände in Strukturen (`(u8*) p + 0x18`, auch in Makros, mit Basisklassen; Abstände über `&((T*) 0)->m` zählen nicht) | 0 (vorher 280 Stellen in 13 Dateien) |
| `overlay_check.py` | Arbeitsbereiche über festen Flächen (`cEm::free`, `cEspgen::Free`) und Klassen über Pool-Plätzen | 1564 Klassen, 2585 Umwandlungen, 0 (vorher 823 Fundstellen) |
| `group_check.py`, `gen_views.py`, `split_pads.py` | Hilfen für die LP64-Sichten der CRI-Bibliothek (Teilsichten an der vollen Struktur ausrichten, erzeugte Platzhalter prüfen) | alle Sichten von `SFD_OBJ`, `MPV_OBJ`, `MWPLY_OBJ`, `MWSFFRM_VFRM` stimmen |

Behoben:
- **CRI-Bibliothek (Patch 0070, 36 Dateien):** `Sint32/Uint32` bleiben
  32 bit (Regel `exact_width_types`; die Spielseite benutzt `int`);
  Adressen in `Sint32`-Werten über `Uint32` zurück (36 Stellen);
  Teilsichten des ADX-Dekoders, des Players, des Sofdec-Handles (`SFD_OBJ`
  über Puffer-, Zeitstempel- und Speicher-Sichten) mit 64-bit-Platzhaltern
  aus der vollen Struktur; Byte-Tabellen `adx_fini.c` und SFH-Arbeitsfläche
  in LP64-Größe mit Größenprüfung; Schleifen mit festem Abstand
  (`adx_tsvr.c`, `adx_sje.c`) über die Glieder. **MPEG-Dekoder `MPV_OBJ`:**
  fünf Sichten unter zwei Namen; `mpv.h` benennt die Zeiger, die die anderen
  Sichten in Platzhaltern führen, die übrigen vier Sichten bekommen
  erzeugte LP64-Platzhalter (alle 177 GameCube-Abstände stimmen überein),
  Handle-Schritt 0x1590, Tabellenbereich `MPV_IXA` auf denselben Abständen.
  **Zeitarbeit:** `SFCON` und `SFTIM_WORK` liegen übereinander (geprüft),
  die Uhrquellen-Tabelle (`sfd_tim.c`, GameCube-Abstand 0xD28) und die
  Bildzeit (Wörter 5/6 der Bildinformation) über die Glieder, der Bildplatz
  vor der Bildinformation über `offsetof`; die drei aus Assembler
  übersetzten Rechenkerne von `mpv_umc.c` lesen ihre Zeiger an den
  LP64-Abständen; `sfd_mem.c` führt die Puffernummer als Zahl.
- **Patch 0071:** die Spielseite des Filmbilds (`MWS_FRM`, `mwply.h`) folgt
  auf LP64 der Sicht der Bibliothek (`mwPlyGetCurFrm` schrieb sonst über
  `cSofdec` hinaus).
- **Patch 0072:** Debug-Werkzeuge (Fenster/Knöpfe des Sscrn-Debugmenüs
  `dbg_button.h`, Modellbetrachter `db_mod.h`).
- **Feste Plätze (Regel `lp64_sizes`, `port/whole/lp64_sizes.json`):** auf
  LP64 waren alle 54 Klassen im Gegner-Pool größer als ihr Platz
  (0xEA0 statt 0xDE0) und vier eigene Gegner-Arbeitsbereiche größer als
  ihre Fläche (bis 0xAB8 statt 0xA00), ebenso 55 Objektklassen und fünf
  Objekt-Arbeitsbereiche, dazu ein Effekt-Arbeitsbereich. Jetzt auf LP64
  Gegner-Platz 0xF60 (Fläche 0xAC0), Objekt-Platz 0x4B0 (Fläche 0xF0),
  Effekt-Fläche 0xD0; auf ILP32 unverändert.
- **Patch 0073:** feste Byte-Abstände im Spielcode über die Glieder
  (`RE4_OFS`, auf ILP32 beim Übersetzen gegen den GameCube-Wert geprüft).
  Dabei gefunden, **auch auf 32 bit falsch:** das Puzzle-Untermenü
  (`ss_pzzl.cpp`) las Zustand und Auswahl seines Nachrichtenfensters an den
  Abständen des GameCube-Compilers (MWCC legt den Tabellenzeiger einer
  Klasse hinter ihre Felder, die Compiler des Ports davor); jetzt über die
  Abfragefunktionen des Spiels. Im Rundgang 10b auf das Puzzle achten.
- Testprogramme: `card_probe.cpp` rechnet den Spielstand-Block vorzeichen-
  behaftet; `save_layout.py` findet den Block über
  `save_data_start_addr` (GameCube 0x4F80, LP64 0x4FB8; Inhalt und
  `.gci`-Form gleich, `--check` auf armhf und x86_64 bestanden).

### Schritt A4: alle Cloud-Tests auf 64 bit (17.10.2026, Cloud)

Neues Bauziel **`aarch64`** (`cmake/linux-aarch64.cmake`,
`whole_build.sh aarch64`): 64-bit-ARM-Linux, die Code-Erzeugung von
`arm64-v8a`, ausgeführt unter qemu-aarch64 (Werkzeuge in
`whole_setup_linux.sh`). Der Vorlader (`host/loader.c`) kommt mit der
Speicheraufteilung von qemu zurecht (Stapel unten, Bibliotheken oben, eigene
Bereiche von qemu unsichtbar): er füllt jetzt jede freie Lücke über 4 GB bis
zum Ende des Adressraums und lässt Platz unter dem Stapel nur, wo er wachsen
kann; `RE4_LOADER_MAPS=1` gibt die Aufteilung aus. Unter qemu liegt `malloc`
oberhalb von 4 GB wie auf Android; das Spiel bekommt solchen Speicher nicht
(nur Hinweis im Log). Der Spiel-Thread der App (`re4_core_start`) bekommt auf
64 bit seinen Stapel aus dem Fenster.

| Ziel | Build | Sprungstellen, Module, Laufzeit (10), Kern (15 Teile) | Karte, `.gci`, Spielstand-Form, Formatvergleich (2) | Filmtest |
|---|---|---|---|---|
| i686 | 0 Fehler | ✅ | ✅ | ✅ |
| armhf (qemu-arm) | 0 Fehler | ✅ | ✅ | ✅ |
| x86_64 | 0 Fehler | ✅ | ✅ | ✅ (die 60-Hz-Prüfung schlug einmal fehl, während qemu und ein Download den Rechner auslasteten; einzeln wiederholt bestanden) |
| aarch64 (qemu-aarch64) | 0 Fehler | ✅ | ✅ | ✅ |

**Nicht geprüft:** das ganze Spiel mit Disc-Daten auf 64 bit (PC-Prüfung,
`test-r100-core.ps1 -Lp64`, neu) und auf einem Gerät.

### Schritt A5: Android arm64 (17.10.2026, Cloud, ungeprüft)

Eine APK für 32- und 64-bit-Geräte: die App-Variante `core` hat jetzt
`abiFilters 'armeabi-v7a', 'arm64-v8a'`; `whole_build_android.sh` baut beide
(`--abi` wählt eine), die Bibliotheken liegen in `src/core/jniLibs/<abi>`.
Die Variante `classic` (eigene Spielmodule, arm64) bleibt unverändert.
Auf 64 bit legt die App (`core_mode.cpp`) zuerst einen Platzhalter über
MEM1 und lädt `libre4_platform.so` und `libre4_main.so` mit
`android_dlopen_ext` in Bereiche, die sie oben im Fenster reserviert hat;
der Kern lädt jedes Modul in einen eigenen Bereich des Fensters
(`module_loader.cpp`, 8 MB je Modul, das größte braucht 2 MB) und gibt ihn
beim Entladen zurück. Beim Start steht im Protokoll, wie viel des Fensters
belegt ist (`re4_window_report`).
`test-core-app.ps1 -Abi armeabi-v7a|arm64-v8a` installiert gezielt eine
Fassung (ohne `-Abi` wählt das Gerät, der Retroid nimmt 64 bit).

**Ungeprüft:** In der Cloud gibt es kein Android-NDK (der Download von
dl.google.com ist durch die Netzwerkregel gesperrt); der neue Android-Code ist
nur gegen einen nachgebildeten Header übersetzt (vor dem ersten 64-bit-Gerätelauf
am 17.10.2026 erneut: `module_loader.cpp`, `core_api.cpp`, `game_window.cpp`,
`os_thread.cpp` des Kerns und `core_mode.cpp`, `core_scene.cpp`, `gx_gl*.cpp`,
`gx_texture.cpp` der App für aarch64 mit `__ANDROID__` ohne Fehler und Warnungen). Offen und erst auf dem Gerät
messbar: ob ART oder die Grafiktreiber Teile des Fensters
0x80000000–0xFFFFFFFF schon belegen.

### Gerätefehler vom 17.10.2026 (Retroid, ARM32, Berichte 17:19 und 17:29)

| Meldung | Ursache laut Bericht | Stand |
|---|---|---|
| Nach der Glocken-Szene in r101 landet die App im Startmenü (Import) | Absturz (SIGSEGV) in `FuncPathParametrize` ← `IDSystem::set` ← `r101_Event30_TitleCall` (`libre4_st1_1.so`): der Kapiteltitel über dem Glocken-Ereignis liest `etc/eng/id101.eff` (Bilder, EFF-Form) und `etc/eng/event001.uwf` (Anordnung) **einzeln** über `DC.setData`; für einzeln geladene `.eff`/`.uwf` gab es keine Regel, beide blieben big-endian („no converter“) | **behoben** (Regeln in `formats.cpp`), Kerntest „single files“; **auf dem Gerät geprüft** (Retroid, ARM32, 17.10.2026: Stage 1 komplett durchgespielt, ohne Fehler) |
| Beim Aufheben eines Gegenstands bleibt die A-Anzeige stehen, das Spiel läuft nicht weiter (SELECT geht) | Die Aufheben-Anzeige lädt das Modell `ss/cmn/itm1a.bin` einzeln (`sce_at.cpp`, `DvdReadN`); keine Regel für einzelne `.bin`, das Modell blieb big-endian | **behoben**: Regeln für `ss/cmn/itm*.bin`, `ss/item/*.bin` (Untersuchen im Inventar) und `etc/*.bin` (Kostüm-Modelle in r300, `r300.cpp`); der Formatvergleich mit Disc-Daten nimmt diese Modelle jetzt mit (als einziger Eintrag eines Archivs, gegen den Modell-Leser der App), der synthetische Vergleich enthält eins (8 statt 7 Dateien); **auf dem Gerät geprüft** (Retroid, ARM32, 17.10.2026: Stage 1 komplett durchgespielt, ohne Fehler) |

Gesucht wurde in allen Ladestellen des Spiels (`DC.setData`, `DvdReadN`):
die übrigen einzeln gelesenen Dateien sind Archive (`ss/*/*.dat`, Regel
vorhanden), Texturen (`*.tpl`) und Ereignisse (`*.evd`). Im Rundgang 10b
achten auf: `st1/r101.das` Eintrag #24 `EVS` (32 Bytes) meldet „conversion
incomplete“ (wohl ein leeres Ereignis), dazu die bekannten Kürzel ohne
Umwandler `DRA` und `FNT`.

### Bildfolge am Anfang (Frage des Projektinhabers, 17.10.2026)

Kein Film: `title.cpp` lädt `SS/<sprache>/title.dat` (Archiv mit
Inhaltsverzeichnis), meldet dessen Bilddaten an (Eintrag 4, EFF-Form) und
spielt eine Anordnung des Bildschirm-Systems (Eintrag 6, mit allem
Freigeschalteten 7) über einen Bildzähler ab: Gesundheitshinweis ab Bild 0,
Capcom ab 105, CRI ab 230, Dolby ab 330, Titel ab 585, Menü ab 615
(`include/title.h`); das Nintendo-Logo ist Anordnung 0xB. Jedes Logo lässt
sich nach `TTL_CANCEL_*` mit START überspringen. Ob ein Logo ein eigenes
Bild über schwarzem Grund oder ein ganzes Bildschirmbild ist, zeigt
`port/tools/title_images.py` mit den Disc-Daten: es schreibt jedes Bild als
PNG und listet je Anordnung, welches Bild wo und wie groß in welchen Bildern
des Zählers steht (getestet mit einem nachgebauten Archiv; mit den echten
Daten noch nicht gelaufen). Der Projektinhaber verzichtet auf ein eigenes Logo
(17.10.2026); das Werkzeug bleibt für spätere Fragen zu den Titelbildern.

### Paketname und Startbildschirm (Entscheidung des Projektinhabers, 17.10.2026)

- **Paketname:** Die Kern-App heißt jetzt `com.ast.re4decomp` (vorher
  `com.ast.re4decomp.core`); die Variante `classic` mit den eigenen
  Spielmodulen weicht auf `com.ast.re4decomp.classic` aus
  (`app/build.gradle`). Eine Installation der Kern-App ersetzt eine noch
  installierte frühere „RE4 Decomp“ mit diesem Namen. `test-core-app.ps1`
  installiert mit `-d` (die frühere App kann eine höhere Versionsnummer haben),
  erklärt den Fall eines anderen Signierschlüssels und kopiert einmal aus
  `Android/data/com.ast.re4decomp.core/files/game` die vollständigen
  Disc-Importe (`sys/fst.bin` und `ready.properties`, über einen
  Zwischenordner `.copy-discN`) und `memcard/card-a.bin`, jeweils nur, wenn
  die neue App sie noch nicht hat (`-NoCopy` lässt das aus, etwa um den
  Startbildschirm ohne Discs zu sehen). Die Einstellungen der App (Grafik,
  Tastentausch) liegen im privaten Speicher und beginnen neu.
  `pull-gx-dumps.ps1` nutzt den neuen Namen.
- **App-Name** unter dem Symbol: „Resident Evil 4 Android“ (vorher „RE4 Kern“),
  ebenso im Betreff des Fehlerberichts.
- **Startbildschirm** (nur zu sehen, solange Disc 1 fehlt; sonst startet das
  Spiel sofort): zentriert „Resident Evil 4 Android“, darunter kleiner
  „By Andreas ‘Andiweli’ Stürmer“, eine Linie, „Start game“, dann
  „Controls“ und „Select ISO/GCM files of the November 25, 2004 debug build“
  (ohne Punkt) jeweils zentriert zwischen zwei Linien mit der Tastenbelegung
  und dem Tausch-Schalter bzw. den Import-Zeilen darunter: „Import Disc 1“
  und „Import Disc 2“ über der linken Hälfte, rechts daneben „Disc N Data is
  missing/ready“ („Cancel import“ ebenso, nur während eines Imports; die
  Statuszeile nur, wenn sie etwas meldet). Nach dem ersten Gerätelauf
  (17.10.2026: „zu hoch“) enger: kleinere Abstände oben, unten und an den
  Überschriften. Der Knopf „Menu“ entfällt im Kernmodus: sein
  einziger Eintrag war der Disc-Import, den der Startbildschirm jetzt immer
  zeigt (`classic` behält ihn). Der Java-Code übersetzt gegen die
  Android-Klassen (Cloud); auf dem Gerät noch nicht gesehen.

### Erster 64-bit-Gerätelauf (Retroid, 17.10.2026, Bericht 22:45, Stand `19f45a9`)

- **Ergebnis:** APK mit beiden Fassungen gebaut (der arm64-Kern mit dem echten
  NDK zum ersten Mal), als `arm64-v8a` installiert („Process: 64-bit“). Nach dem
  Import beider Discs bricht der Start ab: „The game core did not start: MEM1
  0x80000000..0x83000000 is taken in this process“. Der Kern lädt beim Start
  der App (`Re4Application` → `CoreBridge.load()` → `JNI_OnLoad`), also bevor
  irgendetwas der App Speicher anlegt; der Bereich ist damit schon von dem
  belegt, was der Prozess vom 64-bit-Zygote erbt oder was ART beim Start des
  Prozesses anlegt (im 32-bit-Prozess war er frei, siehe „Adressbereich auf
  dem Retroid“). Vermutung (ungeprüft): ART auf arm64 legt Bereiche, die unter
  4 GB liegen müssen (der Bereich für große Java-Objekte „free list large object
  space“ in der Größe von `dalvik.vm.heapsize`, der JIT-Code-Cache), in die erste
  passende Lücke unter 4 GB, und das ist hinter dem Boot-Image (~0x70000000)
  die Gegend um 0x80000000.
- **Messung eingebaut:** Die App hält beim Laden des Kerns fest, was unter
  4 GB liegt (MEM1 und Fenster markiert, größte freie Lücke, ob der Platzhalter
  über MEM1 und die beiden Bibliotheken geladen wurden), schreibt die Zeilen im
  Fenster auch ins Android-Protokoll (`RE4Core`) und legt alles als
  `address-map.txt` in den Fehlerbericht; die Meldung nennt den ersten Eintrag in
  MEM1 („… is taken in this process (by …)“). Der Bericht nennt die Java-Größen
  (`dalvik.vm.heapsize`, `heapgrowthlimit`). Ohne neuen Build liest
  `port/scripts/pull-address-map.ps1` (ADB-Sperre, `run-as`) die Belegung des
  laufenden App-Prozesses und schreibt `adressen-64bit.txt`.
- **Gemessen** (`pull-address-map.ps1`, 22:57, PID 14277, arm64-v8a;
  `dalvik.vm.heapsize` 512m, `heapgrowthlimit` 256m, Build `user`):
  `12c00000-32c00000` ART-Hauptbereich, `6f7e6000-6ff8a000` Boot-Image,
  `6ff8a000-73f8a000` Zygote- und Non-moving-Bereich,
  **`73f8a000-93f8a000 [anon:dalvik-free list large object space]` über MEM1**
  (512 MB, vom 64-bit-Zygote geerbt), `93f8a000-9bf8a000` JIT-Caches (Zygote
  und App), `fa000000`/`fe000000` die beiden Kern-Bibliotheken (oben im
  Fenster, wie geplant). Frei im Fenster: `9bf8a000-ebad6000` (1,3 GB) und
  darüber bis `fa000000`. Die Vermutung stimmt damit für den Bereich der großen
  Objekte; die JIT-Caches liegen im Fenster, aber nicht in MEM1.
- **Ausweg (umgesetzt, auf dem Gerät noch nicht gelaufen):** ART füllt diesen
  Bereich von unten, verschiebt dort nichts und fasst die Daten eines
  lebenden `byte[]` nie an. `core_mode.cpp holdMem1InLargeObjectSpace` legt
  beim Laden des Kerns Java-Arrays in den Bereich, bis eines auf der Seite vor
  0x80000000 beginnt und bis 0x83000000 reicht; dieses hält die App für die
  Lebensdauer des Prozesses (globale Referenz), die Arrays darunter gibt sie
  frei (ART bekommt den Platz zurück). Über die Daten dieses Arrays legt sie
  den Platzhalter, den MEM1 ersetzt (`RE4_MEM1_PLACEHOLDER`). Dauerhaft zählen
  48 MB als Java-Speicher, kurz beim Start bis etwa 240 MB; dafür
  `android:largeHeap="true"` (Grenze dann `heapsize`, 512 MB). Geht es nicht
  (Bereich schon über MEM1 hinaus gefüllt, Array woanders, Java-Speicher zu
  klein), bricht der Start wie bisher mit Meldung ab, und `address-map.txt`
  sagt warum. Geprüft: Übersetzung für arm64 und ARM32 (nachgebildete Header)
  und eine Nachbildung des Bereichs (beste Lücke, sonst Ende) mit fünf Fällen
  in Python (leer, mit Lücken, kurz vor MEM1, 300 KB vor der Seite, schon
  darüber: vier halten MEM1, der letzte bricht sauber ab).
- **Zweiter 64-bit-Gerätelauf (23:12, Stand `4e7e180`):** MEM1 sitzt im Bereich
  der großen Objekte („MEM1 held in ART's large object space …: the data of a
  byte[] at 0x7ffff000..0x83000000 (190 MB below let go)“), der Kern läuft:
  Kartenprüfung, Titel-Archiv geladen, 5 Tastendrücke kommen an. Nach 7 s
  Absturz in `primInit` (aus `titleWait`): `memset` auf `0xffffffff80d427c0`.
  `pG->prim_cnt` ist `s32`, `(void*) pG->prim_cnt` erweitert auf LP64 das
  Vorzeichen. Behoben mit der Regel `lp64_int_casts` (Abschnitt „Mechanische
  Umschreibungen“): `port/tools/m12/int_cast_check.py` fand mit libclang 145
  solche Umwandlungen (142 einfache, 3 in Makros), alle bekommen `RE4_ADDR32`;
  danach 0. Die frühere Suche mit `-Wint-to-pointer-cast` (Schritt A2, 14
  Stellen) hatte die meisten übersehen. Dazu `port/tools/m12/ptr_word_check.py`:
  Zeiger, die als 32-bit-Wort gelesen oder geschrieben werden (auf LP64 bliebe
  beim Schreiben die obere Hälfte stehen): eine Stelle, `*(u32*) &pAnotherRoute
  = 0` in `pl_npc.cpp`, harmlos (die obere Hälfte ist bei allem unter 4 GB
  null). Geprüft: i686, armhf, x86_64 und aarch64 (qemu) alle Cloud-Tests ✅
  (Selbsttest, Module, Laufzeit 10/10, Kern 16/16, Karte, Format-Vergleich,
  Film); `int_cast_check.py` auf x86_64 und aarch64: 0. Der Titel und die Räume laufen in
  der Cloud nicht (keine Disc-Daten): dafür die PC-Prüfung `-Lp64` mit den
  Disc-Daten (noch nie gelaufen) und der nächste Gerätelauf.
- **Dritter 64-bit-Gerätelauf (23:32, Stand `6b896b3`):** Kern und Ton laufen
  (4000 Tonblöcke, der START-Ton ist zu hören), der Titel wird gezeichnet
  (601 Bilder, 11383 Zeichenbefehle), bleibt aber **schwarz**; nach START
  Absturz in `primFree` (aus `titleExit`): `OSFreeToHeap` auf `0xffffffe0`
  (Adresse 0 − 0x20). Ursache: `GxWork` (trans.cpp) liegt über
  `pG->gxStage` bis `pG->prim_base`; sein Zeiger `prim` (dort `prim_base`) hat
  auf LP64 8 Bytes und rückt auf 8 Bytes Ausrichtung, also über `prim_rate`
  und `prim_cnt`: `SetPrimBuffPtr` schrieb dessen obere Hälfte (0) in
  `prim_cnt`, der Zeichenpuffer war damit weg (keine Grundformen, schwarzer
  Titel) und `primFree` gab 0 frei. Behoben: `GxWork` bekommt die feste Form
  (`fixed_form.json`, `prim` 4 Bytes); dazu Patch 0074 für dieselbe Art
  „Sicht mit GameCube-Abständen“: `cast_check.py` prüft jetzt auch C++ und
  Sichten über ein Glied (`(GxWork*) &pG->gxStage`; 29 Funde, davon die
  meisten Umwandlungen zwischen verwandten Klassen, die nur gemeinsame Glieder
  lesen, und zwei absichtlich tote Zuweisungen), dazu eine Suche nach
  Strukturen, die mit einem großen Füllfeld beginnen (24). Behoben: Skelett
  (`cPartsWk`), Speichern (`SaveDataPtrs` feste Form), Fenster (`EmWindowWork`),
  Ereignis-Kamera (`TexRender.cpp`), Raketenwerfer, `EmMarkView`, Lore,
  r300/r310/r30c/r316/r317/r318, Objekt-Betrachter. Geprüft und richtig wie
  sie sind: `FREE_EMSWITCH` (eigener Bereich), `OSLowMem` (in MEM1),
  `PrimBuffView` (Abstand gleich), `GlobalKeep` (Spielstand-Block, gleiche
  Form), `DbModState` (eigene Größe), `EmModule` (unbenutzt). Offen, nur im
  Effekt-Werkzeug: `DbToolWk` (`t_esp`). Wichtig dabei: auf LP64 beginnt der
  eigene Bereich einer abgeleiteten Klasse oft schon in den Füllbytes am Ende
  der Grundklasse (hinter `cEm` bei 0x49C, `sizeof(cEm)` ist 0x4A0; hinter
  `cEsp` bei 0x10C statt 0x110; hinter `cObj` bei 0x3C0 = `sizeof`), also nie
  `sizeof(Grundklasse)` nehmen, sondern `RE4_OWN(B, T, gc)` (Prelude: Abstand
  des ersten eigenen Glieds vom Typ T, auf ILP32 gegen den GameCube-Wert
  geprüft). Annahme bei r300/r318: das Feld liegt 8 Bytes in der eigenen
  Arbeitsfläche des Effekts, die mit 4-Byte-Gliedern beginnt (auf der GameCube
  0x100 = 0xF8 + 8). Geprüft: i686 und x86_64 alle Cloud-Tests ✅ (auf i686
  bestätigen `RE4_OFS`/`RE4_OWN` jeden GameCube-Abstand beim Übersetzen, auf
  x86_64 ein Übersetzungstest die LP64-Werte 0x49C, 0x3C0, 0x10C);
  `cast_check.py` danach: die behobenen Sichten sind weg (übrig bleiben die
  geprüften harmlosen Fälle und die Umwandlung beim Aufruf von
  `TexRenderCamAddOt`, die jetzt nur noch den Zeiger weiterreicht).
- **Vierter 64-bit-Gerätelauf (00:09, Stand `2d6c6e8`) und erste PC-Prüfung
  `-Lp64` (00:10):** Titel sichtbar, die Test-Tastenfolge drückt START (dabei
  wird der Titelruf „Resident Evil 4“ abgeschnitten, wie auf 32 bit mit der
  Tastenfolge), der Vorfilm läuft ganz (76 s, 2278 Bilder, mit Ton); alle
  Filme beider Discs laufen auch in der PC-Prüfung fehlerfrei. Dann:
  Gerät: „stack corruption detected“ in `EventMgr::EvtReadSub` aus `R120Event`
  (Autofahrt): `GetRead` ließ `ReadTbl.GetDat` den Zeiger in `int d` schreiben
  (8 Bytes in 4). PC: Absturz in `EspMove` beim Raumaufbau von r100 (und im
  Kartenlauf): der Effekt-Pool hat feste Plätze von 0x150 Bytes, die größte
  Effekt-Klasse `cEsp3f` hat auf LP64 0x1A8 (`cEsp03/06/0e/4e` 0x170), die
  Plätze überlappten. Behoben: Pool-Platz auf LP64 0x1A8 (`lp64_sizes.json`,
  12 Stellen in `esp.cpp`, gemessen über alle 46 Effekt-Klassen); Patch 0075
  für Zeiger in 4-Byte-Plätzen (neues Werkzeug `ptr_slot_check.py`, 18 Funde:
  `GetRead`, `Event::PFuncTbl`, Puzzle- und Händler-Modelle, Debug-Texte,
  r332, r20d behoben; die übrigen 5 geprüft und richtig). Der Absturzbericht
  der App las auf 64 bit über das Ende des Spiel-Stapels hinaus und stürzte
  selbst ab: liest jetzt mit `process_vm_readv` nur, was lesbar ist.
  Geprüft: i686, armhf, x86_64 und aarch64 alle Cloud-Tests ✅ (der 60-Hz-Teil des Filmtests
  war unter Last einmal rot, allein wiederholt grün); `int_cast_check.py` 0,
  `ptr_slot_check.py` nur die 5 geprüften.
- **Fünfter 64-bit-Gerätelauf (00:30, Stand `3756798`) und zweite PC-Prüfung
  `-Lp64` (00:39):** Die Autofahrt r120 läuft jetzt etwa 2 Minuten. Der
  Projektinhaber meldet: die Fahrt ruckelt und es fehlen immer wieder ein paar
  Sekunden, im Wald fehlen viele Bäume (nur die nahen sind da), Leons
  Spiegelung im Autofenster fehlt teilweise, dann bricht die Fahrt ab. Das
  Protokoll: „GX: display list at 0xfa345000 (96 bytes) is not in memory“
  (8-mal, dann nicht mehr gemeldet), nach 2 Minuten „the command stream lost
  its place“ in `Esp1b_SpTrans`, 8 s kein Bild, die App wird beendet. Die
  PC-Prüfung kommt bis zum Raumaufbau von r100: „OSCheckHeap: Failed
  cell->next->prev == cell“ beim Laden von `em12`, dann Absturz in
  `MotionMoveCore` (`cEm10::move` aus `R100Init`). Ursachen und Abhilfe:
  - **Zeichenliste im Programm:** `GXCallDisplayList(g_EspCommonDisplayList)`
    (das Viereck aller Sprite-Effekte, statische Daten von `libre4_main.so`)
    schreibt die Adresse selbst in den Befehlsstrom; der GX-Nachbau nahm solche
    Adressen nur auf 32 bit an. Mit Weg A liegen die Bibliotheken unter 4 GB:
    `re4gx::listMemory` nimmt sie jetzt auf beiden Breiten (gleiche Prüfung
    `re4_valid_port_ptr`). Alle Sprite-Effekte (Licht, Rauch, Funken …)
    fehlten bisher auf 64 bit.
  - **Effekt 0x1B (gebogenes Band):** Patch 0040 fand die Daten von `cEsp1b`
    über `sizeof(cEsp)`; auf LP64 beginnen sie bei 0x10C (Füllbytes am Ende von
    `cEsp`, 0x110), die Punktzahl war Unsinn, Millionen Vierecke, der Strom
    verlor seinen Platz. Jetzt `RE4_OWN(cEsp, Esp1bSpWork, 0xF8)`. Dieselbe Art
    in r117 (Lichtstrahlen, Byte 0xF9): Patch 0077. Gefunden, nachdem
    `raw_offset_check.py` jetzt auch Abstände hinter dem Ende einer Struktur
    meldet (vorher übersprungen; 4 Funde: r117, dazu der Kopf der Heap-Zellen
    in `main_mem.cpp`/`OSAlloc.c`, der auf beiden Breiten 0x20 Bytes hat).
    Neues Werkzeug `array_view_check.py` (Glied-Felder über ihr Ende hinaus
    indiziert): 3204 Fundstellen, nur 5 rücken auf LP64, alle geprüft und
    richtig (`cDataCtrl::m_DataUnit[32]` als Endmarke, `R203Work.em[6]` immer
    über dasselbe Feld).
  - **Zeichenaufträge (fehlende Bäume, Spiegelung):** Jeder Eintrag der
    Zeichenreihenfolge (`OtPrim`) kommt aus dem Bildpuffer des Raums
    (`GetPrimBuff`, Schritte von 32 Bytes, Größe aus den Raumdaten). Auf dem
    GameCube 0x14 Bytes, ein Schritt; auf LP64 0x28 Bytes, zwei Schritte. Läuft
    der Puffer voll, lässt das Spiel die letzten Zeichnungen des Bildes weg
    (meldet es nur auf dem Bildschirm). `OtData`/`OtPrim` haben jetzt die feste
    Form (`fixed_form.json`, Zeiger 4 Bytes), der Verbrauch ist wie auf dem
    GameCube. Noch nicht mit Disc-Daten bestätigt; dafür:
  - **Spielmeldungen im Protokoll (Patch 0076):** Was das Spiel mit
    `pLog->err/warn/mes` meldet (auf dem GameCube Bildschirm und Konsole), geht
    jetzt als „game: …“ ins Protokoll, jede Meldung die ersten 3 Male, dann
    jedes 1000. Mal mit Zählung (`re4_game_log`, `runtime/log.cpp`).
  - **Heap-Beschädigung (PC-Prüfung):** Neues Werkzeug `alloc_size_check.py`:
    Speicher mit fester GameCube-Größe für eine Struktur, die auf LP64 größer
    ist (auch Tabellen von Zeigern), und Löschen/Kopieren mit fester Länge.
    28 Stellen (9 weitere sind auch auf ILP32 kürzer, absichtlich nur ein
    Kopf; danach 0). Die Kamera eines Modells
    (`ATTACH_CAMERA`, 0x98 → LP64 0xA0, 14 Stellen) und vor allem
    `cModel::cModel`: `memclr_asm(&Motion, 0xD0)` ließ auf LP64 die letzten
    0x20 Bytes von `MOTION_INFO` stehen, darin `pAttachCam`, ein alter Zeiger
    aus dem vorigen Inhalt des Pool-Platzes, über den `MotionMoveCore`
    schrieb. Dazu der zweite Bewegungsbereich von em32, die Bewegungstabellen
    des Spielers (0x6D und 12 Zeiger), die 5 Wegpunkt-Zeiger der Ganados in
    r209 (mit 0x14 Bytes gelöscht, auf LP64 behielten die letzten zwei alte
    Adressen) und drei Debug-Listen. Alles in `lp64_sizes.json`
    (`RE4_LP64_SIZE`, auf ILP32 der GameCube-Wert). Andere Puffer mit fester
    Größe pro Bild (`GetDrawTmpBufAddr`, `IdGetBufferAddr`) halten nur Pixel.
  - **Berichte:** `test-core-app.ps1` und die PC-Prüfung zeigen jetzt die
    Spielmeldungen (je Meldung einmal), `test-core-app.ps1` dazu, wie oft der
    GX-Nachbau Daten nicht erreichte und ob der Befehlsstrom aus dem Tritt kam.
  - Geprüft: i686, armhf, x86_64 und aarch64 alle Cloud-Tests ✅ (auf i686
    und armhf bestätigt `RE4_OWN` beim Übersetzen den GameCube-Abstand 0xF8);
    `alloc_size_check.py` 0. Noch nicht geprüft: Gerät und PC-Prüfung mit
    Disc-Daten.
  - **Ruckeln:** Während der Fahrt wurden etwa 260 neue Grafikprogramme
    übersetzt (75 kamen aus dem Zwischenspeicher, am Ende 333), einzelne Bilder
    brauchten bis 0,64 s. Der Zwischenspeicher der Programme liegt im
    App-Ordner und war mit dem neuen Paketnamen leer; jedes neue Programm wird
    sofort gespeichert. Ein zweiter Lauf derselben Szene sollte ohne diese
    Pausen laufen; die „fehlenden Sekunden“ sind wahrscheinlich die Ereignisse,
    die nach einer Pause zum Ton aufschließen. Bestätigung mit dem nächsten
    Gerätelauf.
- **Sechster 64-bit-Gerätelauf (01:27, Stand `e60ddde`):** Die Autofahrt r120
  läuft jetzt bis zum Ende (Leons Spiegelung im Fenster, viel mehr Bäume, die
  Nebeleffekte sind da). Der Projektinhaber meldet: einige Schattierungen
  stimmen nicht, als ein Polizist austreten geht, sieht man kurz nur
  Grashalme, als das Auto wegfährt klebt eine Nebelwand am Auto und man sieht
  nur Gestrüpp; dann Absturz. Der Absturz kommt beim Aufbau von r100 (der erste
  Ganado, `cEm10::move` aus `R100Init`): `MotionMoveCore` liest
  `pEm->Motion.pHermite_data` = `0x00010000813d85b8`, die untere Hälfte
  stimmt, in der oberen steht 0x00010000. Derselbe Fehler war schon der
  Absturz der PC-Prüfung von `3756798` (auf x86_64 ist so eine Adresse nicht
  kanonisch: „fault address (nil)“). Die Heap-Beschädigung von damals ist weg.
  - **Effekt-Pool in est.cpp:** `EspDelete` (die Effekte eines Besitzers) und
    `EspDeleteEvent` (die Effekte eines Ereignisses an seinem Ende) gingen den
    Pool noch in Schritten von 0x150 durch: sie ließen die echten Effekte
    stehen und gaben Stücke anderer frei. Passt zu Nebel am Auto und Grashalmen
    vor der Kamera. Jetzt 0x1A8 (`lp64_sizes.json`). Gefunden mit der neuen
    Schrittweiten-Prüfung von `alloc_size_check.py` (`(T*) (Anfang + i * K)`;
    bei einer Klasse mit virtuellen Funktionen meldet sie jede feste Schrittweite,
    weil abgeleitete Klassen größer sein können): genau diese 2 Stellen.
  - **Löschen über das Ende hinaus:** Neues Werkzeug `span_check.py` (Löschen
    und Kopieren ab einem Glied über die folgenden): `systemRestartInit` löscht
    `pG` ab `Rno0` mit `sizeof(GLOBAL_WK) - 0x20` (auf LP64 liegt `Rno0` bei
    0x24: 4 Bytes hinter `Global`), jeder Raumwechsel löscht `Snd` ab `room_ok`
    mit `sizeof(SND_WORK) - 0x90` (auf LP64 0x98: 8 Bytes dahinter). Beide jetzt
    mit dem Abstand des Glieds (`lp64_sizes.json`). Dritter Fund im
    Licht-Werkzeug (`t_scroll.cpp`, Debug), gleich auf beiden Breiten.
  - **Wort-Abstände:** `raw_offset_check.py --words` prüft auch Sichten als 2-,
    4- oder 8-Byte-Felder (`((f32*) em)[0x784 / 4]`): die Kamera beim Öffnen
    einer Tür las so `base_mat` der Tür (Patch 0079, jetzt über den Namen); die
    übrigen Funde (`sfd_tim.c`) behandelt Patch 0070.
  - **Absturz in r100, Diagnose:** Statisch zuerst nicht gefunden. Neu:
    Hardware-Haltepunkte (`runtime/watch.cpp`, `re4_watch_write`:
    `perf_event_open` mit einem Schreib-Haltepunkt, je Treffer ein Signal, das
    Befehl und Rücksprungadressen ins Protokoll schreibt; Laufzeittest
    „watch“). Ein Diagnose-Patch ließ `MotionSetCore` die obere Hälfte von
    `pHermite_data` jedes Gegners überwachen. **PC-Prüfung 02:03 (Stand
    `ca46a7f`):** der Schreiber ist `HermiteInterpolation` aus
    `MotionGetSpeed`, Ziel `MOT_HIST(w, flip, n)` = `(u8*) w + (flip * 12 + 8 +
    n * 6)`: 8 ist die GameCube-Lage von `Key_hist`, auf LP64 liegt es hinter
    den zwei Zeigern bei 0x10, der Schlüssel-Verlauf landete in der oberen
    Hälfte von `pHermite_data` (0x000b0000 auf dem PC, 0x00010000 auf dem
    Retroid). Behoben mit Patch 0078 (jetzt `RE4_OFS(MOTION_INFO, Key_hist,
    8)`; der Diagnose-Patch ist entfernt, `watch.cpp` bleibt als Werkzeug).
    `raw_offset_check.py` zählt jetzt auch die festen Teile von Summen mit
    Variablen (`(u8*) w + (flip * 12 + 8 + n * 6)`): dabei derselbe Fehler in
    r320 (`R320_EM_OFS`, `em[]` bei 0x0C statt 0x10), ebenfalls Patch 0078.
    Noch nicht mit Disc-Daten geprüft.
  - Geprüft: i686, armhf, x86_64 und aarch64 alle Cloud-Tests ✅ (Laufzeit jetzt
    11/11 mit „watch“: 3 Treffer für 3 Schreibzugriffe); `alloc_size_check.py`
    und `span_check.py` danach 0. Noch nicht geprüft: Gerät und PC-Prüfung mit
    Disc-Daten.
- **PC-Prüfung 02:20 (Stand `39a2ace`, x86_64 -O2, Disc-Daten):** Formatvergleich
  ✅ (408 Dateien, 0 Fehler), Filme ✅. Der Spiellauf erreicht zum ersten Mal auf
  64 bit die Hauptschleife von r100 (Bild 621, 18 Gegner, Leon und Kamera an
  ihrem Platz): der Absturz in `MotionMoveCore` ist behoben. Danach Absturz im
  ersten gezeichneten Bild: `RefractShaderSetup` aus `commonModelTrans` /
  `ModelRender` / `ExecOt` („fault address (nil)“: eine nicht kanonische
  Adresse).
  - **Ursache:** `ModelTexInfo`, die Sicht des Zeichners auf `cModelInfo` ab
    `flagsDC` (`MODEL_TEX(info)`, Textur-Animation und Überblendung). Auf LP64
    liegt `flagsDC` bei 0x134 und `texBlendTbl` 0xC dahinter, die Sicht hatte
    den Zeiger bei +8 und las 4 Bytes Lücke plus die halbe Adresse. Behoben mit
    Patch 0080 (auf LP64 4-Byte-Packung, Lücke aus den Abständen von
    `cModelInfo`, `static_assert` für jedes Glied, auf allen vier Zielen
    übersetzt).
  - **Warum die Werkzeuge es übersehen hatten:** `cast_check.py` erkannte `&`
    am ersten Wort des Ausdrucks; in einem Makro ist das der Makroname
    (`MODEL_TEX`), die Sicht wurde nie angesehen. Jetzt fragen `cast_check.py`,
    `span_check.py`, `ptr_word_check.py` (`&`, `*`), `array_view_check.py` und
    `alloc_size_check.py` (`+`, `*`) den Operator bei libclang ab, auch in
    Makros. Neu dabei: zwei Zeigertabellen mit 4 Bytes je Eintrag
    (`MEM_ALLOC(n * 4)` im Makro): die Puzzle-Teile (`puzzle.cpp`) und die
    Schatten-Objekte jedes Raums (`ShdInit` in `shadow.cpp`: auf LP64 schrieb
    die zweite Hälfte der Zeiger hinter den Block in den nächsten Heap-Platz).
    Beide in `lp64_sizes.json`. `alloc_size_check.py` liest die Größe jetzt am
    richtigen Argument (`OSAllocFromHeap(heap, size)`, `realloc`) und prüft auch
    Löschen und Kopieren mit `n * K` (keine weiteren Funde). Die übrigen
    Werkzeuge melden nur bekannte, geprüfte Stellen.
  - Geprüft: i686, armhf, x86_64 und aarch64 bauen fehlerfrei (die
    `static_assert` von Patch 0080 halten auf beiden Breiten), Selbsttest,
    Module, Laufzeit 11/11, Kern 16/16, Karte, Format-Vergleich ✅; Filmtest
    auf armhf und aarch64 ✅. Auf i686 und x86_64 war im Gesamtlauf der
    60-Hz-Teil des Filmtests rot (4 Bilder nicht zwei Halbbilder auseinander),
    einzeln wiederholt x86_64 3 von 3 und i686 2 von 3 grün, im zweiten
    Gesamtlauf auf allen vier Zielen grün: ein Echtzeit-Test, der auf diesem
    Rechner wackelt (schon beim Stand `3756798` einmal), der Filmcode ist
    unverändert. Ursache noch nicht untersucht. `cast_check.py`, `alloc_size_check.py` 0. Noch
    nicht geprüft: PC-Prüfung mit Disc-Daten und Gerät.
- **Falls das auf anderen Geräten nicht reicht** (MEM1 in einem anderen Bereich
  von ART): Auswege wären ein frischer App-Prozess über `wrap.sh` (nur
  Debug-APKs) oder ein eigener Prozess für den Kern; beides ändert die Art des
  App-Starts und wird vorher mit dem Projektinhaber abgestimmt.
- Nebenbei: Der Bericht heißt jetzt `memory-card.txt` statt `speicherkarte.txt`
  und ist englisch (Regel: Dateinamen für Tester englisch).

### Neustart M12 (18.10.2026, Cloud): Bericht 11:54, Bestandsaufnahme, Lücken, Vergleichslauf

Branch `claude/exciting-bohr-6akwfd` (von `claude/re4-m12-arm64`, Stand `6a799f4`).

**PC-Prüfung 11:54 (Stand `6a799f4`, x86_64 -O2, Disc-Daten), Auswertung:**
- Build, alle Prüfungen ohne Disc-Daten, Formatvergleich (408 Dateien), alle Filme
  beider Discs: ✅.
- **Spiellauf r100 auf 64 bit: zum ersten Mal ohne Absturz**, 10.000 Bilder ab der
  Hauptschleife (Bild 621), Spieler frei bei Bild 3043, Karte (Z) auf und zu, Leon
  läuft 10.405 Einheiten, Soft-Reset bringt den Titel zurück. Patch 0080 und die
  Puzzle-/Schatten-Tabellen sind damit mit Disc-Daten bestätigt (r100).
- **Speicherkarte: Absturz** im Speicherbildschirm beim Aufbau der Dateiliste
  („card: save: file list“, SIGSEGV an Adresse 0x7). Der Bericht hatte keine
  Aufrufkette (die Kartenläufe zeigten nur ausgewählte Zeilen).
  - Ursache (**bestätigt durch das Protokoll**, siehe unten): 0x7 ist
    `FUNC_PATH_PTR::nPoint`, gelesen über einen Nullzeiger in `FuncPathParametrize`
    aus `CardID::wait` (genau beim Öffnen der Dateiliste). Leer ist `path0` dann,
    wenn `IDSystem::gameInit(0x100)` des Speicherbildschirms keinen Platz bekam und
    jede Einheit der leere Ersatz `tmpId` ist. Der Speicherbildschirm legt seinen
    Speicher in Heap 11 an, dessen Größe `cCard::getUseMemSize` aus GameCube-Größen
    summiert; darin wächst auf LP64 nur der Einheiten-Vorrat (256 × `ID_UNIT`,
    0x138 → 0x158 Bytes: +8 KB).
  - Behoben (Regel `lp64_sizes`): das Budget bekommt auf LP64 genau diese 8 KB dazu.
    **Bestätigt** (Protokoll `karte-speichern.log` des Laufs 11:54, Zeilen 419–435,
    vom Projektinhaber gelesen): „game: alloc[15800]:free[156a0] id_sys.cpp(66)“,
    „game: IDSystem::gameInit() malloc failed“, danach „IDSystem::set() work full“,
    „IDSystem::unitPtr(...): Not found.“ und SIGSEGV an Adresse 0x7. 0x15800 sind
    genau 0x100 × 0x158 (der Vorrat auf LP64), frei waren 0x156A0: es fehlten 0x160
    Bytes; die Regel gibt 0x2000 dazu. Ob der nächste Kartenlauf speichert, zeigt die
    nächste PC-Prüfung.
  - Neue Fehlerart **Speicherbudgets aus GameCube-Größen** (Heap 11 des
    Speicherbildschirms, Heap 10 der Türen, Heap 12 der Untermenüs, Heap 11 der
    Filme, `cDataSwap::SwapOut`): statisch kaum zu fassen (was hineinkommt,
    entscheidet der Ablauf). Darum Patch 0081: jede Anforderung ohne Platz kommt
    ins Protokoll („heap: no room …“) und in den Zustands-Trace, dazu alle 60 Bilder
    der freie Platz jedes Heaps (`OSCheckHeap`); der Vergleichslauf meldet jede
    Anforderung, die nur auf 64 bit scheitert.
- Bericht verbessert: die Kartenläufe zeigen bei einem Absturz Stelle, Register und
  Aufrufkette und alle Spielmeldungen; der Absturz-Handler gibt auch auf x86_64 und
  aarch64 die Register und den Speicher um sie herum aus.

**Schritt 1 – Bestandsaufnahme der Prüfwerkzeuge.** Token-Logik ersetzt (Regel:
Operatoren, Werte, Typen über libclang):
- `raw_offset_check.py`: geprüfte Abstände (`RE4_OFS`, `RE4_OWN`, `offsetof`)
  erkennt es am `offsetof` im Syntaxbaum (statt am Makronamen); Bytetypen über
  ihre Art statt ihren Namen.
- `alloc_size_check.py`: `sizeof` und geprüfte Größen über den Syntaxbaum.
  `RE4_LP64_SIZE` trägt dafür auf beiden Breiten den Aufzählungswert
  `re4_lp64_size` (0). **Die alte Wort-Logik hat Funde verschluckt:** in einem
  Makro gibt libclang für einen Ausdruck aus dem Makro-Rumpf die Wörter ab der
  Definition bis in die nächsten Zeilen zurück; stand dort zufällig „sizeof“,
  galt die Stelle als geprüft (Beispiel im Selbsttest: `CLEAR_WIDE(w)`).
- `int_cast_check.py`: „im Makro?“ über die Schreib-Orte und die Makro-
  Expansionen von libclang statt über Textvergleiche (Umwandlungen in
  Makro-Argumenten sind jetzt umschreibbar).
- `fixed_layout_check.py`: welche Strukturen ein Umwandler nennt, über die
  Typ- und Glied-Verweise im Syntaxbaum statt über Wörter im Quelltext; auch über
  `typedef`-Namen (`ID_FILE_HEADER` heißt in der Ausgabe `_ID_FILE_HEADER`, das
  hatte die Wortsuche nie verglichen): 89 statt 83 Strukturen, alle gleich.
- `cast_check.py` meldet jetzt **jede** Stelle eines Paares (vorher nur die erste)
  und je Stelle die Glieder des Zieltyps, die das Ergebnis benutzt.
- `ptr_slot_check.py`: Bytezeiger + Abstand aus dem Layout (`RE4_OFS`) ist das
  Glied selbst, kein 4-Byte-Platz.
- **Selbsttest** `port/tools/m12/selftest/run_selftest.py`: zwei kleine Dateien
  mit je einem Beispiel jeder Fehlerart (auch in Makros und verschachtelt) und den
  geprüften Formen; jedes Werkzeug muss genau seine Beispiele finden.
  Ergebnis: **17 von 17** Prüfungen finden genau ihre Beispiele.
- **Gesamtlauf** `port/tools/m12/run_all.py` mit der Liste der geprüften Funde
  `port/tools/m12/reviewed.json` (jeder Fund mit Begründung, genau verglichen;
  neue Funde und nicht mehr gefundene Einträge machen ihn rot).

**Schritt 2 – Lücken geschlossen** (jede Form ein Werkzeug, jedes im Selbsttest):
- a) Zahlen-Abstände in Makros und Inline-Funktionen: `raw_offset_check.py` folgt
  jetzt auch Adressen als Zahl (`(u32) em + 0x320`, auch durch mehrere
  Umwandlungen), den Konstanten ganzer Summen mit dem Zeiger in der Mitte
  (`i * sizeof(T) + (u32) w + 0x2C`) und Variablen, die so eine Summe halten und
  danach weiterzählen (`base += 0x30C`); es vergisst eine Sicht, wenn die Variable
  neu belegt wird (aus Assembler übersetzter Code nutzt „Register“ mehrfach).
  Adressmasken (`- 0x80000000`) zählen nicht als Abstand.
- b) Kopien zwischen Strukturen verschiedener Form: neu `copy_view_check.py`
  (`memcpy`/`memmove`/`memcpy_asm` zwischen zwei Strukturen und zwischen Struktur
  und Bytepuffer): 51 Kopien, 0 Funde.
- c) `void*`-Arbeitsflächen mit GameCube-Abständen: neu `work_view_check.py`
  (verbindet über die Deklaration des Zeigers die Stellen, die ihn als Struktur
  lesen, mit denen, die feste Abstände addieren, über alle Dateien): 9 Zeiger,
  16 Paare, 0 Funde.
- d) Zeiger-Rechnung mit `sizeof` eines anderen Typs: neu `sizeof_ptr_check.py`
  (Füllbytes am Ende einer Grundklasse, andere Größe auf LP64).
- e) Zeigertabellen in Disc-Daten: neu `reloc_check.py` (Glieder, die das Spiel an
  Ort und Stelle von Abstand in Zeiger umrechnet, müssen 4 Bytes bleiben): 3 Funde,
  alle Arbeitsstrukturen des Programms bzw. Ausrichtungen, keine Disc-Daten.
- f) Speicher-Anforderer: `alloc_size_check.py` findet sie jetzt selbst (jede
  Funktion, die einen Parameter als Größe an einen bekannten Anforderer
  weitergibt, bis zum Fixpunkt): `MemAlloc`, `memAlloc` der Manager,
  `operator new`, `__builtin_new`, `reverb_mem_alloc`, `__AXFXAllocFunction` …;
  dazu `new u8[K]`. Keine neuen Funde.
- **Gefunden und behoben (Patch 0082), fünf echte 64-bit-Fehler**, alle mit dem
  GameCube-Abstand an einer Zahl-Adresse oder in einer Summe:
  Szenen-Bedingung „Gegner tot“ (`sce_sys.cpp`, `hp` bei 0x320 statt 0x3B8),
  Kamera-Ecken (`sce_at.cpp`, `pCamAt` bei 0x120 statt 0x1B0, mit 4 statt 8
  Bytes geschrieben), das Schiebepuzzle in r20e (Teile bei 0x10 statt 0x18,
  Felder bei 0x178 statt 0x1C8), die Zäune in r20d (`fence[i].t`), die Fallen in
  r21d (`trap[]` bei 0x30C statt 0x380). Auf ILP32 prüft der Compiler jeden
  GameCube-Wert (`RE4_OFS`).

Ergebnis der Bestandsaufnahme (`run_all.py`, armhf gegen x86_64, Stand nach
Patch 0082):

| Werkzeug | prüft | Funde | geprüft (`reviewed.json`) | neu |
|---|---|---:|---:|---:|
| `fixed_layout_check` | Strukturen der Formatumwandler (Disc-Daten) | 0 (89 Strukturen) | 0 | 0 |
| `decl_size_check` | Definition gegen `extern`-Deklaration | 0 (1351 Variablen) | 0 | 0 |
| `view_check` | Sichten gleichen Namens | 0 (798 Strukturen) | 0 | 0 |
| `proto_check` | C-Funktionen mit verschiedenen Struktur-Parametern | 0 (2112) | 0 | 0 |
| `cast_check` (C und C++) | Umwandlung zwischen Strukturen, mit benutzten Gliedern | 26 Paare | 26 | 0 |
| `raw_offset_check` | feste Abstände (Bytes, Adressen als Zahl, Summen) | 6 | 6 | 0 |
| `raw_offset_check --words` | dto. als 2/4/8-Byte-Wörter | 8 | 8 | 0 |
| `overlay_check` | Arbeitsflächen und Pool-Plätze | 0 (1564 Klassen) | 0 | 0 |
| `int_cast_check` | vorzeichenbehaftete 32-bit-Werte zu Zeigern | 0 | 0 | 0 |
| `ptr_slot_check` | 4-Byte-Plätze für Zeiger | 2 | 2 | 0 |
| `ptr_word_check` | Zeiger als 32-bit-Wort | 1 | 1 | 0 |
| `alloc_size_check` | feste Größen beim Anfordern/Löschen/Kopieren, Schrittweiten (17 Anforderer, 9 davon selbst gefunden) | 0 | 0 | 0 |
| `span_check` | Löschen/Kopieren über mehrere Glieder | 1 | 1 | 0 |
| `array_view_check` | Glied-Felder über ihr Ende | 5 | 5 | 0 |
| `sizeof_ptr_check` (neu) | Zeiger ± sizeof eines anderen Typs | 19 | 19 | 0 |
| `work_view_check` (neu) | `void*`-Arbeitsflächen mit festen Abständen | 0 (9 Zeiger, 16 Paare) | 0 | 0 |
| `copy_view_check` (neu) | Kopien zwischen Strukturen verschiedener Form | 0 (51 Kopien) | 0 | 0 |
| `reloc_check` (neu) | an Ort und Stelle umgerechnete Zeiger-Glieder | 3 | 3 | 0 |

Geprüft und richtig (Begründung je Fund in `reviewed.json`), zusammengefasst:
- `cast_check` (26 Paare): Zeiger nur weitergereicht oder in einem Feld anderen
  Typs abgelegt (`pEmCatch`, `pTen[]`, `pCart`, Rückrufwerte), das Objekt hat
  wirklich den Zieltyp, oder es werden nur Glieder der gemeinsamen Grundklasse
  gelesen (`cModel`/`cCoord`/`cUnit`); zwei absichtlich tote Zuweisungen
  (COMPILER-DIFF). Genau angesehen: `subem10_NeckHang_Ashley` schreibt
  `cPlayer::m_Work0` auf ein Objekt, das als `cSubChar` ankommt – es ist aber
  immer `pPL` (die Routine läuft über `EmCatchPLSet` beim Spieler; spielt man
  Ashley, ist sie ein `cPlayer`).
- `raw_offset_check`: Kopf der Heap-Zellen (0x20 auf beiden Breiten), `getCap`
  in r22c (gibt eine Nummer als `cObj*` zurück); `sfd_tim.c` hat seine LP64-Fassung
  (Patch 0070).
- `ptr_slot_check`: `MWPLY_OBJ::x48` (4 Füllbytes dahinter, nie gelesen), r20d
  (`st` ist immer 0). `ptr_word_check`: `pl_npc.cpp` (obere Hälfte ist 0).
  `span_check`: Licht-Werkzeug (Debug). `array_view_check`: `m_DataUnit[32]`
  (Endmarke), r203 `em[6]` (gleiche Überlagerung wie auf dem GameCube).
- `sizeof_ptr_check`: Sofdec-Puffer (`SFBUF_HN`, Platzhalter aus Patch 0070),
  Kartenbibliothek (eigene Parameter, DSP-Abfrage übersprungen).

**Schritt 3 – Vergleichslauf 32/64 bit** (`test-r100-core.ps1 -Compare`,
`whole_r100_check.sh --compare`): baut i686 und x86_64, spielt beide mit derselben
Tastenfolge und `RE4_VI_FAST` (Start r120: Vorfilm, Polizeifahrt, r100; 30.000
Bilder), schreibt je Bild den Zustands-Trace und vergleicht mit
`port/tools/trace_compare.py`: das erste abweichende Bild mit den Werten beider
Seiten, das erste Bild je Bereich, die Drift der Positionen davor, das Ende beider
Läufe, die Heaps (wenigster freier Platz, Anforderungen ohne Platz); am ersten
Unterschied je ein kurzer zweiter Lauf mit GX-Bildern des Bildes davor und des
Bildes (`RE4_GX_DUMP_AT`, Ordner `bilder-vergleich`).
- Trace je Bild neu: Zufallsgenerator (`re4_rnd_state`, Patch 0081; er läuft als
  erstes auseinander, wenn zwei Läufe `Rnd` verschieden oft rufen), Routine und HP
  des Spielers, lebende Objekte, Ereignisse, Effekt-Plätze, Modell-Infos,
  Modellteile, Lichter, Schaden-, Kollisions- und Trefferobjekte, die Bilder der
  laufenden Ereignisse, die GX-Kennzahlen des Bildes (Befehle, Zeichenaufträge,
  Ecken, Listenaufrufe, XF/BP, Texturen, Texturbytes, Kopien, TLUT, unbekannte
  Befehle), alle 60 Bilder die Heaps.
- Gleiche Rechnung auf beiden Seiten: i686 rechnet Gleitkomma jetzt immer in SSE
  (`-msse2 -mfpmath=sse`, wie ARM32 und die 64-bit-Ziele; vorher x87 mit 80-bit-
  Zwischenwerten), und im Vergleich laufen die float-Funktionen der C-Bibliothek
  beider Seiten über ihre double-Fassung (`libre4_detmath.so`, `LD_PRELOAD`; glibc
  rechnet `sinf`, `atan2f` … auf i686 anders als auf x86_64). Positionen bis 1 Einheit
  toleriert.
- **Cloud-Test ohne Disc-Daten** (`port/tools/test_movie_compare.py`, läuft in
  `whole_build.sh`, wenn i686 und x86_64 gebaut werden): der synthetische Film durch
  die CRI-Bibliothek (der größte Teil mit eigenen 64-bit-Sichten, Patch 0070) auf
  beiden Builds, je Halbbild Prüfsummen des Bildes (Y8/C4:4) und des Tons und die
  GX-Kennzahlen: **250 von 250 Halbbildern gleich**. Das Vergleichsprogramm selbst:
  `port/tools/test_trace_compare.py`, 8 künstliche Fälle (gleich, Rauschen in der
  Toleranz, Abweichung bei bekanntem Bild in Zufall/GX/Gegner/Position, Absturz,
  Speicher voll): 8 von 8 richtig.
- **Mit Disc-Daten noch nicht gelaufen** (nur lokal möglich).

**Filmtest 60 Hz (offener Punkt) geklärt:** der Teil misst in Echtzeit; war der
Rechner ausgelastet (Bau, qemu), begann ein Halbbild zu spät, und weil die CRI-Uhr
echte Zeit misst, stand ein Filmbild 1 oder 3 Halbbilder. `movie_probe.cpp` zählt
jetzt verspätete Halbbilder (mehr als ein halbes zu spät), `test_whole_movie.py`
rechnet je verspätetem Halbbild zwei ungleiche Bilder nicht an.

Geprüft (Cloud, `whole_build.sh i686 x86_64 armhf aarch64`, 18.10.2026): alle vier Ziele
0 Baufehler, 47 Stubs wie erwartet, Selbsttest, 114 Module, Laufzeit- und Kerntest (0
Fehler), Speicherkarte und `.gci`, Spielstand-Layout, Formatvergleich (5283 Werte, mit
synthetischem Disc-Ordner 7090 Werte gleich), Filmtest bestanden (i686 und x86_64 0
verspätete Halbbilder; armhf und aarch64 unter qemu 71 bzw. 91 verspätete Halbbilder,
jetzt richtig angerechnet), **Filmvergleich i686 / x86_64: 250 von 250 Halbbildern
gleich**, Selbsttest der M12-Prüfungen 17 von 17, `run_all.py` 18 Prüfungen ohne neuen
Fund. **Nicht getestet:** Android-Bau (kein NDK in der Cloud), Vergleichslauf und
`-Lp64` mit Disc-Daten, Speichern auf 64 bit nach der Regel (Ursache bestätigt), Patch 0082 im
Spiel (r20d, r20e, r21d, Szenen-Treffer, Kamera-Ziel), Gerät.

**PC-Prüfung 15:50–15:56 (Stand `7afb2d0`, -O2, Disc-Daten): drei Läufe, Auswertung:**
- `-Compare` (Start r120), `-Lp64` (r100, x86_64) und 32 bit (r100, i686): Build, alle
  Prüfungen ohne Disc-Daten, Filmvergleich 250/250 ✅; Formatvergleich 408 Dateien ✅,
  alle Filme beider Discs ✅ (beide r100-Läufe).
- **Speicherkarte auf 64 bit: ✅** Systemdatei angelegt, Datei 1 gespeichert, Neustart,
  Optionen und Spielstand geladen, r100 läuft (auch nach „Retry“); 32 bit ebenso. Die
  Regel für das Budget des Speicherbildschirms (Einheiten-Vorrat) ist damit bestätigt.
- **Absturz in allen vier Spielläufen**, immer nach dem Soft-Reset im Titel beim Vorfilm
  `demo0.sfd` (i686 und x86_64, r100 und r120). **Ursache: der Zustands-Trace, nicht das
  Spiel** (Werkzeug: die O2-Bauten desselben Stands in der Cloud, `addr2line`/`objdump`):
  die Absturzstelle liegt in `state_trace.cpp` `frame()`, in der Schleife von
  `aliveCount` (neu mit dem Vergleichslauf, Commit `9863875`); die Fehladresse ist das
  Register + 8 (i686) bzw. ein Unsinn-Zeiger (x86_64), genau der Lesezugriff auf
  `cUnit::next`. Nach dem Soft-Reset läuft der Titel mit den Managern des letzten Raums,
  deren Felder mit den Heaps zurückgegeben sind; der Vorfilm lädt in diesen Speicher, und
  die Schleife folgte einem Zeiger aus den Filmdaten. Der Stand `6a799f4` (ohne diese
  Schleife) lief dieselbe Stelle ohne Absturz.
  - Behoben (`runtime/state_trace.cpp`): die Listen werden nur über Werke des eigenen
    Feldes des Managers verfolgt (`inArray`: Adresse im Feld, auf Werk-Grenze), und
    `plr`, `n` und `evt` kommen erst ab Spielschritt 1 in den Trace (in Schritt 0 sind
    Spieler und Manager Reste des letzten Raums).
  - Test (`--runtime-test` trace, neu, `tests/game_probe.cpp re4probe_stale_list`): eine
    Liste aus zwei Werken des Feldes und dahinter dem Zeiger 0xC7D14C2B; die alte
    Schleife stürzt im Test genau wie im Bericht ab (Fehladresse 0xc7d14c33), die neue
    zählt 2. Im Schritt 0 stehen `plr`, `n`, `evt` nicht im Trace.
- **Vergleichslauf: bis zum Absturz unterscheidet sich nur die Gruppe GX-Kennzahlen**
  (6637 gemeinsame Bilder: Zufall, Spieler, Gegner, Werke, Ereignisse, Positionen, Kamera,
  Tonspuren und Raum-Flags in jedem Bild gleich, durch Vorfilm, Polizeifahrt, r100 und
  den Soft-Reset). Erste Abweichung Bild 2 (Start): 2 Zeichenaufträge, 2 Befehle und 8
  Ecken mehr auf i686. Ursache (im Code belegt, mit dem nächsten Lauf zu bestätigen):
  die Debug-Anzeige des Debug-Builds; ihre Zeitbalken (`debug.cpp` `processBarDisp`)
  schreiben die mit `OSGetTick` gemessene Zeit jedes Teils des Bildes als Zahlen
  (`%4.0f`, `%5.0f`), also je nach Tempo des Rechners mehr oder weniger Ziffern (je
  Ziffer ein Viereck); die Heap-Zahlen der Anzeige sind auf 64 bit ohnehin andere.
  - Geändert: der Vergleichslauf startet beide Seiten mit `RE4_DEBUG_HUD=0` (wie die
    App); `trace_compare.py` zeigt jetzt je Gruppe, in wie vielen Bildern sie abweicht,
    und jeden Abschnitt des Laufs (Raum, Spielschritt) mit seiner ersten Abweichung; GX-
    Bilder auch an der ersten Abweichung in einem Raum (Schritt 3). Selbsttest 9 von 9
    (neu: Abweichung nur beim Start, Raum gleich), läuft jetzt in `whole_build.sh`.
- Die GX-Bilder der beiden r100-Läufe haben gleich viele Zeichenaufträge, die Dreiecke
  weichen um 2–50 ab (z. B. Bild 652: 78779 / 78783, Karte 3244: 4286 / 4328); das passt
  zur Textanzeige des Debug-Builds (2 Dreiecke je Zeichen), ist aber erst mit dem
  Vergleichslauf ohne Debug-Anzeige entschieden.
- Geprüft (Cloud, Stand `f59b9a4`): `whole_build.sh i686 x86_64 armhf aarch64` und
  `RE4_OPT=2 whole_build.sh i686 x86_64` (wie die PC-Prüfung): 0 Baufehler, alle Tests
  bestanden (Laufzeittest mit dem neuen Fall auf allen Zielen), Filmvergleich 250/250,
  `trace_compare` 9 von 9, M12-Selbsttest 17 von 17. **Nicht getestet:** die drei Läufe
  mit Disc-Daten (lokal), Gerät.

**PC-Prüfung 16:25–16:32 (Stand `0e9e653`, -O2, Disc-Daten): drei Läufe, Auswertung:**
- **`-Lp64` (r100, x86_64): ALLES OK** – Build, Formatvergleich 408 Dateien, Spiellauf
  10.000 Bilder ohne Absturz (auch nach dem Soft-Reset durch die Titel-Filme), alle Filme
  beider Discs, Speicherkarte (speichern, Neustart, laden). **32 bit (r100, i686): ALLES
  OK**, ebenso. Die Trace-Korrektur (`inArray`) ist damit mit Disc-Daten bestätigt.
- **Vergleichslauf (Start r120, 30.000 Bilder, Debug-Anzeige aus): beide Seiten ohne
  Absturz bis zum Ende; Vorfilm, Polizeifahrt r120, r100 und Soft-Reset in jedem Bild
  gleich** – Zufall, Spieler, Gegner, Werke, Ereignisse, Positionen, Kamera, Tonspuren,
  Raum-Flags und **alle GX-Kennzahlen** (Abschnitte r120 Schritt 0–4 und r100 Schritt 1–3:
  „keine Abweichung“). Die Abweichung der GX-Kennzahlen im ersten Lauf (Bild 2) ist damit
  als Debug-Anzeige bestätigt (ohne sie gleich).
  - Einzige Abweichung: „Leon“ im Titel nach dem Soft-Reset (ab Bild 6968, Schritt 0),
    Werte wie 3297810 oder 4,6·10^18: die Position las der Trace aus dem Spieler des
    letzten Raums, dessen Speicher der Titel wieder benutzt. Kein Spielwert. Behoben im
    Trace: `pl` und die Gegnerliste erst ab Spielschritt 1 (wie `plr`, `n`, `evt`).
  - **Bilder** (GX-Nachbildung, `bilder-vergleich`, Pixel für Pixel in der Cloud
    verglichen): Bilder 61, 601, 901, 1119, 1201 (r120), 1501, 1801, 2401, 2501, 5001
    (r100) und 6968 (Titel) **auf beiden Seiten Pixel für Pixel gleich**. Bild 2 war ein
    altes Bild des ersten Laufs (der Ordner auf dem Desktop behielt es). **Bild 301
    (WARNING-Bild) verschieden**: 136.914 Pixel, höchstens 35 von 255, Helligkeit im
    Mittel gleich, keine Verschiebung; die großen Unterschiede (über 20) nur an den Kanten
    der Schrift (Zeilen 189–278), dazu schwache Wellen daneben – das Muster einer
    Verzerrung über eine indirekte Textur.
  - Ursache (im Code gefunden, mit dem nächsten Lauf zu bestätigen): `IdShimmerTrans`
    (`id_sys.cpp`, der Flimmer-Effekt der Bildschirm-Elemente) liest die Tiefe `dot`,
    die die Stärke der Verzerrung bestimmt, aus `indMtx`, bevor `indMtx` beschrieben ist
    (so schon im Original; die Effekt-Fassung in `esp_sub.cpp` nimmt die echte Tiefe des
    Effekts). Was dort auf dem Stapel liegt, hängt vom Build ab. **Patch 0083**: `indMtx`
    beginnt mit Nullen, `dot` damit 0 → 1500 (die Untergrenze, die stärkste Verzerrung),
    auf jedem Build gleich. Was der GameCube dort las, ist nicht bekannt.
- Werkzeuge erweitert:
  - Der Vergleichslauf rechnet die Bilder beider Seiten jetzt immer (die GX-Daten der
    beiden Spielläufe, bis 24 Bilder je Seite, dazu die Bilder am ersten Unterschied der
    Traces) und vergleicht sie Pixel für Pixel (`port/tools/picture_compare.py`, nur
    Standardbibliothek). Verschiedene Bilder machen den Lauf rot; für jedes zeigt
    `gx_chunk_inspect.py diff` die ersten verschiedenen GX-Befehle mit den Werten beider
    Seiten (BP-Register, XF-Werte, Ecken, Texel, Paletten; Adressen bleiben außen vor,
    die Heaps liegen auf 64 bit anders), und die GX-Daten beider Seiten kommen in
    `gx-vergleich-daten.zip`.
  - Der Ordner auf dem Desktop: was ein Lauf neu schreibt, ersetzt den gleichnamigen
    Eintrag des letzten Laufs (`copy_out`), es bleiben keine alten Bilder stehen.
  - Selbsttest `port/tools/test_compare_tools.py` (Bildvergleich PNG/PPM, GX-Vergleich:
    gleich, nur Adressen verschoben, XF-Wert, Ecke, Texel): 6 von 6; läuft in
    `whole_build.sh`.
- Geprüft (Cloud, Stand `fbeb173`): `whole_build.sh i686 x86_64 armhf aarch64` und
  `RE4_OPT=2 whole_build.sh i686 x86_64`: 0 Baufehler (Patch 0083 auf allen Zielen), alle
  Tests bestanden (Laufzeittest: im Schritt 0 weder `pl` noch Gegner im Trace), Filmvergleich
  250/250, `trace_compare` 9 von 9, Vergleichswerkzeuge 6 von 6, M12-Selbsttest 17 von 17;
  der Bildteil des Vergleichslaufs mit nachgebauten GX-Daten. **Nicht getestet:** der
  Vergleichslauf mit Disc-Daten (Bild 301), Gerät.

**PC-Prüfung 16:48 (Stand `fbeb173`, -O2, Disc-Daten), Vergleichslauf 32/64 bit: ALLES OK.**
- Beide Seiten 30.000 Bilder ab der Hauptschleife ohne Absturz (Vorfilm, Polizeifahrt
  r120, r100, Soft-Reset, Titel mit den Filmen `demo0`/`demo1`).
- Trace: **alle 30.619 Bilder gleich** (Zufall, Spieler, Gegner, Werke, Ereignisse,
  Positionen, Kamera, Tonspuren, Raum-Flags, alle GX-Kennzahlen; jeder Abschnitt von r120
  Schritt 0 bis zum Titel nach dem Soft-Reset „keine Abweichung“); keine Speicheranforderung
  ohne Platz; der wenigste freie Platz je Heap auf 64 bit im Rahmen (Heap 4: 0xC6B80 frei).
- Bilder: **24 von 24 Pixel für Pixel gleich** (Start, Warnbild, Logos, r120, r100, Titel
  nach dem Soft-Reset). **Bild 301 (WARNING) jetzt gleich: Patch 0083 bestätigt** – die
  Abweichung kam aus dem nicht initialisierten `indMtx` in `IdShimmerTrans`.
- Damit ist Schritt 3 von M12 (Vergleichslauf) erledigt; der Kern rechnet und zeichnet auf
  64 bit wie auf 32 bit, so weit der Lauf reicht (r120, r100, Titel). Noch nicht
  verglichen: andere Räume (10b), Speicherbildschirm im Vergleichslauf (die Kartenläufe
  von `-Lp64` und 32 bit sind einzeln grün).

**Gerät, 64 bit, automatischer Lauf (Retroid, 18.10.2026 16:54, Stand `8f0c425`,
`test-core-app.ps1 -Abi arm64-v8a -Seconds 480`): ✅ ohne Auffälligkeiten** (Projektinhaber:
„ohne sichtliche Auffälligkeiten“). Kern arm64-v8a mit NDK r28c gebaut (0 Fehler, 114
Module, 47 Stubs wie erwartet), App läuft als arm64-v8a (64 bit). Vorfilm `opening.sfd`
ganz (2278 Bilder, 0 übersprungen, Status 3), Polizeifahrt r120, Funkspruch, Ankunft r100,
Gegner, Zielen mit Laser, Untermenüs auf/zu; 480 s ohne Absturz; GX: 0 fehlende Texturen,
0 Shader-Fehler, 59–60 Bilder/s angezeigt; keine Speicheranforderung ohne Platz; Ton 0
verlorene Blöcke, 0 übersprungene Stimmen (53 Mal lief der Tonpuffer leer, vor allem beim
Laden; mit dem 32-bit-Kontrolllauf zu vergleichen). Bildschirmfotos (15–430 s): Logos,
Vorfilm, Polizeiauto im Nebel, Funkspruch, Haus in r100 mit Ganado. Noch offen: Lauf von
Hand (64 bit), Kontrolllauf 32 bit.

**Gerät, 64 bit, von Hand (Retroid, 18.10.2026):** Projektinhaber: „es läuft scheinbar
alles, Stage 1-1 lief problemlos“ (kein Bericht mitgeschickt). Noch offen: Kontrolllauf
32 bit (`-Abi armeabi-v7a`); Speichern/Laden auf dem Gerät mit 64 bit ausdrücklich
bestätigen. Der Release-Bau (`coreRelease`, signiert) ist noch nie gebaut worden.

**Tester (18.10.2026):** „Beim Tester läuft alles soweit.“ Sein Rat: ein anderer App-Name
wegen der Marke „Resident Evil“.

**Neuer Name (Entscheidung des Projektinhabers, 18.10.2026): CubeEvil4, Paketname
`com.ast.cubeevil4`, eigenes Symbol.**
- App-Name unter dem Symbol, Titel des Startbildschirms, Überschrift des App-Menüs
  („CubeEvil4 Options“), Name der Fehlerbericht-Datei (`cubeevil4-report-….zip`); unter dem
  Titel der Hinweis „Requires your own game discs. Not affiliated with Capcom.“ Die Texte
  des Spiels selbst (von der Disc) bleiben, wie sie sind.
- Paketname nur der Variante `core` (`applicationId 'com.ast.cubeevil4'`); das Java-Paket
  (`namespace`, die Klassen und ihre JNI-Namen) bleibt `com.ast.re4decomp`, die Variante
  `classic` bleibt `com.ast.re4decomp.classic`. Version 0.38.0 (versionCode 56).
- Symbol: eigene Zeichnung (`port/android-decomp/docs/icon/`): ein Drahtgitter-Würfel mit
  Flügeln, Seitenhaken, Spitze und langer Klinge im Stil des Kult-Zeichens, auf rotem
  Grund; ersetzt das Zeichen der Los Illuminados (`icon_illuminati.jpg`, Capcom-Grafik,
  entfernt). Das Android-TV-Banner (vorher ein Vektorbild mit „R4“) zeigt jetzt Symbol und
  Namen.
- Android sieht `com.ast.cubeevil4` als neue App: die bisherige (`com.ast.re4decomp`) bleibt
  mit ihren Daten installiert. `test-core-app.ps1` kopiert beim ersten Lauf ihre Discs und
  ihre Speicherkarte einmal in die neue App (wie zuvor von `com.ast.re4decomp.core`); Tester
  exportieren ihre Spielstände vorher im App-Menü als `.gci` und importieren die Discs neu.
  `pull-gx-dumps.ps1` und `pull-address-map.ps1` nutzen den neuen Paketnamen.
- Getestet: XML-Dateien wohlgeformt; `tools/test_host.sh`: alle Java-Tests der App
  bestanden (Discs, Steuerung, Menüs, Start, Räume …), von 42 C++-Tests 40 bestanden. Rot
  sind `enemy_feedback_tests` („Invalid source input rejected“) und
  `inventory_case_contracts` (Assertion in `tests/inventory_case_tests.cpp:23`): beides
  Module der Variante `classic`, ihr Code und ihre Tests sind seit der Übernahme der App
  (Subtree-Merge `4b2df69`) unverändert, also schon vorher rot und unabhängig von der
  Umbenennung (eigene Aufgabe). Der Bau der APK und das Gerät noch nicht (kein Android-SDK
  in der Cloud).
- Repository auf GitHub umbenannt (Projektinhaber, 18.10.2026): `Andiweli/RE4` →
  **`Andiweli/CubeEvil4`** (der alte Name leitet weiter, solange kein neues Repository „RE4“
  angelegt wird). README beginnt jetzt mit CubeEvil4 (App, Stand, Bauen), die Beschreibung
  der Decompilation folgt darunter.
- Tester-Gerät: POCO X7 Pro (Android 16) läuft (Projektinhaber, README-Änderung 18.10.2026).
- Aufräumen der Branches (Entscheidung des Projektinhabers, 18.10.2026): Meilenstein-Tags
  `m4-boot`, `m5-r100`, `m6-gx`, `m7-controls`, `m8-audio`, `m9-movies`, `m10-save-load`,
  `m11-polish` (je die Spitze des Meilenstein-Branches); gelöscht werden
  `claude/re4-gamecube-android-port-97hbjj`, `claude/re4-android-decomp-core`,
  `claude/nifty-feynman-jv729h`, `codex`, `android-port-deprecated`, `claude/re4-m12-arm64`
  (alle vollständig im Arbeitsbranch enthalten); es bleiben `master`, der Arbeitsbranch,
  `re4-standalone` und die Branches von M6 bis M10. Tags und Löschen sperrt die Zugangsschranke
  der Cloud-Sitzung (HTTP 403; nur der eigene Arbeitsbranch ist beschreibbar): der
  Projektinhaber führt es vom PC aus.
- `master` übernimmt den Arbeitsstand (Wunsch des Projektinhabers, 18.10.2026): vorgespult
  von `15cf64f` auf `121b3dd` (208 Commits, ohne Zusammenführungs-Commit, kein Verlauf
  umgeschrieben); die GitHub-Startseite zeigt seitdem die README von CubeEvil4 mit Symbol.
  Weitergearbeitet wird im Arbeitsbranch; `master` bekommt neue Stände auf Wunsch oder am Ende
  eines Meilensteins.
- Repository erneut umbenannt (Projektinhaber, 18.10.2026): `Andiweli/CubeEvil4` →
  **`Andiweli/CubeEvil4-Android`** (beide alten Namen leiten weiter). README-Link „ALPHA
  RELEASE“ zeigt auf die neue Adresse; Text des ersten Alpha-Releases:
  `port/android-decomp/docs/releases/v0.38.0-alpha1.md`.
- Tester, FPS-Aufzeichnung mit der App „Scene“ (18.10.2026 22:24, POCO X7 Pro, Dimensity 8400,
  Android 16, Bildschirm 1080×2400 mit 120 Hz, CubeEvil4 0.38.0-core): Bilder je Sekunde an den
  Bildschirm im Mittel 105,6, höchstens 121, 5 %-Tief 22, tiefster Wert 16; 86,2 % der Zeit
  mindestens 45; Grafikchip 237 s lang zu mindestens 96 % ausgelastet (höchstens 100 %);
  Temperatur höchstens 43,9 °C, Leistung im Mittel 4,4 W. Die tiefen Einbrüche (16–25) häufen
  sich im ersten Teil der Aufzeichnung, bei noch niedriger Temperatur, später nur einzelne.
  Gemessen sind die an den Bildschirm gegebenen Bilder (bis 120 je Sekunde, das Spiel selbst
  liefert 60 Halbbilder); Ursache noch offen: die Aufzeichnung zeigt weder Einstellungen noch
  Spielstelle. Die App wählt bei „Auto“ auf diesem Bildschirm die 3-fache Auflösung
  (1920×1344, wie auf dem Retroid). Angefordert: Fehlerbericht der App direkt nach einem
  solchen Lauf (GX-Werte alle 10 s), Einstellungen, Spielstelle, ein zweiter Lauf derselben
  Stelle und ein Lauf mit 2-facher Auflösung und ohne Tiefenschärfe/Kantenglättung.

**Tester-Bericht 19.10.2026 01:00 (POCO X7 Pro, Android 16, 64 bit, CubeEvil4 0.38.0-core,
`cubeevil4-report-20261019-010055.zip`; 3-fache Auflösung, 2× MSAA, 8× anisotrop, Tiefenschärfe),
Auswertung:**
- **Absturz (64 bit) am 18.10. 22:58 in r104**, gleich nach einem Schuss (R+A): SIGSEGV an
  `0xf81017988` in `cObj04::move` (+52), gerufen aus `EfmSetObj04` (Effekt-Modelle, die bei
  einem Treffer wegfliegen: Splitter, Trümmer); Register `x8 = 0x0000000f81017980`, gelesen
  wird `be_flag` des Eltern-Modells (`pMod->isAlive()`, Abstand 8 auf LP64).
- **Ursache (Absturzbericht und Code):** `EfmSetObj04` (esp_efm.cpp) schreibt Eltern-Modell
  und dessen `guid` über einen Wortzeiger: `u32* link = (u32*) &w->pMod; link[0] = (u32) parent;
  link[1] = parent->guid;`. Auf ILP32 sind das `pMod` und `Guid_pMod`; auf LP64 hat `pMod`
  8 Bytes, `link[1]` ist seine obere Hälfte. So wurde `pMod = guid << 32 | parent`
  (`0xF` und `0x81017980`, genau der Registerwert) und `Guid_pMod` blieb alt.
  **Patch 0084:** die beiden Werte über die Felder selbst, auf ILP32 gleich.
- **Lücke im Werkzeug:** `port/tools/m12/ptr_word_check.py` kannte nur den direkt
  dereferenzierten Wortzeiger (`*(u32*) &p = x`), nicht den in einer Hilfsvariablen
  gehaltenen. Es verfolgt jetzt lokale Variablen, denen ein Wortzeiger auf einen Zeiger
  zugewiesen wird, und verschobene Wortzeiger (`*((u32*) &p + 1)`); „Schreiben“ heißt nur noch
  linke Seite von `=` oder `+=` … (vorher zählte auch die linke Seite von `==` oder `+`).
  Auf dem ganzen Spielcode findet es genau die zwei Zeilen von `EfmSetObj04` neu (nach Patch
  0084: nur noch die geprüfte Stelle in pl_npc.cpp). Selbsttest um drei Beispiele und ein
  Gegenbeispiel erweitert (17 von 17).
- **Gegner nicht geladen (64 bit) am 18.10. 23:12 in r107:** „alloc[3d2080]:free[3b2fa0]
  read.cpp(725)“, „DVD: Memory allocate failed“, „readEmData(): error! em/em12.drs“,
  „EmSetFromList() Em set failed, Id = 12“: `em12.drs` braucht 0x3D2080 Bytes in Heap 4, frei
  waren 0x3B2FA0, es fehlten 0x1F0E0 (124 KB); der Raum lief ohne diesen Gegner weiter.
  Auf 64 bit belegen Werke und Vorräte mit Zeigern mehr Platz in den Heaps (Gegner-Platz
  0xF60 statt 0xDE0, Objekt-Platz 0x4B0 statt 0x3D8, Effekt-Platz 0x1A8 statt 0x150 …): im
  Vergleichslauf 16:48 hatte Heap 4 in r100 auf i686 mindestens 0x115AC0 frei, auf x86_64
  0xC6B80, 0x4EF40 (316 KB) weniger. Ob `em12` in r107 auf 32 bit passt, ist noch nicht
  gemessen (Vergleichslauf mit Start r107, siehe unten).
  **Regel `lp64_sizes` (neuer Eintrag):** das Ende der Heaps (`SystemMemInit`,
  `Heap_end_addr`) liegt auf LP64 1 MB höher, 0x818F4000 statt 0x817F4000, in der Hälfte von
  MEM1, die nur eine Entwicklungskonsole hat (die Reservierung des Ports umfasst 48 MB). Heap 0
  und über `MemReplaceHeap` der Raum-Heap 4 enden dort. Die Laufzeit legt die Dateitabelle
  der Disc (FST) entsprechend höher (`RE4_MEM1_LP64_HEAP_EXTRA`, `re4_runtime.h`); der
  Laufzeittest `tasks` prüft, dass Heap-Ende des Spiels und FST-Platz der Laufzeit
  übereinstimmen (ILP32 0x817F4000, LP64 0x818F4000). Auf 32 bit bleibt alles wie beim GameCube.
- Weitere Meldungen ohne Fehler: `cDataSwap.cpp(64)` und `datactrl.cpp(452)` ohne Platz
  (Merkmal 0): das Spiel weicht dann wie vorgesehen auf das ARAM aus bzw. lädt neu;
  `Debug_alloc` ohne Platz beim Öffnen des Debug-Menüs (`rel/tools.rel`); „ESP : num max.
  retry“ (Effekt-Vorrat voll, eine Meldung des Spiels). Ob eine davon auf 32 bit seltener ist,
  zeigt erst ein Vergleichslauf derselben Stelle.
- **Speichern und Laden auf 64 bit auf dem Gerät des Testers bestätigt** (Protokoll):
  Laden von Spielstand 6 (22:40), Speichern (22:47, „card-a.bin saved“), insgesamt 14 Mal
  gespeichert; die Karte zeigt sieben Spielstände bis r10e (2:40 Spielzeit).
- **Ruckler und Shader (Frage vom 18.10.):** die App schreibt alle 10 s eine Zeile mit dem
  längsten Bild und der Zahl der Programme (übersetzt und aus dem Zwischenspeicher). Über alle
  834 Abschnitte des Berichts: in Abschnitten mit **neu übersetzten** Shader-Programmen hatte
  **99 %** ein Bild über 25 ms (Mittelwert des längsten Bildes 66 ms, schlimmster Fall 1057 ms);
  mit Programmen **nur aus dem Zwischenspeicher** 9 % (höchstens 37 ms), ohne neue Programme
  6 %. Die Ruckler kommen also vom erstmaligen Übersetzen; der Zwischenspeicher wirkt (230
  von 979 Programmen der zweiten Sitzung kamen aus ihm). Sonst zeigt die App im Mittel
  119–120 Bilder je Sekunde, der Kern 30 Spielbilder je Sekunde ohne Warten auf die Grafik.
- App-Version 0.38.1 (versionCode 57), damit Berichte des neuen Stands erkennbar sind.
- Zweites Alpha-Release (Wunsch des Projektinhabers, 19.10.2026: auf GitHub liegt noch 0.38.0):
  Text `port/android-decomp/docs/releases/v0.38.1-alpha2.md`, APK
  `CubeEvil4-v0.38.1-alpha2.apk`, signiert mit demselben Schlüssel wie 0.38.0 (sonst lässt
  Android das Update nicht über die alte App installieren).
- Getestet (Cloud): `whole_build.sh i686 armhf x86_64 aarch64` mit Patch 0084 und der neuen
  Regel: 0 Baufehler, alle Prüfungen bestanden (Laufzeittest mit `tasks` samt Heap-Ende, Kern-
  test, Speicherkarte, Formatvergleich, Filmtest, Filmvergleich 32/64 bit 250/250,
  M12-Selbsttest 17/17); `ptr_word_check` auf dem ganzen Spielcode wie oben; alle 18
  M12-Prüfungen auf den echten Builds (`run_all.py`, 26 min): 0 neue Funde. **Nicht
  getestet:** Disc-Daten (PC-Prüfung), Gerät; dass der Absturz in r104 weg ist, zeigt erst ein
  Lauf an derselben Stelle.

**PC-Prüfung 09:25 (Stand `2b260e2`, -O2, Disc-Daten), Vergleichslauf 32/64 bit mit Start r107,
3000 Bilder: NICHT BESTANDEN, Auswertung:**
- **Speicher in r107 bestätigt:** beide Läufe ohne Absturz, `em12` auf beiden Seiten geladen
  (Brücke: Gegner 0x12 mit Teilen aus `em/em12.drs`), keine Anforderung ohne Platz (i686 0,
  x86_64 0). Heap 4, wenigster freier Platz: i686 0x62BC0 von 0xD220C0, x86_64 0xE0EE0 von
  0xE1E820. Belegt sind auf x86_64 0xD3D940 statt 0xCBF500, 0x7E440 (505 KB) mehr. Ohne das
  zusätzliche MB wäre Heap 4 0xD1E820 groß (genau die Größe auf dem POCO X7 Pro), es fehlten
  0x1F120 Bytes; der Tester-Bericht nannte 0x1F0E0 (64 Bytes Unterschied: ein anderer Weg in den
  Raum). **Ursache und Regel damit bestätigt.** Reserve auf 64 bit jetzt 0xE0EE0 (900 KB) gegen
  0x62BC0 (395 KB) auf 32 bit. Heap 12 (Untermenü) hat auf 64 bit 0x14580 frei gegen 0x25D20:
  reicht, im Blick behalten (Raum-Rundgang 10b).
- **Erste Abweichung Bild 626** (r107, Schritt 3): GX-Kennzahlen (Befehle 29314 / 29416, XF 6418 /
  6520), in 35 von 3622 Bildern; 10 von 17 Bildern Pixel für Pixel gleich, die übrigen höchstens
  1–3 von 255 in kleinen Bereichen. Die GX-Daten zeigen den Grund: XF 0x0613 (die Farbe von Licht 1;
  `gx_chunk_inspect` zeigt das Wort als Gleitkommazahl) wechselt auf i686 (0x918C82, 0x858974,
  0x8C9079, 0x91957E, 0x8F927C: dieselbe Grundfarbe mal 0,83 bis 0,91) und steht auf x86_64 ab Bild
  653 fest auf 0xA0A48B (der Grundfarbe selbst): das Licht schwankt auf 64 bit nicht.
- **Ursache (Compiler und Gegenprobe):** Lichtart 5 (`Light05_Move`, Helligkeit nach einem
  Lichtpfad) legt `LIT05_MOVE_FREE` über `cLight::work`, das die Lichtsätze des Raums in
  GameCube-Form füllen (`cLight::operator=`: `sub = w.sub`). Pfadnummer `Id` bei 0xC, `Flag` bei
  0xD, davor `cLightPath` mit zwei Zeigern; auf LP64 ist `cLightPath` 0x18 Bytes groß und `Id`
  liegt bei 0x18 (gemessen mit den Bau-Flaggen: `offsetof(LIT05_MOVE_FREE, Id)` armhf 0xC,
  x86_64 0x18). Gelesen wurde so Pfad 0 statt des Pfads aus den Raumdaten. Gegenprobe mit dem
  neuen Laufzeittest `light` (eine Lichtart 5 mit Arbeitsbytes in GameCube-Form, Pfad 0: 200,
  Pfad 1: 50, 100): ohne Korrektur auf x86_64 „DispCol.r 200“ (Pfad 0), mit Korrektur 100 auf allen
  vier Zielen.
- **Behoben** (Regel `fixed_form_structs`): `cLightPath` (`include/lightPath.h`) in GameCube-Form,
  Zeiger 4 Bytes. Ebenso `TSceAtHide` (Debug-Werkzeug `t_sce_at`: seine Sicht auf
  `SCE_AT_DATA_HIDE` in den Bereichsdaten des Raums, Rückruf-Zeiger bei 0x30).
- **Lücke im Werkzeug:** `overlay_check.py` prüfte nur, ob eine Struktur in ihren Byte-Bereich
  passt, nicht, ob ihre Form zu den Daten passt, die dort liegen. Neu `--areas` (in `run_all.py`
  als `overlay_areas`, im Selbsttest): jeder Byte-Bereich, über den das Spiel eine Struktur legt,
  die auf LP64 eine andere Form hat. 76 Bereiche: 71 eigene Arbeitsflächen der Vorratsklassen
  (`free` der Abkömmlinge von cEm, cObj, cEsp; ihr Code füllt sie Feld für Feld, Kopien prüft
  `copy_view_check`) werden nur gezählt. Von den 5 übrigen bekamen 2 Daten von der Disc
  (`cLight::work`, `SCE_AT_DATA::data` im Debug-Werkzeug), beide behoben; 3 sind reine
  Laufzeit-Arbeit und in `reviewed.json` begründet (`cCtrl::work`, `cSceSys::SceTaskOt`,
  `tagESPGEN_FREE::buff`).
- Getestet (Cloud): `whole_build.sh i686 armhf x86_64 aarch64`: 0 Baufehler, alle Prüfungen
  bestanden, Laufzeittest `light` auf allen vier Zielen; M12-Selbsttest 18 von 18; alle 19
  M12-Prüfungen auf den echten Builds (`run_all.py`, 28 min): 0 neue Funde. **Nicht
  getestet:** der Vergleichslauf r107 nach der Korrektur (zeigt, ob Bild 626 jetzt gleich ist),
  Gerät.

**PC-Prüfung 10:34 (Stand `2b9a60d`, -O2, Disc-Daten), Vergleichslauf 32/64 bit mit Start r107,
3000 Bilder: ALLES OK.** Trace: alle 3622 Bilder gleich (jeder Abschnitt von r120 Schritt 0 bis
r107 Schritt 3 „keine Abweichung“); Bilder 16 von 16 Pixel für Pixel gleich (auch 653, 773, 901,
1373, 1501, 2401, vorher verschieden); keine Anforderung ohne Platz; Heaps wie im Lauf 09:25.
**Lichtart 5 in GameCube-Form damit bestätigt.** Nur `bericht.txt` im Ausgabeordner: Bilder und
GX-Daten legt der Vergleichslauf nur bei Unterschieden ab.

**PC-Prüfung 10:38 (Stand `2b9a60d`, -O2, Disc-Daten), Vergleichslauf 32/64 bit ab r120,
30000 Bilder: ALLES OK.** Trace: alle 30.619 Bilder gleich (r120, r100, Soft-Reset, Titel mit den
Filmen; jeder Abschnitt „keine Abweichung“); Bilder 24 von 24 Pixel für Pixel gleich; keine
Anforderung ohne Platz. Heap 4 auf x86_64 jetzt mindestens 0x1C6B80 frei von 0xE32F00 (vorher
0xC6B80 von 0xD32F00: genau das zusätzliche MB), i686 unverändert 0x115AC0 von 0xD367A0. Damit
ändern weder die Regel für das Heap-Ende noch die Lichtart 5 etwas am Ablauf von r120/r100 auf
64 bit. Der Bericht bat um den Ordner `bilder-vergleich` auch, wenn alle Bilder gleich waren;
jetzt nur noch bei Unterschieden (`whole_r100_check.sh`).

**Falsche Variante gebaut (Projektinhaber, 19.10.2026):** beim Bauen von 0.38.1 zeigte die
installierte App „RE4 Android Decomp · 0.37.0“. Das ist der Startbildschirm der Variante `classic`
(eigene Spielmodule, Paket `com.ast.re4decomp.classic`, Name „RE4 Decomp“); Android Studio nimmt
ohne Auswahl die erste Variante (`classicDebug` / `classicRelease`). Für CubeEvil4 ist es
`coreRelease`. Die feste Versionsangabe „0.37.0“ im Titel von `classic` stammte noch aus M11: der
Titel lautet jetzt „RE4 Android Decomp (classic, not CubeEvil4)“ mit der echten Version
(`MainActivity.versionName()`); ungeprüft, da in der Cloud kein Android-SDK.

**Hänger im Lichterrätsel der Kirche (r117), 64 bit** (Frage des Projektinhabers, 19.10.2026; Bericht
des Testers `cubeevil4-report-20261019-010055.zip`, POCO X7 Pro):
- **Protokoll:** r117 ab 00:50:48; nach dreieinhalb Minuten öffnet der Tester das Debug-Menü
  (00:54:29) und verlässt den Raum, ab 00:58:31 ist er wieder dort. Ab 00:58:35 läuft das Spiel
  weiter (30 Spielbilder je Sekunde, „GX core“ mit 26 ms Leerlauf), aber Bild und Ton stehen: ab
  01:00:02 alle 10 s genau 222 Zeichenaufträge und 260 Uniform-Uploads je Bild, 0 Stimmen; um
  01:00:47 SELECT und der Fehlerbericht.
- **Ursache (Code und Compiler):** `r117_LightMechanismMove` (Modus 2, „Muster prüfen“) wartet mit
  `SceSleep(1)`, bis `r117_espEnd(W->esp[0])` Bit 2 im Byte 0xF9 des Lichtstrahls sieht. Das setzt
  nur esp06 (der Effekt auf einem Pfad; `ESP06_WK.Flg`, Bit 2: am Pfadende angehalten): von allen
  45 Klassen, die von `cEsp` erben, setzt sonst keine Bit 2 im Byte 1 ihrer eigenen Arbeit.
  `ESP06_WK` enthält einen Zeiger (`pPath`) und beginnt auf LP64 8-ausgerichtet bei 0x110, nicht
  am Datenende von `cEsp` (0x10C). Patch 0077 las `RE4_OWN(cEsp, u8, 0xF8) + 1` = 0x10D. Gemessen
  mit den Bau-Flaggen: `Flg` bei 0xF9 (armhf), 0x111 (x86_64, aarch64); Patch 0077 jetzt
  `RE4_OWN(cEsp, void*, 0xF8) + 1` = 0x111 (vorher 0x10D), auf ILP32 weiter 0xF9. Von 36 Klassen
  mit `m_Free` beginnen 9 auf LP64 bei 0x110 (esp03, 06, 0d, 0e, 11, 12, 16, 3f, 4e).
- **Lücke im Werkzeug:** `RE4_OWN` galt den Prüfungen als geprüfter Abstand, gleich mit welchem
  `T`. Neu `port/tools/m12/own_start_check.py` (in `run_all.py`, Selbsttest 19 von 19): jede
  `RE4_OWN(B, T, gc)`-Stelle, bei der die Klassen, die von `B` erben, ihre eigene Arbeit an mehr
  als einem Abstand beginnen (cEsp: 36 bei 0x10C, 9 bei 0x110; cEm: 54 bei 0x49C, 2 bei 0x4A0).
  5 Stellen, alle mit Begründung in `reviewed.json`: r117 (esp06, jetzt richtig), r300/r318
  (Laser-Endpunkt als Vec 8 Bytes in die Arbeit: nur esp01, esp14, esp41 haben dort einen Vec,
  alle ab 0x10C), `esp_sub.cpp` (esp1b, echter Typ der Arbeit, 0x10C), `em3e.h` (nur cEmMark,
  0x49C).
- App-Version 0.38.2 (versionCode 58).
- Getestet (Cloud): `whole_build.sh i686 armhf x86_64 aarch64`: 0 Baufehler, alle Prüfungen
  bestanden; alle 20 M12-Prüfungen auf den echten Builds (`run_all.py`, 29 min): 0 neue Funde.
  **Nicht getestet:** das Rätsel selbst (braucht das Spiel bis r117 auf einem 64-bit-Gerät).
  **Nachtrag 20.10.2026: auf dem Gerät bestätigt** (0.38.2, Projektinhaber).

### Start erst mit beiden Discs (Entscheidung des Projektinhabers, 17.10.2026)

Im Kernmodus startet das Spiel (Knopf „Start game“ und der automatische Start)
erst, wenn Disc 1 und Disc 2 vollständig importiert sind
(`MainActivity.gameReady()`); bis dahin ist „Start game“ ausgegraut und der
Import sichtbar. `test-core-app.ps1` wartet entsprechend auf beide Discs. Die
Variante `classic` braucht weiter nur Disc 1.

### Touch-Steuerung und 60 FPS (Prüfung und Entscheidungen des Projektinhabers, 19.10.2026)

- **Touch-Steuerung mit RetroTouch** (`Andiweli/RetroTouch`, 1.0.0-beta.4, MIT, eigene Bibliothek
  des Projektinhabers): machbar als Ebene über der Spielansicht, eingespeist als weiteres Gerät
  in `CoreInput`; automatisch nur ohne Controller (`RetroTouchControllers`), im App-Menü
  „Use Touch Controls“. Belegung: linker Stick, Steuerkreuz (Spielmenüs), `A` `B` `X` `Y` `Z`
  `L` `R` `START`, `MENU` (wie SELECT). **C-Stick über die rechte Touchfläche** (Look-Zone,
  Entscheidung des Projektinhabers): RetroTouch meldet zwei Sticks ohne Kennung
  (`onMove(x, y)`), die Look-Zone dagegen getrennt (`onLook`). Umgesetzt mit 1.0.0-beta.5
  (nächster Abschnitt).
- **60 FPS: vorerst nicht** (Entscheidung des Projektinhabers). Befund: `SetSystemVcnt(1)`
  (60 Hz) ließe das Spiel doppelt so schnell laufen; die Spiellogik rechnet in Bildern, die
  Einstellung wird nur an 7 Stellen gelesen (Tasten-Wiederholung, DVD, Filme, Ausnahme,
  Debug-Balken). Echte 60 FPS bräuchten berechnete Zwischenbilder im Renderer.

### Touch-Steuerung eingebaut (RetroTouch 1.0.0-beta.5, 19.10.2026, Cloud, Branch `claude/re4-touch-controls`)

- **Bibliothek:** `port/android-decomp/app/libs/retrotouch.aar` ohne Versionsnummer im Namen
  (Wunsch des Projektinhabers: ein Update ersetzt nur die Datei), eingebunden in
  `app/build.gradle` (`implementation files('libs/retrotouch.aar')`). beta.5 bringt Farben
  (`RetroTouchColors`), sonst die API von beta.4.
- **Aufteilung:** `CoreTouch.java` (ohne Android, Host-Test) übersetzt RetroTouchs Meldungen in den
  GameCube-Controller und hält das Standard-Layout; `TouchControls.java` legt `RetroTouchView` über
  das Bild, `CoreActivity` reicht Berührungen durch (`dispatchTouchEvent`) und teilt die
  SELECT-Logik (`select()`), `CoreInput` hat ein eigenes Gerät `TOUCH` (`touchButton`, `touchStick`,
  `touchClear`).
- **Wann sichtbar:** automatisch, solange kein Controller angeschlossen ist
  (`RetroTouchControllers.isControllerConnected()`: eingebaute, USB- und Bluetooth-Controller;
  geprüft zweimal pro Sekunde und bei jeder Geräteänderung); mit dem Schalter **„Use Touch
  Controls“** im App-Menü (neuer Abschnitt „Controls“, gespeichert in `core_input/touchControls`)
  immer. Auf dem Retroid (eingebaute Tasten) also nur mit dem Schalter.
- **Belegung:** Knöpfe `A` (grün) `B` (rot) `X` `Y` (hellgrau) `Z` (lila) `L` `R` `START` `MENU`
  (grau), Steuerkreuz (auch für die Spielmenüs), linker Stick = Control-Stick, rechte
  Bildschirmhälfte = C-Stick. `MENU` = SELECT: kurz das App-Menü, 1 s halten das Debug-Menü.
  **`L` und `R` rasten ein** (Tippen hält, nächstes Tippen lässt los, RetroTouch zeigt sie dann
  kräftiger): mit zwei Daumen lässt sich R (zielen) sonst nicht halten, während A (feuern)
  getippt wird. Eigene Entscheidung, auf Wunsch leicht umstellbar.
- **C-Stick aus der Look-Zone:** RetroTouch meldet dort nur Bewegungen, kein Aufsetzen oder
  Abheben. Der C-Stick ist darum der Weg des Fingers seit dem Aufsetzen (0,15 der kürzeren
  Bildschirmseite = voll, längere Wege schieben die Mitte mit) und springt beim Abheben zurück.
  Welcher Finger schaut, bestimmt `TouchControls`: ein Finger, der in der Look-Zone aufsetzt und
  keinen Knopf/Stick/Steuerkreuz auslöst. Ein Daumen außerhalb aller Knöpfe nimmt die
  Spielfläche selbst (`setOnTouchListener`), sonst gäbe Android die weiteren Finger dieser
  Berührung an keine Ansicht mehr. Bekommt RetroTouch später „Finger berührt/abgehoben“ für die
  Look-Zone, ersetzt das diese Bestimmung.
- **Editor:** RetroTouchs Zahnrad oben rechts (verschieben, Größe, Deckkraft, speichern); das
  Spiel steht währenddessen wie im App-Menü. Das App-Menü hat zusätzlich „Back to the game“ zum
  Schließen per Touch.
- **Geprüft (Cloud):** `tests/CoreTouchTests.java` (232 Prüfungen, in `tools/test_host.sh`):
  Knöpfe, einrastendes L/R, MENU, Sticks, C-Stick-Weg und Zurückspringen, Finger, die nicht
  schauen, Abbruch, zusammen mit einem echten Controller; das Layout nach RetroTouchs Geometrie:
  keine Touch-Flächen überlappen, alles bei 4:3, 16:9 und 20:9 im Bild, Beschriftungen höchstens
  5 Zeichen (Gegenprobe: ein verschobenes B schlägt an). Alle App-Quellen kompilieren ohne
  Fehler und Warnungen gegen die Android-15-Klassen (`org.robolectric:android-all`, nur zum
  Prüfen, nicht im Repository) und die Klassen der AAR. `CoreInputTests` weiter 69 Prüfungen.
  Die übrigen Host-Tests wie bisher; weiterhin rot nur die bekannten `enemy_feedback_tests` und
  `inventory_case_contracts` (siehe oben).
- **Gerätetest (Retroid, Projektinhaber, 19.10.2026):** Build mit der AAR gelungen, die
  Touch-Steuerung „scheint alles zu klappen“. Danach in den M12-Branch übernommen.
- **Noch nicht getestet:** Lint (läuft erst beim `coreRelease`-Build), ein Handy ohne Controller
  (automatisches Einblenden), An- und Abstecken eines Controllers während des Spiels.

### Grafik-Auslastung: Analyse (19.10.2026, Cloud, nur Code und Berichte, noch nichts gemessen)

Anlass: beim Tester (POCO X7 Pro, Mali-GPU, 3-fach, 2× MSAA, 8× anisotrop, Tiefenschärfe) war
der Grafikchip 237 s lang zu mindestens 96 % ausgelastet, 4,4 W im Mittel. Auf dem Retroid
(Adreno, Zeitabfrage vorhanden) maß die App 8–15,6 ms GPU je Spielbild (16.10., 3-fach) und
17,7–19,7 ms nach M11 (17.10., mit den neuen Grafikoptionen). Gefunden im Renderer
(`port/android-decomp/app/src/main/cpp/gx_gl.cpp`, `core_mode.cpp`):
- **MSAA über eigene Mehrfach-Puffer und Blit-Auflösung:** jede EFB-Kopie (im Bericht des
  Testers 5,45 Kopien je Bild, dazu die Kopie ins XFB) und der Autofokus (in 49 % der Bilder,
  Farbe und Tiefe über das ganze EFB) lösen den MSAA-Puffer mit `glBlitFramebuffer` auf und
  wechseln den Framebuffer. Auf Kachel-GPUs (Mali, Adreno) schreibt jeder Wechsel den ganzen
  Puffer in den Speicher und lädt ihn danach wieder: bei 3-fach (EFB 1920×1584) mit 2 Proben
  Farbe und Tiefe zusammen etwa 46 MB, je Wechsel also bis etwa 90 MB hin und zurück
  (Schätzung ohne die Kompression der Treiber). Üblich auf Mobilgeräten:
  `EXT_multisampled_render_to_texture` (die Proben bleiben im Kachelspeicher, aufgelöst wird
  beim Zurückschreiben).
- **Kein `glInvalidateFramebuffer`:** auch Inhalte, die nicht mehr gebraucht werden (Tiefe am
  Ende des Bilds, Puffer vor einem vollständigen Löschen), werden gespeichert bzw. geladen.
- **Tiefenschärfe in voller Auflösung:** zwei volle Auflösungen (Farbe, Tiefe), ein voller
  Unschärfe-Durchgang und ein volles Zurückkopieren je Bild; die Unschärfe ginge in halber
  Auflösung.
- **Darstellung mit der Bildwiederholrate des Bildschirms:** `drawGx` löscht und zeichnet das
  XFB bei jedem Vsync auf den Bildschirm (POCO: 120/s), das Spiel liefert 30 neue Bilder/s. Mit
  `Surface.setFrameRate(60)` (ab Android 11) liefen Bildschirm und Choreographer mit 60 Hz;
  der VI-Takt folgt dem Choreographer (wie auf dem Retroid mit 60 Hz).
- Weitere Posten: 8× anisotrope Filterung (4× sieht fast gleich aus), Fragment-Shader ohne
  `mediump`, das EFB wird mit 528 Zeilen angelegt, das Spiel nutzt 448 (der Autofokus löst
  alle auf).
- Nächster Schritt (vorgeschlagen): GPU-Zeit je Durchgang (Szene, Kopien, Autofokus,
  Darstellung) mit der Zeitabfrage messen, auf dem Retroid mit einer Reihe von Einstellungen;
  dann in der Reihenfolge Nutzen/Risiko umsetzen und jeden Schritt mit `gx_chunk_render`
  Pixel für Pixel prüfen.

**Grafik-Schritte a–e im eigenen Branch `claude/re4-gpu-efficiency`** (Wunsch des Projektinhabers,
19.10.2026); Schritt (a) liegt außerdem schon im M12-Branch (Commit `715d577`, nur Messung, ohne
Wirkung auf das Bild; bleibt dort, Entscheidung des Projektinhabers, 19.10.2026: kein Umschreiben
des Verlaufs). Der Messlauf auf dem Retroid folgt später.

**GPU-Schritt (a): Messlauf gebaut (19.10.2026, Cloud).** `test-core-app.ps1 -GpuBench` startet in
r100 (Leon steht) und setzt `RE4_GPU_BENCH=1`; die App (`core_mode.cpp`, `benchTick`) schaltet nach
60 s sieben Einstellungen durch (3/2/8/an wie beim Tester, dann Tiefenschärfe aus, MSAA aus,
Anisotropie aus, Auflösung 2, alles aus bei 3, wieder 3/2/8/an), je 5 s Einschwingen und 10 s
Messung, und schreibt je Schritt eine Zeile „GPU bench N/7“: GPU-Zeit je Spielbild (Zeitabfrage
um `Run`), GPU-Zeit je Darstellung (neu: eigene Zeitabfrage um `Present`), Bilder und
Darstellungen je Sekunde, CPU-Zeit des Renderers, GPU-Takt und -Last, wo das Gerät sie lesen
lässt (Adreno: `/sys/class/kgsl/kgsl-3d0`). Danach gelten wieder die Einstellungen von vorher.
Eigene Schritte: `RE4_GPU_BENCH=S/M/A/D,...`, Wartezeit `RE4_GPU_BENCH_DELAY`. Grenzen: ohne
`EXT_disjoint_timer_query` (Mali beim POCO) keine GPU-Zeit; auf Kachel-GPUs kann die Zeit der
Darstellung zu klein ausfallen (der Grafikchip zeichnet den Bildschirm erst beim Bildwechsel,
nach der Abfrage), die je Spielbild nicht (die EFB-Kopien schließen jeden Durchgang innerhalb von
`Run` ab). Getestet: Host-Bau (`gx_gl_check` 0 Fehler, neu „every present counted“, „the presents'
GPU time measured“: 0,3 ms je Darstellung mit llvmpipe, „the chunks' GPU time measured“;
`re4decomp_link_check` mit `core_mode.cpp` gebaut). Nicht getestet: der Messlauf auf dem Gerät.

**GPU-Schritt (a): erster Messlauf auf dem Retroid (19.10.2026, Projektinhaber), Auswertung.**
- Alle sieben Einstellungen hielten 30 Bilder/s und 58–60 Darstellungen/s; die Darstellung kostete
  0,3–0,6 ms (Retroid mit 60 Hz).
- **Zu früh begonnen:** Leon war erst um 16:44:37 frei („run: player free“, Beginn von Schritt 4),
  der Autofokus lief erst ab 16:44:27. Die Schritte 1–3 lagen in Ankunft und Funkspruch (135–290
  Zeichenbefehle je Bild statt etwa 490); vergleichbar sind nur die Schritte 4–7.
- **Zeitabfrage nicht plausibel** in den Schritten 4, 5 und 7 (2× MSAA mit laufendem Autofokus):
  493, 113 und 452 ms je Bild bei 30 Bildern/s; mit 1 Probe oder ohne Autofokus 20–24 ms. Die
  Ursache zeigt das Protokoll nicht (offen).
- **Last mal Takt** (unabhängig von der Zeitabfrage), Schritte 4–7 an derselben Stelle: 3×, 2× MSAA,
  anisotrop aus, Tiefenschärfe an: 87 % bei 670 MHz (≈ 583 MHz Arbeit); 3×, 2×, 8×, an: 88 % bei
  670 MHz (≈ 590); 2×, 2×, 8×, an: 68 % bei 400 MHz (≈ 272); 3×, alles aus: 72 % bei 400 MHz
  (≈ 288). Also verdoppeln MSAA und Tiefenschärfe zusammen bei 3× etwa die Arbeit (der Retroid
  braucht dann den höchsten Takt und ist zu 88 % ausgelastet); 8× anisotrop kostet nichts
  Messbares; Auflösung 2 statt 3 gut die Hälfte.
- **Folgerung für b–e:** Die Darstellung ist auf dem Retroid klein, c (60 Hz) hilft nur auf
  120-Hz-Handys. Der große Posten ist MSAA zusammen mit dem Autofokus (d, e). Wie er sich auf beide
  verteilt, zeigt der zweite Lauf. Der Posten „anisotrop 4× statt 8×“ entfällt.
- **Messlauf verbessert (Cloud):** Der erste Schritt kommt 10 s, nachdem der Kern „Leon frei“
  meldet (`re4_trace_free_at`, schon exportiert, kein Kern-Neubau; ohne die Funktion wie bisher nach
  60 s). Neue Schritte: 3/1/1/0, 3/2/1/0, 3/1/1/1, 3/2/1/1, 3/2/8/1, 2/2/8/1, 3/1/1/0 (MSAA und
  Tiefenschärfe einzeln). Jede Zeile hat zusätzlich Last mal Takt, Zeichenbefehle je Bild (gleiche
  Stelle?), eine Markierung für Zeitwerte über dem Bildabstand und, wie weit die GPU hinterherhinkt:
  ein Fence nach jedem Spielbild, nach dem nächsten werden die noch nicht erreichten gezählt
  (`GxStats::behind*`, `gx_gl.cpp FenceAfterChunk`). Das zeigt ohne die Zeitabfrage, ob die GPU
  mitkommt. `test-core-app.ps1 -GpuBench` beobachtet jetzt 330 s. Getestet: `gx_gl_check` 0 Fehler
  (neu: jedes Spielbild geprüft, nach `glFinish` 0 hinterher), `re4decomp_link_check` gebaut,
  `core_mode.cpp` ohne neue Warnungen. Nicht getestet: der zweite Lauf auf dem Gerät.

**GPU-Schritt (a): zweiter Messlauf auf dem Retroid (19.10.2026, Projektinhaber), Ergebnis.** Alle
Schritte an derselben Stelle (Leon frei, 487–498 Zeichenbefehle je Bild), 30 Bilder/s,
60 Darstellungen/s, Darstellung 0,26–0,37 ms. **Die GPU hinkte nie hinterher** (Fences: 0 Bilder
im Mittel und höchstens 0 in allen Schritten): die Zeitabfrage-Werte von 199–438 ms mit 2× MSAA
und Autofokus sind ein Fehler der Zeitabfrage, keine echte Verzögerung. Mit MSAA allein zeigte
sie sogar weniger als ohne (17,7 statt 18,5 ms): mit MSAA taugt die Zeitabfrage des Retroid nicht.
Last mal Takt (Arbeit), 3-fach:

| Schritt | MSAA | anisotrop | Tiefenschärfe | Takt | Last | Arbeit | gegenüber nichts |
|---|---|---|---|---|---|---|---|
| 1 | aus | aus | aus | 400 MHz | 65 % | 260 | |
| 7 (Wiederholung) | aus | aus | aus | 400 MHz | 68 % | 272 | (Mittel 266) |
| 3 | aus | aus | an | 400 MHz | 79 % | 316 | +19 % |
| 2 | 2× | aus | aus | 587 MHz | 79 % | 462 | +74 % |
| 4 | 2× | aus | an | 670 MHz | 85 % | 568 | +114 % |
| 5 | 2× | 8× | an | 670 MHz | 85 % | 572 | +115 % |
| 6 (2-fach) | 2× | 8× | an | 400 MHz | 72 % | 286 | +8 % |

Folgerungen: **MSAA ist der große Posten** (+74 %), mit der Tiefenschärfe zusammen mehr als die
Summe (+114 % statt +93 %: der Autofokus löst bei MSAA Farbe und Tiefe des ganzen EFB auf und
zeichnet in den Mehrfach-Puffer zurück). Die Tiefenschärfe allein kostet +19 %, 8× anisotrop
nichts Messbares, die Wiederholung (Schritt 7) liegt 5 % neben Schritt 1 (Streuung der Messung).
Reihenfolge der Schritte danach: **b** (Wegwerfen erlauben, klein), **d** (MSAA auf Handy-Art,
der große Posten; braucht `EXT_multisampled_render_to_texture2`, damit die Tiefe als Textur
erhalten bleibt: welche Erweiterungen der Retroid hat, schreibt der nächste Lauf ins Protokoll),
**e** (Tiefenschärfe in halber Auflösung), **c** (60 Hz, hilft nur auf 120-Hz-Handys).

**GPU-Schritt (b): Wegwerfen erlauben (19.10.2026, Cloud, Branch `claude/re4-gpu-efficiency`).**
Vor jedem Zeichnen, das ein Ziel ganz übermalt, darf die GPU den alten Inhalt vergessen
(`glInvalidateFramebuffer`, `gx_gl.cpp ForgetColor`): das Ziel jeder EFB-Kopie und der Kopie ins
XFB (der Bildausschnitt ist die ganze Textur), der Fokus-Texel und das Unschärfe-Bild des Autofokus
(nur der Bildbereich wird gezeichnet und nur er zurückkopiert). Kachel-GPUs laden den alten Inhalt
sonst vor dem Zeichnen in ihre Kacheln. Nicht angefasst: das EFB selbst (das Spiel zeichnet nach
einer Kopie weiter, das Löschen nach einer Kopie betrifft nur den kopierten Bereich) und der
MSAA-Puffer (das ist Schritt d). Schalter: `GxOptions::invalidate` (an), `RE4_GX_INVALIDATE=0`
schaltet es aus. **Prüfmodus „magenta“** (`GxOptions::poison`, im PC-Werkzeug
`RE4_GX_INVALIDATE_POISON=1`): statt zu vergessen wird das Ziel magenta gefüllt; ein Bild, das den
alten Inhalt noch braucht, ändert sich dann. `gx_gl_check` läuft jetzt zweimal (normal und
magenta), die PC-Prüfung (`whole_r100_check.sh`, Abschnitt 5) zeichnet alle GX-Bilder ein zweites
Mal magenta und vergleicht sie Pixel für Pixel („Wegwerfen (GPU-Schritt b) …“). Der Renderer
schreibt einmal je Lauf den Grafikchip und die für Schritt d nötigen Erweiterungen ins Protokoll
(„GPU: …“, im Gerätebericht „Grafikchip“). Messreihe des GPU-Branch: 3/1/1/0 und 3/2/8/1, jeweils
ohne und mit Wegwerfen, alles zweimal (8 Schritte; `S/M/A/D/I`). Getestet: `gx_gl_check` 0 Fehler
in beiden Durchläufen; Gegenprobe: ein absichtlich falsches Vergessen des EFB nach einer Kopie gibt
im Magenta-Durchlauf 4 Fehler; `gx_chunk_render` und `re4decomp_link_check` gebaut; der neue
Abschnitt der PC-Prüfung mit einem Ersatz-Werkzeug (gleiche Bilder: „2 von 2 Bildern gleich“,
abweichende: FEHLER). Nicht getestet: die PC-Prüfung mit echten Spielbildern, der Messlauf auf dem
Gerät.

**GPU-Schritt (b): Ergebnis (19.10.2026, Projektinhaber).** PC-Prüfung (Stand `eb0d192e`): ALLES OK,
„Wegwerfen (GPU-Schritt b), das Vergessene magenta: 19 von 19 Bildern gleich“. Messlauf auf dem
Retroid (je zweimal, Arbeit = Last mal Takt): 3-fach ohne Extras ohne Wegwerfen 260 / 246 (der zweite
bei 305 MHz), mit 275 / 280; mit den Optionen des Testers ohne 593 / 588, mit 576 / 584. Bei gleichem
Takt (400 MHz) ohne Extras mit Wegwerfen etwa 7 % mehr (Arbeit 260 → 275/280, Zeitabfrage 18,5 →
19,6/20,0 ms), mit den Optionen des Testers 2 % weniger (innerhalb der Streuung). Der Adreno 650
zeichnet die Kopien offenbar ohnehin direkt in den Speicher. **Wegwerfen ist darum aus**
(`GxOptions::invalidate = false`, `RE4_GX_INVALIDATE=1` schaltet es ein); Code und Magenta-Prüfung
bleiben (für andere GPUs, und Schritt d braucht dieselbe Art Prüfung). **Der Retroid hat die
Erweiterungen für Schritt d:** „Adreno (TM) 650, OpenGL ES 3.2 V@0746.0;
EXT_multisampled_render_to_texture yes, ..._texture2 yes, EXT_shader_framebuffer_fetch yes,
QCOM_tiled_rendering yes“.

**GPU-Schritt (d): MSAA auf den Kacheln (19.10.2026, Cloud, Branch `claude/re4-gpu-efficiency`).**
Mit `EXT_multisampled_render_to_texture` und `..._texture2` nehmen die eigenen Texturen des EFB
(Farbe und Tiefe) die Proben selbst (`glFramebufferTexture2DMultisampleEXT`, `gx_gl.cpp TiledMsaa`):
die GPU hält die Proben im Kachelspeicher und schreibt am Ende eines Durchgangs (EFB-Kopie,
Autofokus, Bildende) nur Farbe und Tiefe aufgelöst hinaus; der nächste Durchgang liest sie in alle
Proben zurück, `Resolve` hat nichts mehr zu tun. Bisher schrieb jede Kopie die eigenen
Mehrfach-Puffer hinaus, löste sie per Blit auf und las sie wieder ein. Unterschied im Bild: was vor
einer Kopie gezeichnet wurde, behält danach seine aufgelöste Farbe und Tiefe statt der Proben (Kanten,
über die später noch gezeichnet wird). Ohne die Erweiterungen oder mit `GxOptions::tiledMsaa = false`
(`RE4_GX_MSAA_TILES=0`) der alte Weg. Die App schreibt ins Protokoll „... 2 samples (of 4) on the
tiles“ bzw. „in own buffers“, die Messzeilen ebenso. Messreihe des GPU-Branch: 3/1/1/0, dann
3/2/8/1 und 3/2/1/0 je in eigenen Puffern und auf den Kacheln, zweimal, am Ende wieder 3/1/1/0
(`S/M/A/D/I/T`). **Prüfen ohne Gerät:** Mesa `zink` auf `lavapipe` (`LIBGL_ALWAYS_SOFTWARE=1
GALLIUM_DRIVER=zink`, Paket `mesa-vulkan-drivers`, neu in `whole_setup_linux.sh`) hat beide
Erweiterungen. `gx_gl_check` prüft MSAA jetzt auf den Kacheln (wo die GPU es kann) und in eigenen
Puffern; die PC-Prüfung (Abschnitt 5) zeichnet alle GX-Bilder mit 2 Proben auf zink in beiden Arten
und vergleicht sie Pixel für Pixel (Unterschiede nur an Kanten erwartet; `gx_chunk_render`:
`RE4_GX_SAMPLES`). Getestet: `gx_gl_check` 0 Fehler mit llvmpipe (dort ohne die Erweiterungen: alter
Weg) und mit zink (Kacheln: „MSAA on the tiles where asked for and the GPU can“, Kante geglättet,
Kopie und Palette, Autofokus mit MSAA); `gx_chunk_render`, `re4decomp_link_check` gebaut. Nicht
getestet: die PC-Prüfung mit echten Spielbildern, das Gerät.

**Leons Haare fehlen / Flackern (Projektinhaber, 19.10.2026, Retroid, 64 bit).** Beim GPU-Messlauf
„stellenweise“, beim Spielen von Hand **dauernd, bei jeder MSAA-Einstellung**. Die Bildschirmfotos
des Messlaufs (Leon steht) zeigen die Haare. Ohne MSAA ist der Renderer des GPU-Branch gleichwertig
zu M12 (Schritt b aus, Schritt d nur mit MSAA); der Shader-Zwischenspeicher erkennt geänderte
Quelltexte (Hash beider Shader im Dateinamen und im Kopf). Ursache **offen**. Gefunden und behoben
dabei, ohne dass es als Ursache belegt ist:
- **Fused multiply-add nur im arm64-Kern:** Teile ohne die Spiel-Flags wurden ohne
  `-ffp-contract=off` gebaut; clang fasste dort auf aarch64 `a*b+c` zu einem Befehl mit einer
  Rundung zusammen (gezählt mit `llvm-objdump`: `C_MTX44Concat` 48, `C_MTXConcat` 24,
  `C_MTXInverse` 20, `C_QUATMultiply` 12, `C_VECNormalize` 2, `C_VECDotProduct` 2, Hall-Effekte des
  Tons …). armhf (VFPv3) und x86 haben keine solchen Befehle, darum konnte der 32/64-Vergleich am PC
  es nicht sehen. Jetzt `add_compile_options(-ffp-contract=off)` für den ganzen Kern
  (`port/whole/CMakeLists.txt`); `whole_build.sh` zählt die Befehle im aarch64-Build (muss 0 sein;
  ausgenommen `Sk1Loop` und `PsSeries`, die `fmaf` absichtlich wie das `ps_madd` des Originals
  rufen). Versuch dazu (qemu-aarch64, 2 Mio. fast parallele Vektorpaare, die SDK-Ausdrücke):
  `acosf(Skalarprodukt)` wird NaN in 20,8 % mit und 18,3 % ohne Zusammenfassen, x86_64 und armhf
  gleich 18,3 %. Die Haar-Simulation (`pendulum.cpp`) überspringt NaN-Winkel
  (`ang > spdRate && …` ist dann falsch) – als Ursache der fehlenden Haare also nicht belegt.
- Getestet (`whole_build.sh`): aarch64 (qemu) alle Kern-Prüfungen bestanden, gezählt 0; i686,
  armhf (qemu) und x86_64 alle Prüfungen bestanden (0 Bauschritte fehlgeschlagen, Selbsttest,
  Module, Laufzeit- und Kerntest, Formatvergleich, Filmvergleich i686/x86_64 250 von 250 Bildern
  gleich, M12-Selbsttest 19/19, libc++ 162/162). Nicht getestet: der neue Kern auf dem Gerät.
- **Ursache gefunden (19.10.2026, R3-Aufnahme):** GPU-Schritt d im Branch
  `claude/re4-gpu-efficiency` (MSAA auf den Kacheln: der Adreno 650 verliert nach jeder EFB-Kopie
  die Tiefe); dort ist Schritt d jetzt aus, Einzelheiten im Status jenes Branch. Der M12-Branch
  hatte Schritt d nie und ist nicht betroffen.

**GPU-Schritt (d): erster Gerätelauf (19.10.2026).** Arbeit (Last mal Takt), je zweimal: Optionen des
Testers in eigenen Puffern 590 / 596, auf den Kacheln 331 / 329 (−44 %); MSAA allein 453 → 284
(−37 %); ohne Extras 259 / 273. Die Zeitabfrage ist auf den Kacheln wieder plausibel (21 ms). Die
PC-Prüfung (zink, 2 Proben) findet aber in 16 von 19 Bildern Unterschiede zu den eigenen Puffern,
in den Ereignisbildern 8000–14700 Pixel (höchstens 66 von 255), bei Leon in r100 83 Pixel
(höchstens 94). Ob das nur Kanten sind, ist ohne die Bilder offen; Schritt d bleibt im GPU-Branch.

**Leons Haare / Flackern: Ursache gefunden – Schritt d (19.10.2026, R3-Aufnahme des Projektinhabers,
32 bit, 4× MSAA auf den Kacheln).** Der Projektinhaber bestätigt: Haare fehlen und Texturen flackern
in 32 wie in 64 bit, bei 2× und 4× MSAA (im GPU-Branch beides auf den Kacheln). Die 12 Bilder der
Aufnahme:
- **Die Daten des Kerns sind richtig:** in der Cloud nachgezeichnet (llvmpipe ohne MSAA, zink 4× in
  eigenen Puffern und auf den Kacheln) hat Leon seine blonden Strähnen; die Geräte-Bilder zeigen
  eine dunkle Kappe (die Innenschicht), das Haus im Hintergrund ohne Nebel.
- **Flackern gemessen:** von Bild zu Bild ändern sich auf dem Gerät sonst 137–275 Pixel (über 24
  von 255), in Bild 2 aber 59.886 (Leon samt Waffe fehlt) und in Bild 5 154.672 von 229.376 (fast
  alles graubraun, nur die Anzeige); in der Cloud 132–255 in jedem Bild.
- **Haar-Aufträge gefunden** (jeden der 521 Aufträge einzeln weggelassen): Nr. 464 u. a., 1252
  Punkte, Textur 64×128 CMPR, Licht, 4 TEV-Stufen, Rückseiten weg (`GX_CULL_FRONT`).
- **Beleg:** `EXT_multisampled_render_to_texture` lässt die Proben nach dem Ende eines Durchgangs
  undefiniert; zink lädt sie zurück, der Adreno offenbar nicht verlässlich. Im Versuch (Renderer mit
  nachgebildetem Verlust bei jedem erneuten Binden des EFB nach einer Kopie, zink, 4× auf den Kacheln)
  weichen vom Gerätebild ab: ohne Verlust 22.254 Pixel, **Tiefe verloren (0) 14.957** (mittlerer
  Unterschied 6,4 statt 8,7; dunkle Kappe und Haus ohne Nebel wie auf dem Gerät), Tiefe 1,0 33.713,
  Farbe verloren 168.944. Der Adreno 650 behält also die Farbe und verliert die Tiefe; was das Spiel
  nach einer EFB-Kopie mit Tiefenprüfung zeichnet (Haar, Nebel, ganze Figuren), fällt aus.
- **Behoben:** Schritt d ist aus (`GxOptions::tiledMsaa = false`); `RE4_GX_MSAA_TILES=1` schaltet
  ihn für Versuche ein (die PC-Prüfung tut das für ihren Vergleich, der Messlauf je Schritt). Der
  M12-Branch hatte Schritt d nie. Ein späteres „d2“ müsste die Tiefe nach jeder Kopie selbst
  zurückschreiben (oder `QCOM_tiled_rendering` mit Erhalt der Tiefe) und das mit nachgebildetem
  Verlust prüfen. Getestet: `gx_gl_check` 0 Fehler (llvmpipe und zink), `gx_chunk_render` zeichnet
  4× ohne Schalter in eigenen Puffern, mit `RE4_GX_MSAA_TILES=1` auf den Kacheln. Nicht getestet:
  das Gerät mit MSAA in eigenen Puffern (Haare müssen wieder da sein).

**Gerätetest danach (19.10.2026, Projektinhaber, Stand `c698b263`, Kern neu gebaut mit dem
FMA-Fix, 64 bit, von Hand in r100).** PC-Prüfung vorher ALLES OK (Magenta 19/19). **Haare und Nebel
wieder richtig:** das Foto nach 70 s fällt in die Phase mit 2× MSAA in eigenen Puffern (blonde
Strähnen, Haus im Nebel), das nach 147 s ohne MSAA ebenso. Bilder je Sekunde laut Projektinhaber
und Protokoll: ohne MSAA und mit 2× 30 (der Kern 33,3 ms je Bild); **mit 4× sichtbar ruckelig,
20–25** (Kern 49,1 bzw. 62,3 ms je Bild, davon 10 ms Warten auf den Renderer; 22,0 bzw. 25,2
gezeigte Bilder/s). 4× in eigenen Puffern ist bei Auflösung 3 zu viel für den Retroid; der
schnelle Weg (d) braucht dafür eine Fassung, die die Tiefe nach jeder Kopie selbst zurückschreibt
(„d2“). Die Standard-Messreihe misst jetzt 2× und 4× allein, die Tiefenschärfe, die Optionen des
Testers und 4× bei Auflösung 2, alles in eigenen Puffern. Danach: der GPU-Branch in den M12-Stand
übernommen (alle Schritte dort aus, nur die Messung und die Prüfwerkzeuge).

**Reihenfolge danach (Entscheidung des Projektinhabers, 19.10.2026):** erst e, dann c, dann d2.

**GPU-Schritt (e): Tiefenschärfe in halber Auflösung (19.10.2026, Cloud, Branch
`claude/re4-gpu-efficiency`).** Der Autofokus (`gx_gl.cpp Autofocus`) zeichnet die Unschärfe jetzt in
ein Bild mit halber Breite und Höhe (ein Viertel der Pixel, `halfTex`) und mischt sie in voller
Auflösung über das EFB (`mixProgram`, Mischen per Alpha, nur Farbe; Pixel vor dem Fokus werden gar
nicht gezeichnet). Bisher: Unschärfe in voller Auflösung in ein eigenes Bild und danach das ganze
Bild zurückkopiert.
- **Kein Abfärben an scharfen Kanten:** jeder grobe Bildpunkt nimmt nur Teile hinter dem Fokus
  (auch seine vier Mittelpunkte); hat er keine, bleibt er leer (Alpha 0), und das Mischen lässt ihn
  aus. Neue Prüfung in `gx_gl_check` („the background next to the sharp near edge keeps its
  colour“): neben einer scharfen weißen Kante weicht der weiche Hintergrund um 0 (voll), 8 (halb)
  bzw. 20 von 255 ab (halb, wenn die vorderen Mittelpunkte mitzählen; Grenze 12, Gegenprobe
  gemacht).
- **Nur das Bild aufgelöst:** mit MSAA löst der Autofokus nur noch den Bildausschnitt des Spiels
  auf (448 statt 528 Zeilen), für beide Wege; gelesen wird nur dort.
- **Ohne Fokus (Blick in die Ferne):** beide neuen Durchgänge brechen sofort ab.
- Schalter: `GxOptions::halfDof` (Standard an), `RE4_GX_DOF_HALF=0/1` im Renderer für Versuche;
  der Messlauf hat ein siebtes Feld H (`S/M/A/D/I/T/H`, ohne H: an). Standard-Messreihe jetzt:
  nichts, Tiefenschärfe voll, halb, Optionen des Testers voll, halb, halb noch einmal, nichts
  (alles Auflösung 3, MSAA in eigenen Puffern). Die Zeile „GPU bench“ sagt „blur at half/full
  resolution“, „GX renderer ready“ ebenso.
- **PC-Prüfung:** Abschnitt 5 zeichnet die aufgezeichneten Bilder zusätzlich mit voller Auflösung
  der Unschärfe und vergleicht (Zeile „Tiefenschärfe in halber Auflösung (GPU-Schritt e) gegenüber
  voller“; über 40 von 255 bleiben die Bilder zum Mitschicken).
- **Getestet (Cloud):** `gx_gl_check` 0 Fehler mit llvmpipe und zink (alle Autofokus-Prüfungen mit
  halber und voller Auflösung, mit und ohne MSAA, mit Magenta für Vergessenes);
  `re4decomp_link_check` mit `core_mode.cpp` gebaut. Die 11 Retroid-Bilder der Haar-Aufnahme
  (r100, Leon steht, Auflösung 3) haben alle den Autofokus, aber keine Unschärfe (Fokus über 6 m);
  dort sind alt und neu gleich. Mit fest auf 2 m gestelltem Fokus (nur im Versuch) wird 41 % des
  Bilds weich; alt gegen neu: im Mittel unter 1 von 255, 0,1 % der Pixel mehr als 8, höchstens 25
  (ohne und mit 4× MSAA); im Bild nicht zu unterscheiden.
- **Nicht getestet:** die Ersparnis auf dem Gerät (Messlauf) und die PC-Prüfung mit Disc-Daten.
  Hinweis zur Messung: an der Messstelle schaut Leon in die Ferne, gemessen wird also vor allem der
  Aufwand der Durchgänge selbst, nicht die Unschärfe.

**GPU-Schritt (c): 60 Hz auf schnellen Bildschirmen (19.10.2026, Cloud, Branch
`claude/re4-gpu-efficiency`).** Das Spiel liefert höchstens 60 Bilder/s (59,94 Halbbilder); auf
einem 120-Hz-Handy (POCO X7 Pro des Testers) zeichnete die App das Bild 120-mal je Sekunde neu.
- **Bitte um 60 Hz** (`CoreActivity.preferSixtyHz`): kann der Bildschirm schneller als 60 Hz,
  wählt die App seine 60-Hz-Betriebsart gleicher Größe (`preferredDisplayModeId`, ab Android 6)
  und gibt ab Android 11 der Zeichenfläche 60 Bilder/s an (`Surface.setFrameRate`). Ein
  Bildschirm nur mit 60 Hz (Retroid) bleibt, wie er ist.
- **Falls er doch schneller bleibt** (`FramePacing.java`, neu): die App misst den Takt des
  Bildschirms an seinen Vsyncs (Median aus 32 Abständen; verspätete Rückrufe zählen nicht) und
  lässt dann nur bei jedem n-ten Vsync zeichnen (120 Hz und 144 Hz: jeder zweite, 240 Hz: jeder
  vierte; 90 Hz: jeder, sonst wären es nur 45/s). Gewechselt wird erst, wenn zwei Messungen
  übereinstimmen. Technisch: `GLSurfaceView` auf `RENDERMODE_WHEN_DIRTY`, `requestRender` aus
  dem Choreographer. (Die naheliegende Wartezahl `eglSwapInterval(2)` begrenzt Android auf 1.)
  Der Kern bekommt weiter jeden Vsync (`runtime/vi.cpp` kennt 120 Hz mit zwei Vsyncs je Halbbild).
- Protokoll: „display at … Hz (modes: …)“ beim Start und „display at … Hz: the picture drawn …“
  nach der ersten Messung und bei jedem Wechsel; der Bericht von `test-core-app.ps1` zeigt sie als
  „Bildschirm:“. Im Messlauf zeigt „presents/s“ die Wirkung.
- **Getestet (Cloud):** `FramePacingTests` (420 Prüfungen: 60, 90, 120, 144, 165, 180, 240 Hz,
  verspätete Rückrufe, Wechsel 120 → 60 Hz, einzelne Ausreißer, Pause), in `tools/test_host.sh`;
  alle Java-Dateien der App gegen Android 15 (android-all) übersetzt, keine Warnung aus dem
  eigenen Code. **Nicht getestet:** ein Gerät mit mehr als 60 Hz (der Retroid hat 60 Hz: dort
  muss alles bleiben wie bisher, das zeigt der nächste Gerätelauf), Android-Lint.

**GPU-Schritt (d2): MSAA auf den Kacheln, das EFB selbst zurückgemalt (19.10.2026, Cloud, Branch
`claude/re4-gpu-efficiency`).** Schritt d sparte auf dem Retroid 44 % Arbeit, aber der Adreno 650
verlor nach jeder EFB-Kopie die Tiefe (Haare, Nebel). d2 verlässt sich nicht mehr darauf, was der
Treiber nach dem Ende eines Durchgangs behält:
- **Zwei Sätze EFB-Texturen** (Farbe und Tiefe, beide auf den Kacheln, `spareFbo`): jeder neue
  Durchgang zeichnet in den anderen Satz und malt zuerst Farbe und Tiefe aus dem vorigen zurück
  (`UseEfb`, ein bildschirmfüllendes Dreieckspaar, Tiefe per `gl_FragDepth` ohne Rechnung, in jede
  Probe derselbe Wert). Vorher darf der Grafikchip den alten Inhalt vergessen
  (`glInvalidateFramebuffer`), er muss nichts laden. Weil der vorige Satz dabei nur gelesen
  wird, ist das nach GL-Regeln eindeutig (keine Rückkopplung). Kanten, die vor einer Kopie
  gezeichnet wurden, behalten danach ihre aufgelöste Farbe und Tiefe, nicht ihre Proben.
- **Wann:** jedes Binden eines anderen Framebuffers (`BindFbo`, alle Bindungen des Renderers
  laufen jetzt hindurch) und jeder neue Spielbild-Abschnitt beenden den Durchgang; zurückgemalt
  wird erst vor dem nächsten Zeichnen oder Löschen ins EFB (`Draw`, `ClearAfterCopy`, das alte
  Zurückkopieren der Tiefenschärfe). Kopien und Autofokus lesen bis dahin den vorigen Satz.
- **Prüfung mit einem „vergesslichen“ Grafikchip:** `GxOptions::loseTiles` (`RE4_GX_LOSE_TILES=1`
  in `gx_chunk_render`) färbt jeden neuen Durchgang magenta mit Tiefe 0, bevor zurückgemalt wird.
  Neuer Test `TestPassBreak` in `gx_gl_check`: nach einer Kopie verdeckt der Vordergrund noch,
  was dahinter gezeichnet wird, ein Auftrag mit genau derselben Tiefe besteht `EQUAL`, die Farben
  von vorher bleiben (ohne MSAA, eigene Puffer, Kacheln, Kacheln mit Verlust, beide Auflösungen der
  Tiefenschärfe). Gegenprobe: ohne das Zurückmalen 16 Fehler.
- **Echte Bilder** (11 Retroid-Bilder der Haar-Aufnahme, r100, zink, 4 Proben): Kacheln mit
  Verlust gegen Kacheln ohne: **11 von 11 gleich**. Kacheln gegen eigene Puffer, Auflösung 3: 0,4 %
  der Pixel mehr als 8 von 255 (dünne Äste, die vor einer Kopie gezeichnet wurden; 12-mal
  zurückgemalt je Bild); ohne MSAA gegen eigene Puffer sind es 2 % – d2 behält den größten Teil
  der Kantenglättung.
- Schalter: `GxOptions::tiledMsaa` bleibt **aus**, bis der Gerätetest zeigt, dass Haare und Nebel
  da sind; `test-core-app.ps1 -MsaaTiles` schaltet d2 für einen Lauf ein (`RE4_GX_MSAA_TILES=1`).
  Messreihe (8 Schritte): nichts, Tiefenschärfe voll, halb, Optionen des Testers in eigenen Puffern
  und auf den Kacheln, 4× auf den Kacheln und in eigenen Puffern, nichts. Die Zeile „GPU bench“
  sagt bei d2, wie oft je Bild zurückgemalt wurde; die Zeile „GX:“ zählt es auch.
- **PC-Prüfung:** Abschnitt 5 vergleicht jetzt mit 4 Proben (lavapipe hat keine eigenen 2×-Puffer,
  bisher stand 2 gegen 4) eigene Puffer gegen d2 und d2 gegen d2 mit Verlust (muss gleich sein,
  sonst FEHLER).
- **Getestet (Cloud):** `gx_gl_check` 0 Fehler mit llvmpipe und zink; `re4decomp_link_check`
  gebaut; `tools/test_host.sh`: wie vorher 40 von 42 (die zwei Fehlschläge `enemy_feedback_tests`
  und `inventory_case_contracts` gehören zum Klassik-Modus und waren schon vor e, c und d2 da).
  **Nicht getestet:** das Gerät (Haare/Nebel mit d2, Arbeit im Messlauf), die PC-Prüfung mit
  Disc-Daten.

**GPU-Schritte e, c, d2: PC-Prüfung und Messlauf (19.10.2026, Projektinhaber, Stand `308019fc`).**
- **PC-Prüfung: ALLES OK.** Tiefenschärfe halb gegen voll: 2 Bilder mit Autofokus, 19 von 19 gleich
  (an diesen Stellen ohne Unschärfe). d2 gegen eigene Puffer (zink, 4 Proben): 3 von 19 gleich, sonst
  Kanten (2145–14712 Pixel, höchstens 94 von 255); d2 mit nachgebildetem Verlust gegen d2: 19 von 19
  gleich.
- **Messlauf (Retroid, Last mal Takt, 3-fach, Leon steht, 490 Zeichenbefehle je Bild):** nichts 259 /
  271; Tiefenschärfe voll 312, halb 314 (**e bringt auf dem Retroid nichts**: an der Messstelle ohne
  Unschärfe kostet vor allem die Unterbrechung des Bilds durch den Autofokus, nicht das Weichzeichnen);
  Optionen des Testers in eigenen Puffern 574, auf den Kacheln (d2) 450 (−22 %, 12-mal je Bild
  zurückgemalt; d ohne Zurückmalen war 331); 4× auf den Kacheln 654 (98 % Last, 30 Bilder/s, aber
  nur 34 Darstellungen/s), 4× in eigenen Puffern 663 (99 %, **24 Bilder/s**). Zeitabfrage auf den
  Kacheln plausibel (18,1 / 21,5 ms), in eigenen Puffern wie bekannt nicht.
- **c:** „display at 60.0 Hz (modes: 1080x1920@60.0): left as it is“, „drawn at each vsync“: auf dem
  Retroid wie gewünscht ohne Wirkung.
- **d2 auf dem Gerät: Haare fehlen wieder, Flackern** (Projektinhaber; Bild nach 190 s, Schritt 6,
  4× auf den Kacheln: dunkle Kappe, kein Nebel; nach 310 s ohne MSAA richtig). Das Zurückmalen aus
  den Texturen hilft also nicht. Wahrscheinlichste Ursache: der Adreno 650 schreibt bei
  `EXT_multisampled_render_to_texture2` die aufgelöste Tiefe gar nicht in die Tiefentextur (die
  Farbe schon); dann malt d2 eine falsche Tiefe zurück. Belegt ist das noch nicht.
- **Selbsttest beim Start (neu, `TilesSelfTest`):** Mit MSAA auf den Kacheln zeichnet der Renderer
  einmal beim Anlegen in eine 8×8-Ecke Rot mit Tiefe 0,25, beendet den Durchgang, liest zurück, was
  das Gerät in den Texturen behalten hat, malt zurück und zeichnet Grün dahinter (`LESS`) und Blau
  auf derselben Tiefe (`EQUAL`). Protokoll: „MSAA on the tiles, self-test: the device keeps the
  colour …, the depth … (read N of 255, 64 expected); the EFB drawn back: …“ (im Bericht
  „Kacheln-Selbsttest:“). Scheitert er, nimmt der Renderer **von selbst eigene Puffer** (`DropTiles`,
  `OwnMsaa`). Dazu einmal je Lauf die ganze Erweiterungsliste des Grafikchips („GPU extensions“).
  Prüfung: `GxOptions::loseTiles = 2` (`RE4_GX_LOSE_TILES=2`) bildet ein Gerät nach, das die
  aufgelöste Tiefe nie speichert: der Selbsttest scheitert („the same depth FAILS EQUAL -> own
  buffers instead“), alle Bildprüfungen bestehen. `gx_gl_check` 0 Fehler mit llvmpipe und zink.
  **Nicht getestet:** der Selbsttest auf dem Retroid.

**GPU-Schritt d2 auf dem Retroid, von Hand (19.10.2026, Projektinhaber, Stand `9cb28d4a`,
`-MsaaTiles`, 2× im Menü):** Der Selbsttest **besteht** („the device keeps the colour yes, the depth
yes (read 64 of 255, 64 expected); … hidden, … passes EQUAL -> MSAA on the tiles“), im Spiel
**flackert es trotzdem und Teile der Texturen fehlen**, 12 Rückmalungen je Bild. Der Adreno behält
die Tiefe also im einfachen Fall; was im Spielbild verloren geht, zeigt der Selbsttest nicht
(Ursache nicht gefunden; die R3-Aufnahme, 30 MB, blieb auf dem PC). Die Erweiterungsliste des
Retroid enthält u. a. `GL_ARM_shader_framebuffer_fetch`, `GL_QCOM_shader_framebuffer_fetch_noncoherent`,
`GL_QCOM_tiled_rendering`.

**Entscheidung des Projektinhabers (19.10.2026): MSAA bleibt in eigenen Puffern** („2× läuft,
4× schafft nur 20–25 Bilder/s, besser als nichts“). d/d2 bleiben aus (`GxOptions::tiledMsaa`), der
Code mit Selbsttest bleibt für Versuche (`-MsaaTiles`). Die Messreihe hat keine Schritte auf den
Kacheln mehr: nichts, Tiefenschärfe, Optionen des Testers mit 2× und 4×, 4× bei Auflösung 2,
nichts. Stand der GPU-Schritte: (a) Messung ✅, (b) aus (nichts gespart), (c) an (Retroid
unverändert, 120 Hz nicht getestet), (d)/(d2) aus, (e) an (gleiche Bilder, auf dem Retroid ohne
Ersparnis). Der GPU-Branch ist danach in den M12-Branch übernommen.

### Menü, Touch-Farben, Auto-Auflösung, Gyro-Zielen (Wünsche des Projektinhabers, 19.10.2026, Branch `claude/re4-menu-gyro`)

- **Auto-Auflösung eine Stufe kleiner:** die Stufe, deren 336 Bildzeilen den Bildschirmzeilen am
  nächsten kommen, minus 1, mindestens 1 (`core_mode.cpp`, `CoreMenu.autoScale`): der Retroid
  (1080 Zeilen) bekommt 2 statt 3, ebenso der POCO X7 Pro; 720 Zeilen: 1.
- **Menü:** Zeile unter dem Titel „SELECT (short-press) = this Menu · SELECT (long-press) = the
  Game's Debug Menu“. GRAPHICS: Zeile „Other options“ in den Spalten der Auswahlzeilen, „Depth of
  field“ unter „16:9“, „FPS display bottom left“ ab der dritten Spalte (getauscht). CONTROLS: Zeile
  „Select methods“ mit „Use Touch Controls“ und „Use Gyro-Aiming“ in derselben Flucht (ohne
  Gyrosensor ausgegraut), darunter der neue Text des Projektinhabers. MEMORY CARD: nur der Satz
  „Export and Import Savegames as .gci (for Dolphin or GameCube).“ und zwei gleich große Knöpfe
  „Export Savegames (.gci)“ (wie bisher „alle exportieren“: Spielstände und Systemdatei mit den
  Freischaltungen) / „Import Savegames (.gci)“; der Export einzelner Dateien ist weg; eine Zeile
  erscheint nur, wenn die Karte gerade nicht lesbar ist. ERROR LOG: neuer Text, darunter die
  beiden Kästchen und die beiden Knöpfe in vier gleich breiten Teilen. Texte englisch, wie vom
  Projektinhaber vorgegeben.
- **Touch-Farben:** die offiziellen GameCube-Farben (Bild des Projektinhabers): A #109B61 grün,
  B #F6392C rot, Z #6B5CB1 lila, alle anderen Knöpfe #DDDDDD grau (RetroTouch schreibt auf hellem
  Grund dunkel). `CoreTouchTests` prüft sie (241 Prüfungen).
- **Größere Ein-Zeichen-Beschriftung:** geht nur in RetroTouch (Textgröße dort höchstens 16 dp,
  Radius × 0,34). **RetroTouch 1.0.0-beta.6** (vom Projektinhaber, 19.10.2026) als
  `app/libs/retrotouch.aar`: Beschriftungen aus einem Zeichen mit Radius × 0,68 ohne Obergrenze
  (aus dem Bytecode von `drawButton` gelesen), längere wie bisher; öffentliche Schnittstelle
  gegenüber beta.5 unverändert (`javap -public` verglichen), alle Java-Dateien der App damit
  übersetzt. Nicht auf dem Gerät gesehen.
- **PC-Prüfung (Projektinhaber, Stand `fdeda59b`): ALLES OK** (Kern mit `gyro.cpp`, i686, r100
  10.000 Bilder, Bildvergleiche wie zuvor, Filme, Speicherkarte).
- **Nachträge (19.10.2026):** ERROR LOG: „R3 = Graphics capture“ (kein Zeilenumbruch mehr). Ganz
  unten Überschrift SOURCE und „More on this port at https://github.com/Andiweli/CubeEvil4-Android“,
  unterstrichen in der Linkfarbe, mit dem Controller anwählbar (A) oder antippbar: öffnet den
  Browser (`Intent.ACTION_VIEW`; ohne Browser ein Hinweis).
- **Frage des Projektinhabers: der linke Stick kennt nur 8 Richtungen.** So ist das Original:
  `PadRead` (`src/game/pad.cpp`) macht aus dem Hauptstick jenseits von 30 Einheiten nur
  Richtungs-Bits (vorwärts/rückwärts in einem Sektor von ±60°, links/rechts außerhalb davon), und
  Leon läuft mit Panzersteuerung: vorwärts/rückwärts, Drehen mit fester Geschwindigkeit
  (`pl_R1_Walk`: `Key.on & 1/2/4/8`, `SPEED_WALK_TURN`); Rennen über B. Analog wirkt der Stick nur
  beim Zielen (`PlWepLockCtrl`, `CameraScope::move`) und in Menüs. Eine freie 360°-Steuerung gäbe es
  nur mit einem Eingriff in die Spiellogik. **Entscheidung des Projektinhabers (19.10.2026): bleibt
  wie im Original.**
- **Gerätelauf von Hand (Projektinhaber, 19.10.2026 22:39, Stand `fdeda59b`, Kern neu gebaut,
  64 bit):** Leon hat seine Haare (Auflösung 2, 4× MSAA in eigenen Puffern, 30 Bilder/s angezeigt);
  Touch-Knöpfe in den GameCube-Farben, die Buchstaben groß und mittig (Bildschirmfotos), der Editor
  von RetroTouch dreimal geöffnet und der Aufbau verschoben; das Menü wie gewünscht (noch mit der
  alten R3-Beschriftung); Gyro-Zielen eingeschaltet, der Kern meldet „the aim turned by the
  gyroscope“ (also beim Zielen mit einer Schusswaffe gedreht). Wie es sich anfühlt (Stärke, Richtung),
  sagt der Bericht nicht. Nicht getestet: der SOURCE-Link.

- **Gyro-Zielen:** `GyroAim.java` liest den Gyrosensor wie der erste Port (Achsen je nach
  Bildschirmdrehung, 35 % Glättung, unter 0,006 rad/s nichts, älter als 150 ms nichts), nur bei
  eingeschaltetem Kästchen und App im Vordergrund; `CoreBridge.gyro` → `re4_core_gyro` →
  `runtime/gyro.cpp` (siehe Tabelle der Laufzeit-Dateien): dreht das Ziel nur, solange Leon mit
  einer Schusswaffe zielt (nicht vorher, nicht mit dem Messer). Kern neu zu bauen.
- **Getestet (Cloud):** alle Java-Dateien gegen Android 15 übersetzt; `CoreTouchTests` und
  `FramePacingTests` bestanden; `whole_build.sh i686 armhf x86_64 aarch64`: 0 Baufehler, alle
  Prüfungen bestanden (FMA 0, libc++ 163 von 163), `re4_core_gyro` exportiert. **Nicht getestet:**
  das Menü auf dem Gerät, das Gyro-Zielen im Spiel (die Kernprüfungen laufen ohne Spieler), die
  M12-Prüfungen (`run_all.py`; `gyro.cpp` greift nur über Feldnamen zu).

### Kürzerer schwarzer Start, „Alternate Controls“, 32-bit-Startfehler (19.10.2026, Branch `claude/re4-start-controls`)

- **Schwarzer Bildschirm beim Start (Frage des Projektinhabers):** etwa 5,5 s bis zum ersten
  Logo (Gerätelog): `SS/cmn/title.snd` 0,87 s, `ss/cmn/save_e.dat` 0,6 s, Kartenprüfung 1,3 s,
  `SS/eng/title.dat` 2,2 s. Die drei Dateien kosten nur Bilder: die Lese-Warteschlange des Spiels
  macht pro Bild einen Schritt (`cDvd::readProcMain`: `TaskSleep(1)` nach jedem Schritt, ein
  Stück von höchstens 128 KB braucht drei), das Laufwerk des Ports ist sofort fertig. **Patch
  0085:** bis die erste `title.dat` da ist, folgt der nächste Schritt sofort, solange nichts
  unterwegs ist (DVD-Lesen 0x02000000, ARAM-Übertragung 0x01000000), nach je 64 Schritten ein
  Bild Pause (`runtime/dvd.cpp re4_dvd_boot_fast`, `re4_dvd_read_done`); danach und mit
  `RE4_DVD_BOOT_FAST=0` das Tempo des Originals (Laden im Spiel unverändert). Die Kartenprüfung
  (1,3 s, eigene Schritte der CARD-Bibliothek) bleibt. Log: „DVD: start reads done (…): N steps
  without a frame's wait“.
- **„Alternate Controls“ (Wunsch des Projektinhabers):** dritter Schalter unter CONTROLS, eigene
  Zeile „Button layout“ unter „Use Touch Controls“ (drei Kästchen passen nicht nebeneinander),
  darunter die Belegung. `CoreInput.setAlternate`: L1 = L (Messer, Abzug am Anschlag), L2 = R
  (Zielen, analog), R1 = Y (Inventar „Weapons/Recovery“), R2 = A ab halbem Weg (Schießen, mit
  Messer zustechen), Y = Z (Karte); A, B, X (Ashley: Warten/Folgen), START, Kreuz und Sticks wie
  bisher; die Touch-Steuerung behält ihre GameCube-Knöpfe. „Keys/Treasures“ direkt auf Y
  (Wunsch zuerst) gibt es im Original nicht (nur Y = Inventar, Z = Karte; der Reiter liegt in der
  Reihe oben) und hätte einen Eingriff ins Untermenü gebraucht; X als Karte hätte Ashleys Befehl
  verdrängt. **Entscheidung des Projektinhabers (19.10.2026): Keys/Treasures weglassen, X bleibt
  Ashley, Y öffnet die Karte.** Einstellung `alternateControls` in `core_input`.
- **Tester: 32-bit-Fassung startet nicht** („core not started: MEM1 0x80000000..0x83000000 is
  taken in this process“, Gerät unbekannt). Die App reserviert MEM1 schon in `JNI_OnLoad` (aus
  `Re4Application.onCreate`), auf 32 bit aber erst nach dem Laden der Kern-Bibliotheken, und der
  Fehlerbericht hatte auf 32 bit keine `address-map.txt`. Jetzt auch auf 32 bit: der Platzhalter
  für MEM1 vor dem Laden der Bibliotheken (wie auf 64 bit, `RE4_MEM1_PLACEHOLDER`), die
  Speicherkarte des Prozesses im Bericht (Kopfzeile mit Prozessbreite und Kernel, alle Bereiche,
  der höchste belegte, „libre4_main.so at …“), und die Meldung nennt den Bereich in MEM1 („by …“)
  oder, wenn dort nichts liegt, dass das System die Adresse verweigert (ein 32-bit-Kernel, dessen
  App-Adressraum unter 0x80000000 endet: dann geht die 32-bit-Fassung auf dem Gerät nicht). Die
  Ursache beim Tester ist **noch offen**: dafür den Bericht („Send report“ im Fehlerdialog).
- **Getestet (Cloud):** `whole_build.sh i686 armhf x86_64 aarch64` mit Patch 0085: 0 Baufehler,
  alle Prüfungen bestanden. Neu in `runtime-test dvdq`: drei Lesungen über die Warteschlange, wie
  der Bildablauf des Spiels sie treibt: beim Start 1 Bild, `ss/eng/title.dat` 1 Bild, danach
  14 Bilder (Original); mit `RE4_DVD_BOOT_FAST=0` alle drei 14 Bilder (die Prüfung schlägt dann
  fehl, wie sie soll). `CoreInputTests` 116 Prüfungen bestanden (neue Belegung, Tausch A/B–X/Y,
  Touch unberührt); alle Java-Dateien gegen Android 15 übersetzt; `core_mode.cpp` für ARM32 und
  ARM64 ohne Fehler und Warnungen geprüft (Ersatz-Header für Android); die Speicherkarte in einem
  32- und einem 64-bit-Linux-Prozess erzeugt (belegtes MEM1 erkannt, Platzhalter scheitert dann).
  **Nicht getestet:** Start mit Disc-Daten (PC-Prüfung, Gerät), die neue Belegung auf dem Gerät,
  der Bericht auf dem Gerät des Testers.

### Gerätelauf 19.10.2026 23:42, MSAA-Flackern der Äste, Release 0.38.2 Alpha 03 (Branch `claude/re4-start-controls`)

- **PC-Prüfung (Projektinhaber, Stand `c69dce33`): ALLES OK** (mit Patch 0085).
- **Gerätelauf (Retroid, 64 bit von Hand und 32 bit automatisch, Stand `c69dce33`):** vom
  Laden des Kerns bis `title.dat` gelesen **2,1 s** (vorher 5,8 s; 32 bit 4,6 s), „start reads
  done: 111 steps without a frame's wait, 1 with“. 32 bit: MEM1 mit dem neuen Platzhalter
  reserviert, Spiel läuft. „Alternate Controls“ im Spiel eingeschaltet, die Tasten kommen wie
  gewollt an (L2 → R, R2 → A, L1 → L, R1 → Y, Y → Z, X → X).
- **Flackern dünner Dinge mit MSAA (Projektinhaber: Äste vor einem Haus, 2× und 4×, 32 und
  64 bit):** R3-Aufnahme 32 bit (`gx-geraet-00..02`, je 12 Bilder, Leon steht, 2× MSAA). Auf dem
  Gerät weicht nur Bild 00-01 ab: 400 Bildpunkte im Bereich der Äste, dort heller (die dünnen
  Äste weich ins Hintergrundbild verwischt), alle anderen 33 Bilder ruhig. Nachgespielt mit
  `gx_chunk_render` (llvmpipe, 1 und 4 Proben) ist dasselbe Bild ruhig; der Autofokus misst dort
  in allen 12 Bildern 10,4 m, jenseits von 6 m zeichnet er keine Unschärfe. Auf dem Gerät hat er
  in Bild 00-01 also einmal näher gemessen: die Bildmitte (Waffe, Zweige, Haus) liegt dicht
  beieinander, mit MSAA kommt die Tiefe jedes Punktes aus einer Probe (die Auflösung der Tiefe
  wählt eine, die Farbe mischt alle), und was der Adreno dort liefert, sieht man nur auf ihm.
  **Behebung (`gx_gl.cpp` Autofocus):** der Fokus springt nicht mehr. Drei Werte stehen in zwei
  abwechselnden 3×1-Texturen: der benutzte Fokus und die letzten beiden Messungen; der Fokus
  folgt dem Median der letzten drei Messungen um ein Viertel des Weges je Bild (eine einzelne
  falsche Messung ändert nichts, ein neuer Abstand ist nach etwa einer Viertelsekunde da). Ohne
  Autofokus im Bild davor (erstes Mal, nach Zwischensequenz, Menü, Tiefenschärfe aus) beginnt er
  neu mit der Messung. `gx_gl_check`: neue Prüfung (ein Bild „fern“ ändert nichts, nach drei
  Bildern scharf, nach einer Pause sofort neu), alle Fälle bestanden; mit dem alten springenden
  Fokus schlägt sie in allen 10 Varianten fehl. Die R3-Aufnahmen mit dem neuen Code: alle
  36 Bilder bitgleich zu vorher (1 und 4 Proben). **Gerätelauf (Projektinhaber, 20.10.2026
  00:15, Stand `120fd4a8`): das Flackern ist weiter da, auch mit ausgeschalteter
  Tiefenschärfe.** Der Autofokus war also nicht (allein) die Ursache; der ruhigere Fokus bleibt
  drin. Neue R3-Aufnahmen (64 bit, 2× MSAA, `gx-geraet-geteilt.zip`) liegen beim Projektinhaber;
  **Entscheidung des Projektinhabers: das Flackern ein andermal beheben**, im Release als
  bekannter Fehler genannt. Ausgewertet (20.10.2026): drei Reihen zu 12 Bildern (64 bit, 2× MSAA,
  Tiefenschärfe aus, Leon steht). Kein Bild fällt heraus (vorher 400 Bildpunkte in Bild 00-01,
  jetzt höchstens 4–16 Bildpunkte, die hin und zurück springen); die Gerätebilder gleichen dem
  Nachspielen mit 4 Proben bis auf etwa 60 Bildpunkte je Bild in allen Bildern gleich (2 statt 4
  Proben). MSAA verringert im Nachspielen das Springen (1 Probe 71/83/355, 4 Proben 39/71/187
  springende Bildpunkte). Das Flackern kommt nur ab und zu; in diesen 1,2 s war es nicht. Für
  das nächste Mal nötig: eine Aufnahme, die das Ereignis sicher enthält (Vorschlag: die App hält
  die GX-Daten der letzten Bilder im Speicher, R3 sichert sie).
- **Kirche, Tester-Berichte 19.10.2026 17:42 und 17:49:** beide noch mit **0.38.0-core** (Alpha 01)
  gemacht, ohne die Berichtigung von Patch 0077 (erst in 0.38.2). Im übersetzten aarch64-Kern
  liest `r117_espEnd` jetzt Byte 0x111 (`ldrb w8, [x8, #273]`). **Bestätigt (Projektinhaber,
  20.10.2026): mit 0.38.2 (Alpha 3) klappt das Lichterrätsel der Kirche** (64 bit; ohne
  Bericht).
- **Version im Menü (Wunsch des Projektinhabers, 20.10.2026):** nach „CubeEvil4 Options“ etwas
  kleiner „(v0.38.2-Alpha3)“, aus `versionName` ohne den Anhang „-core“. `versionName` trägt dafür
  den Namen des Releases (`'0.38.2-Alpha3'`; Bericht und Startbildschirm: „0.38.2-Alpha3-core“);
  bei jedem Release mit `versionCode` anpassen. Alle Java-Dateien übersetzt; auf dem Gerät nicht
  gesehen. **Entscheidung des Projektinhabers (20.10.2026): die Versionsnummer immer aus
  `build.gradle` nehmen**, nie fest in den Code schreiben. Geprüft: Menü (`CoreMenu.version`),
  Startbildschirm (`MainActivity.versionName`) und Fehlerbericht (`CoreLog`) lesen sie aus der
  App; sonst steht keine Versionsnummer im Code der App.
- **Neue Nummerierung ab der nächsten Alpha (Entscheidung des Projektinhabers, 20.10.2026):** die
  bisherige Form „0.38.x-AlphaN“ hatte zwei Zähler, die immer gemeinsam stiegen („0.38“ stammt
  aus der internen Zählung der früheren App, zuletzt „0.37.17-fix6“). Ab jetzt: jede neue Alpha
  erhöht die mittlere Zahl (nächste **0.39.0**, danach 0.40.0, …), nur ein dringender
  Fehler-Nachtrag (Hotfix) die letzte (0.39.1, 0.39.2 …). Die Nummern steigen immer (Werkzeuge
  wie Obtainium vergleichen sie). `versionName` mit dem Zusatz „-alpha“ (`'0.39.0-alpha'`, Menü
  „(v0.39.0-alpha)“), `versionCode` zählt weiter (60, 61 …). Tag, Release-Titel und APK-Name
  gleich: `v0.39.0-alpha`, „CubeEvil4 v0.39.0 (Alpha)“, `CubeEvil4-v0.39.0-alpha.apk`; der
  Releasetext nennt die neue Nummerierung einmal. Später: Beta etwa `0.9.0-beta`, `1.0.0`
  wenn das ganze Spiel durchspielbar ist.
- **FPS-Anzeige mit der Rate des Bildschirms (Wunsch des Projektinhabers, 20.10.2026):** der
  Tester mit 120-Hz-Bildschirm (POCO X7 Pro) sah im Zähler des Handys 60, in unserem 30: unser
  Zähler zählt die Spielbilder (ein Controller-Lesen je Bild; das Spiel macht wie auf dem
  GameCube 30 je Sekunde, `SetSystemVcnt(2)`), das Handy die Wiederholrate des Bildschirms (seit
  GPU-Schritt c auf 60 Hz gebeten, jedes Spielbild zweimal gezeigt). Die Anzeige lautet jetzt
  „30 FPS (Game) · 60 Hz“: die Rate gemessen (`FramePacing.hz`), bis dahin die von Android
  gemeldete. Alle Java-Dateien übersetzt; auf dem Gerät nicht gesehen.
- **„Alternate Controls“: Zielen mit dem rechten Stick (Wunsch des Projektinhabers, 20.10.2026):**
  solange L2 (Waffe; ab 31 von 255, wie das Spiel R zählt) oder L1 (Messer) gehalten wird, gibt
  der rechte Stick den Steuerstick des Spiels, der linke ruht, der C-Stick steht (das Spiel
  nutzt ihn beim Zielen nicht: `CameraQuasiFPS::calcDepressionRatio` nimmt dann die Neigung der
  Waffe). Sonst links laufen, rechts umsehen wie bisher. Gezählt über alle Geräte (der
  eingebaute Controller meldet Abzug und Sticks getrennt), die Touch-Steuerung ausgenommen;
  die Standardbelegung unverändert. `CoreInputTests` 125 Prüfungen bestanden; Menütext
  ergänzt; auf dem Gerät nicht gesehen.
- **MSAA-Flackern, weiter (20.10.2026):** das eine erfasste Ereignis (32 bit, Reihe 00, Bild 01,
  2× MSAA) ist **keine** Tiefenschärfe: mit erzwungenem Fokus 1,5–4,5 m im Nachspielen wird das
  ganze Bild weich, auf dem Gerät aber nur die Äste heller (Mittel an den 532 Punkten 65,6
  statt 55–57, das übrige Bild gleich), also eher Äste, die in diesem Bild teilweise fehlen. Es
  passt weder zu „ohne MSAA“ noch zu „4 Proben“ im Nachspielen. Für die Ursache nötig: dieselben
  Daten **auf dem Gerät** noch einmal zeichnen. **Neu: Wiedergabe auf dem Gerät**
  (`core_mode.cpp replayGx`, `RE4_GX_REPLAY`; `test-core-app.ps1 -GxReplay ORDNER-ODER-ZIP
  [-GxReplaySeries 00]`): beim Start der App, vor dem ersten Bild des Spiels, zeichnet ein
  eigener Renderer die R3-Folge so wie damals (2× MSAA, Anisotropie 16) dreimal, dazu ohne MSAA,
  mit 4× und ohne Anisotropie; jedes Bild so, wie die GPU es zeichnet
  (`replay-VARIANTE-RUNDE-NN-KK.bmp`, `gx-replay.zip`). Fragen: zeichnet der Adreno dieselben
  Daten jedes Mal gleich, und kommt Bild 01 auf ihm wieder heller? Geprüft (Cloud): beide ABIs
  ohne Fehler und Warnungen übersetzt; `replayGx` in einem Linux-Testrahmen mit llvmpipe auf der
  32-bit-Reihe 00: 72 Bilder, die Runden bitgleich, nahe an den Gerätebildern (Mittel 2,7;
  Bild 01 weicht wie bekannt ab). Nicht getestet: auf dem Gerät, das Skript (kein PowerShell in
  der Cloud).
- **MSAA-Flackern: „centroid“ (20.10.2026).** Projektinhaber: das Flackern einzelner Äste kommt
  nur mit 2× oder 4× MSAA, ohne MSAA ist es weg, gleich was sonst an ist. Verdacht: die Shader
  geben Texturkoordinaten und Farben ohne `centroid` weiter; mit MSAA rechnet die GPU ein
  Randpixel, das ein dünner Ast nur teilweise bedeckt, mit der Koordinate der Pixelmitte, die
  außerhalb des Dreiecks liegen kann, und der Alpha-Test (Ausschnitt des Astes) verwirft oder
  behält den Rand von Bild zu Bild anders. **Jetzt:** mit MSAA alle Übergaben des Vertex-Shaders
  als `centroid` (`GxOptions::centroid`, Standard an; `ShaderKey::centroid`, die Programme mit
  und ohne getrennt, auch im Programm-Zwischenspeicher, der nach dem Quelltext geht). Ohne MSAA
  unverändert. Zum Vergleich `GxOptions::fullResolve` (jedes Auflösen über das ganze EFB) und
  `gx_chunk_render` `RE4_GX_CENTROID=0`, `RE4_GX_FULL_RESOLVE=1`. Nachgespielt (llvmpipe, 4
  Proben, alle sechs R3-Folgen): mit `centroid` springen weniger Bildpunkte hin und zurück
  (z. B. 187 → 137, 958 → 935, 39 → 34), geändert werden nur Randpunkte dünner Dinge (0,75 %
  der Punkte: Äste, Fensterrahmen, Halme), ohne sichtbaren Nachteil. llvmpipe kann nur 4
  Proben, 2× (Retroid) zeigt nur das Gerät. Die Wiedergabe auf dem Gerät vergleicht jetzt
  `s2old` (wie aufgenommen, ohne centroid, dreimal), `s2cen` (zweimal), `s2full`, `s1`, `s4cen`
  (96 Bilder). `gx_gl_check` 0 Fehler; beide ABIs ohne Fehler und Warnungen. **Nicht
  getestet:** auf dem Gerät.
- **MSAA-Flackern: Ursache gefunden (20.10.2026).** Wiedergabe auf dem Retroid (32-bit-Reihe 00,
  12 Bilder, `gx-replay.zip` des Projektinhabers) mit `centroid`: das Flackern ist weiter da.
  Ausgewertet je Bild der Abstand jeder MSAA-Variante zum Bild ohne MSAA (`s1`): in der
  Wiedergabe springt nur **Bild 06**, und zwar in **allen** MSAA-Varianten (`s2old` 320k statt
  272k, 567 statt ~270 stark abweichende Punkte; `s2cen` 355k statt 306k; `s4cen` 441k statt
  363k); live springt nur Bild 01 (307k, 500 Punkte), dazu Bild 04 schwach. Nachgerechnet mit
  `gx_chunk_render` (Ausrichtung 32 wie auf dem Adreno): ein frischer Renderer wechselt den
  Uniform-Ring (16 MB, 4 Viertel zu 4 MB, etwa 1,3 MB je Bild) in Bild 03 (beim 68.
  Zeichenauftrag), **06 (beim 135.)** und 09 (beim 242.). Live läuft der Ring schon länger, seine
  Wechsel liegen um gut 3 Bilder versetzt (01, 04, 07, 10). Dort, wo der Wechsel mitten in die
  Zeichenaufträge der Bäume fällt, zeichnet der Adreno die Äste in diesem einen Bild anders
  (heller); ohne MSAA ändert der Wechsel nichts. Beim ersten Umlauf eines frischen Renderers
  setzt der Wechsel nur einen Fence (`glFenceSync`), gewartet wird nicht: schon der Fence mitten
  im Bild genügt (vermutlich teilt der Treiber dort den Durchgang, und die MSAA-Proben überstehen
  das nicht ganz). Das erklärt alles Bekannte: nur mit MSAA, nur dünne Dinge, „ab und zu“ (etwa
  jedes dritte Bild, nur wenn der Wechsel bei den Bäumen liegt), unabhängig von Tiefenschärfe und
  `centroid`, und in der Wiedergabe in anderen Bildern als live.
  **Behoben:** der Ring wechselt sein Viertel jetzt vor einem Bild, nie während eines Bildes
  (`gx_gl.cpp StreamChunkBegin`): vor jedem Bild muss das Viertel noch Platz für das größte
  bisherige Bild und die Hälfte dazu haben (mindestens 2 MB), sonst geht es gleich zum nächsten.
  Nur ein Bild, das mehr als ein Viertel braucht, wechselt noch mittendrin (einmal im Protokoll:
  „GX uniform stream: a chunk needs more than a quarter's rest …“; Zähler
  `GxStats::streamSwitches`/`streamSplits`, in der Zeile „GX work per chunk“ als „stream
  switches (… inside a chunk)“; Bericht `test-core-app.ps1`: Zeile „Uniform-Ring“). Im Dorf
  wechselt der Ring damit alle 2 Bilder; ein Umlauf sind 8 Bilder, gewartet wird erst, wenn die
  GPU 6 Bilder zurückliegt (vorher etwa 9). **Geprüft (Cloud):** `gx_gl_check` neuer Test
  `TestUniformStream` (Bilder zu 700 Zeichenaufträgen mit je eigener Matrix, etwa 2,3 MB:
  Wechsel nur zwischen den Bildern; ein Bild zu 1400 wechselt mittendrin; alle Quadrate an
  ihrem Platz; ohne MSAA, 4×, MSAA auf den Kacheln): 0 Fehler; ohne die Korrektur 8 Fehler.
  Die 32-bit-Reihe 00 mit `gx_chunk_render` (2×, Auflösung 2): Wechsel in Bild 02, 04, 06, 08,
  10 jeweils vor dem Bild, keiner im Bild, alle 12 Bilder bitgleich zu vorher (llvmpipe hat den
  Fehler nicht). Syntaxprüfung 64 und 32 bit ohne neue Warnungen.
  **Gerätetest (Retroid, 64 bit, Stand `5e015486`, 20.10.2026): behoben.** Projektinhaber: das
  Flackern ist weder mit 2× noch mit 4× zu sehen (r100, etwa 2 Minuten von Hand). Wiedergabe
  derselben 32-bit-Reihe 00 (Anisotropie diesmal 1): Bild 06 springt in keiner MSAA-Variante mehr
  (`s2old` 268k / 255 stark abweichende Punkte statt 320k / 567, `s2cen` 300k statt 355k,
  `s4cen` 353k statt 441k), alle 12 Bilder gleichmäßig (`s2old` 268k–269k), die Runden
  bitgleich. Bericht: „Uniform-Ring: Wechsel nur zwischen Bildern“, 150 Wechsel, 0 im Bild,
  0 Wartezeiten, GPU 16,2 ms je Bild. Nicht erneut getestet: 32 bit (dort war das Flackern
  dasselbe; der Renderer ist für beide ABIs derselbe Code).
- **Rückmeldung eines Testers zum Gyro-Zielen (20.10.2026; erledigt, siehe „Gyro-Zielen: Drehen“):**
  1. „Links und rechts zielt man durch Kippen wie bei einem Rennspiel (Lenkrad), nicht durch
     Drehen (Gieren) wie in den meisten Shootern; sehr unintuitiv.“ Im Code: `GyroAim.java`
     nimmt für links/rechts die Drehung um die Achse durch den Bildschirm (`event.values[2]`),
     wie der erste Port; Shooter nehmen die Drehung um die senkrechte Achse des Bildschirms (im
     Querformat je nach Drehung die x-Achse des Geräts, `values[0]`, Vorzeichen nach
     `getRotation`). Vorhaben: Gieren als Standard; das Lenkrad vielleicht als Wahl im Menü
     (vorher fragen).
  2. „Der rechte Stick wirkt beim Zielen nicht gleichzeitig mit dem Gyro; vielleicht geht das
     beim GameCube-Spiel gar nicht.“ In der Standardbelegung zielt das Spiel mit dem Hauptstick
     (linker Stick); ob der C-Stick beim Zielen im Spiel selbst etwas tut, prüfen. In „Alternate
     Controls“ zielt ab Alpha 4 der rechte Stick (`CoreInput`: beim Zielen wird er zum
     Hauptstick). Prüfen: Stick und Gyro zusammen (`gyro.cpp` dreht `pl->ang.y` und `m3r` dazu;
     `PlWepLockRand` zieht `m3r` zur Mitte zurück, wenn der Stick ruht): geht der Gyro-Anteil
     verloren, sobald der Stick sich bewegt? Wenn der C-Stick beim Zielen im Spiel nichts tut:
     ihn in der Standardbelegung beim Zielen wie in „Alternate Controls“ mitnehmen (vorher
     fragen).
- **Gyro-Zielen: Drehen statt Lenkrad (20.10.2026).** Entscheidungen des Projektinhabers: links
  und rechts nur noch durch Drehen des Geräts (um die senkrechte Achse des Bildschirms, wie eine
  Kamera), das Kippen wie ein Lenkrad fällt weg, keine Wahl im Menü; die Standardbelegung bleibt
  wie am GameCube (der rechte Stick zielt nur in „Alternate Controls“). Neu `GyroAxes.java` (ohne
  Android-Klassen): Gieren und Nicken aus der Messung je Drehung des Bildschirms
  (`Display.getRotation`); das Nicken wie vorher; `GyroAim` nimmt beide von dort. Rechter Stick
  und Gyro zusammen, im Spielcode geprüft: `PlWepLockCtrl` verschiebt mit dem Hauptstick `m3r`
  und `m_Fwork0`/`ang.y` schrittweise, `gyro.cpp` hat vorher (Rahmen-Haken in `PADRead`) auf
  `ang.y`, `m3r` und `Wep->pitch` addiert, `PlWepLockRand` zentriert bei bewegtem Stick auf das
  jetzige `m3r`: beide zählen zusammen. Im Original verschiebt der C-Stick beim Zielen das Ziel
  nicht (`cPlWep::lockMove`: er beendet nur das automatische Anvisieren). **Geprüft (Cloud):**
  `GyroAxesTests` 40 Prüfungen (je Drehung: Drehen um die Senkrechte ist Gieren, nach links
  positiv; Nicken wie vorher; Kippen um die Achse durch den Bildschirm tut nichts), in
  `tools/test_host.sh` aufgenommen; alle Java-Dateien der App gegen Android übersetzt.
  **Nicht getestet:** auf dem Gerät.
- **32 bit startet nicht, Tester-Bericht 20.10.2026 (`cubeevil4-report-20261020-100743.zip`,
  App 0.38.2 Alpha 3):** Mobicel P11 Plus, Android 14, nur 32 bit (`armeabi-v7a`), PowerVR
  GE8322, 2 GB. Meldung „core not started: MEM1 0x80000000..0x83000000 is taken in this process
  (by 80000000-90000000 rw-p [anon:dalvik-main space])“; Adresskarte: „MEM1 placeholder: NOT
  reserved“. Dort liegt **der Java-Speicher von ART** (die Hauptfläche, 256 MB, genau ab
  0x80000000), darunter die Puffer des Müllsammlers „Concurrent mark-compact“ (der neue Sammler
  von ART, ab Android 14; der Retroid mit Android 13 hat den alten, dort ist 0x80000000 frei).
  Der Java-Speicher entsteht schon im Zygote, bevor die App einen Befehl ausführt; jede App
  dieses Geräts erbt ihn an derselben Stelle. Aus der App heraus nicht zu ändern. Wege: (A) den
  Kern in einem eigenen Prozess ohne Java laufen lassen (ein Programm aus dem Bibliotheksordner
  der App, Bilder, Ton und Eingaben über geteilten Speicher), (B) MEM1 auf 32 bit an eine andere
  Adresse legen (jede Stelle in Spiel und Plattform, die 0x80000000 annimmt, müsste umrechnen),
  (C) vorerst nicht unterstützt. **Entscheidung des Projektinhabers (20.10.2026): Weg A**, der
  Kern als eigener Prozess ohne Java, **nur als Rückfall, wenn MEM1 im App-Prozess belegt
  ist** (Entscheidung ebenfalls 20.10.2026); Übergabe an eine neue Sitzung:
  `port/docs/32bit-kernprozess.md`. Im
  Releasetext der Alpha 4 als bekannter Fehler genannt. Nebenbei im Protokoll: der Startfehler-
  Dialog (`CoreActivity.showStartProblem`) bleibt beim Beenden offen („WindowLeaked“), harmlos.
- **Touch: L und R wie echte Knöpfe, Einrasten wählbar (20.10.2026).** Bisher rasteten L und R
  der Touch-Steuerung bei jedem Tippen ein. **Entscheidung des Projektinhabers:** wählbar im Menü,
  Standard wie ein echter Knopf (gedrückt, solange der Finger darauf liegt). `CoreTouch.setLatch`
  (aus: L/R wie die übrigen Knöpfe; beim Umschalten wird Gehaltenes und Eingerastetes
  losgelassen und die Anzeige von RetroTouch zurückgesetzt), `TouchControls.setLatch`,
  Einstellung `touchLatchLR` (`CoreActivity.TOUCH_LATCH`), Schalter „Latch L and R (Touch)“ in der
  Zeile „Button layout“ neben „Alternate Controls“, Menütext angepasst. **Geprüft (Cloud):**
  `CoreTouchTests` 250 Prüfungen (beide Arten, Umschalten mit gehaltenem und eingerastetem
  Knopf); alle Java-Dateien der App gegen Android übersetzt. **Nicht getestet:** auf dem Gerät.
- **Absturz bei Luis' erstem Schuss in der Hütte: behoben mit Patch 0086 (20.10.2026).** Zwei
  Tester-Berichte (App 0.38.2, 64 bit): Samsung Galaxy M23 (Android 14,
  `cubeevil4-report-20261020-083406.zip`, ohne Protokoll; laut Video direkt nach einer
  Zwischensequenz) und POCO X7 Pro (Android 16, `cubeevil4-report-20261020-072230.zip`, mit
  Protokoll: Ereignis `evd/r11cs00.evd`, also r11c, die Hütte, Kapitel 2-3; „Abort message:
  'stack corruption detected (-fstack-protector)'“). Beide: Signal 6, auf dem Stapel
  `libre4_pl14.so+0xa8fc`. In der arm64-`libre4_pl14.so` der Alpha 3 (PC des Projektinhabers,
  19.10.2026 23:44) ist 0xa8fc der Anfang von `cRoutine::shot`, direkt hinter dem
  `bl __stack_chk_fail` am Ende von **`isTarget`** (die Rücksprungadresse eines Aufrufs ohne
  Rückkehr zeigt auf die nächste Funktion); `cAnalysis::move+0x58` liegt hinter `bl isTarget`.
  Ursache, ein Fehler des Originals: `GetWepTargetList2` erhöht sein `max` um eins für jeden
  Trefferbereich mit `YAT_FLAG_THROUGH`, den die Linie kreuzt (bis 0x14), und schreibt so viele
  Einträge; `isTarget` hat `TARGET_WK list[2]` (max 2). Der dritte Eintrag landet auf arm64 (16
  Byte je Eintrag, Liste ab sp+0x28, Wachwert bei sp+0x48) und auf ARM32 (8 Byte, Liste ab sp+52,
  Wachwert bei sp+68, -O2 mit Stack-Schutz nachgebaut) genau auf dem Wachwert: beide ABIs
  betroffen. Auf dem GameCube traf er nur Nachbarvariablen, die nicht mehr gelesen werden. Dasselbe
  Muster bei der Mine (`emmine.cpp`, ein `TARGET_WK`, max 1) und beim Speer (`obj1b.cpp`, eines,
  max 1); `PlWepHitCheck2` hat `list[20]`, die übrigen Listen (`GetWepTargetList`,
  `GetWepTargetListBomb`) halten ihr `max` ein. **Patch 0086:** Platz für 0x14 Einträge an allen
  drei Stellen; gelesen werden wie vorher Eintrag 0 und 1. Nebenbefunde: Luis passt auf 64 bit in
  seinen Gegner-Platz (0x918 von 0xF60). **Geprüft (Cloud):** `whole_build.sh i686 aarch64`
  mit 0086: 0 Fehler, alle Prüfungen bestanden (Module laden, Laufzeit-, Kern- und Filmtest,
  Formatvergleich, M12-Prüfwerkzeuge); der Patch steht in den vorbereiteten Quellen. **Auf dem
  Gerät bestätigt (Projektinhaber, 20.10.2026):** mit dem neu gebauten Kern läuft die Szene in der
  Hütte weiter, Luis schießt ohne Absturz.
- **Ton „Dolby Surround Pro Logic II“ (20.10.2026):** Das Spiel schaltet im Menü AUDIO SETUP
  zwischen MONAURAL, STEREO und DPL2 (`snd_main.cpp`: `AXSetMode` 0/2, `MIXSetSoundMode` 0/1/3);
  unser DSP-Nachbau mischt DPL2 auf Stereo herunter (`runtime/dsp.cpp`), es klingt daher
  praktisch wie Stereo. **Entscheidung des Projektinhabers: Weg 1** – im DPL2-Modus die
  hinteren Kanäle getrennt an Android geben (Androids Raumklang für Kopfhörer, z. B. AirPods,
  wo das Gerät ihn hat; sonst wie heute), im Branch `claude/re4-dpl2-raumklang`. Weg 2 (eigener
  Kopfhörer-Raumklang in der App) später vielleicht als Ergänzung.
- **DPL2 Weg 1 umgesetzt (20.10.2026, Branch `claude/re4-dpl2-raumklang`).** Aufbau im
  DPL2-Modus (SDK `AXCL.c`, `AXAux.c`, `mix.c`): vorne auf den Haupt-Bussen L/R, hinten (Ls,
  Rs) auf den Bussen von Aux B L/R (MIX `vBL`/`vBR`), Hall-Eingang Aux A mit L, R, Ls (Aux A S)
  und Rs (**Aux B S**, MIX `vBS`). Befehl 0x10 schickt die hinteren Kanäle durch den
  DPL2-Puffer von Aux B (zurück einen Block später), 0x13 den Hall (Rückgabe L, R, Ls, Rs).
  - **`runtime/dsp.cpp`:** die hinteren Kanäle getrennt (`gRearMain`: der Rückweg von 0x10,
    `gRearAux`: der Hall); die Stereo-Ausgabe an den AI wie bisher (hinten Haupt voll, Hall
    halb auf die gleiche Seite: bitgleich zu vorher); dazu je AI-Block die vier Kanäle (vorne
    links, vorne rechts, hinten links, hinten rechts; Hall voll), abrufbar mit
    `re4_dsp_surround(blockAdresse)`. **Fehler behoben:** der Hall bekam als Rs bisher Aux A S
    ein zweites Mal statt Aux B S.
  - **`runtime/ai.cpp`:** zweiter Ring mit vier Kanälen (außerhalb DPL2: vorne = Stereo,
    hinten still), Leser `re4_audio_read4` mit derselben Stelle und Takt-Anpassung wie
    `re4_audio_read` (die App liest einen der beiden), `re4_audio_surround_now` (ein
    DPL2-Block in den letzten 0,5 s). Kern-Schnittstelle: `re4_core_audio4`,
    `re4_core_audio_surround` (beide optional für die App).
  - **App:** `CoreAudio` (Kontext der Aktivität) prüft etwa jede Sekunde: das Spiel in DPL2 und
    Androids `Spatializer` (ab Android 12L) vorhanden, verfügbar, eingeschaltet und bereit für
    5.1 bei der jetzigen Ausgabe (`canBeSpatialized`) → AudioTrack 32 kHz 5.1
    (`SurroundPcm.fivePointOne`: vorne, Mitte und Tiefton still, hinten), sonst Stereo wie
    bisher; ein Wechsel baut die Ausgabe neu. Protokoll „sound: game … ; spatializer …“ bei
    jeder Änderung, „sound: AudioTrack 32000 Hz 5.1 …“; Bericht `test-core-app.ps1`: Zeilen
    „Raumklang“ und „Tonausgabe“. JNI `CoreBridge.audio4`, `CoreBridge.audioSurround` (ein
    Kern ohne die Funktionen: Stereo, hinten still).
  - **Geprüft (Cloud):** Kern-Test `ax` neu: DPL2 mit einer Stimme vorne links (1500) und
    hinten rechts (3000): vier Kanäle 1500/0/0/3000, Stereo 1500/3000; Hall in DPL2: der
    Effekt sieht Ls 3000 und Rs 1500 (mit dem alten Fehler 3000: die Gegenprobe scheitert),
    Rückgabe 100/200/300/400 in den vier Kanälen, Stereo 250/400; zurück in Stereo: hinten
    still, nach 0,5 s kein Raumklang mehr. `SurroundPcmTests` 3 Prüfungen; alle Java-Dateien
    gegen Android übersetzt; `core_mode.cpp` 32/64 bit ohne neue Warnungen;
    `whole_build.sh i686 aarch64`: 0 Fehler, alle Prüfungen bestanden. **Gerätelauf Retroid mit
    AirPods Pro (20.10.2026, Stand `e3e5e7b3`):** Kern und App gebaut, Spiel läuft (Spielstand
    r105 geladen), Ausgabe Stereo. Das Protokoll ist abgeschnitten (299 Zeilen, der Anfang
    fehlt), die erste Zeile „sound: game … spatializer …“ ist nicht darin, nach dem Fortsetzen
    wird sie nur bei einer Änderung wiederholt: ob der Retroid einen Spatializer hat, ist offen.
    Jetzt steht der Zustand in jeder Zeile „sound: AudioTrack …“. Auffällig: „45 ran dry“ (Ton
    ohne Nachschub) in etwa 70 s mit Bluetooth-Kopfhörern (AudioTrack-Puffer 8988 statt 2568
    Bilder), sonst etwa 5 – zu prüfen, ob man Aussetzer hört. Projektinhaber: mit den AirPods
    kam der Ton stark verzögert (Schüsse spät gehört). Ursache auf unserer Seite: Android
    verlangt bei Bluetooth einen großen AudioTrack-Puffer (8988 Bilder, 280 ms), und die
    blockierenden Schreibvorgänge hielten ihn ganz voll; dazu kommt die Verzögerung von
    Bluetooth selbst (unter Android meist 150–250 ms, nicht änderbar). **Neu `LatencyTuner`**
    (wie Oboes LatencyTuner, ab API 24): gefüllt wird nur ein Teil des Puffers, anfangs zwei
    Mixer-Perioden (mindestens zwei Schreibvorgänge, 512 Bilder = 16 ms), nach neuen Aussetzern
    der Spur (`getUnderrunCount`) je eine Periode mehr bis zur Kapazität; die nächste Spur auf
    derselben Ausgabe beginnt mit dem Gelernten. Protokoll: „used … frames (… ms), grows on
    underruns“ und „sound: buffer … frames (… ms) after … underruns“. `LatencyTunerTests` 14
    Prüfungen; alle Java-Dateien gegen Android übersetzt.
    **Gerätelauf Retroid, Lautsprecher (20.10.2026, Stand `de48f7fa`):** „spatializer: not on
    this device“ – der Retroid hat keinen Raumklang, die App bleibt bei Stereo (wie gewollt);
    die DPL2-Erkennung wechselt richtig („game in Dolby Pro Logic II“, später „not in“). Puffer:
    Start 512 Bilder (16 ms), in den ersten 2 s sechs Aussetzer, dann ruhig bei 1280 (40 ms,
    vorher immer 80 ms); „6 ran dry“ wie früher. **Danach:** Start mit mindestens vier
    Schreibvorgängen (1024 Bilder, 32 ms), das Gelernte bleibt je Ausgabe (Kapazität der Spur)
    in den Einstellungen (`core_audio`, „frames<Kapazität>“) auch über einen Neustart der App;
    `LatencyTunerTests` 14 Prüfungen. **Gerätelauf mit Kopfhörern (20.10.2026, Stand
    `a9571028`):** die Spur beginnt mit dem Gelernten (1280 Bilder, 40 ms, ohne neue Aussetzer;
    das Spiel in DPL2, Ausgabe Stereo, kein Spatializer). Projektinhaber: mit anderen Kopfhörern
    ist die Verzögerung nicht so groß; die starke Verzögerung liegt an den AirPods und an
    Bluetooth (nicht an der App). Im Bericht „17 ran dry“ in 120 s (sonst etwa 6) – beobachten.
    **Nicht getestet (Raumklang):**
    auf einem Gerät mit Raumklang (Spatializer), wie es klingt; ob der Retroid einen hat
    (steht dann im Bericht).
- **Release 0.38.3 Alpha 4** (Projektinhaber, 20.10.2026: „Alpha4 fertig für heute“): Text
  `port/android-decomp/docs/releases/v0.38.3-alpha4.md`, APK `CubeEvil4-v0.38.3-alpha4.apk`,
  `versionName '0.38.3-Alpha4'`, `versionCode` 59 (`app/build.gradle`; das Menü zeigt
  „(v0.38.3-Alpha4)“). Inhalt seit Alpha 3: MSAA-Flackern dünner Dinge behoben (auf dem Retroid
  bestätigt), Gyro-Zielen durch Drehen (auf dem Gerät nicht getestet), Zielen mit dem rechten
  Stick in „Alternate Controls“, FPS-Anzeige mit der Bildwiederholrate, Version im Menü; intern
  MSAA mit `centroid` und die Wiedergabe von R3-Aufnahmen auf dem Gerät (`-GxReplay`).
- **Menükopf (Wunsch des Projektinhabers):** „CubeEvil4 Options“ und die Zeile darunter
  linksbündig, „Back to the game“ in derselben Zeile rechts.
- **Release 0.38.2 Alpha 03** (Entscheidung des Projektinhabers, 19.10.2026: auf GitHub liegt
  „v0.38.1 Alpha Release 02“, Tag `alpha02`): Text `port/android-decomp/docs/releases/
  v0.38.2-alpha3.md`, APK `CubeEvil4-v0.38.2-alpha3.apk` (Version 0.38.2, versionCode 58,
  `coreRelease`, signiert mit demselben Schlüssel). Vor dem Release-Bau geprüft: neue App-Aufrufe
  über Android 6.0 hinaus; nur `Float.isFinite` (ab Android 7.0) in `GyroAim` war ungeschützt,
  jetzt `isNaN`/`isInfinite`.
- **Getestet (Cloud):** alle Java-Dateien gegen Android 15 übersetzt; Java-Tests der App bestanden
  (`CoreInputTests` 116, `CoreTouchTests` 241, `FramePacingTests` 420 …; die beiden alten
  Fehlschläge der Variante `classic`, `enemy_feedback_tests` und `inventory_case_tests`, wie
  zuvor); `gx_gl_check` 0 Fehler.

### 32 bit: der Kern als eigener Prozess (20.10.2026, Branch `claude/re4-32bit-kernprozess`)

Übergabe, Plan und Entscheidungen: `port/docs/32bit-kernprozess.md`. **Entscheidungen des
Projektinhabers (20.10.2026):** Weg A (der Kern in einem eigenen Prozess ohne Java), nur als
Rückfall, wenn MEM1 im App-Prozess belegt ist, dazu ein Schalter für Tests; die Bibliotheken
werden bei der Installation entpackt (`app/build.gradle`: `useLegacyPackaging = true`; Android
führt Programme nur aus dem entpackten Bibliotheksordner aus).

- **Ablauf:** `core_mode.cpp loadCore` (32 bit): scheitert der Platzhalter für MEM1 und liegt dort
  etwas aus dem App-Prozess (die Meldung „by …“), lädt die App den Kern nicht selbst, sondern ersetzt
  die Tabelle `CoreApi api` durch die Funktionen `re4ipc_*` (`runtime/re4_core_client.h`,
  `core_ipc_client.cpp`). Das erste `re4ipc_reserve` (aus `CoreBridge.start`) startet
  `libre4_coreproc.so` (`vfork` + `execve`, Socket als Datei 3, `LD_LIBRARY_PATH` = der
  Bibliotheksordner, Name `<Paket>:core`). Liegt in MEM1 nichts (das System verweigert die
  Adresse), bleibt es bei der alten Meldung: dann ginge es auch im eigenen Prozess nicht.
  `RE4_CORE_PROCESS=1` (Einstellungen von `CoreBridge.start`, `test-core-app.ps1 -CoreProcess`)
  erzwingt den eigenen Prozess auf 32 bit (der schon geladene Kern der App bleibt unbenutzt); auf
  64 bit wird der Schalter nur gemeldet.
- **Das Kernprogramm** (`host/coreproc.cpp`): Signale frei, MEM1-Platzhalter sofort, fremde
  Dateien zu, `PR_SET_PDEATHSIG`, lädt `libre4_main.so`, `re4_mem1_init`, legt den geteilten
  Speicher an und meldet sich („Hallo“ mit Adresskarte). Threads: Befehle (Hauptthread; Socket
  zu = die App ist weg = Ende), Eingabe (Controller-Warteschlange in Reihenfolge, Vsync, Gyro,
  Zustandsbilder), GX (Chunks in 3 geteilte Plätze; der Chunk des Kerns wird erst frei, wenn die
  App ihre Kopie freigibt: so viele Bilder unterwegs wie im App-Prozess), Ton (genau so viel nach,
  wie die App las, etwa 16 ms Puffer). Ein Rahmen-Haken je Spielbild schreibt Rumble,
  Bildzähler und `freeAt`. Absturz- und Hänger-Handler wie in der App, in dieselbe `crash.txt`
  (das Handle kommt über den Socket).
- **Geteilter Speicher** (`runtime/re4_core_ipc.h`): Steuerblock (Klingeln als Futex,
  Warteschlangen, Spiegel, drei Zustandsbilder, Ton-Ring mit Kanalzahl im Kopf: für den
  DPL2-Branch nicht fest 2), je GX-Platz ein Speicherstück (`memfd_create`, sonst
  `ASharedMemory`), das bei Bedarf wächst (per Socket geschickt). Nach `gxDetach` verwirft die App
  ältere Plätze und Chunks bis zum nächsten RESET.
- **Kern:** einzige Änderung `re4_core_gx_notify` (`runtime/gx_decode.cpp`): ein Rückruf, wenn ein
  Chunk in die Warteschlange kommt (der Server holt ihn sofort); ohne Rückruf wie bisher. Kein Patch.
  `host/loader.c`: die Reservierung unter 0x83000000 in `host/low_memory.h` (das Linux-Kernprogramm
  nutzt sie auch).
- **App:** `CoreBridge.coreProcess()` (Grund und Zustand, Fehlerbericht „Core: …“ in
  `device*.txt`, Adresskarte des Kindes in `address-map.txt`), `CoreBridge.coreProblem()`: endet
  der Kernprozess, zeigt `CoreActivity` „The game core stopped: …“ mit „Send report“ / „Quit“.
  `test-core-app.ps1 -CoreProcess`: Schalter, Bericht „Kern im eigenen Prozess“, ein Ende des
  Kernprozesses beendet die Beobachtung. `whole_build_android.sh` baut und kopiert
  `libre4_coreproc.so` für `armeabi-v7a` (prüft: ein Programm mit `INTERP`).
- **Geprüft (Cloud, 20.10.2026):** `re4_core_remote_test` (in `whole_build.sh`, i686 und armhf
  unter qemu, Kind über `--wrapper qemu-arm`): die „App“ belegt MEM1 selbst, startet
  `re4_coreproc` mit dem Testeinstieg (`tests/core_remote_probe.cpp`) und prüft über den Client:
  Start/Zustand/Meldung, Controller vor dem Start und ein kurzer Tipper während der Pause (genau
  ein Bild), Rumble an/aus, Bildzähler, etwa 700 GX-Chunks (Prüfsumme jedes Chunks gleich der des
  Kindes, Reihenfolge, RESET zuerst und nach `gxDetach`, Speicherstücke wachsen ab 4 KB),
  etwa 700 Zustandsbilder (Prüfsummen), Ton 6386 Bilder genau (1000/-1000), Vsync vollständig, Gyro
  weitergegeben, Pause (das Spiel steht), Speicherkarte (leer, 251 frei, `.gci`-Import, Export
  gleich bis auf den Startblock, „exists“, nur während das Spiel steht), Tiefenschärfe aus/an,
  Debug-Menü einmal, Protokollende, Ende mit „re4_game_main returned 7“, die App endet → das Kind
  endet mit Code 0. `--crash`: das Kind stürzt ab → „the game core process ended: signal 11“,
  `crash.txt` mit Stelle. `--missing`: „the core process does not start (…)“. `core_mode.cpp`,
  `core_ipc*.cpp`, `coreproc.cpp` für ARM32 und ARM64 mit Ersatz-Headern ohne Fehler und
  Warnungen übersetzt (kein NDK in der Cloud: Download gesperrt); alle Java-Dateien der App gegen
  Android 15 (android-all) übersetzt. `whole_build.sh i686 armhf x86_64 aarch64`: 0 Baufehler, alle
  Prüfungen bestanden (auf 64 bit gibt es den Kernprozess nicht, dort prüft es, dass Lader und
  GX-Warteschlange unverändert arbeiten).
- **Nicht getestet:** auf einem Gerät (Retroid mit `-Abi armeabi-v7a -CoreProcess`, das
  Mobicel-Gerät des Testers), der Android-Build (NDK, Gradle, `useLegacyPackaging`), das Testskript
  (kein PowerShell in der Cloud), die Leistung (eine Kopie je GX-Bild), `memfd_create` unter
  SELinux auf Android (sonst `ASharedMemory`).
- **Erster Android-Bau (Projektinhaber, 20.10.2026, Stand `a31c1d85`):** brach in
  `whole_build_android.sh` ab: „libre4_main.so exportiert re4_core_gx_notify nicht“. Die Prüfung
  war falsch: `re4_core_gx_notify` und `re4_add_frame_hook` liegen in `libre4_platform.so` (wie
  `re4_core_gx_acquire`; `dlsym` über `libre4_main.so` findet sie dort). Korrigiert (`d26896d`).
- **Erster Gerätelauf (Retroid, 32 bit, `-CoreProcess -Manual`, Stand `d26896d`, 20.10.2026
  18:29):** der Kernprozess startet (PID 8843, „MEM1 placeholder reserved“, APK 17,6 MB statt
  38,6 MB: Bibliotheken gepackt), das Spiel läuft: Titel, Vorfilm (mit START übersprungen),
  r120/r100, Leon frei, Vibration, Gyro; GX-Speicherstücke wuchsen auf 16 MB je Platz.
  **Projektinhaber: Ton mit starken Rucklern und Aussetzern** (auch im automatischen Lauf, dessen
  `senden.zip` der zweite Lauf löschte). Im Protokoll des Kerns: 32 „skips“ und 54 „ran dry“ in
  70 s (im App-Prozess fast keine). Ursache: die AudioTrack der App nimmt ihre Bilder stoßweise
  (mehrere Lesungen zu 256 hintereinander, wenn der Mischer eine Periode nimmt); der Ring im
  geteilten Speicher hielt nur 512 Bilder vor, die übrigen Lesungen bekamen Stille (Aussetzer), und
  was die App nicht nahm, blieb im Ring des Kerns, bis er übersprang. **Behoben:** fehlt einer
  Lesung etwas, schreibt die App die gewünschte Stelle in den geteilten Speicher
  (`Control::audioWant`) und wartet bis zu 20 ms (Klingel `audioDone`), das Kind liest genau das
  beim Kern nach; vorgehalten werden nur noch 8 ms. Nachgestellt in `re4_core_remote_test`: der
  Testeinstieg spielt Ton im Takt des AI (ein Block je 5 ms), die „App“ liest wie die AudioTrack
  (erst 2560 Bilder, dann je 24 ms drei Lesungen). Mit dem alten Weg: 34 560 von 66 882 Bildern in
  Aussetzern, 5 „skips“ (die Prüfung schlägt fehl); jetzt in drei Läufen 0 Aussetzer, 0 „ran dry“,
  0 „skips“. Prüfung: jede Lücke, die die App hört, ist Stille, die schon der Kern lieferte.
  Neu im Protokoll: „core process: GX copies: 600 chunks, … KB each, … ms each“ und „core process
  sound: 2000 reads, … waited …, … short“ (Bericht: „Kernprozess Bilder kopiert“, „Kernprozess
  Ton zuletzt“). `test-core-app.ps1` hebt die `senden.zip` des Laufs davor als
  `senden-vorher.zip` auf.
  **Projektinhaber: im Intro mit der Polizei ab und zu 15 FPS.** Im Protokoll für diese 10 s: GPU
  31,9 ms je Bild (4× MSAA, Stufe 2, Anisotropie 16), 49 statt 60 gezeigte Bilder je Sekunde,
  47 Shader neu übersetzt (ein Bild 839 ms), der Kern wartete im Mittel nur 2,6 ms je Bild auf den
  Renderer, 18,7 ms Leerlauf. Das spricht für die GPU und neue Shader, nicht für den Kernprozess;
  **offen:** Gegenprobe derselben Stelle ohne `-CoreProcess`.
- **Zweiter Gerätelauf (Retroid, 32 bit, Stand `cc5cdb2`, 20.10.2026, drei Läufe):** Projektinhaber:
  kein Ruckeln mehr im Ton, FPS durchgehend 30, keine Auffälligkeiten.

  | | 1: `-CoreProcess` (automatisch, 240 s) | 2: ohne (automatisch, 240 s) | 3: `-CoreProcess -Manual` (140 s) |
  |---|---|---|---|
  | Kern im eigenen Prozess | ja, PID 11726 | nein | ja, PID 15241 |
  | Ton des Kerns | 46000 Blöcke, 5 ran dry, **0 skips** | 46000 Blöcke, 11 ran dry, 0 skips | 28000 Blöcke, 6 ran dry, **0 skips** |
  | Ton über den Kernprozess | 66 von 2000 Lesungen warteten, höchstens 1 ms, 0 zu kurz | – | 197 von 2000 warteten, höchstens 1 ms, 0 zu kurz |
  | GX-Kopie im Kernprozess | 3901 KB je Bild, 1,14 ms (höchstens 3,2) | – | 3678 KB je Bild, 0,98 ms (höchstens 2,6) |
  | Kern je Bild | 33,3 ms, 0 ms Warten auf den Renderer, 19,3 ms Leerlauf | 33,3 ms, 0 ms, 12,8 ms | 33,3 ms, 0 ms, 18,3 ms |
  | Bildschirm | 60,0 gezeigt/s, GPU 17,6 ms | 59,8/s, GPU 14,3 ms | 60,0/s, GPU 17,5 ms |
  | Filme | Vorfilm ganz (76,0 s, 2278 Bilder, 0 ausgelassen) | Vorfilm (START) | Demo-Film, Vorfilm (beide START) |
  | sonst | Leon frei in r100, 377 Shader aus dem Zwischenspeicher | Leon frei | Vibration 3×, Leon frei |

  Die 15 FPS im Intro beim ersten Lauf kamen also nicht vom Kernprozess (mit dem Zwischenspeicher
  der Shader und denselben Einstellungen jetzt durchgehend 30). Die Kopie kostet auf dem Retroid
  etwa 1 ms je Bild auf einem eigenen Thread (auf dem schwachen Gerät des Testers mehr: dort zu
  messen). **Auf dem Gerät noch nicht mit dem Kernprozess geprüft:** das Menü der App (Pause,
  Grafik, Speicherkarte als `.gci`), Speichern/Laden, das Debug-Menü (SELECT halten), ein Absturz des
  Kernprozesses, und der echte Rückfall (MEM1 vom Java-Speicher belegt: nur auf Geräten wie dem
  Mobicel des Testers; auf dem Retroid mit Android 13 ist MEM1 frei).
- **Vorbereitung 0.40.0-alpha (Wunsch des Projektinhabers, 20.10.2026):** „wenn es läuft und alles
  passt: 0.40.0-alpha, gleich mit DPL2 und allem seit Alpha 4“; danach testen der Projektinhaber
  (Raumklang) und der Tester mit dem 2-GB-Gerät. Seit Alpha 4 gibt es genau zwei Stränge: DPL2
  (`claude/re4-dpl2-raumklang`, mit der neuen Nummerierung) und den Kernprozess.
  - **Zusammengeführt** (`f24a50a4`, ohne Konflikte). Der Tonweg des Kernprozesses kann jetzt
    beides: der Kern hat einen dritten Leser `re4_audio_read6` / `re4_core_audio6` (je Bild die
    Stereo-Werte und die vier Kanäle von derselben Stelle, mit derselben Takt-Anpassung; für 2
    und 4 Kanäle bleibt `readRing` bitgleich). Der Kernprozess liest damit, der Ring im geteilten
    Speicher hat 6 Werte je Bild (`Control::audioChannels`, Version 3), die App nimmt für
    `re4ipc_audio` die ersten zwei, für `re4ipc_audio4` die übrigen vier;
    `re4_core_audio_surround` als Spiegel (`Control::audioSurround`). `useCoreProcess` setzt
    `api.audio4` und `api.audioSurround`.
  - **Version:** `versionName '0.40.0-alpha'`, `versionCode` 60 (die 0.39.0 ist nie erschienen;
    Nummer nach Wunsch des Projektinhabers). Releasetext
    `port/android-decomp/docs/releases/v0.40.0-alpha.md` (englisch): 32-bit-Geräte mit Android 14
    (eigener Prozess), Dolby Pro Logic II über Androids Raumklang, weniger Tonverzögerung
    (`LatencyTuner`), kleinere APK; der bisherige „Known issue“ zu 32 bit ersetzt durch die Bitte
    um Berichte. Tag `v0.40.0-alpha`, Titel „CubeEvil4 v0.40.0 (Alpha)“, APK
    `CubeEvil4-v0.40.0-alpha.apk`.
  - **Geprüft (Cloud):** Kern-Test `ax` neu: `re4_audio_read6` in DPL2 liefert 1500/3000 (Stereo)
    und 1500/0/0/3000 (vier Kanäle) aus einem Lesen; `re4_core_remote_test` liest jede zweite
    Folge als vier Kanäle (vorne exakt, hinten still, `audioSurround` 0), weiter 0 Aussetzer;
    Java-Tests (`CoreInputTests` 125, `CoreTouchTests` 250, `FramePacingTests` 420,
    `GyroAxesTests` 40, `SurroundPcmTests` 3, `LatencyTunerTests` 14) bestanden; alle Java-Dateien
    gegen Android 15 übersetzt; `core_mode.cpp`, `core_ipc*.cpp`, `coreproc.cpp` für ARM32 und
    ARM64 mit Ersatz-Headern ohne Fehler und Warnungen. `whole_build.sh i686 armhf x86_64
    aarch64`: 0 Baufehler; zwei Ausreißer unter qemu (armhf): `runtime-test vsync` einmal
    FAILED (ein Zeittest mit echtem 60-Hz-Thread, danach viermal ok; der Code ist unberührt) und
    `re4_core_remote_test` „AI-Blöcke im Spiegel“: die Tonzähler im geteilten Speicher kamen nur
    je Spielbild, die letzten Blöcke des Testeinstiegs fehlten noch. Jetzt schreibt der
    Kernprozess sie auch nach jedem Lesen des Tons (`mirrorSound`). Danach
    `whole_build.sh i686 armhf` vollständig bestanden, der Kernprozess-Test je dreimal ohne
    Fehler. **Nicht getestet:** der zusammengeführte Stand auf dem Gerät (Kernprozess mit
    DPL2-Ton, Menü/Speichern/Debug-Menü), Raumklang auf einem Gerät mit Spatializer.
  - **Gerätelauf des Release-Kandidaten (Retroid, Projektinhaber, 20.10.2026, Stand `71c68cdf`,
    beide Kerne neu gebaut, APK 17,6 MB):**
    - **64 bit** (`-Manual`, 155 s): Spielstand 2 geladen (r105), Spielstand 3 an der
      Schreibmaschine gespeichert (mit Systemdatei), Debug-Menü (SELECT halten), App-Menü, alle
      fünf Dateien der Karte als `.gci` exportiert; Ton 26000 Blöcke, 9 ran dry, 0 skips; DPL2 im
      Spiel erkannt („game in Dolby Pro Logic II; spatializer: not on this device“: Stereo, wie
      gewollt); 60 gezeigte Bilder je Sekunde, GPU 12,3 ms.
    - **32 bit mit Kernprozess** (`-Abi armeabi-v7a -CoreProcess -Manual`, 164 s): Kernprozess
      PID 24125, Spielstand geladen, gespeichert, Debug-Menü über den Kernprozess, DPL2-Erkennung
      über den Spiegel des Kernprozesses (an und wieder aus), Ton 32000 Blöcke, 11 ran dry,
      **0 skips**; „2000 reads, 892 waited for the core process (at most 1 ms), 0 short“ (mit dem
      kleineren AudioTrack-Puffer des `LatencyTuner` wartet fast jede zweite Lesung, höchstens
      1 ms); Kopien 2203 KB je Bild, 0,80 ms; Kern 33,5 ms je Bild, 0 ms Warten, 21,9 ms
      Leerlauf; 60 gezeigte Bilder je Sekunde; keine Fehlermeldung.
    - Beim APK-Bau „2 warnings generated“ (C/C++); die Datei steht nicht im Bericht, frühere
      Builds zeigten dieselbe Art in `ports/motion64/motion.cpp` und `ports/fdlibm`; mit Clang 18
      sind die neuen Dateien ohne Warnung.
    - **Nicht auf dem Gerät mit dem Kernprozess:** App-Menü und `.gci`-Export (in der Cloud
      geprüft, auf dem Gerät nur in 64 bit), ein Absturz des Kernprozesses, der echte Rückfall
      (das 2-GB-Gerät des Testers).
    - **Projektinhaber: baut 0.40.0-alpha mit Android Studio** (`coreRelease`, signiert).
  - **Der echte Rückfall auf dem Tester-Gerät (Projektinhaber, 20.10.2026):** mit 0.40.0-alpha
    läuft das Spiel beim Tester mit dem reinen 32-bit-Gerät (Android 14, 2 GB; vorher „core not
    started: MEM1 … is taken in this process (by … dalvik-main space)“). Ohne Bericht: wie schnell
    es dort läuft, was die Kopien kosten und ob der Ton sauber ist, zeigt erst sein Fehlerbericht
    („Core: own process: MEM1 is taken in the app's process …“, Zeilen „core process: GX copies“
    und „core process sound“). **Damit ist der 32-bit-Kernprozess erreicht.**
- **Absturz, wenn ein Ganado Luis in der Hütte packt (64 bit): behoben mit Patch 0087
  (20.10.2026, Branch `claude/re4-luis-hotfix`, Hotfix 0.40.1-alpha).** Tester-Bericht
  `cubeevil4-report-20261020-163954.zip` (POCO X7 Pro, Android 16, App 0.40.0-alpha-core, Kern im
  App-Prozess): „signal 11, fault address 0x0, pc 0x00000000, lr libre4_pl14.so+0x91e8
  `_ZN8cSubLuis4moveEv`“, über `emMove` / `cEmMgr::move` / `gameMainLoop`; der Projektinhaber:
  „Offenbar tritt der Fehler mit dem Absturz etwas später wieder auf“ (nach Patch 0086 kommt die
  Belagerung weiter).
  - **Ursache:** `cRoutine::move` (in `cSubLuis::move` eingebettet) ruft in Routine 4
    `owner->m_pFunc()`; die Funktion war 0. Gesetzt wird sie von `SetSubDamage` (`pl_sub.cpp`),
    das ein Gegner beim Packen aufruft (em10 `em10CatchSubCk` → für `pSUB->id == 4`
    `em10_R1_NeckHang_Luis` → `EmCatchSubSet` → `SetSubDamage(em, subem10_NeckHang_Luis)`). Für
    alle Partner außer Ashley (id 3) schreibt das Original über `cSubChar`:
    `sub->m_MotTbl2[0] = ft; *(u8*) &sub->m_MotBase |= 0x40;`. Der einzige solche Partner ist Luis
    (`cSubLuis`, ein `cEm`, **kein** `cSubChar`): auf der GameCube liegen dort sein `m_pFunc`
    (0x584) und sein `status` (0x58C, 0x40 = `F_KARAMI`). Auf LP64 nicht: `cSubChar::m_MotTbl2` bei
    0x680, `m_MotBase` bei 0x690, in Luis' `analysis` (0x670..0x698; `time`, `grenadeTimer`,
    `pEmNearDist`), sein `m_pFunc` bei 0x698 blieb 0.
  - **Prüfung aller Stellen** (libclang über alle 93 Einheiten mit `SUB_CHAR`/`pSUB`/`cSubChar`,
    aarch64): 1109 Zugriffe auf Mitglieder von `cSubChar`, ohne `pl_npc.cpp` und `pl11` (Ashleys
    eigener Code) 32 Funktionen. Luis ist `pSUB` nur während der Belagerung in r11c
    (`r11c_EventBesiegedStart` zerstört Ashley, am Ende `pSUB = 0`). Davon erreicht Luis:
    `SetSubDamage` (der Zweig für id ≠ 3) und `subem10_NeckHang_Luis` (`m_Work0` als Zähler,
    GameCube 0x530 = Luis' `routine.pos.y`, LP64 0x61C = `routine.dist`). Nicht mit Luis: TakeAway
    (em10 wählt für id 4 NeckHang_Luis), Tür-Tritt (`SubOpenDoorSet`) und Leiter
    (`SubLadderClimbCk`) nur aus `pl_npc.cpp`, `SubCharCtrl`/`MoveTo`/`CtrlHide`/`SetSubAux` nur
    aus Räumen mit Ashley, Boot (pl0e/pl0f), Bulldozer, Falle (em2a), El Gigante (em2b), em3c.
    `pl_R1_Fall` (`registPlAction`) und `sceAtFunc_damage` (`setDamage`) würden auch auf der
    GameCube Luis' Zeiger überschreiben (`analysis.owner`); im Original also nicht erreicht.
    Virtuelle Aufrufe über `cSubChar` auf Luis gehen nach Platz in der Tabelle, wie auf der
    GameCube: `EndSubDamage` ruft für id 4 `modelSet` (Platz 12 bei Itanium, 9 bei CodeWarrior),
    bei Luis beide Male `cSubLuis::endDamage`.
  - **Patch 0087:** `SetSubDamage` setzt für id 4 `((cSubLuis*) pSUB)->m_pFunc = ft` und
    `status.on(F_KARAMI)`; `subem10_NeckHang_Luis` zählt in Luis' `routine.pos.y` wie die
    GameCube (`em10.cpp` bindet dafür `pl14.h` ein). Auf 32 bit ändert sich am Speicher nichts
    außer `F_KARAMI`: Patch 0024 hatte das Bit als „ungecachten Spiegel von `cMotBase::pMod`“
    gestrichen; für Luis ist es aber sein Status, `cSubLuis::endDamage` startet damit seinen
    Dank-Satz nach dem Packen (`thankCtr = 30`). Der fehlte bisher auf allen Geräten. 0024 bleibt
    für den (im Port nicht vorhandenen) übrigen Fall.
  - **Neuer Laufzeittest `partner`** (`runtime_tests.cpp`, `game_probe.cpp`
    `re4probe_luis_damage`): genullte Luis-Bytes als `pSUB` mit id 4, `SetSubDamage` mit einer
    Testfunktion; geprüft werden `m_pFunc`, `F_KARAMI`, `r_no_0 == 4`, `pEmCatch` und dass seine
    `analysis` unberührt bleibt. **Gegenprobe** (aarch64 ohne 0087): „partner: faults 0x13“
    (`m_pFunc`, `F_KARAMI`, `analysis`), genau das Bild des Berichts.
  - **Im 64-bit-Kern nachgesehen** (aarch64, `llvm-objdump`): `SetSubDamage` schreibt für id 4
    nach 0x698 (Luis' `m_pFunc`), der alte Zweig weiter nach 0x680; `subem10_NeckHang_Luis`
    zählt bei 0x630 (`routine` 0x4C0 + `pos.y` 0x170).
  - **Version:** `versionName '0.40.1-alpha'`, `versionCode` 61 (Hotfix nach der neuen
    Nummerierung). Releasetext `port/android-decomp/docs/releases/v0.40.1-alpha.md`.
  - **Geprüft (Cloud):** `whole_build.sh i686 armhf aarch64`: 0 Baufehler, alle 39 Prüfungen
    bestanden, `runtime-test partner: ok` auf allen drei; `m12/run_all.py --lp64
    port/build/whole-aarch64`: 0 neue Funde. Die M12-Werkzeuge sahen diese Art nicht (ein
    Zeiger der Basisklasse `cEm`, `pSUB`, wird als eine Geschwisterklasse gelesen, `cSubChar`
    statt `cSubLuis`: jeder Zugriff für sich ist ein gewöhnlicher Abwärts-Cast); die Suche oben
    deckt sie für den Partner ab. **Nicht getestet:** auf dem Gerät (die
    Belagerung in r11c, bis ein Ganado Luis würgt; danach muss Luis weiterkämpfen und seinen
    Dank-Satz sagen), auf 64 bit und auf 32 bit.
- **Touch-Bedienung bleibt ohne Controller versteckt (Redmi 15 5G): Variante A (Entscheidung des
  Projektinhabers, 21.10.2026), Branch `claude/re4-luis-hotfix`, Teil von 0.40.1-alpha.** Ein
  Nutzer ohne Controller sah keine Touch-Tasten; damit war auch das App-Menü (SELECT) und der
  Fehlerbericht nicht erreichbar. Bericht `cubeevil4-report-20261020-233709.zip` (Xiaomi
  25078RA3EL „dew“, Redmi 15 5G, Android 16, 64 bit, App 0.40.0-alpha-core): „touch controls
  shown (a controller is connected, the menu's switch is on)“ (er hatte den Schalter inzwischen
  an) und **keine einzige** Controller-Taste im Protokoll: Android meldet dort ein Gerät mit den
  Quellen GAMEPAD oder JOYSTICK, das kein Controller ist (bekannt etwa vom Fingerabdrucksensor
  `uinput-fpc` der Pixel-Handys, den SDL deshalb übergeht). Welches Gerät, stand nicht im
  Protokoll. Derselbe Bericht zeigt außerdem den Luis-Absturz in r11c (`cSubLuis::move+136`, pc 0),
  den Patch 0087 behebt.
  - **Regel neu** (`TouchShowRule.java`, rein Java): gezeigt bei Schalter, ohne Controller wie
    bisher, **und** nach einer Berührung des Bildschirms, solange die Tasten versteckt waren (das
    Ende der Berührung zeigt sie, die nächste spielt), bis zur nächsten echten Controller-Eingabe:
    eine belegte Taste (`input.key`), SELECT von einem Controller, ein Stick, Trigger oder
    Steuerkreuz über die Hälfte (`TouchShowRule.MOVED`), damit das Rauschen eines ruhenden Sticks
    nicht zählt. Auf dem Retroid bleibt alles wie bisher, nur ein Tippen auf den Bildschirm holt
    die Touch-Tasten, bis wieder eine Taste gedrückt wird.
  - **Protokoll:** `TouchControls.controllerDevices()` zählt nach derselben Regel wie
    `RetroTouchControllers` (Quellen GAMEPAD 0x401 oder JOYSTICK 0x1000010) und nennt jedes
    Gerät (Nummer, Name, Hersteller/Produkt, Quellen, virtuell, eingebaut/extern ab Android 10);
    die Zeile „touch controls shown/hidden (…)“ steht bei jeder Änderung der Anzeige oder der
    Geräte im Protokoll (mit dem Grund „the screen was touched“).
  - **Geprüft (Cloud):** `TouchShowRuleTests` 33 Prüfungen bestanden (in `tools/test_host.sh`);
    alle Java-Dateien der App gegen Android 15 übersetzt, ohne Fehler; `tools/test_host.sh`: die
    Java-Tests bestanden, nativ 40 von 42 wie bisher (die bekannten `enemy_feedback_tests` und
    `inventory_case_contracts`). **Nicht getestet:** auf einem Gerät (Retroid: Touch erscheint nach
    einem Tippen, verschwindet bei einer Taste; Redmi: Touch nach einem Tippen).
- **App-Menü im Spiel in mehreren Sprachen: vorbereitet (Wunsch des Projektinhabers,
  21.10.2026), Branch `claude/re4-menu-sprachen` (vom Hotfix-Stand `5c08212c`).** Die Sprache
  folgt der Sprache des Handys; gibt es sie nicht, bleibt es Englisch. Welche Sprachen kommen,
  liefert der Projektinhaber noch.
  - **Umsetzung mit Androids eigenen Sprachdateien:** alle Texte des App-Menüs (`CoreMenu`) und
    der Meldungen der App im Spiel (`CoreActivity`: Kern startet nicht / beendet, Spiel beenden,
    Speicherkarten-Export und -Import, Bericht senden/speichern, FPS-Anzeige „(Game)“) stehen in
    `app/src/main/res/values/strings_ingame.xml` (Englisch, 67 Texte und eine Mehrzahl-Form „%d
    files exported“). Eine Sprache ist eine Datei `values-<Sprachkürzel>/strings_ingame.xml` mit
    denselben Namen (z. B. `values-fr`, `values-es`, `values-pt-rBR`); Android nimmt die Sprache des
    Handys, sonst `values/` (Englisch), auch für einen einzelnen fehlenden Text. Als erste
    Übersetzung **Deutsch** (`values-de`), zum Ausprobieren. Zahlen im Menü bleiben Ziffern 0–9
    (`Locale.ROOT`), „OK“ kommt von Android selbst.
  - **Bleibt Englisch:** die Texte des Spiels selbst (GameCube), der Startbildschirm der App
    (Disc-Import, „Start game“; nicht Teil des Menüs im Spiel), der Fehlerbericht und das
    Protokoll (für uns), technische Gründe in den Meldungen (z. B. „No data folder“). Die übrigen
    Texte der alten App (`values/strings.xml`) sind als „nicht übersetzt“ gekennzeichnet
    (`tools:ignore="MissingTranslation"`), `app_label` als `translatable="false"` (Name der App).
  - **Prüfwerkzeug** `port/android-decomp/tools/check_translations.py` (in `tools/test_host.sh`):
    je Sprache alle Texte da, keine fremden, gleiche Platzhalter (`%1$s`, `%2$d`) und
    Zeilenumbrüche, Apostroph mit `\'`, `&` als `&amp;`, Mehrzahl mit „other“; jeder
    `R.string.ingame_*` der App steht in der englischen Datei. Gegenprobe mit absichtlich
    fehlerhaften Dateien (fehlender Text, falscher Platzhalter, Apostroph, fehlende Mehrzahl,
    kaputtes XML): alle fünf gemeldet.
  - **Geprüft (Cloud):** alle Java-Dateien der App gegen Android 15 übersetzt (mit den neuen
    Namen), ohne Fehler; `check_translations.py`: values-de 67 Texte, 1 Mehrzahl, ok;
    `tools/test_host.sh` wie bisher (Java-Tests bestanden, nativ 40 von 42). **Nicht getestet:**
    auf einem Gerät (Menü auf Deutsch und Englisch, ob die längeren deutschen Texte in die
    Spalten passen); kein Android-Ressourcen-Bau in der Cloud (kein `aapt2`), das prüft erst
    Android Studio.
- **Gerätelauf Hotfix-Stand (Retroid, Projektinhaber, 21.10.2026, Stand `73ed2246`, beide Kerne
  neu gebaut):** die Belagerung in r11c läuft durch (Ereignisse `r11cs00` → `r11cs10` →
  `r11cs20`), kein Absturz; „Ich konnte die Szene nach Luis weiterspielen ohne Probleme“. Ob ein
  Ganado Luis in diesem Lauf gewürgt hat, zeigt das Protokoll nicht. Touch nach Variante A wie
  geplant (der Retroid-Controller meldet sich als „Retroid Pocket Controller“ vendor 2022 product
  3001 sources 0x1000511 external): jede Berührung zeigt die Touch-Tasten, jede Taste blendet
  sie aus. **Projektinhaber:** mit Controller nervt das (Bildschirmfotos).
- **Touchsteuerung Auto / Erzwingen / Unterdrücken, Menü umgestellt (Wünsche des
  Projektinhabers, 21.10.2026), Branch `claude/re4-menu-sprachen`, Version 0.41.0-alpha.**
  - **Einstellung** `touchMode` (`CoreActivity.TOUCH_MODE`, `TouchShowRule.AUTO/FORCE/SUPPRESS`):
    Auto (Standard) = Variante A; Erzwingen = immer (der alte Schalter „Use Touch Controls“ an
    wird beim ersten Start zu Erzwingen, `CoreActivity.touchMode`); Unterdrücken = aus, solange
    ein Controller verbunden ist, Antippen holt sie nicht zurück. Sicherung: ohne Controller
    sichtbar, und solange seit dem Start kein Controller benutzt wurde, zeigt ein Tippen sie auch
    in Unterdrücken (ein Gerät, das nur einen Controller meldet, ließe sonst gar keine Steuerung
    und kein Menü übrig). Die Protokollzeile nennt die Einstellung („touch controls hidden
    (suppress, a controller is connected: …)“).
  - **Menü:** Zeile „Touch Controls“/„Touchsteuerung“ (früher „Select methods“/„Eingabearten“)
    mit drei Knöpfen Auto, Force/Erzwingen, Suppress/Unterdrücken in gleichen Teilen der Zeile;
    darunter „Latch L and R (Touch)“; kurzer Hinweis auf die drei. „Comfort Controls“/
    „Komfortsteuerung“ (früher „Button layout“/„Tastenbelegung“) mit „Alternate Controls“ und
    „Use Gyro-Aiming“ (von der Touch-Zeile hierher). Englisch und Deutsch angepasst (69 Texte).
  - **Version** `0.41.0-alpha`, `versionCode` 62 (neue Funktionen: neue mittlere Zahl; 0.40.1 ist
    nie erschienen). Releasetext `port/android-decomp/docs/releases/v0.41.0-alpha.md` (statt
    `v0.40.1-alpha.md`): Luis, Touch nach einem Tippen, Touch-Einstellung, Menü in der
    Handy-Sprache. README: Menü und Touch-Einstellung.
  - **Geprüft (Cloud):** `TouchShowRuleTests` 51 Prüfungen (alle drei Einstellungen, Wechsel im
    Menü, Gerät ohne Eingaben in Unterdrücken); `check_translations.py` values-de 69 Texte ok;
    alle Java-Dateien gegen Android 15 übersetzt; `tools/test_host.sh` wie bisher (nativ 40 von
    42). **Nicht getestet:** auf dem Gerät (Menü, die drei Einstellungen mit dem Retroid).
  - **Gerätelauf (Retroid, Projektinhaber, 21.10.2026, Stand `2fb30fb7`, 31 s):** das Menü auf
    Deutsch, alle Zeilen in ihren Spalten (Touchsteuerung mit drei gleich breiten Knöpfen,
    darunter „L und R einrasten (Touch)“, Hinweis, Komfortsteuerung mit Alternativer Steuerung
    und Gyro-Zielen), „Unterdrücken“ gewählt; danach keine Touch-Tasten mehr im Protokoll und im
    Bild, FPS-Anzeige „30 FPS (Spiel) · 60 Hz“. Ob nach der Wahl auf den Bildschirm getippt
    wurde, zeigt das Protokoll nicht: der Wechsel der Einstellung stand nicht darin. Jetzt
    schreibt `TouchControls.setMode` „touch controls: setting auto/force/suppress“.
- **App-Menü in zehn Sprachen (Wunsch des Projektinhabers, 21.10.2026), Branch
  `claude/re4-start-controls` (vom `master`-Stand `0394c644`, 0.41.0-alpha).** Zu Englisch und
  Deutsch kommen Spanisch (`values-es`), Französisch (`values-fr`), Italienisch (`values-it`),
  Portugiesisch (`values-pt`, Wortwahl Brasilien, gilt auch für Portugal), Russisch (`values-ru`),
  Arabisch (`values-ar`), Chinesisch vereinfacht (`values-zh`; Android nimmt sie für zh-Hans,
  China und Singapur; Taiwan und Hongkong mit traditionellen Zeichen bleiben Englisch, bis eine
  `values-zh-rTW` kommt) und Hindi (`values-hi`). Übersetzt von Claude; Muttersprachler unter den
  Testern sollten drüberschauen (vor allem Arabisch, Hindi, Chinesisch).
  - **Schriften:** das Menü nutzt Androids Textfelder, jedes Handy bringt die Schriften für
    Kyrillisch, Arabisch, Chinesisch und Devanagari mit. Die Kapitälchen der Überschriften
    (`CoreMenu.smallCaps`) lassen Schriften ohne Groß/Klein unverändert; Android setzt den
    Buchstabenabstand bei verbundenen Schriften (Arabisch, Devanagari) selbst aus.
  - **Arabisch von rechts nach links:** `android:supportsRtl` war schon an, die Zeilen des Menüs
    spiegeln sich (Beschriftung rechts, Knöpfe links), der Fokus des Steuerkreuzes folgt. Texte,
    die mit lateinischen Buchstaben oder einem Dateinamen beginnen (`SELECT …`, `R3 = …`,
    `%1$s ist schon auf der Karte`), beginnen mit der Marke U+200F, sonst legt Android die ganze
    Zeile links-nach-rechts an; „1280 × 672“ und „.gci“ tragen U+200E, sonst tauschen die Zahlen
    die Plätze bzw. der Punkt wandert hinter „gci“. Mit dem Unicode-Algorithmus für gemischte
    Schreibrichtungen (Paket `python-bidi`) nachgeprüft: zwölf Zeilen in richtiger Reihenfolge.
    Die Mehrzahl „Dateien exportiert“ als „Anzahl der exportierten Dateien: N“ (passt zu allen
    sechs arabischen Mehrzahlformen). Die FPS-Anzeige bleibt unten links und links-nach-rechts
    (`CoreActivity`: `Gravity.LEFT`, `TEXT_DIRECTION_LTR`; vorher `START`, das wäre unter
    Arabisch nach rechts gewandert, obwohl der Schalter „unten links“ sagt). Die Versionsnummer
    neben dem Titel bekommt ihren Abstand als Innenrand am Anfang statt zweier Leerzeichen
    (`CoreMenu`), damit er auch rechts-nach-links zwischen Titel und Nummer liegt.
  - **Mehrzahl:** Russisch one/few/many/other, Arabisch zero/one/two/few/many/other,
    Spanisch/Französisch/Italienisch/Portugiesisch one/many/other, Hindi one/other, Chinesisch
    other. Französisch mit geschütztem Leerzeichen vor „:“ und „?“, geschrieben `\u00A0`
    (Androids Ressourcen-Bau macht aus dem Zeichen selbst ein normales Leerzeichen).
  - **Sprache je App (Android 13 und neuer):** `res/xml/locales_config.xml` mit
    `android:localeConfig` im Manifest: unter Einstellungen → Apps → CubeEvil4 → Sprache lässt
    sich die Menüsprache unabhängig von der Handy-Sprache wählen (ohne Wahl gilt die
    Handy-Sprache wie bisher). `CoreActivity` hat `locale|layoutDirection` in `configChanges`:
    ein Sprachwechsel (Handy oder App) startet das laufende Spiel nicht neu (der Kern lässt sich
    im selben Prozess nicht neu starten); das Menü zeigt die neue Sprache beim nächsten Öffnen.
    Protokoll: „app menu: language de“ beim Start, „menu open, … (language ar, right to left)“
    bei jedem Öffnen, „configuration changed, the game keeps running: language …“ beim Wechsel.
  - **Prüfwerkzeug** `check_translations.py` erweitert: kein `@` oder `?` am Textanfang (Android
    hielte es für einen Verweis), kein Sonder-Leerzeichen wie U+00A0 als Zeichen selbst; für
    Arabisch (und andere Rechts-nach-links-Sprachen) Anfang rechts-nach-links (FPS-Anzeige
    ausgenommen), U+200E bei „N × M“ und „.gci“;
    `locales_config.xml` nennt genau Englisch und die Sprachen mit Datei. Gegenprobe mit
    absichtlich fehlerhaften Kopien (fehlende Marke U+200F, fehlende U+200E bei „×“ und „.gci“,
    `?` am Anfang, Apostroph, Sprache in der Liste ohne Datei, Datei ohne Listeneintrag,
    geschütztes Leerzeichen als Zeichen): alle acht gemeldet.
  - **Geprüft (Cloud):** `check_translations.py`: alle neun Sprachen 69 Texte, 1 Mehrzahl, ok;
    alle Java-Dateien der App gegen Android 15 übersetzt, ohne Fehler und ohne neue Warnung;
    `tools/test_host.sh`: Java-Tests bestanden, nativ 40 von 42 wie bisher (die bekannten
    `enemy_feedback_tests` und `inventory_case_contracts`). **Nicht getestet:**
    Android-Ressourcen-Bau (kein `aapt2` in der Cloud, Googles Server gesperrt; prüft Android
    Studio), auf dem Gerät (Menü in jeder Sprache: passt alles in die Spalten, Arabisch
    gespiegelt, Sprachwechsel bei laufendem Spiel).

- **Absturz beim Wegesuchen der Ganados (64 bit, Hütte r11c): abgefangen mit Patch 0088
  (21.10.2026, Branch `claude/re4-start-controls`).** Tester-Bericht
  `cubeevil4-report-20261021-091210.zip` (Samsung Galaxy M23 „m23xq“, Android 14, 64 bit, App
  0.41.0-alpha-core, Kern im App-Prozess; derselbe Tester wie beim Absturz von Patch 0086, also in
  der Belagerung der Hütte mit Luis als Partner): zweimal hintereinander etwa eine Minute nach dem
  Start „signal 11, fault address 0x4e399b6e0“, pc `getNearPoint`, über `RouteCkToEm` ←
  `em10RouteCk` ← `cEm10::move` (ein Ganado sucht den Weg zum Partner). Fehlerprotokoll war aus
  (`log.txt` leer).
  - **Ursache in `getNearPoint`:** die zehn nächsten Wegpunkte kommen sortiert in `idx[]`
    (nicht vorbelegt) und `dist[]` (1e16). Ein Punkt weiter als 1e8 (Abstand² über 1e16) kommt nie
    hinein; liegen weniger als zehn Punkte so nah, behalten die Einträge dahinter ihren
    Stack-Inhalt, und die Suche nach freier Sicht liest `pt[idx]`. Die Fehladresse passt genau:
    mit `pt` in MEM1 ist `idx` ≈ 0x4609…–0x4639…, als Gleitkommazahl 8800–11900, also eine
    liegengebliebene Koordinate auf dem Stack. Auslöser ist eine Position weiter als 100 000 m
    von allen Wegpunkten: die des Ganados oder die von Luis (`getNearInfo(pTo)`), unendlich oder
    riesig. Woher sie auf 64 bit kommt, zeigt der Bericht nicht (kein Protokoll); die Klassen von
    Luis und Partner passen (`cSubLuis` und `cSubChar` erben einfach von `cEm`, `pos` liegt in
    `cEm`), `EmCatchSubSet`/`EmCatchMotionMove` greifen nur auf `cEm`-Mitglieder zu.
  - **Patch 0088:** nur die gefüllten Einträge durchsuchen, keiner: -1 („kein Wegpunkt“; jeder
    Aufrufer geht dann direkt aufs Ziel). Mit Positionen im Raum sind immer alle gefüllt, dort
    ändert sich nichts. „check: route: the partner (id 0x4, type …) at (x y z): N of the M route
    points of room r11c lie within 1e8 …“ nennt bis zu viermal, wessen Position es war (Spieler,
    Partner oder welcher Gegner mit id/type), damit der nächste Bericht mit Fehlerprotokoll die
    eigentliche Ursache zeigt.
  - **Zweiter Bericht, derselbe Fehler über Ashley** (`cubeevil4-report-20261021-203112.zip`,
    Samsung Galaxy S9+ „SCV40“, Android 10, 64 bit, **App 0.41.0**, also noch ohne 0088): im
    Absturzpuffer des Systems (`previous/crash-system.txt`) 20:16 „SIGSEGV, fault addr
    0xfffffffa814f00a0“, `getNearPoint+412` ← `RouteCkToPos+456` ← **`cSubChar::analyze+588`** ←
    `cSubChar::moveCore` ← `emMove`: Ashley sucht ihren Weg zu einem Ziel, das mehr als 1e8 von
    allen Wegpunkten weg liegt (Register x27/x28 = 1.6e7/1e6, die Abstandsgrenzen aus `analyze`).
    Ziele dort: `pAnotherRoute->pos` (EMI, Typ 0xB, ohne Zeiger), `m_TargetPos` (SubCharMoveTo),
    `m_PlActPos` (Warteplatz an Kanten) oder `pPL->pos`; welches, zeigt der Bericht nicht. Das
    Protokoll dieses Laufs fehlt: der Tester startete danach zweimal neu, die App behält nur den
    letzten und den vorletzten Lauf (danach r10f geladen, ohne Absturz). Zwei ältere Abstürze im
    selben Puffer (19:24, 19:31, `cSubLuis::move+136`, pc 0) stammen von einer APK mit gepackten
    Bibliotheken, also vor 0.40.0 (Patch 0087). Patch 0088 fängt auch den Weg über
    `RouteCkToPos` ab; die Meldung nennt dort „a position“ (eine Kopie des Ziels) mit den
    Koordinaten.
  - **Neuer Laufzeittest `route`** (`game_probe.cpp` `re4probe_route_far`): drei Wegpunkte in
    MEM1, eine Position nahe Punkt 1 findet ihn, eine 2e8 entfernte gibt in beiden Arten -1 (vor
    jedem Aufruf ein Stack voller 0x46464646 wie im Bericht).
  - **Geprüft (Cloud):** `whole_build.sh i686 armhf aarch64`: 0 Baufehler, alle Prüfungen
    bestanden, `runtime-test route: ok` auf allen drei (mit der Meldung „check: route: a position
    … at (0 2e+08 0): 0 of the 3 route points …“). **Gegenprobe** (aarch64 ohne 0088): der
    Laufzeittest stürzt ab, „signal 11, fault address 0x4e7456480“ in `re4probe_route_far`,
    fast dieselbe Adresse wie im Bericht (0x4e399b6e0). **Nicht getestet:** auf dem Gerät; die
    eigentliche Ursache (wessen Position) zeigt erst ein Bericht mit eingeschaltetem
    Fehlerprotokoll.

- **0.41.1-alpha: Fehlerprotokoll auf jedem Handy (Wunsch des Projektinhabers, 21.10.2026: die
  Tester sollen ab sofort damit protokollieren), Branch `claude/re4-start-controls`.** Die Version
  enthält auch Patch 0088 und die Menü-Sprachen (Entscheidung des Projektinhabers: „als 0.41.1“).
  - **Anlass:** Das Fehlerprotokoll ist seit M11 ab Werk an; beim Samsung-Tester (Galaxy M23)
    war `log.txt` trotzdem zweimal leer (Bericht zu 0.38.2 und `…-20261021-091210.zip`), beim
    POCO-Tester nicht. Die App lässt `logcat` mitschreiben; endet es (oder schreibt es nichts),
    stand das bisher nirgends: seine Meldungen gingen in eine nie gelesene Leitung.
  - **Kern schreibt selbst:** `re4_log_file` (`runtime/log.cpp`) schreibt jede Zeile von
    `re4_log` zusätzlich direkt in `logs/core-log.txt` (Zeit wie `logcat`, jede Zeile sofort per
    `write`, ab 4 MB wird die Datei zu `core-log.txt.1`, also höchstens 2 × 4 MB); zuerst die
    gemerkten Zeilen davor (bis 24, die der Anzeige). Die App gibt den Pfad über
    `CoreBridge.setLogFile` (`core_mode.cpp`, `CoreLog.start` und der Menü-Schalter, „“ beendet
    es); der eigene Kernprozess (32 bit) bekommt ihn über die geerbte Umgebung (`RE4_LOG_FILE`,
    `coreproc.cpp`), im App-Prozess schließt `useCoreProcess` die Datei dann. Kosten: im Schnitt
    etwa 3 Zeilen je Sekunde (450–700 Zeilen je Retroid-Lauf), je Zeile wenige Mikrosekunden.
  - **`logcat` überwacht:** ein Faden liest, was `logcat` sagt, und schreibt beim Ende Dauer,
    Rückgabewert und Meldung in `logs/logcat-status.txt` (dazu jeden Start mit dem Befehl);
    endet der erste Versuch (main/system/crash), startet ein zweiter nur mit `-b main`.
  - **Bericht:** `device.txt`/`device-now.txt` mit „Error log: on (logcat running/not running,
    …)“ oder „off“; `device.txt` jetzt nach dem Start der Aufzeichnung.
  - **Version** `0.41.1-alpha`, `versionCode` 63; Releasetext
    `port/android-decomp/docs/releases/v0.41.1-alpha.md`.
  - **Neue Prüfungen:** `runtime-test logfile` (Zeile davor und alle danach mit Zeitstempel,
    Wechsel auf `.1` bei `RE4_LOG_FILE_LIMIT=4096`, beide Dateien höchstens so groß, nach „“ nichts
    mehr); `re4_core_remote_test main` prüft, dass der Kernprozess die Datei selbst schreibt
    (Kopfzeile und „ready (pid …)“).
  - **Geprüft (Cloud):** `whole_build.sh i686 armhf aarch64`: 0 Baufehler, alle Prüfungen
    bestanden (`runtime-test logfile: ok` und `route: ok` auf allen drei; der Kernprozess schreibt
    seine Datei selbst, i686 und armhf je 32 Zeilen). Ein zusätzlicher Lauf von
    `re4_core_remote_test` während des armhf/aarch64-Baus meldete einmal Tonaussetzer
    (`stats[4]`, Volllast); danach in Ruhe dreimal ohne Fehler. Inhalt von 0.41.1 gegen
    `master` (0.41.0) geprüft: Menü-Sprachen, Patch 0088, Fehlerprotokoll; der neue Branch
    `claude/adoring-mendel-vclbu8` (OUYA-Machbarkeit, nur Dokument) gehört nicht zur App.
    Alle Java-Dateien gegen Android 15 übersetzt (ohne neue
    Warnung), `core_mode.cpp` für ARM32 und ARM64 mit Ersatz-Headern ohne Fehler und Warnungen.
    **Nicht getestet:** auf einem Gerät, vor allem auf dem Samsung des Testers (ob `logcat` dort
    endet oder nur schweigt, zeigt `logcat-status.txt`; die Zeilen des Kerns stehen auf jeden Fall
    in `core-log.txt`, die der Java-Seite nur, wenn `logcat` läuft).
  - **Gerätelauf (Retroid, Projektinhaber, 21.10.2026, Stand `3d28b414`, beide Kerne neu gebaut,
    64 bit, `-Manual`, 78 s):** Spielstand in r105 geladen, Spieler frei, Menü auf Deutsch
    („app menu: language de-AT“, „menu open … (language de-AT)“), alle Zeilen in ihren Spalten;
    kein Absturz, keine „check: route“-Meldung. Fehlerbericht aus dem Menü
    (`cubeevil4-report-20261021-150722.zip`): `logs/core-log.txt` 22 KB mit Kopfzeile,
    Zeitstempeln und den Zeilen vor dem Start, `logcat-status.txt` „logcat started …“ ohne Ende,
    `log.txt` 36 KB, „Error log: on (logcat running …)“ in `device.txt` und `device-now.txt`.
    **Nicht getestet:** andere Sprachen und der Sprachwechsel bei laufendem Spiel, das Samsung
    des Testers, der 32-bit-Kernprozess auf dem Gerät.

- **Ursache gefunden: die Gondel in r10f (Patch 0089, 21.10.2026, Branch
  `claude/re4-start-controls`, 0.41.2-alpha).** Zwei Berichte mit 0.41.1:
  - `cubeevil4-report-20261021-103813.zip` (Galaxy M23): in beiden Läufen nach dem Laden in r10f,
    A an der Gondel, Zwischensequenz (Tiefenschärfe aus), START, danach viermal „check: route:
    the partner (id 0x3, type 0) at (-1469.6 -2.32037e+35 -2346.42): 0 of the 83 route points of
    room r10f …“, kein Absturz mehr. Ashleys Höhe ist als Bitmuster 0xFA32C13x, eine Adresse in
    `libre4_main.so` (geladen bei 0xFA000000). `log.txt` in beiden Läufen leer, obwohl
    `logcat-status.txt` „started“ ohne Ende zeigt: auf diesem Samsung schreibt `logcat` still
    nichts; `core-log.txt` (25 und 37 KB) hat alles.
  - `cubeevil4-report-20261021-203815.zip` (Galaxy S9+, 0.41.1): r10f geladen, keine Meldung
    (Gondel in diesem Lauf nicht benutzt), `log.txt` und `core-log.txt` gefüllt.
  - **Ursache:** `r10f_GondolaGetOn`/`r10f_GondolaGetOff` lesen die Plätze von Leon (`posA`) und
    Ashley (`posB`) über eine Struktur-Sicht auf das lokale Zeigerfeld `mot`, also hinter `mot`
    auf dem Stapel, wo der Original-Compiler die beiden Felder ablegte (eine Nachbildung seines
    Codes, Kommentar in `r10f.cpp`). clang speichert `posA`/`posB` bei -O2 gar nicht (nie beim
    Namen benutzt; in der aarch64- und der armhf-Übersetzung von `r10f.cpp` 0 Zugriffe auf ihre
    Konstanten), die Sicht liest gesicherte Register: Gleitkomma-Register für x/z, ein Zeiger in
    die Daten von `libre4_main.so` für y. Betroffen waren also alle Geräte (32 und 64 bit), Leon
    wie Ashley; Leons Fehlplatz fiel nicht auf (die Gondel setzt ihn danach selbst), Ashleys
    unendlich ferne Höhe ließ ihre Wegsuche abstürzen (vor 0088).
  - **Patch 0089:** die vier Aufrufe nehmen `&posA[side]`/`&posB[side]` selbst. Nach dem Patch
    4 Zugriffe auf die Konstanten. Die Suche nach weiteren Sichten dieser Art über lokale Felder
    (`((T*) local)->feld[…]`) in `src/` fand sonst nur `t_esp.cpp` (`SeqCountView` über echte
    Daten, harmlos).
  - **Version** `0.41.2-alpha`, `versionCode` 64; Releasetext
    `port/android-decomp/docs/releases/v0.41.2-alpha.md`.
  - **Gerätelauf (Retroid, Projektinhaber, 21.10.2026, Stand `32f686e5`, beide Kerne neu gebaut,
    64 bit, `-Manual`, 56 s):** Spielstand in r113 geladen, Spieler frei, kein Absturz, keine
    „check: route“-Meldung. Die Gondel in r10f lief dabei nicht (kein Spielstand davor auf dem
    Retroid); erster Durchlauf hatte die ADB-Sperre belegt (`ouya-probe.ps1` der OUYA-Sitzung).
  - **Geprüft (Cloud):** `r10f.cpp` mit -O2 für aarch64 und armhf übersetzt (ohne Patch 0 Zugriffe
    auf `posA`/`posB`, mit Patch 4); `whole_build.sh i686 armhf aarch64`: 0 Baufehler, alle
    Prüfungen bestanden (114 Module laden, Laufzeit-, Kern- und Kernprozess-Tests). **Nicht getestet:** auf dem Gerät (die Gondel in r10f mit
    Ashley: beide kommen auf ihren Plätzen an, keine „check: route“-Meldung mehr).

- **Zwei Berichte eines Testers mit 0.41.2 (Xiaomi 11 Lite 5G NE „lisa“, Android 14, 64 bit,
  GameSir G8, Menü auf Portugiesisch; `cubeevil4-report-20261021-150324.zip` und
  `…-150829.zip`, derselbe Lauf 14:38–15:08 und derselbe vorige 11:32–12:56).**
  Seine Angaben: Burghalle beim Hebel: Ganados mit Sicheln tauchen immer wieder auf oder
  verschwinden; Schießbude: Ashleys Schreie, Texturfehler im Hintergrund, Holzziele tauchen
  immer wieder auf; Waffenverbesserung: Auswahl springt von selbst.
  - **Schießbude (r22c): Ursache gefunden, Patch 0090.** Im Protokoll beider Läufe „game:
    cEmMark::setNextInst() ERR 300“, im vorigen Lauf danach (12:56:43) der Absturz `cEmMark::move`
    → pc 0 (`crash.txt`). Ursache siehe Patch 0090 (Zieltyp und nächster Befehl aus dem falschen
    Byte der Skriptwörter). Vor dem Patch liest das aarch64-Modul `ldrb w9, [x9, #0x3]`, danach
    `ldr w9, [x9]` und speichert dessen niedrigstes Byte (`strb w9` nach `r_no_0`); `init` liest
    das Wort `flag` (`ldr w1, [x8, #0x4]`). `whole_build.sh i686 armhf aarch64`: 0 Baufehler,
    alle Prüfungen bestanden. **Nicht getestet:** die Schießbude auf dem Gerät.
    Ashleys Schreie und der Texturfehler: mit Typ 0 für alle Ziele lief die Schießbude anders als
    im Original (Ashley-Tafeln, Trefferwirkungen `R22cHitEffect`); ob beides mit 0090 verschwindet,
    zeigt erst der nächste Versuch (Bildschirmfoto des Hintergrunds erbeten).
  - **Burghalle (r208):** im Protokoll entstehen in r208 Ganados in Gruppen (14:43:19, 14:43:35,
    14:43:43, 14:44:14, 14:44:25, 14:47:04 sechs auf einmal, 14:48:08, 14:48:32, 14:48:48), ohne
    Fehlermeldung des Spiels oder des Ports. Ob das das Nachrücken des Originals ist oder ein
    Fehler (Verschwinden), lässt sich daraus nicht sagen. Offen: Video oder genaue Stelle, Vergleich
    mit Dolphin; sonst im Raum-Rundgang (10b) prüfen.
  - **Waffenverbesserung (Händler, 15:05:40–15:06):** im Kern-Protokoll nur Tasten (A in schneller
    Folge), keine Stick-Werte; die App zeigte in diesem Lauf neunmal die Touch-Tasten, weil der
    Bildschirm berührt wurde (Klemm-Controller GameSir G8, Einstellung Auto). Ursache offen:
    Stick-Rauschen des Controllers, versehentliche Berührungen oder ein Fehler im Menü. Nachfrage
    beim Tester (passiert es mit Touch Controls auf „Suppress“ und ohne den Stick zu berühren?).
  - **Sonst:** `logcat` läuft auf diesem Xiaomi, `log.txt` und `core-log.txt` gefüllt.
  - **Nachgereicht ohne Protokoll (derselbe Tester):** im Raum, in dem die roten Ganados ihr
    Ritual halten, überlebt ein schwarz gekleideter Gegner eine Granate; verfolgt und am Kopf
    getroffen, platzt sein Kopf nicht („Fehler in seiner Animation“). Offen: im Original platzt
    ein Kopf nur bei einem zufälligen kritischen Treffer oder mit der Plaga; ob der Zustand nach
    der Granate das im Port verhindert, braucht einen Bericht direkt nach der Szene (Raum,
    Gegnertyp) und den Vergleich mit dem Original/Dolphin.
  - **Dritter Bericht** (`cubeevil4-report-20261021-154756.zip`, derselbe Lauf bis 15:47, ohne
    Beschreibung): r205, r204 (13 Ganados beim Betreten), r209 (Ritual-Raum: Ereignis
    `r209s00`, Modul em1a, 8 Ganados; 15:19–15:27, zweimal betreten), r20a, Speichern um 15:30,
    danach fast nur das App-Menü und einmal kurz das Debug-Menü. Kein Absturz, keine Meldung
    des Spiels oder des Ports. Zum Kopfschuss enthält das Protokoll nichts: Treffer und Zustände
    der Gegner zeichnet der Kern nicht auf (Spur gibt es nur für die Waffe, `RE4_TRACE_WEAPON`).
    Weiter: Spielstände des Testers (.gci) zum Nachspielen oder eine Gegner-Spur im Protokoll.
  - **0.41.3** (Patch 0090) erst, wenn der Tester brauchbare Protokolle zu Burghalle,
    Waffenverbesserung und Kopfschuss geschickt hat (Entscheidung des Projektinhabers,
    21.10.2026).

- **Gegner- und Menü-Spur im Fehlerprotokoll (Wunsch des Projektinhabers, 21.10.2026: „schreiben
  wir mehr ins Protokoll“; die Spielstände des Testers lagen hinter den Stellen), Branch
  `claude/re4-start-controls`.** `runtime/state_trace.cpp` schreibt immer (auch ohne
  `RE4_TRACE`) je Änderung eine Zeile in `core-log.txt`:
  - `em: + s12 0x1a/3 set 5 hp 300 at (x y z) in r209` (ein Gegner erscheint, `be_flag` Bit 0),
    `em: - s12 … after N frames` (er ist weg); beim Betreten eines Raums alle seine Gegner.
    Für die Burghalle (Nachrücken, Verschwinden).
  - `em: hit s12 0x1a/3 hp 300 -> 100 part 5 wep 0x10 r 1/16/0/0` mit getroffenem Teil
    (`dmg.m_pDamageYarare->parts_no`, 5 = Kopf) und Waffe (`dmg.m_Wep`), bei Ganados (ids
    0x10–0x1F) dazu `Be_flg`, Waffenart, Klasse, Parasit und `cEm::flag`, also alles, wonach
    `em10DmSetWep*`/`em10LostHead` über den Kopf entscheiden; danach 90 Bilder lang jeder
    Routinenwechsel (`em: s12 r 2/4/0/0`) und `em: s12 head lost` (Bit 0x80 von `Be_flg`). Für
    den Kopfschuss im Ritual-Raum.
  - `em: s12 hidden` / `shown` (`be_flag` Bit 1, höchstens 20 je Leben) und `em: s12 jumps
    (…) -> (…)` (mehr als 3000 in einem Bild).
  - `pad: menu frame N rep 0x… stick (x y) c (x y) buttons 0x…`: jede Cursor-Bewegung in einem
    Untermenü (Inventar, Händler, Karte) mit ihrer Ursache, Steuerkreuz (0x0000000f), linker Stick
    ab 30 (0x000f0000) oder C-Stick ab 10 (0x00f00000), und den Werten beider Sticks. Für die
    springende Auswahl beim Waffenverbessern.
  - Mengen: höchstens 1500 Gegner- und 600 Menü-Zeilen je 1800 Bilder (etwa eine Minute), danach
    eine Zeile „… left out“; keine Obergrenze je Lauf, die Datei selbst hat 2 × 4 MB. Kosten: ein
    Durchgang über die Gegner-Plätze je Bild (Vergleiche, keine Ausgabe ohne Änderung).
  - **Neuer Laufzeittest `enemylog`** (`game_probe.cpp` `re4probe_enemy_log`): zwei künstliche
    Gegner-Plätze in MEM1; erwartet werden die acht Zeilen von Erscheinen, Kopftreffer,
    Routinenwechsel, Kopfverlust, Verstecken, Sprung, Verschwinden und C-Stick im Untermenü.
  - **Geprüft (Cloud):** `whole_build.sh i686 armhf aarch64`: 0 Baufehler, alle Prüfungen
    bestanden, `runtime-test enemylog: ok` auf allen drei. **Nicht getestet:** auf dem Gerät (Menge der Zeilen in einer
    langen Sitzung).

### Todesschleife in r21d: die Insekten stehen bei NaN (Tester-Bericht 22.10.2026, Branch `claude/re4-start-controls`)

- **Bericht** (`cubeevil4-report-20261022-030337.zip`, Samsung SCV40, Android 10, **64 bit im
  App-Prozess**, 0.41.2, Sprache Indonesisch; vom Projektinhaber als „El Gigante“ geschickt):
  r220 → r223 → r224 (El Gigante, em2b, zweimal gestorben, Weitermachen normal) → r21d
  (Novistadore em2d, die Stampfer). In r21d: Tod um 02:54:28 (drei Sekunden vorher die einzige
  Zeile `LIMIT_ANGLE: angle nan is not finite`, die nur einmal je Lauf kommt), Weitermachen normal;
  Tod um 03:02:46, danach **nach jedem Weitermachen 40 ms nach dem Raumstart sofort wieder
  `--DIEDEMO START!!`** (03:02:56, 03:03:05, 03:03:32), bis der Tester den Bericht schickte.
  Nach dem zweiten Tod lud das Weitermachen die Handgranate (`wep19`), beim Betreten von r21d
  und nach dem ersten Tod die Pistole: der Zwischenstand stammte vom Gitter-Schalter
  (`r21d_checkFence` speichert mit `GameSave.save(pSaveData, -1)` den Platz, nachdem die
  Stampfer losgelaufen sind). Ab da laufen die Stampfer schon beim Raumstart
  (`r21d_initFence`), und `R21D_TRAP_HIT` tötet Leon, sobald die Szenenfläche `hitAtNo` eines
  Stampfers in diesem Bild getroffen ist, **von wem auch immer** (`SceAtCheck` prüft Spieler,
  Partner und Gegner; Novistadore 0x2D sind nicht ausgenommen). Die alten Systemabstürze in
  `crash-system.txt` (cSubLuis, getNearPoint, ScenarioMove) sind von früheren Versionen.
- **Ursache:** Der Wandschritt der Novistadore (`em2dSetWallMatrix2`, gleich in em2c, dem
  Insekten-Boss in r221) rechnet `sum.y = sinf(acosf(PSVECDotProduct(&nrm, &w->wallNrm))) * 700`
  und addiert das zur Position. Beide Vektoren sind Einheitsvektoren derselben Fläche. Auf der
  GameCube macht `PSVECNormalize` (`frsqrte` und ein Newton-Schritt) sie etwas **zu kurz**, ihr
  Skalarprodukt bleibt unter 1. Der Port nimmt `C_VECNormalize` (`1/sqrtf`, richtig gerundet),
  das Ergebnis ist manchmal ein letztes Bit **zu lang**, das Skalarprodukt 1,0000001,
  `acosf` NaN, die Position des Insekts NaN. Versuch (`gcc`, 2 Mio. zufällige Flächen, alle vier
  Messpunkte auf derselben Fläche): NaN bei **30,1 %** mit den Funktionen des Ports, **0,2 %**
  mit nachgebildetem `frsqrte` (Tabelle aus Dolphin, größter Fehler der Schätzung 2^-12,4) und
  dem Newton-Schritt; schon die Neigung (0,1 1 −0,05) trifft es. Eine Position aus NaN liegt in
  **jeder** Vierecksfläche (`areaHitCheck_xz4` weist nur ab, alle Vergleiche mit NaN sind
  falsch), also auch in den Stampfer-Flächen: Leon stirbt im ersten Bild, in dem ein Insekt
  sich auf eine schräge Fläche setzt, nach dem Weitermachen also gleich am Anfang. Auf der
  GameCube würde ein Insekt bei NaN `LIMIT_ANGLE` endlos laufen lassen; dass das Spiel dort nicht
  hängt, passt zu den kurzen Einheitsvektoren. Dass die Stampfer-Flächen Gegner zählen, ist aus
  dem Bericht nicht belegt (die Flächendaten liegen nur auf der Disc); belegt sind NaN-Winkel,
  Zwischenstand und Schleife.
- **Behoben:** `whole_prepare.py` schreibt alle 42 `acosf(`/`asinf(` des Spielcodes zu
  `re4_acosf(`/`re4_asinf(` um (Prelude): ein Wert höchstens 1e-5 jenseits von ±1 gilt als ±1
  (das Insekt rückt 0 statt etwa 0,2 Einheiten ab), weiter draußen bleibt NaN wie im Original.
  Gewählt statt eines nachgebildeten `PSVECNormalize`, weil es nur Werte ändert, die sonst NaN
  wären; das nachgebildete `frsqrte` (näher an der GameCube, ändert aber jede Normalisierung)
  bleibt eine Möglichkeit für später. Jeder Eingriff zählt mit (`math: acosf/asinf of 1.00000012
  taken as 1 (N times so far …)`, beim ersten Mal und bei jeder Zehnerpotenz).
- **Mehr im Fehlerprotokoll** (`runtime/state_trace.cpp`, immer an):
  - `room: r21d runs (frame N), Leon at (x y z) facing a, life 1200/1200, weapon 0x02[, continue
    after a death]`: jeder Raumstart, mit dem Zwischenstand nach einem Tod.
  - `die: r21d after N frames, life L[, partner life P], Leon at (x y z); scene areas hit: 0x05 …`
    und je getroffener Fläche (höchstens 8) `die: area 0x05 id … on … trg …: Form, for 1|2|8[,
    moves with a model]`: wodurch ein Raumskript tötet.
  - `pl: Leon's position is not a number in r21d (…)` und `em: s12 position is not a number,
    last at (x y z)`.
  - Die Zeilen „sub screen open/closed“ und „player hit“ hörten nach je 40 je Lauf auf (im Bericht
    um 02:38 schon verbraucht); jetzt höchstens 120 je Minute wie die Gegnerzeilen.
  - `run: area …` (Flächenliste der PC-Prüfung) nennt jetzt auch, wer die Fläche auslöst (`for`).
- **Tests:** Laufzeittest `deathlog` (`game_probe.cpp` `re4probe_death_log`: künstlicher Spieler und
  eine Fläche in MEM1, Raumstart, Tod mit den Flächen 0x05 und 0x13, Leon bei NaN, Weitermachen,
  ein Gegner bei NaN) und `arc` (`re4probe_arc`: der Wandschritt mit den Vektorfunktionen des
  Ports und der Neigung (0,1 1 −0,05): Skalarprodukt über 1, Ergebnis eine Zahl; knapp über ±1
  ergibt 0, π, π/2; 1,001 bleibt NaN; 0,5 unverändert; drei bzw. vier Eingriffe gezählt).
- **Geprüft (Cloud):** `whole_build.sh i686 armhf aarch64`: 0 Baufehler, `whole_prepare`: 42 Aufrufe umgeschrieben, keiner übrig; `runtime-test deathlog: ok` und `arc: ok` auf allen drei (das Skalarprodukt über 1 tritt auf allen drei Zielen auf), alle Laufzeit-, Kern- und Kernprozess-Tests 0 Fehler. **Nicht getestet:** r21d auf dem Gerät (Gitter heben, Stampfer, Weitermachen) und ob die Stampfer-Flächen Gegner zählen (zeigt die `die: area`-Zeile beim nächsten Tod).

### Bericht Anbernic RG 477V (0.42.1, 22.10.2026 15:17): schwarze Ränder auf einem 4:3-Bildschirm, Shader auf Mali; Release 0.43.1-alpha

- **Gerät:** Anbernic RG 477V (MediaTek k6897 = Dimensity 8300, Mali-G615 MC6, Treiber r44p1),
  Android 14, 64 bit im App-Prozess, Bildschirm **1280×960 (4:3)**, 60/90/120 Hz (die App nimmt 60).
  Kein Absturz (`crash.txt` leer), r100 flüssig (Renderer 6–7 ms je Bild, Kern 11–12 ms Rechenzeit,
  30 Bilder/s). Der Tester hat im Menü zwischen 4:3 und 16:9 gewechselt (15:11:43–15:12:20) und die
  Auflösung kurz auf 1× gestellt.
- **Schwarze Ränder oben und unten (Bildschirmfoto, Einstellung „4:3 (Original)“):** das ist das
  Original. Das Spiel malt am Ende jedes Bildes zwei schwarze Balken über die Zeilen 0–56 und
  393–449 seines 512×448-Bildes (`cinesco.cpp` `Draw_cinesco`, aufgerufen in `main.cpp:186`, nur
  der Debug-Schalter `DBG_CINESCO_OFF` lässt sie weg); auf einem 4:3-Fernseher sah RE4 so aus.
  „16:9“ schneidet genau diese Zeilen 56..391 aus, auf einem 4:3-Bildschirm kommen die Ränder dann
  von der App; „Gestreckt“ zieht sie über den ganzen Bildschirm (Figuren zu schmal).
  **Befund für eine Lösung:** unter den Balken liegt echtes Bild. Im Spiel (ohne Ereignis) zeichnet
  das Spiel die ganze Höhe (Projektion 4:3, `camera.cpp:98`; Schere nur, wenn `SYS_SCISSOR_ON`
  gesetzt ist: in Ereignissen `SceEventStart`, `sce_com.cpp:124`, und in r10b/r11b). Ein Bildformat
  „4:3 ohne Balken“ (Balken nur im Spiel weglassen, in Ereignissen und Menüs wie bisher) wäre klein;
  siehe nächster Abschnitt.
- **Shader der Brücke auf Mali:** `room 100: 0:14: S0022: Symbol 'refract' redeclared` beim
  Neuaufbau des Renderers (Auflösung umgestellt). Der Raum-Shader der Zustandsbrücke
  (`scene_gl.cpp`) hatte ein Uniform `refract`, den Namen einer eingebauten GLSL-Funktion; Adreno
  und Mesa nehmen das hin, Mali (und der GLSL-Referenzübersetzer) nicht. Im GX-Bild ohne Folgen
  (die Brücke zeichnet nur, solange GX nichts zeigt), behoben: `refractOn`.
  Neues Werkzeug **`port/android-decomp/tools/check_shaders_strict.sh`**: baut
  `tools/shader_dump_shim.c` (per `LD_PRELOAD` in die Host-GL-Prüfungen der App, schreibt jeden
  Shader aus `glShaderSource`) und lässt jeden Shader durch `glslangValidator` (Paket
  `glslang-tools`) übersetzen. Ergebnis: 165 verschiedene Shader (GX-Programme aus `gx_gl_check`,
  Brücke, Tiefenschärfe, Helligkeit, Effekte), 0 abgelehnt. Gegenprobe: der Raum-Shader mit dem
  alten Namen wird abgelehnt, in Zeile 14 wie auf dem Mali. `scene_gl_check`, `actor_gl_check`,
  `environment_gl_check`, `m33_subscreen_gl_check` brauchen Disc-Daten (in der Cloud übersprungen);
  `r100_distance_gl_check` baut mit clang 18 nicht (C++20-Erweiterung, alter Fehler, nicht im Skript).
- Nebenbefund: nach dem Neuaufbau des Renderers zählt die GX-Statistik etwa 3 fehlende Texturen je
  Bild für rund 3 Sekunden (282 in 94 Bildern, danach 21, dann keine): EFB-Kopien, die das Spiel
  liest, bevor es sie im neuen Renderer wieder geschrieben hat (dann weiße Textur). Nur nach einem
  Wechsel der Grafikoptionen; nicht behoben.
- **Gegner-Zeile mit Dynamit-Merkmal** (Wunsch aus der Dynamit-Frage, `state_trace.cpp`): beim
  Erscheinen jetzt `em: + s12 0x1a/3 list 5 set 0x24 f 0x00020000 hp 300 at (x y z) in r209`:
  Listeneintrag (`emset_no`, bisher „set“ genannt), `cEm::set` und `cEm::flag` aus der Gegnerliste
  (bei Ganados Bit 0x20000 Dynamit in der Hand, 0x2000 am Gürtel, 0x10000 unbegrenzt). Ein Bericht
  aus r104 beantwortet die Dynamit-Frage damit allein. Tests `enemylog`/`deathlog` angepasst.
- **Release 0.43.1-alpha** (versionCode 69): 0104, Mali-Shader, Gegner-Zeile; Releasetext
  `port/android-decomp/docs/releases/v0.43.1-alpha.md`.
- **Geprüft (Cloud):** `whole_build.sh i686 armhf aarch64`: 0 Baufehler, alle Laufzeit-, Kern- und
  Kernprozess-Tests 0 Fehler (`enemylog: ok` auf allen drei); strenge Shader-Prüfung 0 abgelehnt.
  **Nicht getestet:** APK nicht gebaut, nichts auf dem Gerät.

### Bildformat „4:3 ohne Balken“ (nach dem Anbernic-Bericht, 22.10.2026, Cloud)

- **Neu im App-Menü:** Bildformat jetzt „4:3 (Original)“, **„4:3 (ohne Balken)“**, „16:9“, „Gestreckt“
  (gespeicherter Wert 3, `ingame_43_no_bars` in allen zehn Sprachen). Gezeigt wird wie bei 4:3 das
  ganze Bild (448 Zeilen in einem 4:3-Rahmen von 480, auf 1280×960 also oben und unten je 32 Pixel
  frei wie bisher); dazu sagt die App dem Kern im Zeichen-Thread, wenn sich das Format ändert,
  `re4_core_set_letterbox(1)` (im getrennten Kernprozess über `kSetLetterbox`).
- **Kern:** Patch 0105 (`main.cpp`): `Draw_cinesco` fällt weg, solange der Schalter an ist und der
  Spieler spielt (Hauptschleife `Rno0` 3, kein Untermenü, kein Ereignis, kein `SYS_SCISSOR_ON`).
  **Diese erste Fassung reichte nicht** (Retroid 18:18: wie „4:3 (Original)“): `SYS_SCISSOR_ON` ist im
  Spiel immer an; die zweite Fassung lässt auch die Schere weg (Abschnitt „Gerätetest 22.10.2026 18:18“).
  In Ereignissen schneidet das Spiel das Bild auf die Zeilen zwischen den Balken zu
  (`SceEventStart` setzt `SYS_SCISSOR_ON`, `Render` die Schere 0,56..), außerhalb wäre dort nichts
  gezeichnet; darum bleiben die Balken dort, im Untermenü (Koffer, Karte, Händler) und in r10b/r11b
  wie im Original. Beim Wechsel zwischen Spiel und Ereignis erscheinen/verschwinden die Balken ohne
  Überblendung. Die Anzeigen (Lebensleiste, Munition) bleiben, wo das Spiel sie hinsetzt (im Band).
- **Risiken (erst am Gerät zu sehen):** Dinge, die das Spiel nur zwischen den Balken erwartet:
  Effekte, die per Schere auf das Band beschränkt sind (`filter09` Pause-Filter, `filter0b`
  Überblendung der Untermenüs; beide nur beim Öffnen/Schließen von Menüs), Modelle, die oben oder
  unten zu früh verschwinden (Sichtprüfung des Spiels), Kanten von Räumen.
- **Tests:** Kern-Test `pause` prüft `re4_letterbox_off` (aus, an, aus); Kernprozess-Test:
  `re4ipc_set_letterbox(1)` dann `(0)` kommt im Kernprozess an (`remote-probe: letterbox changes 2
  (now 0)`). Alle Java-Quellen der App übersetzt gegen `android-all` 15 und RetroTouch (0 Fehler),
  `core_mode.cpp` für den Host und armv7a ohne Fehler (aarch64 scheitert an der fehlenden
  Ersatzdatei `android/dlext.h`, schon vorher), Übersetzungsprüfung alle ok.

### 60 FPS (experimentell): Zwischenbilder im GX-Renderer (Entscheidung des Projektinhabers, 22.10.2026, Cloud)

Das Spiel rechnet weiter 30 Bilder/s; die App zeigt zwischen zwei Spielbildern ein berechnetes
Zwischenbild. Kein Eingriff in `src/` oder den Kern. Entwurf nach einer Analyse des Renderers
(Abschnitt „60 Bilder/s mit Zwischenbildern … nur Analyse“).

- **Schalter:** App-Menü „60 FPS (experimentell)“ unter den Grafikoptionen (`ingame_smooth60`, zehn
  Sprachen), gespeichert als `smooth60`, beim Start und bei jeder Änderung `CoreBridge.setSmooth`
  (`optSmooth` in `core_mode.cpp`). Aus: alles wie bisher.
- **Renderer** (`gx_gl.cpp`, `GxRenderer::SetRecording`/`Interpolate`):
  - Jeder echte `Run` notiert je Zeichnung: Kennung (`DrawSig`: Shader-Programm, Primitiv, Ecken-Layout
    und -Zahl, Matrix-Indizes, Schlüssel der benutzten Texturen; Kopien nach Adresse), die
    Positions- und Normalenzeilen, mit denen sie gezeichnet wurde (3 Zeilen, 30 bei Matrix-Indizes je
    Ecke), die Projektion und die Lage (Verschiebung der Positionsmatrix im Kameraraum).
  - `Interpolate(prev, next, t)` spielt `next` ein zweites Mal ab. Jede 3D-Zeichnung sucht ihre
    Vorgängerin (gleiche Kennung; die erste noch freie, wenn sie nah genug liegt, sonst von den ersten
    acht die nächste; „nah genug“: weniger als ein Viertel ihrer Entfernung plus 400 Einheiten).
    Gefunden: Positions- und Normalenzeilen sowie Projektion werden zwischen beiden gemischt; haben
    sich die Ecken selbst bewegt (die Figuren skinnt das Spiel auf der CPU), auch Positionen und
    Normalen der Ecken (nicht bei mehr als 1500 Einheiten Abstand). 2D-Zeichnungen, Farben, Licht,
    Nebel und Texturmatrizen bleiben wie in `next`. Nach jeder Zeichnung kommen die Zeilen aus dem
    XF-Speicher zurück.
  - Danach steht der Renderer **genau wie nach `prev`**: BP-, XF-Register, TEV-Farben, gebundene
    Texturen, Paletten, Perspektive und der Autofokus werden zurückgesetzt; das Zwischenbild kopiert
    das EFB in eigene Texturen (Effekte, die das letzte Bild einblenden, lesen danach weiter die echten
    Kopien), vergisst keine Texturen, setzt nicht neu auf und zählt nicht als Bildstück; was es
    hochlädt, lädt `Run(next)` nicht noch einmal.
  - **Kein Zwischenbild** (dann `next` sofort): Bildstück, das neu aufsetzt, angehalten, schwarz oder
    markiert ist; ohne Folgenummer; ohne 3D-Zeichnungen (Filme, Titel, 2D-Menüs); **Schnitt**: weniger
    als die Hälfte der 3D-Zeichnungen gefunden oder mehr als 30 % davon um mehr als 20° gedreht.
- **Takt** (`drawGx`): kommt Bildstück N+1, zeigt die App beim selben Bildschirmtakt das Zwischenbild
  und hält N+1; beim nächsten Takt läuft N+1. So entstehen 60 verschiedene Bilder je Sekunde, alle
  etwa 16,7 ms später als bisher. N bleibt bis zum Zwischenbild gehalten (seine Ecken); höchstens
  zwei Plätze der Warteschlange. Bei Aufnahmen (GX-Dumps, R3-Folgen, Treffer-Bilder) kein
  Zwischenbild; neuer Renderer, neue Fläche, Abzug: gehaltene Bildstücke zurück (`smoothDrop`).
  **Abschaltung:** liegt die GPU im Schnitt mehr als 2,5 Durchläufe zurück (Zäune nach jedem Lauf,
  geprüft mit jeder GX-Protokollzeile), aus bis zum nächsten neuen Renderer; Zeile
  `60 FPS: the GPU does not keep up …`. Im Protokoll regelmäßig `60 FPS: N in-between pictures, M
  cuts, … 3D draws matched so far; the GPU x Runs behind`.
- **Kosten:** jedes Spielbild wird zweimal gezeichnet (doppelte GPU-Arbeit; Retroid bei 3× zu viel,
  bei 2× wohl machbar), etwas mehr Arbeit im Zeichen-Thread; +16,7 ms Verzögerung.
- **Tests (Cloud, llvmpipe):** `gx_gl_check` neu `TestInterpolation`: grüne Karte über ihre Matrix,
  rote über ihre Ecken bewegt → im Zwischenbild beide genau auf halbem Weg; danach `Run(B)`
  pixelgleich mit einem Renderer ohne Zwischenbild; 90° gedreht → Schnitt; t = 1 → das Bild B; ohne
  Notizen oder ohne Folgenummer → nichts. `gx_chunk_render` mit `RE4_GX_INTERP=1`
  (`RE4_GX_INTERP_T`): vor jedem Folgebild das Zwischenbild als `<name>-zw.ppm` und eine Zeile
  (gezeigt/Schnitt, Zuordnungen); mit drei künstlichen Aufnahmen: Karte bei 264 → 272 (zw) → 280 →
  288 (zw) → 296. Alle `gx_gl_check`-Prüfungen 0 Fehler, `core_mode.cpp` für Host und armv7a ohne
  Fehler.
- **Nicht getestet:** echte Spielbilder (dafür R3-Folgen vom Retroid, `pull-gx-dumps.ps1`, dann in der
  Cloud `RE4_GX_INTERP=1 gx_chunk_render`), Wirkung und Kosten auf dem Gerät, Schnitte im echten
  Spiel (Schwellen eventuell nachstellen).

### „Force Performance“ im App-Menü und R3 für die Touch-Steuerung (Wünsche des Projektinhabers, 24.10.2026, Cloud)

- **Force Performance** (Grafik, nach der Auflösungszeile): „Default“ (Vorgabe, Entscheidung des
  Projektinhabers: Android verteilt die Threads wie vor 0.45) oder „Use performance cores“ (Spiel-
  und Zeichen-Thread auf den schnellen Kernen, Androids Leistungshinweis für Spiel-, Vertex- und
  Zeichen-Thread, alle 30 Bilder zurück auf die schnellen Kerne). Darunter der Hinweis: mehr
  CPU-Leistung, das Gerät wird wärmer. 10 Sprachen (`ingame_force_performance`,
  `ingame_perf_default`, `ingame_perf_cores`, `ingame_perf_help`; `check_translations.py`: 82 Texte
  je Sprache). Gespeichert als `fastCores` (0/1) in den Grafik-Einstellungen, ohne neuen Renderer.
  Weg: `CoreBridge.setFastCores` → `core_mode.cpp` (`optFastCores`; der Zeichen-Thread setzt sich
  bei der nächsten Run um und sagt es dem Kern) → `re4_core_set_fast_cores` (`core_api.cpp`, auch
  über den eigenen Kern-Prozess: `kSetFastCores`) → `re4_gx_set_fast_cores` (`gx_decode.cpp`; der
  Spiel-Thread setzt sich an der nächsten Spielbild-Grenze um). Zurück auf „Default“:
  `re4cpu::ReleaseCores` (alle Kerne) und der Leistungshinweis geschlossen. Zeilen `core: Force
  Performance on/off`, `cores: … left to Android (Force Performance: Default)`.
- **R3 in der Touch-Steuerung:** Aktion `r3` (Beschriftung „R3“, grau) bei RetroTouch angemeldet,
  nicht im Standard-Layout: der Editor (Zahnrad) fügt sie hinzu. Ein Druck löst wie R3 am
  Controller die Grafik-Aufnahme aus (`CoreActivity.capture`, nur mit „R3 = Graphics capture“).
- **Tests:** `CoreTouchTests` 267 Prüfungen bestanden (R3: kein GameCube-Knopf, Meldung an die App,
  nicht im Standard-Layout, Platz unter RetroTouchs 16 Knöpfen); `CoreInputTests` 141; Java der App
  übersetzt ohne Fehler; i686: 0 Baufehler, alle Tests 0 Fehler (auch der eigene Kern-Prozess);
  App-Linkprüfung ohne Fehler. **Am Gerät nicht getestet.**

### 0.90.1-beta auf dem Branch `beta-optimisations` (Entscheidung des Projektinhabers, 24.10.2026, Cloud)

- **Veröffentlicht** hat der Projektinhaber die Beta als **0.90.0-beta** (Tag `0.90.0-beta` =
  `97c5bf6`, 24.10.2026 00:31 UTC; im Repository hieß sie `0.9.0-beta`). Releasetext jetzt
  `v0.90.0-beta.md` mit dem veröffentlichten Inhalt (4266 Shader).
- **Nächste Version 0.90.1-beta** (versionCode 74, Releasetext `v0.90.1-beta.md`), Arbeit dafür auf
  dem neuen Branch **`beta-optimisations`** (Wunsch des Projektinhabers): alles nach dem Tag, also
  Shader-Liste 8068, Zeile für zu kurze Disc-Dateien, Threads zurück auf die schnellen Kerne.

### Berichte realme RMX3710 (0.46.0, Mali-G52) und RMX3741 (0.9.0-beta, Mali-G68), 24.10.2026: Threads auf den langsamen Kernen (Cloud)

- **Berichte:** RMX3710 (Android 15, 8 Kerne: 0–5 bis 1,8 GHz, 6–7 bis 2,0 GHz; Stufe 1) in r400/r404,
  der Kern braucht 36–46 ms je Spielbild (unter 30 Bilder/s); RMX3741 (Android 15, 0–5 bis 2,0 GHz,
  6–7 bis 2,6 GHz; Stufe 2/Dynamisch) im Dorf, 60 gezeigte Bilder/s. Kein Absturz in den Sitzungen;
  `crash-system.txt` des RMX3710: `sceInLock` vom 22.10. 22:43 (ältere Version, Patch 0101).
- **Gefunden:** beide setzen den Spiel-Thread beim Start auf die Kerne 6–7 (`cores: the game thread
  on cores 6 7`), die Zeile `GX core` zeigt ihn eine bis zwei Minuten später zu 90–99 % auf den
  langsamen Kernen (RMX3741: 26–30 ms Rechenzeit je Bild von 33,3 ms; RMX3710 ohne Menü
  dazwischen). Android setzt die Kern-Zuordnung eines Threads zurück, wenn es die App zwischen seinen
  Kern-Gruppen (cpusets) verschiebt.
- **Geändert:** `re4cpu::KeepOnFastCores` (`re4_cpu_info.h`): der Thread merkt sich die Kerne von
  `KeepOffSlowCores`; Spiel-Thread (`gx_decode.cpp`, je Spielbild-Grenze) und Zeichen-Thread
  (`core_mode.cpp`, je Run) prüfen sie alle 30 Bilder (`sched_getaffinity`) und setzen sie neu, wenn
  Android sie geändert hat; Zeile `cores: the game/render thread put back on its cores (… N times so
  far)`, höchstens viermal. i686: 0 Baufehler, alle Tests 0 Fehler; App-Linkprüfung ohne Fehler.
  **Am Gerät nicht getestet.**
- **Shader-Liste:** **8068 Programme** (194 neu).

### Bericht Xiaomi 2109119DG (0.46.0, 23.10.2026 22:15, Insel r316–r321): Hänger beim Laden von r325; Vibration bestätigt (Cloud)

- **Vibration:** der Projektinhaber bestätigt sie im Spiel (Änderung `rumbleTick`, Stand `ea1aea9`).
- **Bericht:** Xiaomi 11 Lite 5G NE (Android 14, 64 bit, Kern im App-Prozess), noch 0.46.0-alpha,
  Stufe 2, gestreckt. Sitzungen 17:33–20:01 (r220 … r317) und 21:10–22:15 (r316 … r321).
- **Hänger 22:12:48 beim Wechsel r321 → r325 (Tür):** `st3/r325.das` (Disc 2) gelesen, Teil 1 (Yz2,
  0x24363E Byte) richtig, danach 31 Einträge der Teile-Tabelle `MRAM, Größe 0` (alle Nullen, Typ 0
  = `TRANS_MRAM`) und kein Ende-Eintrag: das Spiel liest Teile der Größe 0, dann 8 s kein Bild
  (`cDvdQueue::readMain`). Auf dem PC lief der Rundgang über alle Räume (r325 eingeschlossen) mit
  denselben Disc-Daten; die App prüft beim Import jede Datei gegen die Referenzliste. Ursache
  offen. Neu (`runtime/dvd.cpp readFile`): liest der Port innerhalb der Dateilänge weniger Bytes, als
  die Disc nennt (Datei auf dem Gerät kürzer), steht eine Zeile `DVD: … only N of M bytes … import
  the disc again` im Protokoll (höchstens 8). i686: 0 Baufehler, alle Tests 0 Fehler.
- **Älterer Absturz 23.10. 17:33 (0.46.0, kein Protokoll der Sitzung):** `ScenarioMove` sprang an
  0xC8003E4C (keine Funktion); offen, nur aus `crash-system.txt` bekannt. Die übrigen Abstürze dort
  (20./21.10.: `cSubLuis::move`, `cEmMark::move`, `RefractShaderSetup`) stammen aus älteren Versionen.
- **Shader-Liste:** **7874 Programme** (3608 neu, Insel r300–r321), alle bauen auf Mesa ohne Fehler
  (mit und ohne MSAA; 6760 verschiedene Quellen). Erster Start: der Hintergrund-Thread baut sie in
  einigen Minuten (SCV40 etwa 28 ms je Programm), der Releasetext sagt das.

### Gerätetest 24.10.2026 (Retroid, 0.9.0-beta neu gebaut, 5 Berichte, 5 R3-Folgen; Cloud)

- **Bestätigt (Projektinhaber):** Ganados schreien beim Fallen (Patch 0109), QTEs per Tipp gehen,
  Ashley still in r22c (Patch 0110).
- **Vibration im Spiel nicht spürbar:** der Kern meldet sie (`pad: rumble from frame …` nach jedem
  Schrotflinten-Schuss, Tabelle `CORE_VIBRATION` Eintrag 0, umgewandelt), ein Schuss lässt den
  Motor aber nur 3–5 Spielbilder laufen. Die App machte daraus einen schwachen Ruck (Stärke 70 +
  185 · Anteil, beim Ende sofort `cancel`), nur das lange Rütteln im Optionsmenü war zu spüren.
  **Geändert (`CoreActivity.rumbleTick`):** Stärke 150 + 105 · Anteil der letzten vier Spielbilder,
  jeder Ruck läuft seine 120 ms aus (kein Abbruch, wenn der Motor endet), eine stärkere Stufe
  startet sofort, eine schwächere wartet. Java-Übersetzung ohne Fehler; **am Gerät nicht getestet.**
- **r118, Planken des Holzwegs fehlen, Leon und Ashley laufen in der Luft:** nur über die
  Raumauswahl des Debug-Menüs besucht. `R118Init` blendet je nach `SCF_R117_FIND_ASHLEY` die
  Raumteil-Gruppe 0x26 (Ashley gefunden) oder 0x1B aus (`SmdSetTrans(…, 0)`); welche Teile das
  sind, zeigt nur eine R3-Folge an der Stelle. Keine Fehlermeldung des Spiels im Bericht. **Offen:**
  R3-Folge und Bildschirmfoto in r118, Vergleich in Dolphin mit demselben Spielstand.
- **Lichtschein durch Wände (R3-Folgen r20b, r204):** die Lichter jedes Zeichenaufrufs aus den
  Folgen gelesen: die bläulichen Lichter des Händlers (Farbe F0FFFF) sind Spots mit
  Winkel-Abschwächung cos (0, 1, 0), Entfernung 1 / (1 + d/2770) bzw. 1 / (1 + d/4808), gespeicherte
  Richtung nach unten, also nach oben strahlend; das Spiel gibt sie Raumteilen in Reichweite (Treppe
  r204), ohne Sichtprüfung. Wie zuvor: sehr wahrscheinlich Original, Gegenprobe in Dolphin offen.
- **Shader-Liste:** **4266 Programme** (851 neu), alle bauen auf Mesa ohne Fehler (mit und ohne
  MSAA). Keine Abstürze, keine Fehlermeldungen in den Berichten.

### Quick-Time-Events per Tipp (Wunsch des Projektinhabers, 24.10.2026, Cloud)

- **Wunsch:** ohne Controller sind die kurz aufblitzenden Tastenpaare kaum zu treffen; ein Tipp auf
  die eingeblendete Grafik soll das QTE bestehen.
- **Umsetzung (`runtime/qte_touch.cpp`, kein Patch):** das Spiel zeigt QTEs über den Aktionsknopf
  (`ActBtn`, `DISP_FLAG` im ersten Byte von `Cckpt.m_ActBttn`) und prüft die Tasten in
  `cActionButton::checkButton`. Jeder Tipp auf das Bild (`re4_core_touch`, die App gibt nur Tipps
  neben den Touch-Tasten weiter) geht auch an `re4_qte_tap`; steht ein QTE-Symbol und ist der
  Aktionsknopf aktiv (kein Untermenü), drückt ein Bild-Haken (PADRead, `re4_pad_add`) seine Tasten
  zwei Spielbilder lang: L+R, A+B, A (schnell drücken, Akzent, auch das Losreißen `DISP_GACHA`, das
  A mitzählt), B (auch schnell), X, Y, Z, L, R. Jeder Tipp ist ein Druck (Hämmern = mehrmals
  tippen). Das einfache A (Türen, Leitern, Tritte; die Touch-Tasten haben A) und die Stick-Symbole
  (Jetski, Hebel) bleiben bei den Touch-Tasten. Ein Tipp gilt überall auf dem Bild, das Symbol
  eingeschlossen. Zeile `qte: a tap pressed L+R for the prompt (kind 3), N taps so far` (je Art
  einmal).
- **Test:** Laufzeittest `qtetouch` (`game_probe.cpp` `re4probe_qte_touch`): L+R zwei Bilder lang,
  dann nichts; A+B; drei Tipps beim Hämmern = drei Drücke; nichts beim einfachen A, im Untermenü,
  ohne aktiven Knopf; die Tabelle. i686: 0 Baufehler, alle Tests 0 Fehler, libc++-Prüfung 170/170.
  **Nicht getestet:** am Gerät (z. B. Ausweichen vor dem Felsen r101, Messerkampf r317).
- **Releasetext 0.9.0-beta** um die Korrekturen seit `caae87d` ergänzt (Patches 0109/0110, Lichter
  in 60 FPS, QTE per Tipp, 3415 Shader); 0.9.0 wird neu gebaut (Wunsch des Projektinhabers).

### Berichte Retroid 23./24.10.2026 (0.9.0-beta, 14 Berichte, Dorf bei Nacht bis Burg): sechs Meldungen (Cloud)

- **Sitzung:** 22:55–00:32 in einem Stück (r112 … r10f, r11f, dann Burg r200 … r22c, r209, r20a,
  r20b), kein Absturz. Shader-Liste mit den Berichten **3415 Programme** (1487 neu), alle bauen
  auf Mesa ohne Fehler (mit und ohne MSAA). Der Shader-Thread baute nach dem Wechsel der MSAA-Stufe
  1449 Programme in 46 s im Hintergrund. Die Zeile `sound: up to …` (Patch 0107) kam nie: der
  Anfrage-Speicher war nicht die Ursache der stummen Schreie.
- **Gegner schreien beim Fallen nicht:** Fehler des Prototyps. Der Fallschrei (`Se_tbl[14]`)
  kommt nur aus `Dm_Roof` (2/7) und `Dm_Ladder` (2/6); in `Dm_Roof` führt nur `em10RoofDmCk`, das
  die Schadens-Behandlung der schweren Waffen (`em10DmSetWep09`: Schrotflinten, Magnum, Gewehre)
  nie fragt; sie schickt in `Dm_Blow`, das stumm über die Kante fällt. Im Bericht: 198
  Schrotflinten-Treffer, alle 2/4; die vier Seilbahn-Ganados in r10f (Sätze 0x2C/0x2D/0x31) fielen
  bis 6 s lang stumm, kein Ganado in 2/7. **Patch 0109.**
- **Ashley ruft „Help“ im Vorraum der Schießbude:** Fehler des Prototyps. r22c stellt Ashley
  (wartend) neben die beiden Tore der Bude, die als Zielscheiben (`cEmMark`, em 0x3E, hp 1000)
  als Gegner unter 4 m zählen; sobald Leon mehr als 2 m weg ist, ruft sie alle 151 Bilder.
  **Patch 0110.**
- **Ashley ruft „Pervert“ ohne Grund:** Original. Die Prüfung (`pantsCheck`, `pl_npc.cpp`) ist rein
  geometrisch: Leons Kopf unter 5 m, Blick mindestens 45° nach oben, Ashley innerhalb ±45° seiner
  Körperrichtung, keine Wand dazwischen; keine Kamera, kein Stick. Unter einer Terrasse (r208)
  reicht es, unter ihr zu stehen.
- **Kamera in Leons Kopf:** Original. Die Zielkamera (`CameraQuasiFPS::hitCheck`) setzt sich bei
  einer Wand auf den Treffpunkt, im Extremfall 10 cm neben den Kopf; das Spiel blendet Leon nie aus.
- **Fackel des Händlers leuchtet durch Wände:** sehr wahrscheinlich Original. Lichter wählt das
  Spiel je Raumstück nach Kasten und Reichweite (`cLightMgr::setModel2`), ohne Sichtprüfung; der
  GameCube hat keine Schatten. Umwandler (LIT, SMX `LitSelectMask`) und Renderer (Licht-Register,
  Masken, Abschwächung wie Dolphin) geprüft. Gegenprobe offen: derselbe Ort in Dolphin.
- **Flackern in Gebäuden bei Gewitter:** Original (r111, r112, r113, r11c blitzen innen absichtlich:
  Fenster-Blitz bei `STA_CAMERA_IN_ROOM`). **Dabei gefunden, Fehler des Ports im 60-FPS-Modus:** die
  Zwischenbilder mischten Kamera und Projektion, nicht aber die Lichter (in Kamera-Koordinaten des
  nächsten Bilds): Lichtflecken schwammen bei Kameradrehung. **Behoben** (`gx_gl.cpp`
  `BlendLights`): je Zeichenaufruf die Bewegung der Kamera C = MV(Mitte) · MV(neu)⁻¹ auf Position
  und Richtung der Lichter, danach zurück aus dem XF-Speicher. Neuer Test `TestInterpolatedLights`
  (Lichtfleck im Zwischenbild wie in A und B; ohne die Korrektur 119 statt 125, schlägt fehl).
- **Tests:** i686-Build mit 0109/0110: 0 Baufehler, alle Kern-, Laufzeit- und Filmtests 0 Fehler;
  `gx_gl_check` 0 Fehler. **Nicht getestet:** am Gerät (Schreie, Ashley still in r22c, 60 FPS).

### Beta 0.9.0 (Entscheidung des Projektinhabers, 23.10.2026, Cloud)

- **Entscheidung:** „Beta jetzt“: mit den 1928 Programmen der Liste veröffentlichen; Burg und Insel
  kommen mit späteren Updates aus den Berichten dazu (der Releasetext bittet darum).
- **Version:** versionCode 73, `0.9.0-beta` (nach 0.46.0-alpha der Sprung zur Beta, Kommentar in
  `app/build.gradle`), APK `CubeEvil4-v0.9.0-beta.apk`, Releasetext `v0.9.0-beta.md`. Seit 0.46.0
  kein Code geändert, nur Liste, Version und Texte: die Prüfungen von `74a0043` gelten.
- **README:** Status „Beta“ (Abzeichen, Link „BETA RELEASE“), der Galaxy S10 (SCV40) als
  durchgespielt, Shader-Hinweis (1928 bekannt, Burg und Insel folgen).
- **Prüfung der Liste (Cloud, Mesa):** ein Probeprogramm mit `WarmProgramCache` über die Liste:
  1928 Schlüssel, 1865 verschiedene Programme gebaut (63 Schlüssel ergeben dieselben Quellen wie ein
  anderer), **0 Fehler**, ohne und mit MSAA (Feld `centroid` 0 und 1), je etwa 20 s; der zweite
  Durchgang findet alle mit Binärdatei (gut 160 ms). Die Binärdateien sind auf Mesa zusammen 60 MB
  groß; auf den Geräten nicht gemessen (der SCV40 hat schon etwa 3200 Programme im Cache).
- **Nicht getestet:** die APK selbst (Bau und Start auf dem Retroid durch den Projektinhaber).

### Bericht SCV40 (0.46.0, 23.10.2026 23:02): Shader des Durchspiel-Testers, Liste jetzt 1928 (Cloud)

- **Bericht:** Samsung SCV40, Android 10, `0.46.0-alpha-core`, Stufe 2 ohne MSAA, kein Absturz.
  Sitzung davor 21:29–22:13 im Dorf (r100–r10b, r120; Stufen 2/3/4 und MSAA 1/2/4 ausprobiert),
  die letzte (22:57) in r331. `previous/crash-system.txt` ist der alte Absturz vom 22.10. 15:07
  (`CalculateShape_new` in 0.41.3, seit 0.42.1 behoben: „Bericht SCV40 (0.41.3, 22.10.2026 16:21)“).
- **Shader beim Start (22:57):** der Thread fand alle 1921 Schlüssel schon mit Binärdatei vor
  (0 gebaut, 1260 ms), Prewarm lud 1894 vorab (6370 ms in Häppchen von 4 ms je Spielbild). Die
  längsten Bilder der Sitzung davor kamen von neuen Programmen: 21:38:59 1754 ms und 21:39:16
  782 ms gleich nach dem Wechsel auf MSAA 4 und 2 (neue Programme für die MSAA-Einstellung),
  22:05:43 914 ms und 22:07:08 508 ms in r102/r108 (dort etwa 150 Programme neu gebaut).
- **Liste der App:** `program-keys.bin` des Berichts hat 1919 Programme; die Liste wächst von 627
  auf **1928** (1301 neu), `RE4L`, 464 Byte je Schlüssel. Von den 627 des Retroid (Dorf, See)
  hatte der SCV40 618 (98,6 %): in den Bereichen, die beide gespielt haben, fehlt fast nichts.
- **Was fehlt:** Schlüsseldateien schreibt die App erst seit `d58dbb0` (22.10. 22:49, also ab
  0.45.0). Der Tester hat Burg (r2xx) und den Großteil der Insel (r30x–r32x) noch mit 0.44.0
  gespielt (sein Bericht 23.10. 10:31 mit 0.44.0 war schon am Jetski, r331–r333). Enthalten sind
  Dorf und See (r100–r11b, r120) und das Inselende (r331–r333, r405). Die Gesamtzahl bleibt
  geschätzt: Dorf, See und Inselende allein ergeben etwa 1900, Burg und Insel bringen schätzungsweise
  1000–2000 dazu, die Liste hat also etwa die Hälfte bis zwei Drittel (die frühere Schätzung
  2000–3000 war zu niedrig).
- **Wege zum Rest:** (a) ein Bericht mit 0.46.0 aus Burg und Insel (Spielstände dort laden und ein
  Stück spielen), (b) ein Shader-Rundgang auf dem Retroid über ADB mit dem Raumstart des
  Debug-Menüs (`RE4_START_ROOM`, Eingaben wie im PC-Rundgang über die 126 Räume), am Ende ein
  Bericht mit `program-keys.bin`.
- **Erster Start mit der Liste:** der Thread baut auf einem neuen Gerät bis zu 1928 Programme im
  Hintergrund (SCV40 etwa 28 ms je Programm, also knapp eine Minute; schwache Geräte länger);
  ein Programm, das das Spiel vorher braucht, wird wie bisher beim ersten Zeichnen gebaut.

### `master` auf 0.46.0 (Entscheidung des Projektinhabers, 23.10.2026, Cloud)

- Der Projektinhaber: alles nach `master`. `master` hatte zwei README-Commits (Reviews, Controls),
  die schon im Arbeitsbranch waren (gleiche README); `origin/master` in `claude/re4-start-controls`
  gemischt (`f30b19c`, keine Änderung am Inhalt), dann `master` vorgespult. `master`,
  `performance` und `claude/re4-start-controls` stehen auf demselben Stand (versionName
  `0.46.0-alpha`).
- **Nächstes (Plan des Projektinhabers):** sobald der SCV40-Tester (Spiel durchgespielt, Schlüssel
  seit 0.45.0 im Cache) einen Bericht mit 0.46.0 schickt und der Großteil der Shader in der Liste
  ist, **Beta 0.9.0**.

### Release 0.46.0-alpha (Entscheidung des Projektinhabers, 23.10.2026, Cloud)

- **Gerätetest des Projektinhabers (Retroid, Stand `74a0043`, Bericht 16:09):** „Die Umschaltung
  klappt sauber.“ Bericht mit MSAA 4 und „Dynamisch“; Räume r10c, r10e, r112, r117–r119, r11b;
  beim Start 717 Schlüssel bekannt (314 aus der Liste), der Shader-Thread fand alle 314 schon mit
  Binärdatei vor (34 ms), Prewarm lud 311 vorab (595 ms).
- **Shader-Liste:** mit `program-keys.bin` des Berichts (627 Programme auf dem Gerät) jetzt **627
  Programme** (313 neu). Format geprüft: `RE4L`, 464 Byte je Schlüssel, `sizeof(ShaderKey)` 464
  für Host, ARM32 und ARM64. Gesamtzahl des Spiels unbekannt; Anhaltspunkte: der SCV40 kam in einer
  Sitzung (r100 und das Inselende) auf 1030 Programme, das Retroid aus dem Dorf- und Seebereich
  auf 627; geschätzt 2000–3000 für das ganze Spiel, also etwa ein Viertel. Der SCV40-Tester hat
  das Spiel seit 0.45.0 durchgespielt: sein Bericht mit 0.46.0 brächte die übrigen Bereiche.
- **Version:** versionCode 72, `0.46.0-alpha`; seit `74a0043` nur Liste, Version und Texte
  geändert (kein Code): die Prüfungen von `74a0043` gelten (siehe den Abschnitt davor).
- **0.45.1 nicht veröffentlicht** (Projektinhaber): der Releasetext von 0.46.0 nennt alles seit
  0.45.0 (Shader auf eigenem Thread, schnelle Kerne, Leistungshinweis, „Dynamisch“, …);
  `v0.45.1-alpha.md` ist darin aufgegangen und entfernt.

### Gerätetest 23.10.2026 15:06 (Retroid, 0.45.1) und Wünsche des Projektinhabers: Dynamisch, Auflösung in der FPS-Anzeige, Einrasten für Controller, Menüzeilen, Gyro mit der Harpune (Cloud)

- **Bericht:** „Auto“ arbeitet (r10c: Stufe 2 → 1 → 2). Zwei Fehlauslöser: 15:01:14 beim Laden von
  r10c (11,5 Bilder/s, GPU 0,9 zurück) und 15:04:34, 4 s nach einem Aufstieg (ein Ruckler).
  Schnelle Kerne aktiv (`cores: … on cores 4 5 6 7`), der Leistungshinweis bekommt auf dem Retroid
  keine Sitzung (`the system gave no session`: das Android des Geräts bietet keine Hint-Sitzungen).
  Shader-Thread: erster Start 115 Schlüssel, 28 gebaut, 836 ms; nach dem Wechsel auf MSAA 2 die 70
  der Liste neu für MSAA (2537 ms). `program-keys.bin` im Bericht: die Liste der App wächst damit
  von 70 auf 314 Programme.
- **Regel geduldiger:** erst zwei schlechte Fenster in Folge (etwa 4–6 s Überlast) senken die Stufe;
  ein einzelnes (Laden, neuer Shader) nicht. `TestAutoLevel` angepasst (kurzer Ruckler bleibt, zwei
  Fenster in Folge senken, Rückfall nach dem Aufstieg verdoppelt die Wartezeit).
- **„Auto“ heißt jetzt „Dynamisch“** (`ingame_dynamic`, 10 Sprachen: Dynamic, Dynamisch, Dinámica,
  Dynamique, Dinamica, Dinâmica, Динамическое, 动态, डायनैमिक, ديناميكي), auch im Hinweis unter den
  Grafikoptionen; Protokoll `dynamic resolution: …`. Touch behält „Auto“.
- **FPS-Anzeige:** nach den Hz die Auflösung, mit der gerade gezeichnet wird, und die Stufe, z. B.
  `30 FPS (Game) · 60 Hz · 1280×672 (2×)` (bei 4:3 448 Zeilen je Stufe), auch wenn „Dynamisch“ die
  Stufe gesenkt hat (`CoreBridge.graphicsInfo`).
- **Einrasten in der Komfortsteuerung, auch für den Controller:** „L und R einrasten“ und „B
  einrasten“ ohne „(Touch)“ in der zweiten Zeile der Komfortsteuerung (neben „Messer per Wischen“),
  der Hinweis dazu neu `ingame_latch_help`, aus dem Touch-Hinweis entfernt. `CoreInput.setLatch`: ein
  Druck auf L oder R des Controllers (Trigger über die Hälfte, losgelassen unter einem Viertel; mit
  der alternativen Steuerung L1 und L2) hält ihn voll gedrückt bis zum nächsten Druck, B ebenso;
  `clear` (Fokus weg, Layout) lässt alles los; Touch rastet wie bisher selbst ein. Die Schlüssel
  `touchLatchLR`/`touchLatchB` bleiben (die Einstellung der Spieler bleibt erhalten).
  `CoreInputTests` 141 Prüfungen (16 neue), `CoreTouchTests` 260: bestanden.
- **Menüzeilen zweizeilig:** alle waagrechten Zeilen und Auswahlgruppen des App-Menüs ohne
  Grundlinien-Ausrichtung (`setBaselineAligned(false)`, `CoreMenu.row()`): ein Text, der umbricht,
  zeigt seine zweite Zeile ganz (vorher schnitt die Ausrichtung an der ersten Zeile sie ab).
- **Gyro mit der Harpune (Del-Lago-Kampf, `gyro.cpp`):** während Leon im Boot zielt (pl0f
  `plboat_R2_SpearSet`/`SpearSet2`, Zustand 0/0xF/3 oder 0xA, Schritt 3) bewegt das Drehen das
  Visier quer (`m_BoatPlDir`, ±22,5°, am Rand dreht das Boot wie mit dem Stick) und hoch/runter
  (`m_Blend`, ±255, so viele Pixel je Grad wie quer), mit der Verstärkung der Waffen (35 %). Neuer
  Laufzeittest `gyroboat` (`game_probe.cpp re4probe_gyro_boat`): quer, hoch, Grenze und Boot, nicht
  beim Aufstehen, wieder beim Mund-Wurf, nicht im Untermenü, Protokollzeile.
- **Tests (Cloud):** `gx_gl_check` 0 Fehler, `re4decomp_link_check` gebaut, `core_mode.cpp`
  Syntaxprüfung ARM64/ARM32 ohne Fehler und Warnungen, alle Java-Quellen mit javac gegen die
  Android-Klassen, `check_translations.py` ok (78 Texte je Sprache), `whole_build.sh` i686 mit
  `gyroboat`, armhf und aarch64: je 0 Baufehler, Laufzeit- und Kerntests 0 Fehler.
- **Nicht getestet:** auf dem Gerät (Menü-Aussehen mit langen Texten, Einrasten mit dem
  Retroid-Controller, Gyro im Boot, Anzeige der Auflösung).

### Release 0.45.1-alpha (Entscheidung des Projektinhabers, 23.10.2026, Cloud)

- **Entscheidung:** nach dem SCV40-Bericht „mit den weiteren Performance-Optimierungen“ ein Release;
  auf Nachfrage **0.45.1** (versionCode 71). `performance` (`9370fe5`, `534c118`, `8da2415`,
  `9cd61a1`) ist in `claude/re4-start-controls` vorgespult. Releasetext
  `port/android-decomp/docs/releases/v0.45.1-alpha.md`.
- **Tests (Cloud):** siehe die Schritte 1–4 oben; auf dem Release-Stand `whole_build.sh` i686,
  armhf und aarch64 nacheinander: je 0 Baufehler, Laufzeittests (auch ohne Helfer) und Kerntests 0
  Fehler, Filmtest bestanden; App-Renderer `gx_gl_check` 0 Fehler, `re4decomp_link_check` gebaut.
- **Nicht getestet:** alles davon auf einem Gerät.

### Bericht SCV40 (0.45.0, 23.10.2026 19:12): Spiel durchgespielt, Jetski-Abspann geht, Ruckler beim ersten Mal (Cloud)

- **Tester:** das Spiel bis zum Ende durchgespielt; die Jetski-Flucht (r333) und der Abspann (r405)
  liefen (PTH-Umwandlung `97d6f7d` bestätigt). „Ein paar Ruckler und Hänger“. Kein Absturz.
- **Gerät:** Samsung SCV40 (Galaxy S10+, Snapdragon 855), Android 10, 64 bit, Stufe 2, ohne MSAA,
  Tiefenschärfe an. Zwei Läufe im Bericht: 18:35–18:53 (r331, r332, r333, r405) und 19:03–19:12
  (neues Spiel r120/r100, dann wieder r331–r333).
- **Ursache der Hänger:** fast jeder große Ausreißer (Bildstück 200–1177 ms) fällt mit vielen neuen
  Shader-Programmen zusammen: 18:36:50 5 → 65 Programme (1177 ms), 18:43:06 252 → 308 (905 ms),
  18:49:45 358 → 414 (684 ms), 18:52:44 510 → 565 (603 ms), 19:05:14 758 → 793 (934 ms), 19:12:36
  999 → 1030 (651 ms). 0.45.0 hat die Shader-Quellen geändert (XF in drei Teilen): alle alten
  Binärdateien galten nicht mehr, und Schlüsseldateien gibt es erst seit 0.45.0. Im zweiten Lauf
  luden sich 565 Programme vorab aus dem Cache; neue (erstmals gesehene) bauten sich beim ersten
  Zeichnen. Das löst Schritt 3 (`8da2415`): Bau auf eigenem Thread, Liste in der App, die mit den
  Schlüsseln der Berichte wächst (dieser Bericht von 0.45.0 hat noch keine).
- **Mehrere Kerne wirken hier:** in r331/r332 rechnen die Eckpunkt-Threads 13–16 ms je Bild
  (`vertices: 340 batches in 14.4 ms on 3 more threads`), der Spiel-Thread braucht 18–20 ms und
  hat 5–13 ms frei; ohne die Helfer läge er über 33 ms. Bilder 34–40 ms an vollen Stellen, der
  Zeichen-Thread 12–16 ms je Bildstück.
- **Ton:** 31-mal leergelaufen in 18 Minuten (kurz), nichts verloren.

### Branch `performance`, Schritt 4: Spielkern mit -O3 übersetzt? Nein (23.10.2026, Cloud)

- **Versuch:** `RE4_OPT=3` für i686 und aarch64: 0 Baufehler, Laufzeit- und Kerntests 0 Fehler.
- **Messung GX-Decoder** (`RE4_GX_BENCH`, ohne Helfer, 120 000 Eckpunkte je Bild): i686 -O2
  3,1–3,25 ms, -O3 3,24–3,41 ms; Streifen zu 8: 4,1–4,4 gegen 4,5–5,2 ms. aarch64 unter qemu (nur
  als Hinweis): -O2 31,3–31,5 ms, -O3 37,0–38,8 ms; Streifen zu 8 gleich (52–57 ms).
- **Ergebnis:** -O3 macht den Decoder nicht schneller, eher langsamer; der Kern bleibt bei -O2.
  Die Spiellogik selbst lässt sich in der Cloud nicht messen (Disc-Daten).

### Branch `performance`, Schritt 3: Shader-Liste in der App, Shader auf einem eigenen Thread (Wunsch des Projektinhabers, 23.10.2026, Cloud)

- **Ziel:** auch der erste Start (und jeder neue Effekt, den ein anderes Gerät schon kannte) soll
  nicht ruckeln, weil ein Shader-Programm erst beim ersten Zeichnen gebaut wird (gemessen bis
  0,6 s auf dem Retroid).
- **Liste in der App:** Asset `gx-programs.keys` (Schlüsseldatei: `RE4L`, Größe eines Schlüssels,
  Anzahl, Schlüssel; je Programm ein Schlüssel, MSAA-Feld 0: die App setzt die Einstellung des
  Geräts ein). `CoreActivity` legt sie in den Cache (nur wenn sie sich änderte) und gibt sie als
  `RE4_GX_PROGRAM_LIST` an den Kern-Modus. Erste Liste: 70 Programme aus den R3-Folgen des Retroid
  (r10a, r10b vom 22.10., r11b vom 23.10.), nachgezeichnet mit `gx_chunk_render` und neu
  `RE4_GX_PROGRAM_CACHE`.
- **Wachsen der Liste:** jeder Fehlerbericht enthält jetzt `logs/program-keys.bin` (alle Schlüssel
  des Geräts, `CoreBridge.writeProgramKeys` → `WriteProgramKeyList`). `port/tools/gx_program_list.py
  merge port/android-decomp/app/src/main/assets/gx-programs.keys BERICHT.zip …` nimmt die neuen dazu
  (auch Ordner, einzelne `.key`-Dateien, andere Listen; Schlüssel anderer Größe, also anderer
  Renderer-Versionen, bleiben weg).
- **Eigener Thread (`WarmProgramCache`, `core_mode.cpp` `startWarm`):** einmal je Lauf (und wieder
  nach einer anderen MSAA-Einstellung) mit eigenem GL-Kontext (1×1-Pbuffer), Priorität unter dem
  Spiel (nice 10), auf allen Kernen: jedes Programm aus Liste und eigenem Cache, das für diesen
  Treiber noch keine Binärdatei hat, wird gebaut und abgelegt; alte Dateien derselben Schlüssel
  (andere Quellen oder Treiber) werden gelöscht. `GxRenderer::Prewarm` auf dem Zeichen-Thread lädt
  nur noch fertige Binärdateien (1–2 ms), baut nie selbst; solange der Thread läuft, wartet es auf
  dessen Ergebnis. Zeilen im Protokoll: `GX programs made ahead on a thread of its own: N keys (M
  from the app's list), K linked, H had a binary, …` und `GX programs ahead of their first draw: …`.
  `RE4_GX_WARM=0` schaltet den Thread ab. Dateien werden unter einem Namen je Thread geschrieben
  und umbenannt (beide Threads können dasselbe Programm ablegen).
- **Tests (Cloud):** `gx_gl_check` `TestPrewarm` neu: ohne den Thread lädt Prewarm nur die
  zwischengespeicherten Programme und baut keines; `WarmProgramCache` auf einem zweiten Thread mit
  eigenem Kontext baut den veralteten und den Listen-Schlüssel (MSAA-Feld aus der Liste ersetzt),
  findet die übrigen 60 vor, löscht die alten Dateien; danach lädt Prewarm alle 62 aus dem Cache und
  dieselben Zeichnungen bauen nichts; `WriteProgramKeyList` schreibt alle 62. Alle Prüfungen 0
  Fehler. `gx_program_list.py`: zweimal zusammengeführt bleibt bei 70, aus einem Bericht-Zip 70.
  `core_mode.cpp`/`gx_gl.cpp` Syntaxprüfung ARM64 und ARM32 ohne Fehler und Warnungen,
  `re4decomp_link_check` gebaut, alle Java-Quellen mit javac gegen die Android-Klassen übersetzt.
- **Nicht getestet:** auf einem Gerät (zweiter GL-Kontext auf Adreno/Mali, Zeit des Threads, ob der
  erste Start nicht mehr ruckelt).

### Branch `performance`, Schritt 2: Spiel- und Zeichen-Thread auf die schnellen Kerne (Wunsch des Projektinhabers, 23.10.2026, Cloud)

- **Vorbild:** KartPad-AAOS nennt in seiner README „CPU affinity adjustments“; der Code dazu ist im
  öffentlichen Repository nicht enthalten. Umgesetzt in `re4_cpu_info.h` (Kern und App):
  - `KeepOffSlowCores`: der aufrufende Thread darf nur noch auf die Kerne, die nicht zur langsamsten
    Art gehören (Retroid: 4–7, Galaxy A30s: 6–7). Nicht, wenn alle Kerne gleich sind, die Takte nicht
    lesbar sind oder `RE4_FAST_CORES=0`.
  - `PerfHint`: Androids Leistungshinweis (`APerformanceHint`, Android 13, zur Laufzeit gesucht;
    ältere Android-Versionen und Linux haben keinen): eine Sitzung je Thread-Gruppe, nach jedem Bild
    die Arbeitszeit gegen 33,3 ms; das System hebt dann rechtzeitig Takt oder Kern. `RE4_PERF_HINT=0`
    schaltet ab.
- **Kern (`gx_decode.cpp`):** beim ersten Bildstück nach dem Start der Eckpunkt-Threads: der
  Spiel-Thread weg von den langsamen Kernen, eine Sitzung für ihn und die Eckpunkt-Threads (die
  Eckpunkt-Threads selbst bleiben frei: auf Geräten mit zwei schnellen Kernen brauchen sie die
  langsamen); je Bild die Arbeit des Spiel-Threads ohne Schlaf bis zum Bildtakt und ohne Warten auf
  den Renderer. **App (`core_mode.cpp`):** der Zeichen-Thread genauso (je neuem Zeichen-Thread),
  eigene Sitzung, je Bildstück die Zeit von `Run`.
- **Protokoll:** `cores: the game thread on cores 4 5 6 7, off the slowest (at most 1804 MHz)`,
  `performance hint: on (the game thread and 3 vertex threads, 33.3 ms a picture)`, dasselbe für
  `the render thread`. **Testskript:** `test-core-app.ps1 -NoFastCores` / `-NoPerfHint` zum
  Vergleich.
- **Tests (Cloud):** Laufzeittest `cpuinfo` erweitert (gleiche Kerne und `RE4_FAST_CORES=0` lassen
  den Thread; die Kerne dieser Maschine mit der unteren Hälfte als „langsam“ auf eigenem Thread:
  danach nur noch die obere Hälfte erlaubt; der Leistungshinweis meldet unter Linux „not on this
  system“). `whole_build.sh` i686, armhf, aarch64: 0 Baufehler, Laufzeit- und Kerntests 0 Fehler
  (`534c118`). App-Code Syntaxprüfung ARM64/ARM32 ohne Fehler und Warnungen.
- **Nicht getestet:** auf einem Gerät (ob Android die Kerne erlaubt, ob der Hinweis eine Sitzung
  gibt, Wirkung auf Bildrate und Wärme).

### Branch `performance`, Schritt 1: „Auto“ regelt die Auflösung nach dem Grafikchip (Wunsch des Projektinhabers, 23.10.2026, Cloud)

- **Reihenfolge des Projektinhabers (23.10.2026):** (1) automatische Auflösung über die vorhandene
  Einstellung „Auto“, (3) Spiel- und Zeichen-Thread auf die schnellen Kerne, (4) Shader-Liste in der
  App, (6) Spielkern stärker optimiert übersetzen. Alles auf `performance`, ohne auf Berichte zu
  warten.
- **Regel (`gx_auto_level.h`):** „Auto“ beginnt wie bisher bei der Stufe des Bildschirms (1080 Zeilen
  → 2) und geht nie darüber. Je Fenster von mindestens 2 s und 20 Spielbildern: unter 28,5 Bildern/s,
  während der Grafikchip hinterherhängt (Zäune nach jedem Bildstück, im Mittel mindestens 0,5 nicht
  fertig), eine Stufe tiefer; ist das Spiel aus anderem Grund langsam (Prozessor, Laden), bleibt die
  Stufe. Wieder eine Stufe höher nach 5 Fenstern in Folge (10 s) mit mindestens 29,5 Bildern/s und
  einem Grafikchip, der mitkommt (unter 0,1), frühestens 30 s nach dem letzten Abstieg; geht es
  innerhalb von 15 s nach dem Aufstieg wieder hinunter, verdoppelt sich die Wartezeit (höchstens 5
  Minuten). Kein Wechsel bei offenem App-Menü, während einer R3-Folge oder der Treffer-Bilder und
  während der GPU-Messung. Zeilen im Protokoll: `auto resolution: from level N …`, `auto resolution:
  level 2 -> 1 (… game pictures/s, the GPU … chunks behind)` und `… -> 2 (10 s at …)`. Die Zeile
  „Graphics in use“ des Berichts nennt die Stufe gerade.
- **Wechsel ohne Neustart des Renderers (`GxRenderer::Rescale`):** zwischen zwei Bildstücken; EFB
  (dieselben Texturnamen, neue Größe), MSAA-Puffer (wie beim Start: auf den Kacheln oder eigene),
  Unschärfe-Puffer der Tiefenschärfe, das Bild für den Bildschirm und jede EFB-Kopie neu in der neuen
  Größe, Kopien und Bild skaliert hinübergezeichnet (Effekte, die das Bild davor lesen; das Bild, das
  der VI hält); noch gebundene Kopien zeigen auf die neue Textur. Programme, Texturen und der
  GX-Zustand bleiben: kein Nachladen, kein Ruckler. Das EFB beginnt geleert wie nach einer
  Display-Kopie.
- **App-Menü:** der Hinweis bei „Auto“ sagt jetzt in allen 10 Sprachen, dass Auto eine Stufe
  tiefer geht, solange der Grafikchip nicht mitkommt (`check_translations.py`: ok).
- **Tests (Cloud):** `gx_gl_check` neu `TestRescale` (Stufe 2 → 1 → 3, eine noch gebundene Kopie
  bleibt sichtbar, das Bild hat die neue Größe; ohne das Umhängen der Bindung 2 Fehler) und
  `TestAutoLevel` (die Regel mit erfundenen Messwerten: bleibt bei 30 Bildern/s, bleibt bei
  langsamem Prozessor, nicht bei offenem Menü, hinunter bei hängendem Grafikchip, nie unter 1 und
  nie über die Bildschirmstufe, Wartezeit 30 s, verdoppelt nach einem Rückfall): 0 Fehler.
  `gx_chunk_render` neu `RE4_GX_RESCALE=K:S`. Die R3-Folgen aus r11b (Retroid 13:01/13:02) mit
  Wechsel 2 → 1 und 1 → 2 mitten in der Folge, ohne und mit 4× MSAA: die Bilder davor bitgleich, das
  erste danach höchstens 9–13 Stufen Abweichung (im Mittel 0,1–0,2; der Regen liest das Bild davor),
  danach höchstens 2. `core_mode.cpp` und `gx_gl.cpp` Syntaxprüfung ARM64 und ARM32 ohne Fehler und
  Warnungen.
- **Nicht getestet:** auf einem Gerät (Schwellen, MSAA auf den Kacheln beim Wechsel; llvmpipe kann
  das nicht). Geräte mit 720 Zeilen stehen schon auf Stufe 1, dort ändert sich nichts.

### Vergleichsläufe 23.10.2026 13:22/13:24 (Retroid, mit und ohne Helfer): versehentlich 0.43.1 (Cloud)

- **Ablauf:** zwei Läufe mit `test-core-app.ps1 -Manual -SkipCore -SkipApk` (einmal mit
  `-GxThreads 0`), Stufe 2, MSAA 2. Beide Berichte nennen **0.43.1-alpha-core**: `-SkipApk` nimmt
  `app\build\outputs\apk\core\debug\app-core-debug.apk`, und Run in Android Studio erneuert diese
  Datei nicht (es installiert aus `app\build\intermediates`). Die alte APK wurde dabei über 0.45.0
  installiert (`install -r -d`). Die Läufe sagen über die Helfer nichts (0.43.1 hat keine).
- **Behoben im Skript:** `test-core-app.ps1` liest die Version der APK (`aapt2 dump badging` aus den
  Build-Tools des SDK, sonst Vergleich der Dateizeit mit `app\build.gradle`) und bricht ab, wenn sie
  nicht zum Stand im Repository passt; nach der Installation steht `App-Version auf dem Geraet` im
  Bericht. Nicht unter Windows ausgeführt (kein PowerShell in der Cloud).
- **Nebenbei** (0.43.1, Stufe 2, MSAA 2): Kern 33,9 bzw. 36,9 ms je Bild, davon 18–19 ms frei,
  11,1–11,5 ms Rechenzeit; der Kern hat auf dem Retroid also reichlich Luft.

### Gerätetest 23.10.2026 13:01 (Retroid, 0.45.0 gegen 0.44.0, Bericht und zwei R3-Folgen; Cloud)

- **Meldung:** 0.45.0 fühlt sich 1–2 FPS langsamer an als 0.44.0. Beide Läufe im Bericht
  (`previous` = 0.44.0 um 12:55, aktuell 0.45.0 um 13:01), derselbe Spielstand in r11b (See, Regen).
- **Einstellungen nicht gleich:** 0.44.0 lief die erste Minute mit Stufe 2 und MSAA 2 (GPU
  14–15 ms je Bild, Kern 34,5 ms je Bild, also volle 30 FPS), ab 12:55:57 MSAA 4, ab 12:56:13
  Stufe 3 mit MSAA 4. 0.45.0 lief nur mit Stufe 3 und MSAA 4.
- **Gleiche Einstellungen (Stufe 3, MSAA 4, r11b):** 0.44.0 19,4 gezeigte Bilder/s, GPU 29,4 ms je
  Bildstück, Render-Thread 8,5 ms; 0.45.0 19,0/s, GPU 26,6 ms, Render-Thread 7,5 ms (das Fenster
  von 0.45.0 enthält die Kamerafahrt beim Raumstart). Kein messbarer Unterschied; die Grenze ist
  der Grafikchip: über 33 ms je Bild fällt die Anzeige bei 60 Hz von 30 auf 20 Bilder/s.
- **R3-Folgen (0.45.0, 13:01 und 13:02, je 12 Bilder) in der Cloud nachgezeichnet**, mit dem
  Renderer von 0.44.0 (`0d0a9e9`) und von 0.45.0: alle 24 Bilder pixelgleich; je Bild 1125 → 1356
  bzw. 757 → 919 Uniform-Uploads, 2725 → 1462 KB bzw. 1984 → 1062 KB.
- **Erster Start nach dem Update:** `0 programs from the cache` (die Shader-Quellen haben sich mit
  der XF-Aufteilung geändert), 48 Programme neu gebaut, ein Bildstück bis 843 ms; ab dem zweiten
  Start aus dem Cache und vorab (Schlüsseldateien gibt es erst seit 0.45.0).
- **Ergebnis:** kein Rückschritt nachweisbar. Empfehlung für den Retroid an Stellen wie r11b:
  Stufe 2 mit MSAA 4 (0.44.0: 59 gezeigte Bilder/s, GPU 12,5 ms) oder Stufe 3 mit MSAA 2.
  Mehrere Kerne helfen, wo der Prozessor bremst (schwache Handys), nicht wo der Grafikchip bremst.
- **Nicht getestet:** längerer Vergleich mit gleichen Einstellungen; 0.45.0 beim zweiten Start.

### Release 0.45.0-alpha (Entscheidung des Projektinhabers, 23.10.2026, Cloud)

- **Entscheidung:** „Mach jetzt gleich 0.45.0 draus“, auf Nachfrage **mit** dem Branch `performance`.
  `performance` ist in `claude/re4-start-controls` zusammengeführt (Merge `296a750`), danach wieder
  auf denselben Stand gebracht. versionCode 70, versionName `0.45.0-alpha`, Releasetext
  `port/android-decomp/docs/releases/v0.45.0-alpha.md` (Fixed / New / Changed, je ein Satz).
- **Inhalt seit 0.44.0:** Inventar-Touch (`21bac7a`, `251c60f`, `db02821`), Tonanfragen-Bank 256
  (Patch 0107, `1adc18d`), schnellerer GX-Decoder und Zeitaufteilung im Bericht (Patch 0108,
  `ee81a44`), Grafik-Programme vor dem ersten Zeichnen (`d58dbb0`), Eckpunkte auf mehreren Kernen
  (`3a64e56`), XF-Uniforms in drei Teilen (`72ce66a`), PTH-Umwandlung für den Jetski (`97d6f7d`),
  Stay-Zeilen im Fehlerprotokoll (`cfb7726`).
- **Tests (Cloud, Stand nach dem Merge):** `whole_build.sh` für i686, armhf und aarch64: je
  `0 failed build steps`, runtime-test (auch mit `RE4_GX_THREADS=0`) und core-test 0 Fehler. Einmal
  rot: der armhf-Filmtest (Bildtaktung 2,41 Halbbilder je Bild, 263 späte Halbbilder) – unter qemu,
  während alle drei Builds gleichzeitig liefen; allein zweimal wiederholt: 2,03, bestanden. App-
  Renderer (`port/build/app-host-gl`): `gx_gl_check` 0 Fehler, `re4decomp_link_check` gebaut.
- **Nicht getestet:** alles davon auf einem Gerät (Retroid: mehrere Kerne, auch mit
  `-GxJobCheck`; Jetski-Abspann; Stay-Zeilen; Vorab-Programme beim zweiten Start; Todesschreie in
  großen Kämpfen).

### Bericht Xiaomi 2412DPC0AG (POCO X7 Pro, 0.44.0, 22.10.2026 21:58, ohne Beschreibung; Cloud)

- **Ablauf:** r207 (Burg), Ganados 0x11/7, kein Absturz, `crash.txt` leer; der Tester öffnet am
  Ende das App-Menü und schickt den Bericht. Bildrate in Ordnung (GX core 33,3 ms je Bild, davon
  24 ms frei; Renderer 4,7 ms).
- **21:55:51–53:** 35 Bilder lang je 7 Lebenspunkte weniger (930 → 685) und dabei drei A-Drücke:
  ein Ganado hat Leon gepackt (Würgegriff), Leon reißt sich los, der Ganado wird weggestoßen
  (`em: hit s3 … r 1/52/5/1`). So im Original.
- **21:57:17:** ein Treffer 685 → 316 durch einen Ganado; ebenfalls Spielverhalten.
- **`crash-system.txt`:** der alte Absturz `cSubLuis::move()+136` (pc 0) vom 20.10.2026 auf
  demselben Gerät (Version vor Patch 0087), längst behoben.
- **Ergebnis:** im Bericht ist kein Fehler zu sehen; ohne Beschreibung des Testers bleibt offen,
  was gemeldet werden sollte.

### Berichte 22.10.2026 abends: stumme Todesschreie, Ruckler (Retroid, Galaxy S7, moto e6s; Cloud)

- **Berichte:** Retroid 23:23, 23:41, 23:42 (0.44.0, Kern 64 bit, Stufe 2, MSAA 2, Anisotropie 16);
  Galaxy S7 SM-G930U (Snapdragon 820, Adreno 530, Android 8, 64 bit, 0.44.0, 18:13, ein Tester:
  Ruckler); moto e6s (MediaTek, 8 Kerne Cortex-A53 bis 2,0 GHz, 2 GB, Android 9, nur 32 bit,
  0.42.1, 16:40, ein Tester: Ruckler).
- **Stumme Todesschreie (Projektinhaber: Gegner schrien beim Runterfallen mehrmals nicht):** der
  Schrei kommt im Spiel zu Beginn der Treffer-Reaktion (`em10SetDamageVoice` → `Ctrl11SetSe2` →
  `SndCall(8, Se_tbl[0])`; beim Schrotflinten-Treffer `Dm_Blow`, auch wenn der Ganado danach über
  eine Kante fällt: im Bericht `r 2/4/2` bei s19, s24, s25 in r108 und s12 in r10a). Kein Hinweis
  des Tontreibers (`ISS Voice Drop`, `Snd_axv_work is full`, `REQ_NO`), kein Kopf verloren
  (`Be_flg 0x80`: zwei Zeilen `head lost` in 25 Minuten). **Unterschied des Ports gefunden:** jede
  Ton-Anfrage (neuer Ton, Stopp, und je Bild die Nachführung jedes Raumklangs, `sndSurroundCalc`,
  bis zu 48) belegt einen Platz im Anfrage-Speicher (`Snd_req_work`, 2 × 64); der Ton-Takt des
  Spiels führt einen Speicher aus und tauscht. Auf dem GameCube läuft der Takt alle 5 ms mitten im
  Spielbild, die Anfragen eines Bilds verteilen sich auf mehrere Speicher; im Port laufen die Takte
  gesammelt zwischen zwei Spielbildern (`vi.cpp` `re4_os_idle`, `ai.cpp`), alle Anfragen eines
  Bilds landen in einem. Ist er voll, gibt `Snd_open_req_work` nichts zurück und `SndCall` spielt
  den Ton stillschweigend nicht. **Patch 0107:** 256 Plätze (alle Stellen nutzen `SND_REQ_MAX`).
  Neue Zeile `sound: up to N requests for one audio frame (bank of 256, the GameCube's 64): M audio
  frames over 64, K full, in rNNN` (`state_trace.cpp`, gezählt vor jedem Ton-Takt über
  `re4_ai_set_block_hook`, höchstens alle 10 s und nur, wenn es vorkam): der nächste Bericht zeigt,
  ob mehr als 64 Anfragen auf einmal kamen. **Nicht bewiesen**, dass das die Ursache war.
- **Ruckler Retroid (23:42, r11b nach dem Speichern):** 10 s lang brauchte der Zeichen-Thread
  29,9 ms je Bild (vorher 4–6 ms, höchstens 86 ms), davon nur 7,3 ms Rechenzeit: er wartete auf
  Grafikchip oder Treiber; 669 Zeichenaufrufe und 9,5 EFB-Kopien je Bild, gezeigt 32,9 Bilder/s;
  die GPU-Messung nennt 14,5 ms. Danach wieder normal. Nicht behoben; als Nächstes eine R3-Folge an
  der Stelle (dann in der Cloud nachmessen) und derselbe Ort ohne MSAA.
- **Galaxy S7:** Einstellungen schon am kleinsten (Stufe 1, ohne MSAA, Anisotropie 1, ohne
  Tiefenschärfe). Der Spielkern braucht im Dorf 21–31 ms Rechenzeit je Spielbild (Rahmen 33,3 ms),
  der Zeichen-Thread 12–20 ms, die GPU 15–22 ms: knapp 30 Bilder/s, an vollen Stellen 27–48
  gezeigte Bilder/s. Die Zeit des Kerns wächst mit den Eckpunkten, die der GX-Decoder umwandelt
  (Dorf 140 000–170 000 je Bild aus 1,1–1,4 MB Display-Listen; auf dem Retroid r105 34 000 in
  7,7 ms, r108 176 000 in 19,7 ms).
- **moto e6s:** der Kern braucht in r100 60–80 ms je Spielbild auf den A53-Kernen (2,0 GHz):
  12–16 Spielbilder/s, der Ton lief über 3000-mal leer. Für den Port zu langsam (0.42.1).
- **Vorschlag (Entscheidung des Projektinhabers zur Reihenfolge):** die Zeit des Kerns aufteilen
  (Spiellogik gegen GX-Decoder) und den Decoder beschleunigen (Lesefunktionen je Eckpunkt-Format,
  unveränderte Display-Listen nur einmal umwandeln); hilft allen schwächeren Geräten.
- **Tests:** neuer Laufzeittest `sndbank` (`game_probe.cpp` `re4probe_snd_bank`): `SND_REQ_MAX`
  256; 70 Anfragen zählen als „über 64“, die andere Bank zählt nicht; `Snd_open_req_work` gibt
  genau bis 256 Plätze, dann nichts; ein voller Speicher zählt als voll. i686, armhf, aarch64
  0 Baufehler, alle Tests 0 Fehler (auch Kern-, Ton- und Filmtest mit dem größeren Speicher);
  Patch 0107 auf das Original angewandt geprüft.
- **Nicht getestet:** am Gerät (ob die Schreie jetzt kommen und ob die Zeile `sound:` in großen
  Kämpfen erscheint).

### Tester-Meldung (0.44.0, Xiaomi 2412DPC0AG, Bericht 22.10.2026 22:28): Gegner folgen kurz nicht mehr (Cloud)

- **Meldung:** „people stopped following, I stayed there, then I moved and they started following
  me again“ (Burg: r200, r202, r207, r203, r201; Ort und Zeitpunkt nicht genannt).
- **Im Bericht nicht zu sehen:** die Zeilen `em: sN r …` folgen dem Zustand eines Gegners nur 3 s
  nach einem Treffer (`kFollowFrames`). Kein Absturz, keine Route-Warnung (`check: route`, Patch
  0088).
- **Im Spielcode (em10.cpp):** Ganados (auch die Kultisten der Burg, 0x11) halten absichtlich an
  (R1 0x1B `Stay`): `em10StayCk` lässt nur die Nächsten auf der Route angreifen (mehr als 3 näher,
  auf leichten Stufen weniger) und lässt die übrigen warten, solange sie weiter als 8–12 m auf der
  Route sind; Gegner, deren Wachbereich Leon weit verlassen hat, warten ebenso (`L_pl_guard >
  Guard_r + 2000`). Bewegt sich Leon, ändern sich diese Abstände, und sie laufen wieder los: das
  beschriebene Bild. Sehr wahrscheinlich Verhalten des Originals, nicht bewiesen.
- **Neu im Bericht (`state_trace.cpp`):** `em: sN stays back (Stay): X m from Leon, Y m on the
  route, K Ganados nearer on the route; Leon Z m from its post, guard radius R m` (je Gegner höchstens
  alle 10 s) und `em: sN follows again after T s (r 1/NN)` (wenn er mindestens 3 s wartete). Damit
  zeigt der nächste Bericht, welche Regel greift: viele näher (Gedränge), Leon außerhalb des
  Wachbereichs, oder keins von beidem (dann ein Fehler, etwa in der Route).
- **Test:** `enemylog` erweitert (`game_probe.cpp` `re4probe_enemy_log`): der Ganado hält an
  (6 m entfernt, 7,5 m Route, Wachbereich) und folgt 4 s später wieder; beide Zeilen im Protokoll.
- **Nicht getestet:** am Gerät.

### Bericht SCV40 (0.44.0, 23.10.2026 10:31): im Abspann fehlt die Grafik großteils, Jetski-Strecke big-endian (Cloud)

- **Bericht:** SCV40 (Samsung, Android 10, 64 bit, Stufe 3), durchgespielt bis zur Flucht auf dem
  Jetski (r333): danach fehlt im Abspann der größte Teil des Bilds.
- **Ursache:** beim Laden von r333 `formats: archive entry "PTH" (first in st3/r333.das (unpacked)):
  no converter, left big-endian`. PTH ist die Fahrstrecke des Jetskis (`r333.cpp`:
  `setRail(ROOM_ARC_PTR(pG->pRoom, 0x1F))`, `pl0e.cpp` `cPl0e::setRail`, `pl0ePathMove`), ein
  `Path` aus `path.h` (u16 Anzahl, je Punkt 0x28 Byte: Position, Normale, Länge als Floats, dann
  Gewichts-Bytes). Big-endian gelesen stimmen Anzahl und Länge nicht: `PathGetPosEm` lieferte erst
  (0 0 0) (Abstand außerhalb der Strecke; Protokoll 10:30:47 `em: s1 jumps … -> (0 0 0)`, `s19`
  ebenso), dann Unsinn (10:31:00 Spieler bei z = 1,97e28). Jetski, Leon, Ashley und die Kamera
  waren damit irgendwo, das Bild des Abspanns leer.
- **Behoben (`formats_eff.cpp`, `formats_arc.cpp`):** Archiv-Kürzel `PTH` mit eigenem Umwandler
  `convertPathEntry` (der vorhandene `convertPath` der Effekt-Pfade, jetzt auch außerhalb der
  Effektdateien). Betrifft auch `PTH` in `em36` (bisher ohne Leser im Code).
- **Test:** neuer Laufzeittest `pathentry` (`game_probe.cpp` `re4probe_path_entry`): ein
  big-endian-Archiv mit einem PTH-Eintrag (3 Punkte) durch den Lader, dann lesen die Pfad-Funktionen
  des Spiels (`PathGetLength`, `PathGetPos`) Länge 200, den Punkt bei 150 und hinter dem Ende
  (0 0 0) richtig. Ohne den Umwandler schlägt er fehl (Fehlerbits 0x17: Anzahl, Länge, Punkt,
  Normale).
- **Die übrigen Kürzel ohne Umwandler in den Berichten:** `FNT` (ss_cmmn.dat), `DRA` (Werkzeug
  `t_dr.cpp`), `DMY` (em2f), der Eröffnungsfilm (Bytestrom): bekannt, ohne Folgen.
- **Nicht getestet:** am Gerät (Jetski-Flucht und Abspann).

### Schwächere Geräte, Schritt 1: Zeitaufteilung im Bericht, schnellerer GX-Decoder (Wunsch des Projektinhabers, 22.10.2026, Cloud)

- **Ausgang:** Galaxy S7, moto e6s, Galaxy A30s und Galaxy S10 ruckeln; auf dem Gerät gingen zuletzt
  68–71 % der Rechenzeit des Kerns in den GX-Decoder (Display-Listen und FIFO nach GLES3-Ecken
  umwandeln, `gx_decode.cpp`). Die R3-Folgen des Retroid (r10a 23:32, r10b 23:35 zweimal) liegen in
  `files/gx-dumps` und kommen nicht mit dem Bericht; holen mit `port/scripts/pull-gx-dumps.ps1`.
- **Neue Zeile im Bericht (Patch 0108):** `frame: 600 frames in rNNN: logic … ms, drawing … ms, rest …
  ms, frame end … ms, waiting … ms (averages; the longest frame … ms)`. Quelle ist die eigene
  Zeitmessung des Spiels (`ProcessTickGet` in `main.cpp`, `proc_tick` in `debug.cpp`, Echtzeit):
  logic = Aufgaben-Planer (Leon, Gegner, Effekte, Kamera, Raum), drawing = IdSys und Trans bis
  `SndWatcher` (die GX-Befehle der Modelle, die der Decoder sofort umwandelt), rest = Texte,
  Kinobalken, Sortierliste, frame end = `Render_done` und Prüfungen, waiting = Warten auf den
  Bildtakt. Alle 600 Spielbilder (`Rno0` 3), über das Zeilenfenster `run`. Damit zeigt jeder
  Bericht, ob ein Gerät an der Spiellogik oder am Zeichnen hängt.
- **Decoder schneller, Ausgabe unverändert:**
  - Eckpunkte in Blöcken zu 128: erst je Ecke nur die Quelladressen (Matrix-Bytes, direkte und
    indizierte Attribute), dann je Attribut eine Lesefunktion für den ganzen Block. Die Lesefunktion
    je Format (Zahlentyp, Komponenten, Byte-Reihenfolge; Farben je Farbformat) wählt `describe()`
    einmal je Eckpunkt-Format (`posBlock`, `nrmBlock`, `nrmPartBlock` für NBT3, `texBlock`,
    `clrBlock`), statt je Ecke und Attribut zu verzweigen.
  - Array-Basis und -Schrittweite (`gGp.arrayBase/arrayStride`) werden nicht mehr bei jeder
    Zeichnung neu nach Host-Adressen aufgelöst: Zwischenspeicher je Array, ungültig beim Schreiben
    der CP-Register 0xA0/0xB0 und wenn `gx_fifo.cpp` ein neues Adressfenster vergibt
    (`aliasGeneration`, einmal je Zeichnung und Matrix-Ladung geprüft, `arraysCurrent`).
  - Indizes werden gleich mit dem Versatz einer angehängten Zeichnung geschrieben (vorher ein
    zweiter Durchlauf).
  - **Messung** (Host i686, `-O2`, `RE4_GX_BENCH=300`, 120 000 Ecken je Bild, Cloud-Rechner mit
    Schwankungen): Streifen zu 20 Ecken 3,25 → 2,6–2,9 ms, Streifen zu 8 (wie der Wald von r100,
    neu `RE4_GX_BENCH_STRIP=N`) 4,22 → 3,5–3,8 ms, also etwa 15 % weniger Decoder-Zeit. Danach
    (perf): `draw` selbst 41 % (der Durchlauf je Ecke), `xfWrite` 8 %, die Lesefunktionen zusammen
    15 %, `command` 4 %, `emit` 3 %. Am Gerät nicht gemessen.
- **Größerer Hebel (Grundsatzentscheidung, offen):** den Decoder in einen eigenen Thread legen
  (das Spiel schreibt weiter, der Decoder wandelt parallel um, mit Rückstand-Grenze wie der
  GameCube-FIFO). Auf Mehrkern-Geräten würde damit ein Großteil der Decoder-Zeit vom Spielthread
  verschwinden.
- **Tests:** neuer Laufzeittest `sections` (`game_probe.cpp` `re4probe_frame_sections`: Marken bei 1,
  3, 8, 11, 12 und 33,33 ms, ein Bild mit `Rno0` 2 zählt nicht, dann 600 Bilder in r105 → genau die
  Zeile `frame: 600 frames in r105: logic 2.0 ms, drawing 5.0 ms, rest 3.0 ms, frame end 1.0 ms,
  waiting 21.3 ms`). GX-Fingerabdruck (`gx`, 0x02524012c6419af2) unverändert, alle Laufzeittests 0
  Fehler (i686 `-O2`); `whole_build.sh` i686, armhf, aarch64: 0 Baufehler, Kern- und Laufzeittests
  0 Fehler (`ee81a44`). Am Gerät nicht getestet.

### Branch `performance`: kleinere Uniform-Uploads zum Grafikchip (Wunsch des Projektinhabers, 22.10.2026, Cloud)

- **Ausgang:** in r10b (R3-Folge 01 des Retroid) 876 Uniform-Uploads mit 2,4 MB je Bild: jede
  Änderung im XF-Block (meist die Matrix des nächsten Modells) schickte den ganzen Block (3,3 KB)
  in den Uniform-Ring.
- **Umbau (`gx_gl.cpp`, `gx_gl_shader.*`):** der XF-Block geht in drei Teilen zum Grafikchip, jeder
  an einer eigenen Bindung (`kXfParts`): `XFM` Positions-, Textur- und Normalenmatrizen (1,5 KB),
  `XFP` die Post-Matrizen (1 KB, selten geändert), `XFL` Lichter, Farben, Viewport, Projektion
  (0,75 KB). Ein Zeichenauftrag lädt nur die geänderten Teile (`UploadXf`, Änderungen je Teil in
  `Dirty`). Ist der Ring in sein nächstes Viertel gerückt (`streamMoved`), gehen alle Teile neu
  hinein (ein in einem alten Viertel gebundener Teil könnte sonst überschrieben werden, bevor die GPU
  ihn liest; vorher galt das für den ganzen Block nach einem Wechsel mitten im Bildstück ebenso).
  Ohne Ring: ein Puffer je Teil. Vier Teile (Normalen getrennt) kosteten 1708 statt 1073 Bindungen
  für 1 % weniger Bytes, zwei (Post-Matrizen bei den Lichtern) 16 % mehr Bytes: drei Teile.
- **Ergebnis (R3-Folgen, `RE4_GX_WORK=1`):** r10a 334 → 437 Uploads, 786 → 422 KB; r10b 876 → 1073,
  2371 → 1285 KB; r10b (2) 221 → 289, 528 → 309 KB: knapp die Hälfte der Bytes, ein Fünftel mehr
  Bindungen. Alle 36 Bilder der drei Folgen pixelgleich zu vorher, auch mit `RE4_GX_INTERP=1` (die
  echten Bilder gleich, 11 Zwischenbilder). Die Shader-Quellen ändern sich: alle gespeicherten
  Programme werden einmal neu übersetzt (das Vorladen erledigt das auf dem Titelbild).
- **Tests:** `gx_gl_check` 0 Fehler; `TestUniformStream` auf die kleineren Uploads umgestellt
  (Bildstücke mit 2400 statt 700 und 5000 statt 1400 Zeichenaufträgen, damit der Ring wie vorher
  zwischen und in Bildstücken wechselt). Am Gerät nicht gemessen (weniger Bytes gegen mehr
  Treiberaufrufe).

### Branch `performance`: Eckpunkte auf mehreren Kernen (Entscheidung des Projektinhabers, 22.10.2026, Cloud)

- **Entscheidung:** Umbau zur Auslastung der Kerne und später kleinere Uniform-Uploads, beides auf
  dem Branch `performance` (abgezweigt von `claude/re4-start-controls` bei `366c48e`); erscheint mit
  den übrigen offenen Optimierungen als 0.45.0.
- **Warum nicht der ganze Decoder in einen eigenen Thread:** das Spiel wartet am Ende jedes Bilds
  ohnehin auf die Grafik (`Render_done`, `main_sub.cpp`: zweimal `GXDrawDone`); ein Decoder-Thread
  liefe nur neben dem kleinen Rest der Zeichenphase. Die Zeit des Kerns wächst dagegen mit den
  Eckpunkten (Retroid: r105 34 000 Eckpunkte in 7,7 ms, r108 176 000 in 19,7 ms, also etwa 85 ns je
  Eckpunkt), und genau diese Umrechnung lässt sich aufteilen.
- **Umbau (`gx_decode.cpp`):** der Spiel-Strang liest die Befehle weiter der Reihe nach (Register,
  Matrizen, Texturen, Paletten, Zeichenbefehle). Jeder Zeichenbefehl bekommt sofort seinen Platz im
  Bildstück (Eckpunkte, Indizes, Zeichenauftrag, Verbinden mit dem vorigen); die Umrechnung seiner
  Eckpunkte (`convertVertices`, `writeIndices`) wird ein Eintrag eines Pakets. Pakete ab 384
  Eckpunkten oder 64 Einträgen (im Wald von r100 hat ein Zeichenbefehl etwa 8 Eckpunkte) nehmen sich
  Helfer-Threads; muss der Spiel-Strang warten, rechnet er selbst mit. Vertex-Format und Arrays
  gehen als Kopie mit (`Snapshot`, neu nur nach einer Änderung), Eckpunkt-Bytes aus dem FIFO werden
  kopiert (der Strom rückt weiter), Display-Listen bleiben, wo sie sind. Die Helfer warten nach einem
  Paket 0,3 ms wach, bevor sie schlafen; geweckt wird nur, wenn keiner wach ist (das Wecken je Paket
  kostete mehr als das Paket). Die Eckpunkt-Bytes löscht der Thread, der sie umrechnet (vorher
  löschte sie der Spiel-Strang: jede Cache-Zeile wechselte den Kern).
- **Wann alles fertig ist (`finishJobs`):** bevor die Grafik dem Spiel etwas meldet (BP 0x45 draw
  done, Tokens 0x47/0x48: danach darf das Spiel ändern, was gezeichnet wurde), bevor eine EFB-Kopie
  ihre Markierung in den Speicher schreibt, bevor ein Bildstück an den Renderer geht, bevor eine
  Display-Liste in den Speicher aufgezeichnet wird (`gx_fifo.cpp`) und bevor die Puffer des
  Bildstücks wachsen müssten. Die Helfer lesen Arrays und Display-Listen also etwas später als der
  Befehl kam, wie die GPU des GameCube, die neben der CPU herlief.
- **Anzahl:** `RE4_GX_THREADS=N` (0: alles auf dem Spiel-Strang wie bisher), sonst Kerne minus 3,
  höchstens 3 (Retroid und 8-Kern-Handys 3, 4-Kern-Geräte 1). Protokoll beim ersten Zeichnen
  `GX: the vertices of the draws converted on N more threads (M cores)`; die Zeile `GX core` nennt
  jetzt zusätzlich Pakete, ihre Rechenzeit und die sofort umgerechneten Zeichenbefehle.
- **Messung** (Host i686 `-O2`, 4 Kerne, 120 000 Eckpunkte je Bild, mit dem Warten auf die Helfer;
  der Test zählt die Display-Kopie jetzt mit): Streifen zu 20 Ecken ohne Helfer 3,1–3,4 ms, mit 1
  Helfer 1,6, mit 2 1,1, mit 3 0,9 ms; Streifen zu 8 Ecken 4,1 → 2,1 / 1,5 / 1,5 ms.
- **Tests:** der Fingerabdruck (`gx`, `02524012c6419af2`) läuft jetzt mit 3 Helfern
  (`re4_gx_threads`, oder wie `RE4_GX_THREADS` sagt); `whole_build.sh` startet die Laufzeittests ein
  zweites Mal mit `RE4_GX_THREADS=0`. Einmal je Programmstart (der Decoder behält Zustand von einem
  Lauf zum nächsten). Gefunden beim Bau: die Markierung einer EFB-Kopie überschrieb Speicher, den
  ein Helfer noch las (jetzt `finishJobs` davor). Im Dauertest (Hunderte Läufe, mit künstlicher
  Last) wich der Fingerabdruck selten ab, immer zum selben Wert: ein Index „hinter dem Array“ des
  Testgenerators reicht bis in den Bereich, in den der Test die nächsten Display-Listen desselben
  Bilds schreibt. Der Spiel-Strang las dort vor dem Schreiben, ein Helfer manchmal danach (auf dem
  GameCube derselbe Wettlauf; ein Spiel schreibt solchen Speicher erst nach `GXDrawDone`). Der Test
  wartet jetzt vor jedem Schreiben einer Liste auf die GPU (`re4_gx_wait_jobs`), der Referenzwert
  bleibt. Zur Fehlersuche `RE4_GX_JOB_CHECK=1`: jeder Zeichenauftrag wird zusätzlich sofort
  umgerechnet; weicht ein Paket davon ab, meldet das Protokoll `GX job check: … memory it reads
  changed meanwhile` (so wurde der Testgenerator gefunden). Danach 40 Läufe unter Last mit 1–3
  Helfern ohne Abweichung; unter Last je 60 Läufe ohne und mit 2 Helfern: 0 Ausfälle (auch der
  zeitabhängige Test `tasks`, der vorher bei Überlast gelegentlich ausfiel). `whole_build.sh` i686, armhf, aarch64: 0
  Baufehler, Kern- und Laufzeittests 0 Fehler, auch der zweite Lauf mit `RE4_GX_THREADS=0`
  (`3a64e56`). `test-core-app.ps1` neu `-GxThreads N` und `-GxJobCheck`.
- **Nicht getestet:** am Gerät (Gewinn je Gerät, ob das echte Spiel Speicher ändert, den ein Helfer
  noch liest: dafür ein Lauf mit `-GxJobCheck`).

### R3-Folgen des Retroid (22.10.2026 23:32/23:35) und Grafik-Programme vor dem ersten Zeichnen (Cloud)

- **R3-Folgen:** drei Folgen zu 12 Bildern aus dem Bericht 23:42 (`pull-gx-dumps.ps1`, 155 MB):
  r10a 23:32 (266 Zeichenaufträge, 64 000 Dreiecke), r10b 23:35:42 (788 Zeichenaufträge, 106 000
  Dreiecke, 10 Kopien, 33 Shader, über dem See im Nebel), r10b 23:35:52 (150 Zeichenaufträge).
  `gx_chunk_render` zeichnet sie wie der Retroid (mittlerer Unterschied 0,7–1,3 von 255 ab dem
  zweiten Bild; das erste Bild jeder Folge ohne die EFB-Kopie des Bilds davor, wie erwartet). Kein
  Grafikfehler darin. GL-Arbeit je Bild in r10b: 876 Uniform-Uploads mit 2,4 MB (der XF-Block je
  Matrixwechsel), 96 Programmwechsel.
- **Ursache der Hänger im Bericht 23:42 (Zeilen `GX:` alle 10 s):** die längsten Bildstücke fallen
  genau mit neuen Shader-Programmen zusammen. 23:20:14 37 neu übersetzt, 8 aus dem Zwischenspeicher:
  ein Bildstück 633 ms; 23:35:37 (r10b, der See) 22 neu, 39 aus dem Speicher: 435 ms; danach 171,
  137, 71 ms, während weitere 70 Programme dazukamen. Übersetzen kostet auf dem Retroid etwa 16 ms
  je Programm, Laden aus dem Speicher 1–2 ms. In 28 Minuten 529 Programme, davon 328 aus dem
  Speicher: nach einem Update der App passen die gespeicherten Programme nicht mehr (die
  Shader-Quellen ändern sich), alle werden beim ersten Auftauchen neu übersetzt; nach einem neuen
  Renderer (zurück in die App, 23:16:18) wird alles neu geladen.
- **Grafik-Programme vor dem ersten Zeichnen (`GxRenderer::Prewarm`, `gx_gl.cpp`):** neben jedem
  Programm im Zwischenspeicher (`cache/gx-programs/<hash>.bin`) liegt jetzt sein Schlüssel
  (`<hash>.key`, `ShaderKey`, auch für schon gespeicherte Programme beim nächsten Gebrauch). Ein neuer
  Renderer liest beim ersten `Prewarm` alle Schlüssel und macht die Programme der Reihe nach: aus
  dem Speicher, oder neu übersetzt, wenn sich Quellen oder Treiber geändert haben (dann werden die
  alten Dateien gelöscht). Programme mit anderer MSAA-Einstellung (`centroid`) bleiben liegen. Die
  App ruft es bei jedem Bildschirmtakt ohne neues Bildstück (das Spiel zeichnet 30 Bilder je
  Sekunde, jeder zweite Takt ist frei), bis 4 ms (`kPrewarmMs`, mindestens ein Programm). Mit
  „60 FPS“ (Zwischenbilder) ist kein Takt frei, dann nur bei Ladepausen und angehaltenem Bild.
  Protokoll: `GX programs ahead of their first draw: N keys in the cache`, am Ende `… M from the
  cache, K linked anew (J old files removed), T ms in all`; die Zeile `GX:` zählt `… ahead of their
  first draw`. Wirkung erst ab dem zweiten Start mit dieser Version (die Schlüssel entstehen beim
  Gebrauch); danach auch nach jedem Update: die Programme werden auf dem Titelbild neu übersetzt
  statt mitten im Spiel.
- **Tests:** `gx_gl_check` neu `TestPrewarm`: 60 Programme aus zufälligen TEV-Einstellungen → 60
  Binärdateien und 60 Schlüssel; ein Schlüssel unter altem Namen (Quellen geändert) → neuer Renderer
  macht vor dem ersten Zeichnen alle 61 (60 aus dem Speicher, einer neu, die alten Dateien gelöscht),
  ein Programm je Aufruf ohne Zeitbudget, dieselben Zeichnungen machen danach kein Programm mehr; mit
  4× MSAA keins der einfachen. Alle `gx_gl_check`-Prüfungen 0 Fehler, Linkprüfung der App-Bibliothek
  (`re4decomp_link_check`) ohne Fehler, `gx_gl.cpp` für ARM32/ARM64 und `core_mode.cpp` für ARM32
  ohne Fehler (aarch64: fehlende Ersatzdatei `android/dlext.h`, schon vorher).
- **Nicht getestet:** am Gerät (wie viele Hänger verschwinden, Dauer des Vorladens auf dem Titelbild).
- **Möglicher nächster Schritt:** kleinere Uniform-Uploads (nur geänderte Matrixzeilen statt des
  ganzen XF-Blocks, 2,4 MB je Bild in r10b) entlasten den Zeichen-Thread schwacher Geräte.

### Gerätetest 22.10.2026 21:22 (Retroid): Karte ohne Finger-Verschieben, kein „Exit“-Aufblinken aus dem Koffer (Cloud)

- **Befund des Projektinhabers (Bericht 21:22, Logcat):** von ganz links bis zur Karte geht das
  Wischen, auf der Karte bleibt es stehen (im Bericht 20-mal `touch: swipe ignored in the zoomed map
  (the finger moves the map)`: die Karte öffnet in der gezoomten Ansicht, dort hielt die Regel des
  Finger-Verschiebens jedes Wischen fest). Aus dem Koffer blinkt vor der Karte und vor
  Schlüssel/Schätze kurz „Exit“ gelb auf. Koffer-, Schlüssel- und Akten-Tipps sowie das Messer per
  Wischen liefen (`slash 1` … `7`, `files: open row 1`, `key items: row 1`).
- **Entscheidung des Projektinhabers (22.10.2026):** die Karte nicht mit dem Finger verschieben oder
  zoomen; dafür von der Karte weiter zu den Akten und zurück wischen. **Ausgebaut:** `re4_core_drag`,
  `re4_touch_map_shift` und die Kartenbewegung in `runtime/menu_touch.cpp`, Kernprozess-Nachricht 27
  `kDrag` (`re4ipc_drag`, `coreproc`), App `CoreBridge.drag` und das Senden bei jeder
  Fingerbewegung (`CoreActivity.watchTap`), die Ausnahme „kein Reiterwechsel in der gezoomten Karte“.
  Ein Wischen wechselt jetzt in beiden Kartenansichten den Reiter (aus der gezoomten Ansicht führt
  B direkt zur Leiste, aus der Gesamtansicht zoomt das erste B hinein).
- **„Exit“ blinkt auf, Ursache:** das B aus dem Koffer (`PieceSelect` Zustand 0, `mode = 2`) setzt
  `menu_next = 4`, und `SsPzzlMain::move` ruft im selben Bild `sscrnMainMenuInit(wk, 1)`: die
  Hervorhebung liegt ein Bild lang auf „Exit“, bevor die App sie im nächsten Bild auf den
  Ziel-Reiter setzte. **Behoben:** Patch 0106 ruft zusätzlich gleich nach `cur->move`, vor dem
  Zeichnen (`IdSub.move`/`trans`), `re4_touch_sscrn_moved` auf; hat die Leiste in diesem Bild den
  Fokus bekommen und wartet ein Reiter, setzt es `menu_next` und die Hervorhebung sofort (ohne
  eigenen Ton, das B des Spiels hat ihn schon gespielt). Gilt für alle vier Bildschirme (die
  anderen setzten die Hervorhebung ein Bild lang auf ihren eigenen Reiter). A kommt weiter nach 6
  Bildern mit Hervorhebung.
- **Dazu (aus dem Logcat 20:48 und 20:54, Stand davor):** `touch: tab 0 dropped (the row did not
  get the focus)` nach schnellem Wischen über die Karte: die 45 Bilder Wartezeit liefen weiter,
  während die Karte lud (über 1,5 s). Jetzt zählen die Bilder eines Wechsels (und die 90 Bilder, in
  denen ein gewählter Reiter die Basis des nächsten Wischens ist) nur auf einem der vier
  Bildschirme, nicht während ein Bildschirm lädt. Ein Wischen, während ein Bildschirm lädt und
  kein Wechsel unterwegs ist (mit dem Steuerkreuz gewählt, oder das Untermenü öffnet), zählt vom
  kommenden Bildschirm (höchstens 90 Bilder aufgehoben; Zeile `touch: swipe kept until the screen
  shows`), statt mit `no tab there` verloren zu gehen.
- **Tests:** `menutouch`: Wischen in der gezoomten Karte → B, A im 7. Bild mit den Akten; aus dem
  Koffer → B, das Spiel setzt `menu_next` 4, nach `re4_touch_sscrn_moved` steht er auf 2, A im 6.
  Bild; ohne wartenden Reiter ändert der Aufruf nichts; Wischen, während die gewählte Karte 120
  Bilder lädt: keine Tasten, nicht verworfen, danach B und A mit dem Koffer; Wischen, während ein
  mit dem Steuerkreuz gewählter Bildschirm lädt: aufgehoben, danach vom Koffer aus B und A mit der
  Karte. Ohne Karten-Verschiebe-Test, Kernprozess-Test ohne `re4ipc_drag`. i686, armhf, aarch64
  0 Baufehler, alle Tests 0 Fehler (`menutouch` ok); Patch 0106 auf das Original angewandt geprüft;
  App-Java übersetzt, `re4decomp_link_check` gelinkt.
- **Nicht getestet:** am Gerät (ob „Exit“ nicht mehr aufblinkt und die Karte in beide Richtungen
  verlassen wird, prüft der Projektinhaber auf dem Retroid).

### Gerätetest 22.10.2026 20:30 (Retroid): Hervorhebung beim Wischen; Schlüssel, Akten und Karte per Touch (Cloud)

- **Befund:** das Wischen landet beim richtigen Reiter, aber oben blinkt kurz ein falscher auf (der
  alte Reiter, aus dem Koffer „Exit“). Ursache: die App setzte `menu_next` und drückte A im Bild, in
  dem die Reiterleiste den Fokus bekam; die Hervorhebung zeichnet das Spiel nur bei einem Druck aufs
  Steuerkreuz neu (`sscrnMainMenu` → `idMainMenu`), sie blieb also auf dem alten Reiter (oder auf
  „Exit“, wohin das B aus dem Koffer die Leiste setzt), bis der neue Bildschirm erschien.
  **Behoben:** hat die Leiste den Fokus, setzt die App `menu_next`, die Hervorhebung wie `idMainMenu`
  (`showTab`, Einheiten 0–4 von `IDC_SSCRN_MAIN_MENU`) und den Ton 0xA, hält das 6 Bilder und drückt
  erst dann A. Im gezoomten Kartenbild wechselt ein Wischen den Reiter nicht (dort verschiebt der
  Finger die Karte); in der Gesamtansicht schon.
- **Neu (Wünsche des Projektinhabers):**
  - **Schlüssel/Schätze** (`ss_item.cpp`): ein Tipp auf einen Gegenstand einer der beiden Reihen
    (Einheiten `frameMarkNo` von `IdNum`/`IDC_SSCRN_1`, nur sichtbare) fährt den Cursor wie das
    Steuerkreuz hin (hoch/runter für die Reihe, links/rechts für den Gegenstand, ein Druck je Bild),
    ein Tipp auf den Gegenstand unter dem Cursor ist A (Befehlsmenü); beim Kombinieren nur in der
    Reihe; das Befehlsmenü (`ItemCommand`, gleicher Aufbau wie `PieceCommand`, Einträge ab `id[4]`)
    und seine Frage (hoch/runter) wie im Koffer, ein Tipp auf einen Gegenstand schließt es (B).
  - **Akten** (`ss_file.cpp`): ein Tipp auf eine Zeile der Liste (Namens-Einheiten 0x21–0x25 von
    `IDC_SSCRN_FAR_1`, eine Zeilenhöhe) öffnet diese Akte: aus der Kategoriezeile hinunter in die
    Liste, hoch/runter zur Zeile, A (noch nicht gefundene Akte: der Fehlerton des Spiels).
  - **Karte** (`ss_map.cpp`, gezoomte Ansicht `MapRead`): ein gezogener Finger verschiebt die Karte
    mit (`re4_core_drag`, Kernprozess-Nachricht 27 `kDrag`, App `CoreBridge.drag` bei jeder Bewegung
    nach der Tipp-Schwelle, als Bruchteil des Spielbilds). Die Kamera wandert wie bei
    `mapCameraMove` (Position und Ziel in x/z, `CameraSetOrientationUp`), so weit, dass der Boden
    unter dem Finger mitgeht (aus Ansicht und Projektion der Kartenkamera im Kartenausschnitt
    `SsMapWork::vp`).
  - Die Drücke gehen je einer pro Bild hinaus (`Key.trg` und `Key.rep` wie ein frischer Druck) und
    hören auf, sobald der Bildschirm wechselt oder gewischt wird.
- **Tests:** `menutouch`: auf der Leiste A erst im 7. Bild mit dem richtigen Reiter (alle vier
  Wechsel); Wischen im gezoomten Kartenbild ohne Tasten; Tastenfolgen für Schlüssel (Reihe wechseln
  plus links, rechts, A, beim Kombinieren nichts in der anderen Reihe) und Akten (aus der
  Kategoriezeile, hoch, runter, A); Karte: mit einer Draufsicht-Kamera wie `mapCameraInit` wandert
  der Boden unter dem Finger genau um dessen Weg (±0,001). Kernprozess-Test: `re4ipc_drag` kommt an.
  i686, armhf, aarch64 0 Baufehler, alle Tests 0 Fehler; App übersetzt und gelinkt.
- **Nicht getestet:** am Gerät; die Trefferflächen der Schlüssel-Einheiten und der Akten-Zeilen
  kommen aus den Disc-Daten (in der Cloud nicht prüfbar).

### Reiter per Wischen wieder eingebaut, kürzere Bildformate (Wünsche des Projektinhabers, 22.10.2026, Cloud)

- **Klarstellung des Projektinhabers:** das Wischen für den Reiterwechsel hätte bleiben sollen; nur
  das Antippen der Reiter bleibt draußen. Wieder eingebaut (`re4_core_swipe`, Kernprozess-Nachricht
  25 `kSwipe`, App `CoreBridge.swipe` wie vor `6157f5b`), dabei die Ursache des „willkürlichen“
  Springens behoben: die Reihenfolge war die des Spiels (`sscrnMainMenu`: links/rechts zählen
  `menu_next` herunter/hinauf, 0 Schlüssel, 1 Koffer, 2 Karte, 3 Akten, 4 Beenden), aber
  - **kein Ring mehr:** am linken und rechten Ende tut ein Wischen nichts (vorher sprang es von den
    Schlüsseln zu den Akten und zurück); Zeile `touch: swipe to the one before: no tab there`;
  - **schnelles Wischen zählt vom Ziel:** ein Wischen, während ein Wechsel unterwegs ist, zählt vom
    Reiter, zu dem der Wechsel geht (vorher vom noch gezeigten Bildschirm: zwei schnelle Wischer
    landeten beim selben Reiter), auch nach dem A noch, bis dessen Bildschirm erscheint (höchstens
    90 Bilder); ein zweites Wischen behält die schon gedrückten B; zurück zum gezeigten Reiter, bevor
    B gedrückt wurde, bricht den Wechsel ab.
- **Tests:** `menutouch` neu: Tipp auf die Reiterleiste → keine Tasten; nach links → B, dann A mit
  Reiter 2; von der Karte (unechtes `SsMapMain`) nach rechts → B, A mit 1; zwei schnelle Wischer vom
  Koffer → Reiter 3 (nicht 2), nur ein B; nach 90 Bildern zählt wieder der Koffer, nach rechts → 0;
  weiter nach rechts unterwegs und von den Schlüsseln aus → nichts; beim Schließen nichts.
  i686, armhf, aarch64 0 Baufehler, alle Tests 0 Fehler; App-Java übersetzt, Link-Test gelinkt.
- **Bildformate im App-Menü kürzer** (Wunsch des Projektinhabers: auf kleinen Bildschirmen brach die
  Zeile um): „4:3 (GC)“ statt „4:3 (Original)“, „4:3“ statt „4:3 (ohne Balken)“, dazu „16:9“ und
  „Gestreckt“ (bleibt übersetzt); in allen zehn Sprachen gleich (Arabisch mit Richtungszeichen),
  Übersetzungsprüfung ok.

### Gerätetest 22.10.2026 20:09 (Retroid, 0.44.0): Reiter per Touch wieder ausgebaut

- **Befund des Projektinhabers:** das Antippen der Reiter oben funktioniert nicht, das Wischen
  springt scheinbar zu irgendeinem Reiter. Im Bericht: drei Tipps auf die Reiterleiste landeten als
  `touch: case: not on a cell (0.23 0.19)` (die Trefferprüfung der Reiter-Einheiten traf nicht), ein
  vierter als `touch: tab 3 (tap)`; die Wischgesten liefen logisch richtig im Ring (3 → 2 → 1 → 0 →
  3 nach rechts, 0 → 1 → 2 → 3 → 0 nach links), für den Spieler aber nicht nachvollziehbar.
- **Entscheidung des Projektinhabers (22.10.2026):** ausbauen. `runtime/menu_touch.cpp` behält nur
  die Tipps im Koffer (Cursor, A auf dem Gegenstand unter dem Cursor, Befehlsmenü, Frage; im
  Bericht ✅: `cursor to (2 3) … item 0x2a`, `A on item 0x2a`, `command 0 of 3: A`). Entfernt:
  Reiter-Trefferprüfung, Wechsel über B/A, `re4_core_swipe` (Kern, Kernprozess-Nachricht 25 nur noch
  als Kommentar, App `CoreBridge.swipe`); ein Wischen in Untermenüs tut nichts (das Messer-Wischen
  ignoriert Untermenüs). Patch 0106 bleibt unverändert (die Haupt-Widgets von Gegenständen, Karte
  und Akten werden nicht mehr gelesen).
- **Tests:** `menutouch` ohne die Reiter-Fälle, neu: Tipp auf die Reiterleiste im normalen Inventar
  und Tipp beim Schließen geben keine Tasten; Kernprozess-Test ohne `re4ipc_swipe`.

### Gerätetest 22.10.2026 20:00 (Retroid, Stand `f112ff7`, Logcat) und Freigabe 0.44.0

- **„4:3 (ohne Balken)“:** `picture: the game draws without its letterbox (r105)` beim Einschalten,
  `… with its letterbox …` beim Zurückschalten auf „4:3“ oder 16:9: Patch 0105 wirkt im Spiel.
  Ob ein 4:3-Bildschirm ganz voll ist, prüft ein Tester mit einem echten 4:3-Gerät (Bericht folgt).
- **60 FPS** bei Stufe 2 ohne MSAA: `618 in-between pictures, 7 cuts`, GPU 4,5–5,3 ms je Spielbild,
  0,4 Durchläufe zurück; der Render-Thread braucht 10,6–11,7 ms Prozessorzeit je Spielbild (ohne
  60 FPS 2,4–4,9 ms). In diesem Abschnitt wartete das Spiel zeitweise auf den Renderer (`GX core:
  46.6 ms each, of it 13.9 ms waiting for the renderer`, mit Menüpausen und Stufenwechseln im
  Zeitraum, darum nicht sicher); bei Bedarf später den Prozessoranteil der Zwischenbilder senken.
- **Messer per Wischen** ✅ (`knife: slash 1 by a swipe`), GPU-Prüfung der Filmfarben auf dem
  Adreno 650 weiter `ok, 32 of 32` (bisherige Schreibweise bleibt).
- **Entscheidung des Projektinhabers (22.10.2026):** daraus wird **0.44.0** (versionCode 69, wie
  vorbereitet); Releasetext auf je einen Satz pro Punkt gekürzt.

### Gerätetest 22.10.2026 19:40 (Retroid, Stand mit Messer per Wischen; Bericht und Logcat)

- **Messer per Wischen ✅:** 7 Schnitte (`knife: slash 1 … 7 by a swipe`), Neigung von -0,77 bis
  +0,79; je Schnitt L, dann L+A, dann L, wie geplant.
- **60 FPS ✅ (Zwischenbilder entstehen):** bei Stufe 1 ohne MSAA `188 in-between pictures` auf 190
  Spielbilder, 0 Schnitte, 78 % der 3D-Zeichnungen zugeordnet, GPU 2,3–2,9 ms je Spielbild, die
  GPU 0,3–0,4 Durchläufe zurück; der Render-Thread braucht dabei 9–10 ms Prozessorzeit je Spielbild
  (ohne 60 FPS 4–5 ms). Projektinhaber: flüssiger, aber am Retroid nur ohne MSAA und eine
  Auflösungsstufe tiefer (bei Stufe 2 mit 4× MSAA braucht schon ein Bild 15–17 ms GPU; mit
  Zwischenbild doppelt so viel). **Die FPS-Anzeige blieb bei 30:** sie zählt die Bilder des Spiels
  (das Spiel bleibt bei 30). **Neu:** mit „60 FPS“ zeigt sie zusätzlich die Bilder auf dem
  Bildschirm, Zwischenbilder mitgezählt: `30 FPS (Game) · 60 FPS (Screen)` (`ingame_fps_smooth`,
  zehn Sprachen; `CoreBridge.pictures`, Zähler `picturesMade` in `drawGx`).
- **„4:3 (no bars)“ am Retroid:** oben und unten blieb ein schwarzer Rand. Ursache: die App setzt das
  Bild des Spiels (448 Zeilen) wie ein Fernseher in einen 4:3-Rahmen von 480 Zeilen; auf dem
  1080 Pixel hohen Bildschirm bleiben oben und unten je 36 Pixel frei. **Neu:** in diesem Format
  füllen die Zeilen die ganze Höhe, in ihrer eigenen Form 640 × 448 (10:7); auf Bildschirmen, die
  schmaler sind, bis hinunter zu 4:3 (die Zeilen dann bis zu 7 % höher), damit ein 4:3-Bildschirm
  ganz voll ist. „4:3 (Original)“ bleibt wie bisher. Ob das Spiel selbst ohne seine Balken zeichnet,
  zeigt jetzt das Protokoll: `picture: the game draws without its letterbox (r105)` /
  `… with its letterbox …` bei jedem Wechsel (höchstens 40; Laufzeit-Test `letterbox` prüft beide
  Zeilen). Im Logcat des Tests war das Format nur rund 4 s lang im freien Spiel an.

### Filmfarben Galaxy A36: Ursache gefunden und behoben (Bericht 22.10.2026 19:30, 0.43.0, Cloud)

- **Bericht** (SM-A366B, Adreno 710, Treiber 0800.58, 64 bit): die GPU-Selbstprüfung meldet
  `6 of 32 pixels wrong`, nur nach der **vierten** Film-Stufe; nach 1, 2 und 3 Stufen alles richtig.
  Falsch: Rot immer 0, Blau 0, sobald der richtige Wert über 127 liegt (Grün stimmt).
- **Ursache:** die vierte Stufe (`sofdec.cpp` `setTevPrm` Stufe 3: `lerp(CPREV, APREV, K3)`, K3 =
  (0, 255, 0)) ist die einzige mit einem Eingang `a` ungleich 0. Der Shader rechnet die Mischung wie
  die Hardware: `(a << 8) + (b - a) * c`. Der Treiber des Adreno 710 rechnet `a << 8` nur mit 16 Bit
  (mit Vorzeichen): ab `a` = 128 wird der Wert negativ, nach der Begrenzung 0. Nachgerechnet
  (Python und in der Cloud als absichtlich so geschriebener Shader): **genau die sechs gemeldeten
  Pixel mit denselben Werten**. Rot und Blau laufen in dieser Stufe über `a` (c = 0), Grün über `b`
  (c = 255, `a` dort klein): darum blieb Grün richtig, helle Flächen wurden grün.
- **Behoben** (`gx_gl_shader.cpp`, `SetTevMath`): drei gleichwertige Schreibweisen der Mischung:
  wie bisher (`a << 8`), **ohne die Verschiebung** (`a + (((b - a) * c, gerundet) >> 8)`: für jede
  Eingabe dieselbe Zahl, weil `a * 256` ein Vielfaches von 256 ist) und in Gleitkomma (exakt, ganze
  Zahlen unter 2^24); die Mischung des Nebels ebenso ohne `prev * 256`. Die GPU-Selbstprüfung beim
  Start (`gx_selfcheck.cpp`) prüft erst die bisherige; mit falschen Pixeln die anderen, die erste
  richtige gilt für alle danach erzeugten Shader (des ganzen Spiels, nicht nur der Filme). Zeile im
  Protokoll z. B. `…; with the lerp without the shift by 8: 0 wrong; in use: the lerp without the
  shift by 8 (the movies right)`. Geräte, die richtig rechnen (Retroid), bleiben bei der bisherigen.
  Die Prüfung läuft jetzt vor jedem Renderer (auch vor `RE4_GX_REPLAY`).
- **Tests (Cloud, llvmpipe):** `gx_gl_check` neu `TestTevMath`: Schreibweise „16 Bit wie das A36“
  ergibt in der Film-Prüfung genau die sechs Pixel des Berichts (Zeichen für Zeichen), die
  Selbstprüfung wählt dann „ohne Verschiebung“ (0 falsch); alle drei echten Schreibweisen geben die
  Filmfarben richtig; 4 × 60 zufällige TEV-Einstellungen (1–4 Stufen, Register, Konstanten,
  Eckfarben über 127, alle Bias-, Scale-, Subtraktions-, Begrenzungs- und Vergleichsarten, Nebel)
  Byte für Byte gleich in allen drei Schreibweisen, die 16-Bit-Schreibweise weicht ab (108000 von
  201600 Pixeln, die Prüfung sieht es). `gx_gl_check` 0 Fehler; strenge Shader-Prüfung 1119 Shader,
  0 abgelehnt; `re4decomp_link_check` gelinkt.
- **Nicht getestet:** auf dem A36 selbst (der nächste Bericht muss `in use: the lerp without the
  shift by 8 (the movies right)` zeigen, die Filme richtig).

### Messer per Wischen (Wunsch des Projektinhabers, 22.10.2026, Cloud)

Wunsch: das Messer (zücken und schneiden) auch mit einer Wischgeste. Kein Eingriff in `src/`, kein
Patch: die App drückt für das Spiel die Tasten, die ein Spieler drücken würde; die Messer-Routine
des Spiels (`pl_knife.cpp`) macht den Rest (Bewegung, Treffer, Ton, Effekt).

- **App:** `CoreActivity.watchTap`: ein Finger, höchstens 700 ms, mindestens ein Sechstel der kürzeren
  Bildseite, in jede Richtung, nicht auf einem Bedienelement der Touch-Steuerung →
  `CoreBridge.slash(tilt)` (`tilt` = Steigung des Wischens, 1 senkrecht nach oben, -1 nach unten;
  getrennter Kernprozess `kSlash` = 26). Das waagerechte Wischen für die Reiter bleibt; der Kern
  wertet im Untermenü nur das eine, im Spiel nur das andere. Schalter im App-Menü unter
  „Komfortsteuerung“: **„Messer per Wischen“** (`ingame_knife_swipe`, zehn Sprachen, an; Vorgabe
  `knifeSwipe` in `core_input`), mit einer Hilfezeile (`ingame_knife_swipe_help`).
- **Kern** (`runtime/knife_swipe.cpp`, mit den Spiel-Flags in libre4_main; `re4_core_slash`): einmal
  je Spielbild, bevor das Spiel den Controller liest (Bild-Haken in `PADRead`), legt er Tasten auf
  Kanal 0 drauf (`re4_pad_add` in `runtime/pad.cpp`, neu: Tasten eines Bild-Hakens für genau dieses
  Bild, L/R mit vollem Analogwert, A mit `analogA`):
  - **L** (Messerhaltung, `joyLKamae`), bis das Messer draußen ist (`r_no_0` 0, `r_no_1` 0xB,
    `r_no_2` 1). Nur, wo L das Messer zieht: Hauptschleife, kein Untermenü, kein Ereignis, Leon oder
    Krauser, Spiel-Option „Messertaste“ (`CFG_KNIFE_MODE`) aus, Spieler in einer Lauf-Routine
    (`r_no_1` 0–5, dort läuft `actionSelect`) oder das Messer schon draußen. Vorher **kein A**: in den
    Lauf-Routinen ist A die Aktionstaste (Türen, Leitern, Gegenstände).
  - dann **L und A** (Feuertaste, `joyFireOn`), bis der Schnitt beginnt (`r_no_2` 2). Im ersten
    dieser Bilder setzt die Steigung die Höhe: die Mischung `m3r` (-1..1 für 45° nach unten .. oben;
    beim Zücken aus der Neigung der Kamera) wandert um den Teil der Steigung über 0,35 (fast
    waagerecht: gerader Schnitt), die Mitte des Zitterns (`cPlWep::pitch`, `m3r`·π/2) mit, wie der
    Stick beide in `PlWepLockCtrl` bewegt (und wie `gyro.cpp`).
  - **L allein**, solange der Schnitt schwingt, danach noch 10 Bilder: ein weiteres Wischen in dieser
    Zeit schneidet sofort wieder, ohne neu zu zücken. Danach loslassen, das Spiel steckt das Messer weg.
  - Abbruch (Tasten sofort weg): Untermenü, Ereignis, der Spieler verlässt die Messer-Routine
    (Treffer, gepackt), Zeitgrenzen (Zücken 45 Bilder, Schnittbeginn 15, Schwung 60). Ein Wischen,
    das nicht schneiden kann, oder eines älter als 400 ms verfällt.
  - Protokoll: `knife: swipes draw the knife and slash`, `knife: slash N by a swipe (tilt -1.00)`,
    `knife: swipe ignored (…)`, `knife: swipe slash stopped (…)`.
- **Grenzen:** mit sichtbarer Touch-Steuerung bewegt ein schnelles Wischen in der Look-Zone zugleich
  die Kamera (C-Stick) und schneidet; wer dort oft schnell schaut, schaltet den Schalter aus. Der
  Schnitt selbst bleibt die Bewegung des Spiels (immer dieselbe Seite); die Richtung des Wischens
  ändert nur die Höhe. Ada, Ashley, HUNK, Wesker: kein Messer auf L, Wischen ohne Wirkung. Mit der
  Spiel-Option „Messertaste“ an: ohne Wirkung (Zeile im Protokoll).
- **Tests (Cloud):** Laufzeit-Test `knifeswipe` (`game_probe.cpp` `re4probe_knife_swipe`: ein
  künstlicher Leon wird Bild für Bild durch die Routine geführt wie `pl_knife.cpp`): Untermenü →
  verworfen und nicht aufgehoben; aus dem Stand L, L, dann L+A mit `m3r` -1 und `pitch` -π/2 bei
  senkrechtem Wischen, L im Schwung, zweites Wischen im Schwung → sofort L+A, gerade (0,2 → keine
  Höhe), 10 Bilder L, dann nichts; Messer schon draußen → sofort L+A; getroffen → Tasten weg; Waffe
  im Anschlag, Ashley, Messertaste → nichts; Spiel zieht nie → genau 45 Bilder L. Kernprozess-Test:
  `re4ipc_slash` kommt an. i686, armhf, aarch64: 0 Baufehler, alle Tests 0 Fehler. App: alle
  Java-Quellen gegen `android-all` und RetroTouch übersetzt (0 Fehler), `core_mode.cpp` für den
  Host gebaut und gelinkt (der Link-Test `re4decomp_link_check` bekam dafür die Dateien des
  Kernprozess-Clients, ohne sie fehlten alle `re4ipc_*`), Übersetzungsprüfung alle ok.
- **Nicht getestet:** am Gerät (Gefühl der Geste, Schnitthöhe, Look-Zone).

### Gerätetest 22.10.2026 18:18 (Retroid, Stand mit 0105/0106) und Entscheidungen des Projektinhabers

- **Entscheidung des Projektinhabers (22.10.2026):** 0.43.1 wird **nicht** veröffentlicht; stattdessen
  kommt die experimentelle 60-FPS-Option dazu, alles zusammen später als **0.44.0** (versionCode 69,
  Releasetext `v0.44.0-alpha.md`; der Text von 0.43.1 ist darin aufgegangen und entfernt).
- **Gegnerliste r104/r106/r107 (`emlist_dump.py`, Disc 1 G4BE08):** 10 Einträge mit Dynamit.
  - **r104 (vor der Fischfarm):** in der ersten Gruppe (0xF0–0xFB, 0xFE) **ein Werfer**: 0xF7
    „Homing Bomber“ (`set` 0x25, Dynamit mit Ersatz am Gürtel, unbegrenzt, „Roof on“, auf einem Dach
    bei Höhe 3570, `Guard_r` 8000); bei den drei Stürmern der Fläche 0xE tragen 0xFC und 0xFD Dynamit
    (laufen auf Leon zu), in den Wellen 0xD9–0xE4 trägt 0xDC Dynamit. Insgesamt 4 Dynamit-Ganados.
  - r106: drei „R106 Fix Bomber“ (0x8D–0x8F) und ein Homing Bomber (0x90); r107 (Fischfarm): zwei
    „Homing Bomber2“ (0xEA, 0xEB).
  - **Ergebnis:** der Prototyp hat in r104 Dynamit-Ganados, auch gleich am Anfang (0xF7). Im Bericht
    `rep134850` (r104) erschien 0xF7 (Liste 247) und bekam eine neue Waffe in die rechte Hand (so
    nimmt ein Werfer das Dynamit vom Gürtel). Ob er im Port wirft, ist offen. **Neu im Protokoll:**
    `em: s12 at the dynamite post (list 247)`, `em: s12 lights a dynamite stick (1 so far)`,
    `em: s12 leaves the dynamite post` (em10 R1 0x50 FixBomber, Zündschnur bei `r_no_2` 4). Ein Bericht
    aus r104 (eine Minute spielen, zum Dach mit dem Werfer schauen) beantwortet es.
- **„4:3 (ohne Balken)“ sah aus wie „4:3 (Original)“:** im Bericht kam der Schalter im Kern an
  (`core: letterbox bars off while playing`, Spieler frei in r105). Ursache: das Spiel schneidet das
  Bild im Spiel **zweimal** zu: jeder Raumstart schaltet `SYS_SCISSOR_ON` ein (`game.cpp`
  `gameRoomInit`, außer mit dem Debug-Schalter `DBG_SCISSOR_OFF`), `Render` zeichnet dann nur die
  Zeilen 56..392 (`STA_SCISSOR`), die Balken von `Draw_cinesco` liegen darüber. Unter den Balken war
  also nichts gezeichnet. **Behoben (Patch 0105 erweitert):** `re4_letterbox_open()`
  (`runtime/picture.cpp`: Schalter an, Hauptschleife, kein Untermenü, kein Ereignis) lässt in `Render`
  die Schere und in der Hauptschleife die Balken weg, wie die beiden Debug-Schalter des Spiels.
  Die Lichtschein-Prüfung `esp0e` zählt weiter einen Rand von 56 Zeilen als verdeckt (Flag bleibt),
  ein Lichtschein ganz oben verblasst also etwas früher.
- **Reiter per Tipp:** der erste Tipp wechselte zur Karte (`touch: tab 2`, dann `ss_map.dat`), auf
  der Karte reagierten die Reiter nicht, weil die erste Fassung dort absichtlich kein B gab. Jetzt
  gibt sie auf der Karte B (aus der Zoom-Ansicht `MapRead` zur Reiterleiste, die Gesamtansicht
  zoomt erst hinein), bis zu dreimal im Abstand von 12 Bildern, bis die Leiste den Fokus hat; der
  Reiter wartet höchstens 45 Bilder. Ein Tipp auf den Reiter der eigenen Seite tut nichts.
- **Neu: Wischen** (Wunsch des Projektinhabers): ein Finger, waagerecht, mindestens ein Achtel der
  Bildbreite, höchstens 700 ms (`CoreActivity.watchTap`) → `re4_core_swipe` (getrennter Kernprozess
  `kSwipe`): nach links der nächste Reiter, nach rechts der vorige, die vier Seiten im Ring
  (Schlüssel, Koffer, Karte, Akten), nie „Beenden“. Hinweis: mit sichtbarer Touch-Steuerung bewegt
  ein Wischen in der Look-Zone zugleich den C-Stick (im Untermenü bewegt er den Cursor).

### Touch im Koffer (Wunsch des Projektinhabers, 22.10.2026, Cloud)

Wunsch: die Reiter oben antippen, im Koffer einen Gegenstand mit einem Tipp wählen, mit einem
zweiten Tipp das kleine Menü (ausrüsten, kombinieren …) öffnen.

- **App:** `CoreActivity.watchTap` erkennt einen Tipp (ein Finger, höchstens 500 ms, kaum bewegt)
  neben den Touch-Knöpfen (`TouchControls.onControl`: Knöpfe, Sticks, Steuerkreuz zählen nicht,
  die Look-Zone schon; bei ausgeblendeter Touch-Steuerung, etwa mit Controller, jeder Tipp) und
  gibt ihn an `CoreBridge.tap(x, y)`. `core_mode.cpp` merkt sich, wo `drawGx` das Bild zuletzt
  gezeigt hat (`notePicture`: Rahmen und gezeigte XFB-Zeilen, auch bei 16:9 und „Gestreckt“) und
  rechnet den Tipp in einen Platz im Spielbild um (u quer, v über die 448 Zeilen, 0..1); außerhalb
  des Bildes nichts. Weiter an `re4_core_touch` (im getrennten Kernprozess über `kTouch`).
- **Kern:** `runtime/menu_touch.cpp` (mit den Spiel-Flags in `libre4_main`) und Patch 0106
  (`ss_main.cpp` `SubScreenTask`): einmal je Bild des Untermenüs, bevor das aktuelle Menü `Key`
  liest, `re4_touch_sscrn(wk, cur, pzzlMain, itemMain, mapMain, fileMain)`. Ein Tipp, höchstens
  400 ms alt, wird zu Cursor und Tasten dieses Bildes:
  - **Reiter** (nur im normalen Inventar, `open_flag` 1; Treffer über die fünf Markierungseinheiten
    `IdSub` 0..4 Klasse 0): hat die Reiterleiste den Fokus (`state` 1 der Haupt-Widgets), `menu_next`
    und A (`sscrnMainMenu` wechselt dann wie mit Steuerkreuz und A); sonst erst B (im Koffer nur am
    Cursor, ohne Teil in der Hand und mit leerem Ablagefeld, wie B dort; in Schlüssel- und
    Aktenansicht B; auf der Karte nicht, dort vergrößert B die Gesamtansicht), der Reiter dann im
    Bild, in dem die Leiste den Fokus hat (höchstens 8 Bilder).
  - **Koffer-Cursor** (`PieceSelect` Zustand 0) und **Kombinationsziel** (`PieceCombine`): Tipp auf
    ein Feld (Koffer, Ablagefeld nur mit Teilen darauf) setzt `m_p_active_board`, `m_cur_x/y`,
    `cursor_flag` und Geräusch 6 wie das Steuerkreuz; Tipp auf das Teil unter dem Cursor = A (das
    Befehlsmenü bzw. das Kombinieren).
  - **Befehlsmenü** (`PieceCommand` Modus 0): Tipp auf einen Eintrag (`id[6+2i]`/`id[7+2i]`) wählt
    ihn (`cmd_menu_no`, Geräusch 0xA), zweiter Tipp = A; ein Tipp auf den Koffer schließt das Menü
    (B). **Frage** (Modus 2, Wegwerfen ja/nein, Leon/Ashley): Tipp auf eine Antwort (`sub[5]`,
    `sub[7]`) wählt sie (links/rechts), zweiter Tipp = A. Kein Tipp löst allein etwas aus, das das
    Spiel nicht zurücknimmt (Kombinieren, Wegwerfen, Benutzen brauchen zwei).
  - Nicht angefasst: Teil in der Hand (Verschieben weiter mit den Tasten), Untersuchen, die
    Schlüssel-, Karten- und Aktenansicht selbst, Händler und Funk; Debug-Editoren offen: nichts.
  - Lage auf dem Bild: Id-Einheiten wie `IdCommonTrans` (`IDSystem::m_scrn_mat` · `mat` · `ver`,
    dann `pG->Camera.ProjMat`), Koffer-Felder (100 Einheiten, `pzlBoard::m_mat`) mit
    `pG->Camera.v_mat` und `ProjMat`; Tipps auf den Koffer nur, wenn er ruht (Einheit 0xFE der
    Klasse 1 hat das Hereingleiten beendet). Während des Untermenüs ist das Bild 640×448 statt 512
    breit (`ScreenReSize`), die Anteile u, v bleiben gleich.
  - Protokoll: `touch: tab 2`, `touch: case: cursor to (6 3) in the case, item 0x05`, `touch:
    case: A on item …`, `touch: command 1 of 3: A`, `touch: question option 0: A`, `touch: tab 3
    dropped (…)` (die ersten 80, dann jede 50.).
- **Tests:** Laufzeittest `menutouch` (`game_probe.cpp` `re4probe_menu_touch`: Kamera des
  Untermenüs, Koffer 10×6 mit Pistole und Spray, nachgebaute Widgets: Feld → Cursor, gleiches Teil
  → A, kein Tipp → nichts, Befehl wählen → zweiter Tipp A, Tipp auf den Koffer → B, Frage: andere
  Antwort → links, gewählte → A, daneben → nichts; Treffer einer Id-Einheit). Kernprozess-Test:
  `re4ipc_touch` kommt an. Die Reiter selbst brauchen die echten Einheiten (Disc-Daten): **am Gerät
  zu prüfen**, ebenso ob die Treffer-Rechtecke zu den sichtbaren Bildern passen.
- Recherche dazu (Code gelesen, nicht ausgeführt): Reiterleiste, Koffer-Geometrie (Größen 10×6,
  11×7, 12×8, 15×8, Ablagefeld 6×12, Neigung −0,57 rad), Befehlsmenü je Gegenstandsart (Waffen:
  Ausrüsten, Untersuchen, Wegwerfen; …), Risiken (Wegwerfen „ja“, Kombinieren, B/Y beim Aufheben
  eines Gegenstands lässt ihn liegen, Zeiger `puzzlePlayer` nach dem Schließen ungültig).

### 60 Bilder/s mit Zwischenbildern, ohne die Engine umzubauen (Frage des Projektinhabers, 22.10.2026, nur Analyse)

- **Ausgangslage:** das Spiel rechnet 30 Bilder/s, alles in Bildern gezählt (Abschnitt „Touch-Steuerung
  und 60 FPS“, 19.10.2026: `SetSystemVcnt(1)` ließe es doppelt so schnell laufen). Die App zeigt schon
  jetzt 60 Bilder/s am Bildschirm, aber jedes Spielbild zweimal.
- **Weg 1, Zwischenbild im GX-Renderer (empfohlen, wenn überhaupt):** jedes Spielbild kommt als Chunk
  mit fertig aufgelösten Ecken, Indizes und den Matrizen (XF-Schreibzugriffe, `re4_gx_frame.h`). Liegen
  Bild N und N+1 vor, kann der Renderer Bild N+1 ein zweites Mal abspielen, dabei jede Zeichnung, die es
  in N genauso gab (gleiche Art, Textur, Zahl der Ecken, Matrix-Platz), mit den halbierten Matrizen und
  Eckpunkten (die Figuren rechnet das Spiel auf der CPU, `CalcSk1`) zwischen N und N+1 zeichnen; neue
  Zeichnungen (Partikel, Mündungsfeuer) erscheinen wie in N+1, 2D-Anzeigen ändern sich nicht. Kopien des
  Bildes (Tiefenschärfe, Hitzeflimmern, Unschärfe) laufen beim Abspielen von selbst mit. Schnitte
  (Kamera springt, Raumwechsel, Untermenü) erkennen und dort kein Zwischenbild. Kosten: doppelte
  GPU-Arbeit (Retroid bei 3× um 20 ms je Bild: zu viel, bei 2× machbar), doppelte Arbeit des
  Zeichen-Threads (Anbernic 6–7 ms: genug Luft), **+17 ms Verzögerung** (das Zwischenbild braucht das
  nächste Spielbild). Risiken: falsch zugeordnete Zeichnungen „schwimmen“, Texturverschiebungen und
  Effekte mit Zufallswerten flackern. Kein Eingriff in `src/`.
- **Weg 2, Bewegungsschätzung aus zwei fertigen Bildern** (wie Fernseher oder „Frame Generation“):
  braucht keinerlei Wissen über das Spiel, kostet auf Handy-GPUs aber viel und zeigt Fehler an Kanten
  und an den Anzeigen; ebenfalls +17 ms.
- **Weg 3, nur die Kamera mit 60 Bildern/s nachführen** (Umprojektion): Leon und die Gegner blieben bei
  30 Bildern/s, bei RE4 folgt die Kamera Leon, er würde gegenüber dem Bild ruckeln. Nicht sinnvoll.
- **Vorschlag:** Weg 1 als Schalter „Flüssige Bewegung (60 FPS, experimentell)“, in Schritten:
  (1) in der Cloud im Werkzeug `gx_chunk_render` mit einer Folge aufeinanderfolgender GX-Aufzeichnungen
  (R3 am Retroid speichert schon heute 12 Bilder hintereinander, `pull-gx-dumps.ps1`; oder vom PC
  mit `RE4_GX_DUMP`): Zwischenbilder als PNG, Zuordnung und Schnitterkennung prüfen; (2) im Renderer der App hinter dem Schalter, Gerätetest auf dem Retroid und
  einem schwächeren Handy. Aufwand etwa zwei bis drei Sitzungen. **Entscheidung des Projektinhabers
  offen** (am 19.10.2026: „vorerst nicht“).

### Meldung eines Testers (22.10.2026): Dynamit-Ganados fehlen in Kapitel 1-2 vor der Fischfarm

Frage des Testers: „Did you know about the absence of the Ganados carrying explosives—the ones
that don't appear in Chapter 1-2, right before you reach the fish-processing area?“ Untersucht in
der Cloud (Code und vorhandene Berichte), dann die Gegnerliste von den Disc-Abbildern ausgelesen
(Projektinhaber, PC). **Ergebnis: der Prototyp hat Dynamit-Ganados in r104 (vier Einträge, siehe
„Gegnerliste ausgelesen“ unten); kein Port-Fehler im Code gefunden. Ob sie auf dem Gerät werfen, ist
noch nicht gesehen: dafür jetzt Dynamit-Zeilen im Fehlerprotokoll.**

- **Räume im Prototyp:** r107 ist die Fischfarm (Fische id 0x27 auf Seehöhe, `src/st1/r107.cpp:71-83`;
  kein Gegnerskript, alles aus der Gegnerliste). r104 ist der Bereich davor („Steinbruch“): die Tür
  r104→r107 braucht Schlüssel 0xA6 oder seine Hälften 0xA4 + 0xA5 (`r104.cpp:181-185`, `:436-468`),
  erster Händler (`:690`). Gegner in r104 nur per Skript: 12 Ganados nach Ereignis s00 (Liste
  0xF0–0xFB und 0xFE, `:746-757`), drei stürmen bei Fläche 0xE herein (0xF2/0xFC/0xFD, `:223-231`),
  vier Nachschub-Wellen 0xD9–0xE4 (`:78`). Kapitel 1-1 endet in r106 (`r106.cpp:375`), 1-2 in r105
  (`r105.cpp:582`). r104, r106, r107 lesen die Gegnerliste `etc/emleon00.esl` (`stage.cpp:85`).
- **Ob ein Ganado Dynamit trägt, steht nur in der Gegnerliste:** Bit 0x00020000 von `flag`
  („Weapon Bomb“ im Listen-Editor, `src/t_emlist/t_emlist.cpp:34`) gibt `Wep_type = 9` in die Hand
  (`em10.cpp:18101-18106`), mit 0x2000 („Pickup waist“) als Ersatz am Gürtel (`:18590`), 0x10000
  („Weapon Mugen“) = unbegrenzt nachnehmen. Feste Werfer: `set` 0x24/0x25/0x2B starten FixBomber
  (Routine 0x50, `em10.cpp:3077-3091`, Ablauf `:9850`); der Editor nennt 0x24 **„R106 Fix Bomber“**
  (`t_emlist.cpp:190`). `r106_ctrlEm0/1` schicken die Ganados 0x8F und 0x8C auf feste Posten
  (`r106.cpp:239-266`), dort übergibt Goto-Art 1 an FixBomber (`em10.cpp:6894-6899`). Die Vermutung,
  die Werfer stünden in diesem Stand nur in r106, war falsch (siehe die Liste unten).
- **Nichts im Code entfernt Dynamit:** keine Schwierigkeits-, Szenario- oder Raumprüfung löscht
  0x20000 (`Game_level` ändert nur Zeiten). `type`, `set`, `flag`, `Guard_r` kommen 1:1 aus der Liste
  (`em_set.cpp:57-77`). Ein Werfer wirft nur, wenn zwei Bahnen 5000 Einheiten vor ihm frei sind
  (`em10BombThrowScaCk`, `:26612`); kommt Leon in `Guard_r`, wird er ein normaler Ganado, behält aber
  das Dynamit (`:9897`).
- **Port-Seite:** der Formatumwandler der Gegnerliste dreht genau die mehrbytigen Felder
  (`formats_room.cpp:424-436`), der Spielstand trägt die Liste mit richtiger Byte-Tabelle
  (`save_layout.inc`). Kein Patch berührt `em_set.cpp`, `emwep.cpp`, `obj08.cpp`; die Patches an em10
  (0023, 0087) und r104 (0091) betreffen keine Waffen. Dynamit-Ganados funktionieren in anderen Räumen,
  auch auf 64 bit (Trefferzeilen mit `wt 9`: rep112803 r223, rep125155/rep134431 r21x/r22x,
  rep200158 r3xx). Keine Zeile „Em set failed“, „SetWeapon() ModelInit failed“ oder „setEm failed“ in
  allen Berichten. Alter, behobener 64-bit-Fehler in r107: bis 0.38.0 passte `em12.drs` nicht in den
  Speicher, alle Ganados id 0x12 der Fischfarm fehlten (Regel für das Speicherende, Abschnitt
  „Neustart M12 (18.10.2026, Cloud)“) – nur wichtig, falls der Tester eine alte Version hatte.
- **Bericht mit r104:** nur `rep134850` (Retroid, 0.42.1, 64 bit, „File 01 r104“): nach s00/s01 alle
  12 Ganados ohne Fehler (Liste 240–251, 254), zehn Waffen (id 0x42), fünf davon am Gürtel; 1,85 s
  später eine neue Waffe in einer rechten Hand – so entsteht eine Waffe nur bei „Pickup waist“ +
  „Weapon Mugen“, genau wie bei FixBomber mit leerer Hand. Vielleicht läuft also doch ein Werfer in
  r104; welche Waffe es war, zeigt das Protokoll nicht (keine Trefferzeile, Lauf nach 25 s zu Ende).
- **Gegnerliste ausgelesen** (Projektinhaber am PC, 22.10.2026, mit dem neuen Werkzeug
  `port/tools/emlist_dump.py`: liest `etc/*.esl` aus den Disc-Abbildern, einem mit Dolphin entpackten
  Ordner oder der Datei selbst, Namen aus dem Listen-Editor des Spiels; Test `test_emlist_dump.py`
  mit einem künstlichen Abbild, läuft in `whole_build.sh` mit). Aufruf:
  `wsl python3 port/tools/emlist_dump.py --data /mnt/e/Development/Android/RE4-Discdata --rooms 104,106,107`
  (der erste, reine PowerShell-Befehl fand nichts: `RE4-Discdata` enthält die Abbilder
  `RE4_2004-11-25_Disc1.gcm` und Disc 2, beide mit derselben Liste). 69 Einträge, 10 mit Dynamit:
  - **r104 (4):** `F7` „Homing Bomber“ (`set` 0x25, Dynamit am Gürtel, unbegrenzt), erscheint nach
    s00 mit den ersten 12 und läuft als Streife (`r104_patrolData`) auf dem erhöhten Weg (Höhe 3570)
    zwischen (18840 3570 −27700) und (24710 3570 −27760); `FC`, `FD` (Dynamit am Gürtel) stürmen mit
    `F2` herein, wenn Leon Fläche 0xE erreicht (oben bei (33500 6720 −18000), `r104_execEmDash`);
    `DC` in der zweiten Nachschub-Welle (`r104_resetData[1]`).
  - **r106 (4):** drei „R106 Fix Bomber“ (`8D`, `8E`, `8F`) und ein „Homing Bomber“ (`90`), dazu
    acht Bärenfallen (id 0x2A).
  - **r107 (2):** zwei „Homing Bomber2“ (`EA`, `EB`, `set` 0x2B) unter den zwölf Ganados der
    Fischfarm.
  - Im Bericht `rep134850` (Retroid, 0.42.1) erscheint `F7` als s27 (Liste 247) bei
    (18841 3530 −27711); die Waffe, die 1,85 s später in einer Hand auftaucht (s79), ist sein
    Dynamit (FixBomber nimmt bei leerer Hand zuerst eine Stange vom Gürtel, Schritt 6/7). Ob er
    wirft, zeigt der Bericht nicht (Lauf nach 25 s zu Ende).
- **Wann ein Werfer wirft** (`em10_R1_FixBomber`, `em10.cpp:9850`): erst wenn er Leon gesehen hat
  (Homing: `Be_flg` & 1), Leon vor ihm steht (in seinem Blick höchstens 2000 seitlich, 1000 bis
  20000 voraus) und zwei Bahnen 5000 voraus in Höhe 2000 frei sind (`em10BombThrowScaCk`, Treffertest
  der Szenerie mit `EatMgr.hitCheck`). Kommt Leon näher als `Guard_r` (F7: 8000), wird er ein
  normaler Ganado mit Dynamit. Die Stürmer zünden über `em10IgnitionCk` (Leon näher als 15000 auf
  dem Weg und gesehen).
- **Neu im Fehlerprotokoll** (`state_trace.cpp`, nach 0.43.1, kommt mit 0.44.0): `em: s79 dynamite thrown by s27 at
  (x y z)` (eine Waffe, id 0x42, startet als Dynamit: `cEmWep::setBombThrow`, Routine 9) und
  `em: s79 dynamite exploded at (x y z)` (Routine 2 nach der Explosion; „fell into the water“ bei
  Routine 4). Die Treffer-Zeilen, die Wurf (hp 1) und Explosion (hp 0) vorher ausgelöst hätten,
  entfallen dafür. Test `enemylog` (Laufzeittest) um Wurf und Explosion einer Waffe erweitert.
  **Geprüft (Cloud, Stand nach dem Zusammenführen `68265621`):** `whole_build.sh i686 armhf aarch64`
  0 Baufehler, alle Tests 0 Fehler, `enemylog: ok` auf allen drei, `enemy list tool: ok`.
- **Offen:** auf dem Gerät „File 01 r104“ laden, in Sichtweite des erhöhten Wegs gehen
  (F7) und zur Fläche 0xE hinauf (FC/FD); wirft niemand, liegt ein Port-Fehler vor (dann
  `em10BombThrowScaCk`/`EatMgr.hitCheck` und die Sicht prüfen). Der nächste Tester-Bericht aus r104
  zeigt es mit den neuen Zeilen.

### Bericht SCV40 (0.43.0, 22.10.2026 21:55): der Aufzug nach Saddler, jetzt Abbruch im Ton

- Patch 0101 wirkt: der Aufzug von r332 fährt, der Raumwechsel nach r331 kommt
  (`DOORDEMO START/END`, `GetDataExt DSE` ohne Absturz in `sceInLock`). Dann in r331 20 ms nach
  `BGM0 seq 2 play`: `SND SEQ data error : 07 63 00` und `OSPanic … snd_seq2.cpp:60`, SIGABRT.
- **Ursache:** beim Raumwechsel bittet `nextRoomBgmCheck` beide Musik-Plätze, die noch spielen,
  auszublenden (`stop_req`), und setzt den Speicher für Musik-Bänke zurück: die Bänke des neuen
  Raums liegen wieder ab `mram_base_addr_bgm`/`aram_base_addr_bgm` abwärts, über denen des alten
  (r332: BGM1 bei MRAM 0x8039a180–0x8039c400; r331: BGM0 bei 0x8039af80–0x8039d200, ARAM ebenso
  überlappend). `SndRoomBgmLoad` wartete nur auf den Platz, den es lädt. Nach Saddlers Ereignis
  war BGM 0 schon aus (`SND: BGM 0 STOP` 21:41:00), BGM 1 blendete noch aus; die neue Bank
  überschrieb seine Noten, sein Sequenzer las die neuen Bytes als MIDI. Ohne das Ereignis spielten
  beide, das Warten auf Platz 0 deckte Platz 1 mit ab (0.42.1, 19:54: BGM 1 eine Sekunde vor BGM 0
  aus). Der Port lädt einen Raum in Millisekunden, die Disc brauchte viel länger.
- **Behoben mit Patch 0104:** `SndRoomBgmLoad` wartet, bis kein Musik-Platz mehr ausblendet (mit
  der Unterbrechung aus Patch 0059).
- **Geprüft (Cloud):** `whole_build.sh i686 armhf aarch64` mit 0104: 0 Baufehler, alle Tests 0
  Fehler, die Zeile im vorbereiteten Baum. **Nicht getestet:** der Aufzug auf dem Gerät.

### Bericht SCV40 (0.42.1, 22.10.2026 20:01): Spiel durchgespielt, ein Absturz am Aufzug in r332

- Der Tester hat das Spiel durchgespielt (Spielstände nach 9:25 und 11:33 Stunden, r316/r331). Ein
  Absturz um 19:52 in r332, 23 s nach dem Ende von Ereignis r332s10, beim Aufzug: SIGSEGV in
  `sceInLock` (Aufgabe für eine verschlossene Tür), gestartet über `TaskExec_hook`, Fehleradresse =
  Argument + 0x6E (`door.key_id`), das Argument zeigte in einen Stapel, der nicht mehr da war.
- **Ursache:** `SceAtExecRoomJump` (Raumwechsel aus Skripten: die Aufzüge von `sce_com.cpp`, r120,
  r221, r225, r30a, r30b, r316, r331) baut eine Tür-Fläche als lokale Variable und füllt nur das
  Ziel; `key_id`, `key_flg`, `key_se`, `open_se`, `fade_eff` blieben Stapel-Reste. Auf der GameCube
  harmlos, im Port war `key_id` 1 oder 2: `sceAtFunc_door` startete `sceInLock` mit `&w`, die
  Aufgabe lief ein Bild später, als der Stapel schon weg war. Auch der Raumwechsel wäre nicht
  gekommen. **Behoben mit Patch 0101:** die Fläche beginnt mit Nullen (offene Tür, normale
  Blende, erstes Türgeräusch des Ziels). Ein schlummernder Fehler des Originalcodes (nicht gesetzte
  lokale Variable, auch im Prototyp), den erst die anderen Stapelinhalte des Ports zeigen. Suche
  nach weiteren lokalen `SCE_AT_DATA` und Aufgaben mit Zeigern auf lokale Variablen: kein Fall.
- Die übrigen Abstürze in `crash-system.txt` (15:07, 15:40 Formen der Brücke; 16:07–16:21
  Bulldozer) sind die schon behobenen aus 0.41.3 (0098, Brücken-Formen).
- **Geprüft (Cloud):** `whole_build.sh i686 armhf aarch64` mit 0101: 0 Baufehler, alle Tests 0
  Fehler, die Zeile steht im vorbereiteten Baum. **Nicht getestet:** der Aufzug auf dem Gerät (der
  Absturz hing vom Stapelinhalt ab, also nicht jedes Mal).

### Berichte Galaxy A36 (Filmfarben, 0.42.1, 22.10.2026 14:28) und Galaxy S10 (langsam, 0.41.2, 21.10.2026 23:24)

- **A36 (SM-A366B, Snapdragon 6 Gen 3, Adreno 710, Treiber 0800.58, Android 16, 64 bit):** der
  Vorfilm läuft im richtigen Takt (2278 Bilder in 76 s, nichts ausgelassen, 0 CRI-Fehler), 0
  Shader-Fehler, die Programme kommen teils aus dem Programm-Speicher. Die Farben rechnet also die
  GPU anders. Der Film-Shader (`sofdec.cpp` `setTevPrm`, vier TEV-Stufen) ist der einzige mit einem
  negativen Farbregister (C0 = −111, 0, −138, 68), einer Verdopplung negativer Zwischenwerte und
  Grün über den Alpha-Weg (Stufe 3 mischt APREV in G). Nachgerechnet (Python, alle Formeln des
  Shaders): 16-bit-Überlauf, Verschieben negativer Zahlen, logisches statt arithmetisches
  Rechtsschieben ergeben andere Bilder; eine fehlende Begrenzung der Farbwerte (ivec3-`clamp`) gäbe
  grüne helle Flächen und harte Stufen, ohne Bildschirmfoto nicht sicher. **Neu: GPU-Selbstprüfung**
  (`gx_selfcheck.cpp`, einmal je Lauf vor dem Renderer, auf einem eigenen Renderer bei Stufe 1): acht
  Testfarben (Y, U, V) durch die Film-Stufen nach 1, 2, 3 und 4 Stufen, zurückgelesen und mit der
  Rechnung des Shaders auf der CPU verglichen; Log-Zeile `GPU check (movie colours): ok, 32 of 32
  pixels …` oder die falschen Pixel mit Wert und Sollwert. Der nächste Bericht des A36 zeigt damit,
  welche Stufe falsch rechnet. Baukasten für solche Chunks jetzt gemeinsam
  (`gx_chunk_builder.h`, aus `tools/test_gx_gl.cpp`).
- **S10 (SM-G970F, Exynos 9820, Mali-G76, Android 10, 64 bit; Stufe 1, ohne MSAA):** in ruhigen
  Szenen 33,5 ms je Bild (30 Bilder/s, 19–30 ms Leerlauf), in r100 mit Gegnern 38–46 ms, davon
  10–20 ms **Warten auf den Renderer**: die Grafik ist hier der Engpass (≈ 22–26 Bilder/s). Ob GPU
  oder Prozessor des Render-Threads, sagt 0.41.2 nicht: auf Android 10 nimmt die App kein logcat auf,
  die Zeilen des Renderers (`RE4GX`, `RE4Core`: GX-Statistik, GPU-Zeit, Render-Thread) fehlen im
  Bericht. **Neu:** die App schreibt ihre Zeilen zusätzlich in die Log-Datei des Kerns
  (`re4gx::LogTo`, `re4_log_file_line` in `runtime/log.cpp`), damit auch Berichte von Android 10 und
  älter sie enthalten. Vorschlag an den Tester: Tiefenschärfe im App-Menü aus (auf dem Retroid +19 %
  GPU-Arbeit), mit 0.43.0 einen neuen Bericht nach einigen Minuten im Dorf.
- **Geprüft (Cloud):** `gx_gl_check` (llvmpipe): `GPU check (movie colours): ok, 32 of 32`, 0 Fehler;
  absichtlich verfälschter Shader (negative Werte vor der Verdopplung auf 0) → 28 von 32 Pixeln als
  falsch gemeldet, mit Wert und Sollwert. `re4decomp_link_check` übersetzt `core_mode.cpp` mit den
  Änderungen (nur die bekannten fehlenden `re4ipc`-Teile des Host-Links, auch ohne die Änderung).
  `whole_build.sh i686 armhf aarch64` mit `re4_log_file_line`: 0 Baufehler, alle Tests 0 Fehler.
  **Geprüft (Retroid, Adreno 650, 22.10.2026):** `GPU check (movie colours): ok, 32 of 32 pixels`,
  30 ms beim Start; die Filme dort richtig. **Nicht getestet:** das A36 und die Log-Zeilen der App
  in `core-log.txt` eines Berichts.

### Das „E“ mitten im Untermenü (Projektinhaber, 22.10.2026, Retroid, 0.42.1; Berichte 13:42 und 13:48, R3-Mitschnitt)

- **Bild:** ein kursives „E“ und ein durchsichtiges Rechteck mitten im Koffer, das „E“ bleibt an
  derselben Stelle auf allen Seiten (Schlüssel, Akten, Karte). Mitschnitt (`gx_chunk_render`,
  jedes Zeichnen einzeln weggelassen): Zeichnen 138 ist das „E“ (Textur 18×17 C4 aus
  `ss_cmmn.dat`), Zeichnen 131 das Rechteck (Kachel ±200 × ±100 um den Weltursprung, Farbe
  `ffffff20`).
- **Was es ist:** die Ausrüstungsmarke. `ss_pzzl.cpp` `pzzlEquipDisp` setzt die Marke (IdSub
  0x30, `IDC_SSCRN_ETC`) an die Ecke der ausgerüsteten Waffe im Koffer und zeichnet um die Waffe
  einen Rahmen (`pieceFrameDisp` Art 3). Beim Schließen läuft `sscrn_pzzl_out` weiter und lässt
  die Marke mit dem Koffer aus dem Bild gleiten; sie bleibt „sichtbar“, aber außerhalb.
- **Ursache:** die ausgerüstete Waffe hatte keinen Platz im Koffer. Beide Spielstände
  (r105 nach 26 Minuten mit dem Killer7, `weapon 0x06`; r104 nach 13 Minuten mit der Handcannon (0x37),
  `weapon 0x0f`; Munitionsanzeige „EMPTY“ bzw. „01“, im Koffer nur die Pistole mit 2 Schuss) haben
  Gegenstände, die `pzlPlayer::init` nicht an ihren gespeicherten Platz legen kann (Log:
  `game: pzlPlayer::pzlPlayer() Can't locate piece.`; das Spiel-Log zeigt jede Meldung nur dreimal,
  es können mehr sein). Das Teil liegt dann nirgends, sein Modell wird nie platziert (Matrix im
  Ursprung): Marke und Rahmen landen in der Bildmitte und gleiten beim Schließen nicht mit.
  Solche Waffen so früh im Spiel gibt wahrscheinlich der „Case Maker“ des Debug-Builds (`ss_debug.cpp`:
  Inventar-Sätze `ItemMgr.setUp`, Koffergröße), den R1 im Koffer bis 0.41.3 unsichtbar öffnete
  (Patch 0093 seit 0.42.0); dort ersetzt links/rechts das Inventar und ändert die Koffergröße.
  Die Spielstände sind vom 20.10.2026.
- **Behoben mit Patch 0100:** `pzlPlayer::init` legt erst alle Teile an ihre Plätze, danach die
  ohne Platz an die erste freie Stelle (im Koffer, sonst im Ablagefeld; wie gespeichert, sonst
  gedreht wie `PutInCase`) und schreibt `case: item 0x2a had no place at (2 1) turn 0 in the case;
  moved to (11 1) turn 0 in the case`. Der Koffer speichert die Anordnung in jedem Bild, der
  Gegenstand behält den neuen Platz (nach dem nächsten Speichern auch im Spielstand). Ohne freie
  Stelle bleibt es beim Original-Fehler; eine Waffe ohne Platz bekommt dann keine Marke
  (`pzzlEquipDisp`). Für gültige Spielstände ändert sich nichts (der Zweig läuft nur, wenn ein Teil
  nicht passt).
- **Tests:** Laufzeittest `casefree` (`game_probe.cpp` `re4probe_case_free_place`): kleiner Koffer
  mit Pistole und Spray an den Plätzen eines neuen Spiels, ein Killer7 auf dem Platz der Pistole,
  Pistolenmunition außerhalb des Koffers; danach liegen alle vier im Koffer, Pistole und Spray
  unverändert, Killer7 bei (11 1), Munition bei (17 0), beide Zeilen im Log.
- **Geprüft (Cloud):** `whole_build.sh i686 armhf aarch64` mit 0100: 0 Baufehler, `casefree: ok` auf
  allen drei, alle Laufzeit-, Kern- und Kernprozess-Tests 0 Fehler.
- **Geprüft (Retroid, 22.10.2026, logcat des Projektinhabers):** neues Spiel, über das Debug-Menü des
  Spiels (SELECT halten) die Handcannon geholt und ausgerüstet (`Wep15 MAGNUM prolog Ok`); beim
  Öffnen des Koffers `case: item 0x37 had no place at (0 0) turn 0 on the spare board; moved to
  (13 1) turn 0 in the case`. Auch das Debug-Menü gibt Gegenstände also ohne Platz (`construct`:
  Ablagefeld, (0 0)), nicht nur der „Case Maker“. Im Bild (Projektinhaber): die Handcannon liegt
  sichtbar im Koffer, kein „E“ mehr in der Mitte und auf den anderen Seiten; ein geladener alter
  Spielstand ebenso. Bildschirmfoto 15:36: nach dem Ausrüsten des Killer7 im
  Koffer steht das „E“ an seiner linken oberen Ecke mit dem hellen Rahmen um die Waffe, wie im
  Original. Die Waffe aus dem Debug-Menü (`t_option.cpp` → `cItemMgr::debugWeapon`: `get` ohne
  Platz, dann `m_pWep`) zeigte es nach Angabe des Projektinhabers erst nach erneutem Ausrüsten;
  im normalen Spiel rüstet man nur über den Koffer aus. Beim nächsten Öffnen des Koffers bleibt das „E“ an
  der Waffe (Projektinhaber). Erledigt. Logcat 15:29–16:29 (Spielstand 3, r105): drei Gegenstände
  aus dem Ablagefeld verlegt (0x2a Killer7 und 0x37 Handcannon in den Koffer, 0x03 Matilda auf eine
  freie Stelle des Ablagefelds); nach dem Speichern um 15:36 und erneutem Laden um 16:29 keine
  `case:`-Zeile mehr, die neuen Plätze stehen also im Spielstand.

### App-Menü: Bildformat „Gestreckt“, neue Reihenfolge, mehr Zeilenabstand (Wünsche des Projektinhabers, 22.10.2026)

- **Bildformat:** Reihenfolge jetzt „4:3 (Original)“, „16:9“, neu „Gestreckt“ (`ingame_stretched`
  in allen zehn Sprachen). Die gespeicherten Werte bleiben (1 = 4:3, 0 = 16:9, Standard), neu 2:
  dieselben Zeilen 56..391 der 448 wie bei 16:9, aber über den ganzen Bildschirm gezogen
  (`core_mode.cpp`, `Present` mit Bildschirmbreite und -höhe). Die Zeile mit der Auflösung zählt
  bei „Gestreckt“ wie bei 16:9 336 Zeilen.
- **Zeilenabstand:** zwischen allen Zeilen des Menüs 4 dp Luft (eine unsichtbare Trennlinie der
  `LinearLayout`, `SHOW_DIVIDER_MIDDLE`); die Abstände um die Überschriften bleiben dazu.
- **Geprüft (Cloud):** Übersetzungsprüfung alle ok, alle Java-Quellen mit javac übersetzt,
  `core_mode.cpp` ohne Fehler in der Syntaxprüfung für ARM64 und ARM32. **Nicht getestet:** auf dem
  Gerät (Aussehen, „Gestreckt“ auf einem 20:9-Bildschirm).

### App-Menü: „B einrasten (Touch)“ und Gyro-Empfindlichkeit (Wünsche des Projektinhabers, 22.10.2026)

- **B einrasten (Touch):** in der Touch-Zeile neben „L und R einrasten“ (unter „Unterdrücken“).
  Ein Antippen hält B (Laufen) bis zum nächsten Antippen, Leon läuft also ohne Finger auf B;
  aus: B wie bisher nur gedrückt, solange der Finger darauf ist. `CoreTouch.setLatchB`
  (unabhängig von L/R), Einstellung `touchLatchB`, beim Umschalten und beim Zurücksetzen (Pause,
  Steuerung ausgeblendet, Editor) wird B losgelassen; RetroTouch zeigt das eingerastete B stärker.
  Hilfetext in allen Sprachen um B ergänzt. Neue Prüfungen in `tests/CoreTouchTests.java`.
- **Gyro-Empfindlichkeit:** Schieberegler rechts neben dem Gyro-Haken, nur bedienbar, solange
  Gyro-Zielen an ist; 0,5× bis 3× in Schritten von 0,25 (1× = wie bisher, der Wert steht daneben).
  `GyroAim.setSensitivity` multipliziert die gefilterten Drehraten (die Totzone von 0,006 rad/s
  bleibt vor der Multiplikation), Einstellung `gyroSensitivity`. Mit dem Controller: Steuerkreuz
  hoch/runter auf den Regler, links/rechts verstellt ihn. Der Haken heißt jetzt kürzer
  „Gyro-Aiming“ / „Gyro-Zielen“ (Platz, Vorschlag des Projektinhabers; zh ohne „使用“), die
  übrigen Sprachen waren schon kurz.
- Neue Texte `ingame_latch_b`, `ingame_gyro_sensitivity` (Beschreibung des Reglers für
  Bedienungshilfen) in allen zehn Sprachen; `tools/check_translations.py`: alle ok.
- **Geprüft (Cloud):** `CoreTouchTests` 260 Prüfungen bestanden; alle Java-Quellen der App mit
  javac gegen die Android-Bibliothek übersetzt (0 Fehler). **Nicht getestet:** auf dem Gerät
  (Aussehen der Zeilen in allen Sprachen, Regler mit dem Controller).

### Rückmeldungen zu 0.42.1 (22.10.2026): Laser-Gang r318, Filmfarben auf dem Galaxy A36

- **Laser-Gang (SCV40 = Galaxy S9, Snapdragon 845, Adreno 630, 64 bit; Bericht 17:34 und Video):**
  Leon geht durch r318, die Wand-Emitter zeigen kurze schräge Strahlen, Leon wird zerschnitten
  (`run: player hit, life 1580 -> 0`, zweimal, bei x ≈ -12 700, in den Szenenflächen 5 und 6),
  danach „Continue“. Die Muster 0 und 1 sind im Original ohne Ausweich-Taste (Modus 2: ausweichen
  durch Abstand halten, jeder Treffer des Strahls tötet). Ursache: der Strahl wurde auf 64 bit vom
  falschen Anfangspunkt gezeichnet (Patch 0099), der Treffertest nahm den richtigen Strahl. Die
  Effekt-Sicht `R318EspView` dahinter ist seit 0074 richtig. Suche nach weiteren festen Abständen
  im vorbereiteten 64-bit-Baum: übrig sind Datenpuffer von der Disc (Teile-Daten `part + 0x20`,
  Tabellen, Texturen), `cAtariInfo::m_flag` (+0x1A, ohne Zeiger davor) und `ESP1B_WK` (gleiche
  Lage auf 32 und 64 bit, nachgerechnet): kein weiterer Fall.
- **Filmfarben (Galaxy A36, Snapdragon 6 Gen 3, Adreno 710; zwei Bildschirmfotos, kein Bericht):**
  der Vorfilm (opening.sfd) mit verfälschten Farben: helle Flächen grün, dunkle braun, dazwischen
  türkis, mit harten Stufen. Die Filme laufen über die GX-Nachbildung (CRI zeichnet Y, U und V als
  I8-Texturen und rechnet die Farbe mit TEV-Stufen und Konstanten). Grün in hellen Flächen passt zu
  Farbanteilen (U, V), die zu klein ankommen; die harten Stufen zu einem Überlauf in der
  Ganzzahl-Rechnung des TEV-Shaders. Auf dem Retroid (Adreno 650) sind die Filme richtig, von anderen
  Geräten keine solche Meldung. Ohne
  Bericht des A36 (GPU-Treiber, Shader-Fehler) nicht weiter eingrenzbar. **Offen.**
- **Geprüft (Cloud):** `whole_build.sh i686 armhf aarch64` mit 0099 (Stand `1453b41b`): 0 Baufehler,
  alle Tests 0 Fehler; `RE4_OFS` bestätigt beim Bauen für i686 und armhf den GameCube-Abstand 0x70.
  **Nicht getestet:** der Laser-Gang auf dem Gerät.

### Bericht SCV40 (0.41.3, 22.10.2026 16:21): Absturz auf dem Bulldozer, Absturz beim Laden

Meldung: „When Ashley gets carried away by a zombie, the game crashes. Also, the game sometimes
crashes when loading from a checkpoint.“ Gerät wie zuvor (Samsung SCV40, Android 10, 64 bit).
`crash-system.txt` enthält vier Abstürze, zwei Arten:

- **16:07 und 16:15, r30f (Insel, Bulldozer):** SIGSEGV (Schreibzugriff auf Code) in
  `cEm::setStatus`, gerufen von `Sub_dm_bull`, Ashleys Schadens-Handler während der Fahrt. Ein
  Ganado packt oder trifft Ashley auf dem Bulldozer, `damageCheck` setzt Routine 1, `moveDamage`
  ruft `pAuxDm()` ohne Argument, der Handler nimmt sein erstes Argument als Ashley: auf ARM64 die
  eigene Adresse (Fehleradresse 0xfa163698 = `Sub_dm_bull` bei 0xfa163218 + 0x480, wo `status` auf 64 bit liegt). **Behoben mit Patch 0098**
  (Aufruf mit `this`, wie r3 auf der GameCube). Dieselbe Fehlerart wie 0091 (Wert aus einem
  Register); die Werkzeuge der Fehlersuche sahen sie nicht, weil ein Aufruf ohne Argument kein
  ungesetzter Wert ist. Suche über alle Funktionen, die als `void (*)()` gespeichert und ohne
  Argument gerufen werden: nur dieser Handler liest sein Argument (die übrigen sind
  Zeichen-Rückrufe mit eigenem Argument-Feld).
- **15:07 und 15:40 (beim Laden):** SIGSEGV in `HermiteInterpolation` ← `CalculateShape_new` ←
  `re4_bridge_capture`: die Zustandsbrücke rechnete in jedem Spielbild die laufenden Formen
  (Gesichter) aller lebenden Modelle nach; beim Laden oder Weitermachen zeigt die Formliste eines
  noch lebenden Modells schon auf freigegebenen, neu belegten Speicher. Das Spiel zeichnet ein
  solches Modell nicht, die Brücke las es trotzdem. **Behoben:** die Brücke rechnet Formen nur
  noch für ihre eigene Ansicht (`RE4_VIEW=bridge`, `RE4_BRIDGE_SHAPES=1`,
  `re4_bridge_shapes`), die GX-Ansicht braucht sie nicht. Spart nebenbei Rechenzeit im Kern.
  Kerntest `morph` prüft beides (ohne Brücken-Ansicht keine Formen, mit ihnen wie bisher).
- Das „Luis-Hütten“-Problem (0087), an das der Tester dachte, ist ein anderer Weg (Luis statt
  Ashley, `SetSubDamage`), aber dieselbe Familie: Partner-Rückrufe, die auf 64 bit anders laufen.
- **Geprüft (Cloud):** `whole_build.sh i686 armhf aarch64` (Stand `811297e2`): 0 Baufehler, alle
  Tests 0 Fehler, neu `partnerdmg` und der Kerntest `bridge` mit und ohne Formen. **Nicht
  getestet:** auf dem Gerät (Bulldozer r30f, Laden und Weitermachen).

### Bericht Galaxy A30s (0.42.0, 22.10.2026 05:53): durchgehend 10–15 Bilder/s

- **Gerät:** Samsung SM-A307G (Galaxy A30s, Exynos 7904: zwei schnelle Kerne Cortex-A73 1,8 GHz,
  sechs langsame A53 1,6 GHz; **Mali-G71 MP2**), Android 11, 64-bit-Prozess, Kern im
  App-Prozess. Einstellungen schon am unteren Ende (Auflösung 1, keine Kantenglättung, keine
  Tiefenschärfe, Anisotropie 1), Bildschirm 720×1560 bei 60 Hz. GPU-Zeit nicht messbar (seit
  0.42.0 auf Mali aus). Kein Absturz, kein Fehler im Protokoll.
- **Spielbilder je Sekunde** (aus den `pad: frame`-Zeilen; 30 = volle Geschwindigkeit): Titel und
  Wald (r120, r100) 27–30; **Dorf (r101) 10–25, oft 10–13**; r103 wieder um 24. Das passt zur
  Meldung.
- **Der Kern** braucht im Wald 34–37 ms je Spielbild bei 0–6 ms Leerlauf (Grenze 33,3 ms): er
  allein schafft knapp 27–29 Bilder/s. Auf dem Retroid (Snapdragon 865) waren es 33,3 ms mit
  13–19 ms Leerlauf.
- **Der Renderer** (der Zeichen-Thread der App) braucht im Dorf 40–74 ms je Spielbild für 450–890
  Zeichenbefehle (700–1100 Uniform-Blöcke, 1,1–2,2 MB, 100–150 Programmwechsel, 360–815
  Textur-Bindungen); der Kern wartet dann 10–34 ms je Bild auf ihn. Auf dem Retroid waren es 4–6
  ms bei 500–900 Befehlen, also etwa zehnmal schneller. Ob der Grafikchip (Mali-G71 MP2, ein
  sehr kleiner Chip) oder der Prozessor im Treiber die Zeit braucht, zeigt der Bericht nicht.
- **Neu im Protokoll** (für den nächsten Bericht eines schwachen Geräts): `re4_cpu_info.h`
  (`port/whole/runtime/`, nur Kopfdatei, von Kern und App benutzt). Einmal die Kerne des Geräts
  (`processor: 8 cores: 0-5 at most 1586 MHz, 6-7 at most 1794 MHz`), dazu in der Kern-Zeile
  `GX core: … ; N ms of processor time each, on fast/middle/slow cores a/b/c %, last on core K at
  M MHz` und eine neue Zeile `GX render thread: N ms of processor time per chunk of M ms run, on
  fast/middle/slow cores …`. Rechenzeit gleich Laufzeit: der Thread rechnet (beim Renderer: der
  Treiber); deutlich weniger: er wartet (beim Renderer: auf den Grafikchip). Der Anteil auf den
  langsamen Kernen zeigt, ob Android die beiden vollen Threads auf die schnellen Kerne legt
  (auf diesem Gerät gibt es nur zwei). Neuer Laufzeittest `cpuinfo`.
- **Mögliche Schritte** (Entscheidung des Projektinhabers offen): je nach Messung die beiden
  Threads auf die schnellen Kerne legen; im Renderer weniger GL-Aufrufe je Zeichenbefehl
  (Texturen und Uniform-Blöcke zusammenfassen); im Kern den GX-Decoder (Punkte umrechnen, auf
  dem Retroid ein Drittel der Kernzeit) beschleunigen.
- **Geprüft (Cloud):** `whole_build.sh i686 armhf aarch64`: 0 Baufehler, alle Tests 0 Fehler, neuer
  Test `cpuinfo` auf allen drei; `core_mode.cpp` ohne Fehler in einer Syntaxprüfung mit clang für
  ARM64 und ARM32 (Android-Kopfdateien als Platzhalter). **Nicht getestet:** die neuen Zeilen auf
  einem Gerät (kommen mit der nächsten Version).

### Drei Berichte mit Video (0.41.3, 22.10.2026): Aufrüsten beim Händler, unsichtbarer Biss

Meldung des Projektinhabers zu den drei SCV40-Berichten (11:28, 12:51, 13:44) mit einem Video:
das Aufrüsten einer Waffe wirkte nicht, nach einem Aufrüsten ging das Nachladen nicht mehr, und in
*431 griff ein Gegner Leon an, ohne dass seine Bewegung zu sehen war.

- **Aufrüsten (Port-Fehler, behoben mit Patch 0094):** `LvUpConfirm::move` schrieb die Stufen über
  die Bitfeld-Sicht `TuneLevel`; auf ARM und x86 liegen die Felder in umgekehrter Reihenfolge. Wer
  die Feuerkraft kaufte, bekam Kapazität (und umgekehrt), Schussgeschwindigkeit und Nachladen
  tauschten ebenso. Damit „wirkt“ das Aufrüsten nicht, und Feuerkraft/Kapazität und Nachladen
  stehen auf Stufen, die die Waffe so nie hatte. Zwei Wege zu „kein Nachladen“: ein späterer
  Kauf kann die Kapazität wieder senken (das Magazin hält dann mehr, als erlaubt ist, und gilt als
  voll), oder die gekaufte Schussgeschwindigkeit landet als Nachlade-Stufe über der Tabelle (drei
  Stufen, `PlReloadEndTbl`). Welcher es beim Tester war, zeigt das Protokoll nicht: das Aufrüsten
  schrieb noch keine Zeile. `view_scan.py` kennt diese Fehlerart
  jetzt (`bits`); der Lauf über den ganzen Spielcode findet nur `TuneLevel` (die übrigen Bitfelder,
  `db_cam.h`, `snd.h`, werden nie als ganzes Wort gelesen). **Bereits aufgerüstete Waffen in
  vorhandenen Spielständen behalten ihre vertauschten Stufen**; ein weiterer Kauf setzt alle vier
  Stufen neu aus den angezeigten Werten.
- **Unsichtbarer Biss (Fehler des Prototyps, behoben mit Patch 0095):** in r307 (13:44:03) ein Regenerator
  (s9, em36 Typ 0) ohne Beine im Kriech-Zustand; ab 13:44:13 99 Bilder lang je 14 Schaden (Leon
  1380 → 1). Der Gegner läuft in Routine 1/0x14 (`D_Catch`) und 1/0x15 (`D_CatchHit`, 155 Bilder),
  dem Biss am Boden. Leons Seite (`plem36_D_CatchHit`) prüft aber, ob der Gegner noch in 1/0xB
  (dem Biss im Stehen) ist: das ist nie der Fall, Leon wird im ersten Bild losgelassen, die
  Gegner-Bewegung und die Kamera fehlen, der Biss trifft weiter, „SHAKE OFF“ bleibt stehen. Der
  Code trifft die SHA1-Prüfsumme, also derselbe Fehler auf der GameCube-Debug-Fassung (das
  Verkaufsspiel hat ihn behoben). **Entscheidung des Projektinhabers (22.10.2026): Fehler des
  Prototyps, die das fertige Spiel nicht hat, werden behoben** (Regel in `CLAUDE.md`). Patch 0095
  prüft jetzt 1/0x15. Neues Werkzeug `port/tools/scan/catch_scan.py`: vergleicht bei allen
  Gegnern die Routine, die ein Halte-Rückruf prüft, mit den Routinen, die ihn setzen (18
  Prüfungen). Nur 0095 war ein Fehler; em10 (Luis im Würgegriff prüft 0x34 statt 0x35) und em39
  (Krausers Tritt prüft 0x1B statt 0x2D) verknüpfen mit `&&` und lassen erst los, wenn der Gegner
  die Routinen-Gruppe 1 verlässt; dort kommen diese Nummern nicht vor (Schaden 22/6, Tod 6/2
  Einträge): ohne Wirkung, nicht geändert.
- Sonst in den Berichten nichts Neues (siehe den Abschnitt „Drei Berichte des SCV40-Testers“).

**Geprüft (Cloud):** `whole_build.sh i686 armhf aarch64` mit 0094 und (zweiter Lauf) 0095: 0
Baufehler, die Patches gelten in allen drei Bäumen, 114 Module ohne offene Verweise, Laufzeit-,
Kern-, Kernprozess-, Format- und Filmtests 0 Fehler. **Nicht getestet:** das Aufrüsten und der
Biss am Boden auf dem Gerät (kein Test im Cloud-Lauf erreicht den Händler oder r307).

### Drei Berichte des SCV40-Testers mit 0.41.3 (22.10.2026 11:28, 12:51, 13:44)

- Samsung SCV40 (Android 10, 64 bit), ohne Beschreibung. Etwa zweieinhalb Stunden Spiel von r220
  bis r306 (Erzmine mit Lore r223/r21b, Statuen-Jagd r226, Salazars Thronsaal r228, Insel r300);
  der zweite und dritte Bericht sind derselbe Lauf.
- **Kein Absturz, keine Todesschleife, keine ungültige Position** (`pl:`/`em: … not a number`),
  keine `check:`-Zeile. In r21d zwei Tode durch die Stampfer mit voller Lebensenergie, Leon jeweils
  innerhalb der Stampfer-Fläche (`die: area 0x06` / `0x14`, „for 1“: die Flächen zählen nur den
  Spieler); das Weitermachen am Gitter-Zwischenstand lief normal weiter. `math: acosf/asinf …
  taken as 1` in r21d über 100-mal: die Korrektur aus 0.41.3 greift dort laufend.
- Sonstige Tode mit Leben 0 ohne Auffälligkeit (r223 dreimal, Lore r21b zweimal, r226, r228
  fünfmal beim Boss). Mehrfache Raumstarts in r228 und r300 ohne Tod: „Retry“ aus dem Menü
  (`--DOORDEMO START!!` ohne `--CONTINUE SELECT!!`).
- Menü-Cursor: alle 1747 Bewegungen kamen vom Steuerkreuz oder vom linken Stick mit passendem
  Ausschlag, keine vom C-Stick, keine ohne Eingabe.
- Ergebnis: nichts zu beheben; offen, ob der Tester etwas Bestimmtes zeigen wollte.

### Bericht moto g72 (0.41.3, 21.10.2026 18:13): Absturz im Mali-Grafiktreiber

- **Gerät:** Motorola moto g72 (Android 13, 64 bit im App-Prozess, **Mali-G57 MC2**, MT6789).
  Sitzungen 18:04–18:07 (r208, die Burghalle: Raumstart mit 27 Gegnern, zwei Treffer auf s20,
  Leon 1200 → 480 → 1, dann vom Tester beendet) und 18:11–18:13 (r214, Inventar). Die neuen
  Zeilen kommen an (`em:`, `room:`, `pad: menu`, `math: acosf/asinf of 1.00000012 taken as 1`
  in r214: die Korrektur von 0.41.3 greift). Zur Burghalle in 25 Sekunden kein Verschwinden
  (`em: -`), zu kurz für den gemeldeten Fehler.
- **`crash-system.txt`:** 18 Abstürze des Geräts seit 20.10.2026, davon 10 (alle Versionen bis
  0.41.3, zuletzt 17:50 nach einer Stunde Spiel) mit derselben Stelle: SIGSEGV an Adresse 0 in
  `libGLES_mali.so`, aufgerufen von `re4gx::GxRenderer::Impl::TimerPool::Collect` (GPU-Zeitmessung
  über `GL_EXT_disjoint_timer_query`, `glGetIntegerv(GL_GPU_DISJOINT_EXT)` /
  `glGetQueryObjectuiv`). Die übrigen 8 sind ältere Kernfehler vom 20.10. (behoben seitdem).
- **Behoben (App, `gx_gl.cpp` `TimerInit`):** auf Mali-Grafikchips (`GL_RENDERER` enthält „Mali“)
  keine Zeitmessung mehr, eine Zeile `GPU time: off on … (its driver crashes in timer queries)`.
  Die Messung dient nur der Diagnose (GPU-Zeit im Protokoll, GPU-Messlauf); Adreno (Retroid)
  misst weiter. Geprüft: Syntaxprüfung von `gx_gl.cpp` mit clang (Android-Kopfdatei als
  Platzhalter). **Nicht getestet:** auf einem Mali-Gerät.

### Fehlersuche mit Werkzeugen, Teil 1: der Code (22.10.2026, Cloud, Branch `claude/re4-start-controls`)

Auftrag des Projektinhabers: die Fehlerarten der letzten Berichte vorab im ganzen Spielcode suchen
(Teil 2, der automatische Raum-Rundgang am PC, folgt). Die letzten fünf Fehler waren nur einmal
reine 64-bit-Fehler (0087); sonst ein ungesetzter Wert (0088), eine Annahme über die Lage lokaler
Variablen (0089), die Byte-Reihenfolge (0090) und Rundung (r21d). Neue Werkzeuge in
`port/tools/scan/`, beide über die Übersetzungsbefehle eines Builds (`whole-armhf`):

- **`uninit_scan.py`**: clangs Warnungen für ungesetzte Werte (70 Meldungen, 44 Stellen) und der
  statische Analysator (Pfad für Pfad; 387 Meldungen, davon 269 Null-Zeiger und 23 Teilungen durch
  0, die auf der GameCube genauso wären; 61 Stellen außerhalb der Werkzeug-Module geprüft).
- **`view_scan.py`** (libclang): Sichten mit anderer Breite auf ein Objekt an fester Stelle
  (`((u8*) p)[3]` über einem `s32`, auch über Zeigerwerte, die `whole_byte_views.py` nicht sah)
  und die Adresse einer lokalen Variable über die Variable hinaus (als Zahl, als größerer Typ,
  Zeigerrechnung). Selbsttest: findet 0089, 0090 und 0063 im Originalcode wieder. 43 Funde.
- **Gleitkomma:** nach `acosf`/`asinf` (`ARC_CALL`) keine weitere Stelle: `SQRTF` gibt bei Werten
  ≤ 1e-5 schon im Original 0 zurück, die `sqrtf` der Bibliothek nehmen nur Abstände (Summen von
  Quadraten).

**Behoben:**
- Patch 0091: r221 (`SceEventStart` mit ungesetztem Wert in drei der vier Gasbomben-Szenen, im Raum
  des Insekten-Bosses, in dem der r21d-Tester vorher sechsmal starb; ob das die Ursache war, ist
  nicht belegt), `cMot3::set` (Überblendlänge der Ziel-Bewegungen aller Waffen aus einem
  weggelassenen Register), r104 (geöffnete Regale/Kisten beim Wiederkommen).
- Patch 0092: Krauser (em39) verglich bei der Wahl seines nächsten Punkts die falschen Bytes.
- Patch 0093: **Debug-Editoren auf R1** im Gegenstand- und Koffer-Bildschirm (Z der GameCube):
  unsichtbar, aber mit dem Steuerkreuz und A wirksam (Gegenstände geben/entfernen, ganzes Inventar
  ersetzen, Koffergröße, Peseten). Jetzt nur mit `RE4_DEBUG_HUD=1`. Antwort auf die Frage des
  Projektinhabers, ob die Debug-Fassung Fehler macht, ohne dass jemand das Debug-Menü öffnet:
  diese beiden schon (seltsame Inventar- oder Menü-Effekte von Testern passen dazu, belegt ist es
  nicht). Sonst ist im normalen Spiel nichts davon erreichbar: L+START ersetzt (0061), Pads 2–4
  „kein Controller“, Debug-Schalter beim Start aus, START+Z zeigt nur das (ausgeblendete) Protokoll.

**Geprüft, unbedenklich** (Original-Verhalten, Fehlalarm oder ein Weg, den das Spiel nicht
nimmt): `est.cpp` `eff1` (im Original genauso ungesetzt verglichen, nur Treffereffekte), `item.cpp`
`sz` (nur bei 0 Einträgen), `em_wrap.cpp` `beginEvent/endEvent` (nicht aufgerufen), `ss_pzzl.cpp`
`n` (toter Wert), `r332.cpp` (`strstr` einer Konstanten), `route_ck.cpp` (0088 hält die gefüllten
Einträge), `math_sub.cpp` `Matrix2AxisAngle` (`v3` später gesetzt), `motion.cpp` (Werte aus der
Tabellen-Funktion), `ctrl14`, `pl_push`, `foot_shadow`, `ik`, `cam_motion`, `cockpit`, `obj09`,
`obj1b`, `r20e`, `ss_map`, `dvd`, die ungesetzten `GXColor` der Filter und Schatten (`GXSetChanAmbColor`; die Kanäle nehmen die Umgebungsfarbe dort von den Eckpunkten, `GXSetChanCtrl(…, GX_SRC_VTX, …)`),
Schleifen-Variablen (`card.cpp`, `r202.cpp`, `ss_shop.cpp`, `esp08.cpp`); Byte-Sichten: die
Bewegungsfolgen (`motion.cpp`: der Umwandler lässt Byte 2 als Byte, der Mischtabellen-Kopf ist ein
`u32`), `shape.cpp`, `r117.cpp` (geprüfte Port-Zeilen), EMI-Typ-Bytes, `sce_at.cpp`; die Adressen
lokaler Variablen als Zahl (`libgpu.cpp`, `r209.cpp`, `sce_at.cpp`, `r21d.cpp` `setReset`: die
Aufgaben-Stapel liegen unter 4 GB, AArch64 erweitert 32-bit-Argumente mit Nullen).
Nur in Entwickler-Werkzeugen: `dbmodule.cpp` `Draw_tile` (Farbe rückwärts), `t_rck`, `t_flag`,
`db_light`, `t_*`.

**Geprüft (Cloud):** `whole_build.sh i686 armhf aarch64` mit 0091–0093: 0 Baufehler, alle Patches
gelten, 114 Module ohne offene Verweise, Laufzeit-, Kern-, Kernprozess-, Format- und Filmtests 0
Fehler. **Nicht getestet:** die Stellen auf dem Gerät (r221 Gasbomben, Zielen mit Waffen, r104
Wiederkommen, Krauser, R1 im Inventar).

### Fehlersuche mit Werkzeugen, Teil 2: der Raum-Rundgang (22.10.2026, lokal am Windows-PC in WSL2, Branch `claude/re4-start-controls`)

Auftrag des Projektinhabers (vorgezogener Teil von 10b): jeden Raum mit den Disc-Daten über die
Raumauswahl laden, eine Weile laufen lassen und Abstürze, ungültige Positionen („not a number“),
`check:`-Hinweise und Hänger sammeln; dazu ein Prüf-Build mit Sanitizern; erst i686, dann ARM64
unter qemu.

**Werkzeuge:**
- **Rundgang** (`whole_r100_check.sh --rooms LISTE`, Windows: `test-r100-core.ps1 -Rooms all`):
  mehrere Räume gleichzeitig (`--tour-jobs`, Standard: halb so viele wie Prozessorkerne), je
  6000 Bilder ab der Hauptschleife (vorher 1500, ohne Eingaben). Sobald Leon frei ist, zweimal
  dieselbe Folge (laufen, A, drehen, rennen, zielen und fünf Schüsse beim Drehen, Karte auf und zu,
  rückwärts, Kurve, Messer, Y und START mit B); solange er nicht frei ist, ab Bild 1500 A, A+B, L+R
  und START (Funksprüche und Hinweise, Reaktionstasten in Ereignissen, Söldner-Menü).
  `--tour-script none` lässt ihn stehen. Neu `port/tools/room_tour_report.py`: eine Zeile je Raum,
  dann **Fehler** (Absturz mit Bibliothek, Versatz und Funktion; Stillstand mit Rückverfolgung;
  Raum startet nicht; `not a number` und `LIMIT_ANGLE … not finite`; Todesschleife = mindestens
  zwei Tode höchstens 120 Bilder nach dem Raumstart; AddressSanitizer) und **Hinweise** mit den
  Räumen, in denen sie vorkamen (`check:`, UBSan, Spiel-Heap voll, Umwandler, Stubs, Meldungen des
  Spiels, Gegner-Sprünge, Tode, Raumwechsel, Räume ohne freien Leon, acosf/asinf-Schutz).
  Einzelheiten in `rundgang-einzelheiten.txt`, alle Protokolle in `rundgang-logs.tar.gz`, je
  Raum das Ergebnis in `rundgang.json`; ein Rundgang eines anderen Ziels wird mit dem letzten
  i686-Rundgang verglichen (`room_tour_report.py --compare`).
- **Prüf-Build mit Sanitizern** (`RE4_SANITIZE=address,undefined port/tools/whole_build.sh i686`,
  Ordner `whole-i686-O2-san`; im Rundgang `--sanitize`, Windows `-Sanitize`): AddressSanitizer
  hält beim ersten Zugriff außerhalb einer globalen Variable, einer lokalen Variable oder eines
  Blocks des C-Heaps an (mit Datei und Zeile), UndefinedBehaviorSanitizer nennt jeden undefinierten
  Rechenschritt einmal je Lauf. Ausgelassen (Begründung in `port/whole/CMakeLists.txt`): vptr,
  function, return, null, nonnull-attribute, pointer-overflow, object-size (`PG_OFS`,
  `&((GLOBAL_WK*) 0)->f`), alignment (über 100 Stellen der Tonbibliothek, erlaubt wie auf der
  GameCube), float-cast-overflow (durch `-fno-strict-float-cast-overflow` festgelegt).
  `port/src/stack_context.cpp` meldet ASan jeden Stapelwechsel (nur im Sanitizer-Build). ASan-
  Optionen: kein Leck- und ODR-Test, die eigene Absturzbehandlung des Ports, keine „Fake Stacks“
  (je Spiel-Stapel 11 MB: der 32-bit-Adressraum lief voll, r106; lokale Variablen lagen außerhalb
  von `VALID_PTR`, das Spiel meldete tausendfach `cLight::getNormal() INVALED PTR`).
- **ARM64 unter qemu** (`--aarch64`, Windows `-Arm64`): mit `QEMU_RESERVED_VA` = 4 GB. Ohne das
  bleiben neben qemus eigenen Bereichen 53919 Seiten über 4 GB unreserviert, und ein später
  geladenes Gegner-Modul (em23 in r100) landete dort (`re4_check_game_address` hält an); das
  betrifft nur qemu, auf Android lädt die App selbst.
  `whole_build.sh` startet aarch64 jetzt ebenso (im UBSan-Build stürzte sonst der Laufzeittest in
  `Yz2DecodeExec` an einer gekürzten Adresse ab); geprüft mit dem normalen und dem UBSan-Build,
  alle Prüfungen 0 Fehler.
- **Sanitizer auf 64 bit:** nur UBSan (`--sanitize` wählt das selbst). ASan geht nicht: auf
  x86_64 liegt sein Schattenspeicher (0x7FFF8000–0x8FFF6FFF) über MEM1, unter qemu-aarch64 stürzt
  schon ein Dreizeiler mit GCCs `libasan` ab (clang hat für aarch64 keine Laufzeit), und in 4 GB
  Gast-Adressraum ist für den Schatten ab 64 GB kein Platz. UBSan nimmt auf aarch64 GCCs
  `libubsan` (gleicher Code).

**Ergebnisse mit Disc-Daten** (126 Räume der Raumtabellen, je 6000 Bilder, i686 -O2):
- **i686, erster Lauf:** 126 von 126 ohne Fehler. Leon war in 15 Räumen nie frei (dort liefen
  keine Eingaben); dafür die Rückfall-Tasten.
- **i686, zweiter Lauf** (mit Rückfall-Tasten, Patches 0096/0097, Stand `24656174`): **126 von 126
  ohne Absturz, Stillstand, NaN oder Todesschleife**, gut 7 Minuten bei 8 Räumen gleichzeitig.
  Leon starb in 44 Räumen (Weitermachen lief jedes Mal), wechselte in 15 Räumen durch eine Tür
  oder ein Ereignis den Raum, war nur noch in r101, r206, r215, r227, r22b und r316 nie frei. Der
  acosf/asinf-Schutz (r21d, 0.41.3) griff in 120 Räumen. Einziger `check:`-Hinweis: die bekannte
  Formen-Tabelle in r229 (Patch 0064). Spiel-Heap voll nur `cDataSwap` (mit Rückfall auf ARAM) in
  r206 und r226.
- **i686 mit Sanitizern, erster Lauf:** 118 von 126; ASan fand `commonModelTrans` (unten, Patch
  0096) in r20a, r20b, r216, r31d und lief in r106, r208, r210, r40a aus Speicher (Fake Stacks);
  UBSan die Verschiebung um 255 (Patch 0097) und drei geprüfte Stellen (unten).
  **Zweiter Lauf: 126 von 126**, keine ASan-Meldung, UBSan nur die zwei geprüften Feld-Überläufe.
  Etwa 12 Minuten.
- **aarch64 unter qemu** (Stand `24656174`, 14 Räume gleichzeitig, 3 bis 11 Minuten je Raum, zusammen knapp eine Stunde): **126 von 126 ohne Fehler**, und jeder Raum gleich beurteilt wie auf i686 (`--compare`: Fehler, `check:`-Hinweise, Tode, Raumwechsel). Nur der Engpass von `cDataSwap` (r206, r226) kam auf aarch64 nicht vor. **aarch64 mit UBSan** (`--aarch64 --sanitize`, gut eine Stunde): ebenfalls 126 von 126 und gleich beurteilt wie i686; UBSan meldet dieselben zwei geprüften Feld-Überläufe (Lore r21b, r203) wie auf i686, auf 64 bit nichts Eigenes. Danach armhf-Build mit allen Prüfungen unter qemu-arm: 0 Fehler.

- **armhf (ARM32, der Kern auf den meisten Geräten) unter qemu-arm** (nachgereicht, `--armhf`,
  Windows `-Arm32`; 14 Räume gleichzeitig, 5 bis 28 Minuten je Raum, zusammen gut 1,5 Stunden):
  **125 von 126**, jeder andere Raum gleich beurteilt wie auf i686. **r222 stürzte mit SIGBUS ab**
  (Patch 0102, unten). Danach eine **Ausrichtungs-Suche** über alle Räume (`RE4_SANITIZE=alignment
  --sanitize`, i686: UBSan meldet jeden Zugriff an einer Adresse, die für den Typ nicht passend
  ausgerichtet ist): außer der Tonbibliothek nur 16-bit-Werte an ungeraden Adressen (Bewegungen,
  Kamera, Formen; ARM liest sie mit einfachen Befehlen) und die CRI-Tabellen (Patch 0103); keine
  Gleitkommazahl, kein Vektor, kein Zeiger. **Nach 0102/0103** (mit den Patches 0100/0101):
  i686 126 von 126, armhf r222 und die 27 Räume von `set` 28 von 28 (gleich beurteilt wie i686),
  die Ausrichtungs-Suche ohne Fund außer den 16-bit-Werten, Builds i686, armhf und aarch64 mit
  allen Prüfungen 0 Fehler.
**Behoben:**
- `re4_sdk_intrinsics.h` / `re4_cri_port.h`: `__rlwimi` und `__rlwinm` nehmen von Verschiebung und
  Maskengrenzen nur die unteren 5 Bit, wie der Befehl. `GXBump.c` `__GXSetIndirectMask` übergibt
  `~0xFF` als Verschiebung (auf der GameCube 0, 24, 31: das untere Byte von `bpMask`); x86 rechnete
  eine leere Maske (das Register blieb 0x0F0000FF), ARM stimmte nur zufällig, für den Optimierer
  undefiniert. Die GX-Nachbildung wertet dieses Register (Indirekt-Textur-Maske) nicht aus.
- **Patch 0096** `trans.cpp`: Modelle mit `blend_mode` 5 (r20a, r20b, r216, r31d) lasen die
  Mischart hinter der Tabelle `bl[5][4]`. Auf der GameCube stehen dort die Adressen der
  Licht-Funktionen von `trans_lit.cpp` (`funcLightParam`): kein Mischen, Logik-Op 12; im Port
  lagen dort andere Konstanten (ein ungerades erstes Wort hätte mit beliebigen Faktoren gemischt).
- **Patch 0097** `esp_app.cpp`: Effekt-Bereiche mit Nummer 255 (r102, r103, r119, r120, r227)
  setzten mit `1 << 255` auf aarch64 und x86 Bit 31 (Zahl modulo 32), auf GameCube und armv7 nichts:
  auf 64-bit-Geräten liefen die Effekte von Bereich 31 dort an.
- Formatumwandler: ein Ereignis-Bündel (`EVS`) ohne Ereignisse, dessen Tabellen-Zeiger das
  Füllmuster 0xCDCDCDCD trägt (r101, r111, r400, r401, r407–r409), und ein Gegenstands-Paket
  (`ITM`) ohne Gegenstände mit BIN-Tabelle bei 0 (r215, offen seit 17.10.2026) blieben
  big-endian, weil der Umwandler Tabellen las, die das Spiel bei 0 Einträgen nie liest
  (`EventMgr::SetEvs`, `cItmSys::DataLoad`). Jetzt umgewandelt; neuer Kerntest „empty tables“.
  Für ihn liefert `re4_formats_entry_status` den Umwandlungsstand eines Archiv-Eintrags.
- **Patch 0102** `include/ctrl.h`: `cCtrl::work` lag bei 0x13, jeder Arbeitsbereich der kleinen
  Raum-Helfer also an einer ungeraden Adresse. clang fasste in `GetCtrlDragon` (r222, die Drachenköpfe
  im Burgsaal) zwei Zeiger-Schreibzugriffe zu `strd` zusammen, und das braucht auf ARM32 eine durch 4
  teilbare Adresse: SIGBUS. Ein 64-bit-Kernel korrigiert das für 32-bit-Apps erst ab Linux 6.1, auf
  den Geräten stürzte der 32-bit-Kern also in r222 genauso ab (64 bit und x86 erlauben den Zugriff).
  `work` liegt jetzt bei 0x14, das Objekt bleibt 0x214 Byte.
- **Patch 0103** `mpv_bdec.c`, `mpv_lib.c`: vier Byte-Tabellen des Film-Dekoders, die `UTY_MemcpyDword`
  wortweise kopiert (ab 16 Wörtern auf ARM32 mit `ldm`), lagen nur zufällig auf Wortgrenzen; eine
  davon fand die Ausrichtungs-Suche 2 Byte daneben. Sonst würde jeder Filmstart auf dem 32-bit-Kern
  mit SIGBUS enden. Jetzt auf 4 Byte ausgerichtet wie auf der GameCube.
- Werkzeuge: `RE4_SANITIZE=alignment` nimmt die Ausrichtungs-Prüfung (sonst ausgelassen); UBSan
  überlässt Abstürze wie ASan der eigenen Behandlung des Ports (`handle_segv=0` …); `--rooms` kann
  mischen (`222,set`); jeder Rundgang ergänzt die gespeicherten Ergebnisse seines Builds Raum für Raum
  (`room_tour_report.py --merge`), der Vergleich prüft nur die Räume des neuen Rundgangs (vorher
  überschrieb ein kurzer i686-Lauf die volle Vergleichsliste).
- **r10b, Effekt-Block (`st1/r10b.das` Eintrag 7, nachgereicht am 22.10.2026):** blieb unvollständig
  umgewandelt, weil der Umwandler bei 0xddfe0 Bewegungsdaten mit 175 Gelenken las, deren erstes auf
  `+0x20202020` zeigte. Ursache in den Originaldaten: Effekt-Modell 0x7c hat in seiner
  Bewegungs-Tabelle drei Einträge zu je 0x2040 Byte, die **Texturen** sind (TPL-Kennung `00 20 AF 30`,
  ein Bild 128×128 CMPR, Daten ab 0x40), bei denen **jedes Null-Byte als 0x20 gespeichert ist**
  (`20 20 af 30 20 20 20 01 20 20 20 0c …`). Kein anderes Archiv der Discs hat ein Effekt-Modell mit
  Bewegungen. Das Spiel liest eine solche Bewegung nur für einen Effekt `ESP_EFM04` mit `Tool_flg` 8
  (`esp_efm.cpp`, `EspGetEfmMotAddr` → `MotionSetCore`); auf der GameCube wären diese Daten genauso
  unbrauchbar, r10b benutzt sie im Rundgang nicht. Der Umwandler (`formats_eff.cpp`) erkennt eine
  Textur in einem Bewegungs-Platz jetzt an der TPL-Kennung (auch mit 0x20 statt 0), lässt sie
  unverändert und meldet `effect model at 0x…: 3 of the 3 motions are textures (TPL magic 2020af30,
  original data)`; der Block gilt als umgewandelt. Neuer Kerntest „effect texture motion“ (schlägt
  ohne die Änderung fehl). Geprüft: Builds i686, armhf und aarch64 mit allen Prüfungen, r10b, r100 und r21d im Rundgang
  ohne Fehler, `GetDataExt EFF` in r10b „umgewandelt ja“.
- Prüfprogramme (nur Tests): `gx_probe.cpp` legte eine Palette von 16 Einträgen an, eine
  TLUT-Ladung liest aber den ganzen Bereich (256 Einträge, wie die GameCube); `core_probe.cpp` gab
  `mwPlyCalcWorkCprmSfd` nur die 0x24 Byte der Erzeugungsparameter, die CRI-Seite liest 0x30 (das
  Spiel hält dahinter `AP_OBJ.pad_48` frei).

**Geprüft, unbedenklich:** `objTrolley.cpp` (r21b): die Schleifen laufen über `pSat[0..4]` eines
Felds mit 3 Einträgen und treffen `pEat[0..1]`, die lückenlos folgen (auch auf 64 bit), nur
doppeltes Abschalten. `r203.cpp`: `em[3..8]` eines Felds mit 3 Einträgen überdecken `pt[]` und den
Füllbereich, bleiben in den 0xAC Byte der Arbeit (auch auf 64 bit), und Gegnerwelle und Wandern
kommen in getrennten Zweigen. Gegner s28 (`em21`) springt in 44 Räumen von (-32184 34 -32043) nach
(-39510 34 -24090): überall dieselben Punkte, nach Spielverhalten aussehend, nicht weiter verfolgt.

**Offen / nicht getestet:**
- Die Spiel-Heaps liegen in MEM1 ohne Schutzzonen: Überläufe zwischen Spiel-Objekten sieht ASan
  nicht. Ungesetzte Werte (MemorySanitizer) gehen nicht (bräuchte eine instrumentierte
  C++-Bibliothek und 64 bit, dort liegt MEM1 im Weg).
- Leon bleibt in r101, r206, r215, r227, r22b und r316 ohne Eingaben (Ereignis oder Untermenü bis
  zum Ende); was nur beim echten Spielen passiert, prüft weiterhin 10b.
- Patches 0096 und 0097 und die Umwandler-Korrektur sind nicht auf dem Gerät getestet.

## Aufbau

| Ergebnis | Inhalt |
|---|---|
| `libre4_platform.so` | Plattformschicht: MEM1, Dolphin-OS-Ersatz (Arena, Heap aus dem SDK, Threads, Zeit), DVD über Ordner, ARAM, VI, OSLink/Modul-Lader, Yz2-Kern, SDK-Mathematik |
| `libre4_main.so` | Spielcode von `main.dol` (295 Dateien), SN-Toolchain-Ersatz, erzeugte Platzhalter |
| `modules/libre4_<modul>.so` | eine Bibliothek je Original-REL-Modul (114) |
| `re4_host` + `libre4_host.so` | Linux-Vorlader (`host/loader.c`) und Startprogramm (`host/main.cpp`: `--self-test`, `--check-modules`, `--runtime-test`, sonst Spielstart; `--data DIR`; `kill -USR1 <pid>` gibt einen Backtrace der laufenden Stelle aus) |
| `re4_coreproc` / `libre4_coreproc.so` | (32 bit) der Kern als eigener Prozess ohne Java (`host/coreproc.cpp`, Android: im Bibliotheksordner der App), mit dem Client `runtime/core_ipc_client.cpp` der App; Linux-Prüfung `re4_core_remote_test` (`port/docs/32bit-kernprozess.md`) |

Warum zwei Bibliotheken: Der Spielcode ersetzt `operator new/delete` durch
seine Heaps (`main_mem.cpp`). Die Version-Script-Datei
`runtime/libre4_main.ver` hält diese vier Operatoren lokal in `libre4_main`,
die Module erreichen sie über `--wrap=_Znwj …` (`runtime/game_heap.cpp`).
Plattform-Code, C++-Laufzeit und später Renderer/Android-Teil nutzen so
weiter `malloc` und berühren die Spiel-Heaps nie, auch nicht vor
`SystemMemInit`.

| Pfad | Inhalt |
|---|---|
| `port/whole/CMakeLists.txt` | Build: Objektbibliothek je Modul, Link-Plan, Bibliotheken, Startprogramm |
| `port/whole/cmake/linux-*.cmake` | Toolchains i686 (Cloud-Test) und armhf (ARM32, Test unter qemu-arm) |
| `port/whole/include/` | Ersatz-Header: `types.h`, `dolphin/types.h`, Prelude, `re4_task_func.h`, `fake_tgmath.h` |
| `port/whole/patches/` | Geprüfte Einzelkorrekturen, angewendet auf die Kopie |
| `port/whole/runtime/` | Plattformschicht (Tabelle unten), SN-Toolchain-Ersatz, Modul-CRT |
| `port/whole/runtime/formats/` | Formatumwandler: Disc-Daten in Host-Reihenfolge (siehe unten) |
| `port/whole/host/` | Linux-Vorlader `loader.c` und Startprogramm `main.cpp` mit Absturz-Backtrace |
| `port/whole/tests/` | Selbsttest der Modul-Weiterleitungen, Laufzeittests, Test-REL-Modul |
| `port/tools/whole_prepare.py` | Kopie von `src/` + `include/`, Umschreibungen, Patches, SDK-C-Auszug, Modulplan `modules.json` |
| `port/tools/whole_link_plan.py` | Probe-Link, erzeugt Platzhalter, Modultabelle und Weiterleitungen |
| `port/tools/whole_make_patch.py` | Erzeugt einen Patch gegen die umgeschriebene Fassung |
| `port/tools/whole_build.sh` | Ein Befehl für Bauen und alle Prüfungen |
| `port/tools/whole_setup_linux.sh` | Installiert die Werkzeuge (Cloud-Startskript, WSL2) |
| `port/tools/whole_disc_folder.py` | Legt den Disc-Ordner an (aus Disc-Abbildern oder dem Speicher des alten Imports), prüft ihn |

Die Original-Quellen werden nie verändert. `whole_prepare.py` legt bei jedem
CMake-Lauf eine Kopie unter `port/build/whole-*/prepared/tree` an. Geänderte
Dateien werden nur bei echten Änderungen neu geschrieben, damit nicht alles neu
kompiliert wird.

Die Zuordnung der Dateien folgt dem Original-Link (`configure.py`):
`config/G4BE08/objects.py` liefert die 295 Spieldateien von `main.dol`,
`config/G4BE08/modules.py` und `config.yml` die 114 Module mit ihren Dateien
in Link-Reihenfolge. Insgesamt werden 703 Übersetzungseinheiten gebaut, weil
gemeinsame Dateien (z. B. `em_wrap.cpp`, `st2.cpp`) wie im Original in jedes
Modul einzeln kompiliert werden. Jedes Modul bekommt `-DREL_MODULE=<modul>`
und seine `-D`-Schalter aus `modules.py` (`TOOLS_ARRAY` …). Der GCC-Schalter
`-fno-implement-inlines` hat bei clang keine Entsprechung und entfällt.

Die 58 `.c`-Dateien in `src/game` (`printf.c`, `strcpy.c`, `sn_malloc.c` …)
sind die SN/newlib-C-Laufzeit. Sie entfallen, weil die Plattform-C-Bibliothek
sie ersetzt.

## Mechanische Umschreibungen (`whole_prepare.py`, Prelude)

| Regel | Grund |
|---|---|
| Inline-`asm(...)` und `__asm__(...)` entfallen (Prelude-Makro) | PowerPC-Registerbindung, PPC-Befehle, Abschnittsdirektiven |
| `asm volatile` → `asm` | damit die vorige Regel greift |
| `decl asm("name")` außerhalb von `register` bleibt als `__asm("name")` erhalten | Symbol-Alias: zweiter Name für eine bestehende Funktion/Variable |
| GCC-2.95-Namen in Aliasen → Itanium-Namen (Tabelle `GCC295_SYMBOLS`) | z. B. `FlgOnStatus__5EventUl` → `_ZN5Event11FlgOnStatusEm`; unbekannte brechen den Build ab |
| `extern T x asm("y")` mit `y` in derselben Datei → `static T& x = *(T*)&y` | GCC 2.95 löste den Alias auf die eigene (statische) Variable auf; modernes C++ benennt sie intern um |
| `operator new/delete(unsigned int …)` → `size_t` | C++-ABI |
| `strstr/strchr/strrchr/strpbrk/memchr` → Wrapper mit C-Signatur | C++ deklariert `const`-Überladungen; das Spiel weist an `char*` zu |
| System-C-Header werden vor allen Spielheadern eingebunden | ältere Eigen-Deklarationen (`memcpy` in `joy.h`) sind danach verträglich |
| `types.h`: auf ILP32 bleiben `s32/u32` = `long` wie im Original | gleiche C++-Namen für `u32` und `unsigned long` (sonst 90 Scheinfehler beim Linken) |
| `include/types.h`, `include/dolphin/types.h`, `src/lib/cri/cri_xpt.h`: `s32/u32` und `Sint32/Uint32` auf LP64 = 32-bit-Typen (`exact_width_types`, M12) | Spielheader holen mit `#include "types.h"` das Original (nicht den Ersatz-Header); `long` hat auf LP64 8 Bytes |
| Alias-Namen, die von der Breite von `u32`/`size_t` abhängen: `{u32}` in `GCC295_SYMBOLS` → `"…" RE4_ASM_U32` (`m` auf ILP32, `j` auf LP64), `_Znwj/_Znaj` → `RE4_ASM_NEW(_ARRAY)` (Prelude, M12) | `FlgOnStatus__5EventUl` und `__builtin_new` passen sonst auf LP64 nicht zur Funktion; `whole_asm_check.py` kennt die `RE4_ASM_*`-Makros als Teil eines Namens |
| `operator new(unsigned …)`, eigene `extern "C"`-Deklarationen von `memcpy/strlen …` mit `unsigned int` → `__SIZE_TYPE__` (`OPERATOR_SIZE`, `LIBC_DECL`, M12) | auf LP64 ist `size_t` 8 Bytes; sonst Widerspruch zu den System-Headern |
| Füllfelder `[A - sizeof(X)]` → `[((A) > sizeof(X) ? (A) - sizeof(X) : 0)]` (`PAD_TO_SIZE`, M12) | auf LP64 ist `X` größer als der feste Platz `A`: sonst negative Feldgröße. Die festen Plätze selbst werden in M12-A3 angepasst |
| Strukturen mit fester Form (`port/whole/fixed_form.json`): Zeiger-Glieder → `RE4_P32(T)` (`re4_ptr32.h`; ILP32: `T*`, LP64: 4 Bytes `Re4Ptr32<T>`), Datei bekommt `#include "re4_ptr32.h"` (`fixed_form_structs`, M12) | Daten, die das Spiel von der Disc liest und an Ort und Stelle umbiegt (Texturen, Kollision, Kamerabereiche, Modelle, Szenen-Treffer, Raumtabelle), der Kopf des Spielstands und die Modul-Verkettung: ihre Form ist durch die Disc bzw. die Speicherkarte festgelegt. Dazu `GxWork`, `SaveDataPtrs` (Sichten solcher Daten) und `OtData`/`OtPrim` (Zeichenreihenfolge: aus dem Bildpuffer des Raums, dessen Größe für die GameCube-Form gemacht ist). Dazu `cLightPath` (am Anfang von `LIT05_MOVE_FREE` über den Arbeitsbytes der Lichtsätze des Raums; Vergleichslauf r107, 19.10.2026) und `TSceAtHide` (Debug-Werkzeug, Sicht auf `SCE_AT_DATA_HIDE`). Bricht ab, wenn ein Glied nicht umschreibbar ist |
| `TaskFunc`: Klasse statt `void*` (`re4_task_func.h`, gleiche Größe; auf LP64 4 Bytes wie auf der GameCube, M12) | GCC 2.95 wandelte Funktionszeiger still in `void*` um, modernes C++ nicht |
| Feste Plätze und Arbeitsflächen (`port/whole/lp64_sizes.json`, `lp64_sizes`, nach den Patches, M12): die Größe in `EM_WORK_SIZE`, `OBJ_WORK_SIZE`, den 57 `u8 free[0xDE0 - 0x3E0]` der Gegner, den 25 `u8 free[OBJ_WORK_SIZE - 0x328]` der Objekte und `cEspgen::Free` → `RE4_LP64_SIZE(gc, lp64)` (Prelude; ILP32 der GameCube-Wert; auf beiden Breiten mit `+ re4_lp64_size`, einem Aufzählungswert 0, an dem die Prüfwerkzeuge eine geprüfte Größe im Syntaxbaum erkennen) | auf LP64 wird `cEm` 0xC0 Bytes größer, `cObj` 0x98; die eigenen Arbeitsbereiche mit Zeigern wachsen über ihre Fläche (bis 0xAB8 statt 0xA00). LP64: Gegner-Platz 0xF60, Fläche 0xAC0; Objekt-Platz 0x4B0, Fläche 0xF0; Effekt-Fläche 0xD0; Effekt-Pool-Platz 0x1A8. Dazu Speicher mit fester GameCube-Größe und Löschen mit fester Länge (`ATTACH_CAMERA`, `MOTION_INFO`, Bewegungstabellen des Spielers, Zeigertabellen der Puzzle-Teile und der Schatten-Objekte, Debug-Listen; `alloc_size_check.py`) und das Speicherbudget des Speicherbildschirms (`cCard::getUseMemSize`, + 8 KB für die 256 Bild-Bausteine). Geprüft mit `overlay_check.py`; eine andere Anzahl Fundstellen bricht den Build ab Dazu das Ende der Heaps (`SystemMemInit`, `Heap_end_addr`): auf LP64 1 MB höher (0x818F4000), die Laufzeit legt die FST darüber (`RE4_MEM1_LP64_HEAP_EXTRA`; Laufzeittest `tasks`); Anlass: `em12` fand in r107 auf dem POCO X7 Pro keinen Platz (19.10.2026) |
| `(u16*) &pG->stage_no` → `(u16*) &pG->room_id`, `stage_prev` → `room_id_prev` (`ROOM_ID_ADDR`) | `G_ROOM_ID` und `RsfCheck(*(u16*) &pG->stage_no, …)` lesen die Raumnummer über die Adresse ihres ersten Bytes; auf little-endian liegt `stage_no` nach Patch 0016 am anderen Ende (auf der GameCube dieselbe Adresse) |
| `new (p) T()` → `new (p) T` (`PLACEMENT_VALUE_INIT`) | C++11 füllt eine Klasse ohne eigenen Konstruktor bei `T()` erst mit Nullen; der GameCube-Compiler nicht. `em.cpp` setzt `subArc` vor `new (em) cEm10()` (Absturz in `Em12Set`, 14.10.2026). Prüfung: `port/tools/whole_value_init.py` |
| Prelude: `#define _ctype_ re4_newlib_ctype_` (nach den System-Headern) | Bionic exportiert `_ctype_` als Zeiger, newlib (das Spiel, `t_movie`) als Feld; die Tabelle heißt jetzt `re4_newlib_ctype_` (`runtime/newlib_ctype.c`), Bionics Zeiger bleibt unberührt (K2) |
| Prelude: `Re4BeBytes2` | Byte-Array-Sicht `b[0]` = hohes, `b[1]` = niedriges Byte eines Halbworts, unabhängig von der Host-Reihenfolge (Patch 0016: `cItem::lv8`, `SND_RND::b`) |
| CodeWarrior-`asm { … }`-Blöcke, die nur Register umkopieren (`mr`) und keine C-Variable ändern → `/* port: register pin */;` (`whole_asm_check.drop_register_pins`, seit M9) | Registerbindungen des Decompilats (Reihenfolge der Register wie im Original), ohne Wirkung auf das Ergebnis; clang kennt die Syntax nicht. Blöcke mit echten Befehlen bleiben und brechen den Build ab (dann Patch) |
| Vorzeichenbehaftete 32-bit-Werte, die zu Zeigern werden (`port/whole/lp64_int_casts.json`, `lp64_int_casts`, zuletzt, M12): der Operand jeder gelisteten Umwandlung `(T*) x` → `RE4_ADDR32(x)` (Prelude: `(unsigned int) (x)`) | auf LP64 wird aus einem `int`/`s32` mit gesetztem Bit 31 die Adresse `0xFFFFFFFF8…` (Absturz in `primInit` auf dem Retroid, 17.10.2026: `(void*) pG->prim_cnt`); vorzeichenlos gelesen behält der Zeiger das Bitmuster von ILP32, auch bei Zahlen, die keine Adressen sind (`(void*) -1`, Aufgabenparameter). Liste von `port/tools/m12/int_cast_check.py` (libclang, 142 Stellen, dazu zwei in Makros von Hand: `EM3D_GUN_SHOT`, `R332_TASK_SET`); eine Stelle, die nicht wie gelistet gefunden wird, bricht den Build ab |
| `acosf(` / `asinf(` im Spielcode (`src/` außer `src/lib`) → `re4_acosf(` / `re4_asinf(` (`ARC_CALL`, zuletzt, 22.10.2026; 42 Stellen) | Prelude: ein Wert höchstens 1e-5 über ±1 gilt als ±1, weiter draußen bleibt NaN wie auf der GameCube. Das Spiel nimmt den Winkel zweier Einheitsvektoren als `acosf` ihres Skalarprodukts; die GameCube normalisiert mit `frsqrte` und einem Newton-Schritt (etwas zu kurz), der Port mit `1/sqrtf` (`C_VECNormalize`, manchmal ein letztes Bit zu lang): Skalarprodukt 1,0000001, `acosf` NaN. Der erste Fall und jede Zehnerpotenz stehen im Protokoll (`math: acosf/asinf of … taken as 1`) |
| `GXWGFifo.T = x` / `GXWGFifo->T = x` → `re4_gx_put_T(x)` (`GX_FIFO_STORE`, in `GXVert.h` als `re4_gx_put_##T`), Datei bekommt `#include "re4_gx_fifo.h"` | Die Schreibadresse `0xCC008000` des Grafikprozessors gibt es im Port nicht; der FIFO des Ports (`runtime/gx_fifo.cpp`) bekommt jeden Wert (G1) |

Compiler-Schalter: `-std=gnu++11 -Wno-c++11-narrowing -fdelayed-template-parsing
-fno-strict-aliasing -ffp-contract=off -fwrapv -fno-strict-float-cast-overflow -w`.
Auf LP64 (M12) statt `-w`: `-fms-extensions -Wno-everything -Werror=class-varargs
-Wformat`. `-fms-extensions` macht aus den 3060 Zeiger-Kürzungen
(`(u32) ptr`) Warnungen statt Fehler; das ist nur richtig, solange alles, was
das Spiel adressiert, unter 4 GB liegt (M12-A2). `-Werror=class-varargs`
bricht ab, wenn ein `Re4Ptr32`-Glied an eine Funktion mit `...` geht (dort
käme es als 4-Byte-Objekt an statt als Zeiger; Abhilfe `re4_ptr(…)`, Patch
0067).
C++11 wird gebraucht, damit `TaskFunc` trotz Konstruktoren in Unions stehen darf.
`-fno-strict-float-cast-overflow` (seit 15.10.2026): ProDG wandelt mit
`-mfast-cast` Kommazahlen in u8/s8/u16/s16 über `psq_st` mit GQR2–5 um, und
diese Umwandlung **begrenzt** auf den Wertebereich (300 → 255, −4 → 0). clang
ließ überlaufen (300 → 44) bzw. reichte auf ARM den unbegrenzten Wert im
Register weiter; mit dem Schalter begrenzt clang ebenso (Laufzeittest `math`).
Bei u32/s32 bleibt ein Unterschied: ARM begrenzt negative Werte für u32 auf 0,
die GameCube ließ sie überlaufen (schon vorher so, selten).

## Patches (`port/whole/patches/`)

| Patch | Grund |
|---|---|
| 0001 `scheduler.h` | `TaskFunc` aus `re4_task_func.h` |
| 0002 `dbg_var.h` | `this->` für Member der abhängigen Basisklasse (GCC 2.95 suchte dort ohne) |
| 0003 `mes.h` | `lineSpace` öffentlich; GCC 2.95 prüfte den Zugriff nicht |
| 0004 `esp0a.cpp` | Rückruf-Cast für `AddOtWorldPos` |
| 0005 `db_light.cpp` | `DrawTileV`: Alias setzte PPC-Strukturübergabe per Referenz voraus, jetzt Wrapper |
| 0006 `r222.cpp`, 0007 `r224.cpp` | Konstante aus Top-Level-asm (`.rodata`-Wort) als `static const f32` |
| 0008 `main.cpp` | `main()` heißt `extern "C" re4_game_main()`; das Startprogramm (Linux, Android) ruft es auf |
| 0009 `r406.cpp`, 0010 `t_block.cpp` | Hilfsfunktion `static`: im selben Modul hat eine zweite Datei eine gleichnamige (laut Original-Symboltabelle zwei Kopien) |
| 0011 `dvd.cpp` | `cDvd::ReadCheck` (4 Argumente) hat kein `return`; GCC 2.95 ließ das Ergebnis von `readCheckMain` in r3, die Aufrufer prüfen es (`< 0` = Fehler) |
| 0012 `main.cpp` | Die zwei Leerlauf-Schleifen auf `vsync_cnt` rufen `re4_wait_interrupt()`: dort wird der VI-Interrupt zugestellt (sonst Endlosschleife) |
| 0013 `dvd.cpp` | Die 1-KB-Teiletabelle (`DvdHeader`, nur 32-bit-Felder) wird direkt nach dem Lesen in Host-Byte-Reihenfolge gedreht |
| 0014 elf Dateien | Module rufen/lesen `static`-Symbole von `main.dol` beim Namen (der SN-REL-Linker löste sie auf): `EmCatchSubSet`, `QuakeKill`, `setRoomEtcBreakDisp`, `Em32_fs_tbl`, `SceAtSys`, `ScrIdRefTbl`, `ScrObjIdNum`, `fontTMtx`, `g_EspToolSeqHedAddr` werden nicht-statisch; `cModel::isTrans`, `cPlayer::subCharLiveCheck` ohne `inline`; `cDbgWindow::Init` wird in `db_toolbase.cpp` erzwungen ausgegeben |
| 0015 `dvd.cpp` | Ruft `re4_formats_part_loaded` (Formatumwandler) am Ende jedes fertig gelesenen Dateiteils der Lade-Warteschlange (`cDvdQueue::readMain`) |
| 0016 elf Header | Byte-Sichten in Unions für little-endian umgedreht (`#if __BYTE_ORDER__`): `EspGenPrm` (h/b), `cAnm::Frames`, `cEffectCore::Call_no` (esp.h), `SstList::no` (espgen.h), `CRoomInfo::roomNo` (room_jmp.h), `RoomNo_next`/`room_id`/`room_id_prev` (global.h), Teile-Wort (model.h), `ParentNo` (light.h), `no`/`type` (etc_model.h), `save` (sscrn.h), `m_line_gap` (mes.h), `ROOM_SAVE_DATA::RoomNo` (room_data.h); Array-Sichten über `Re4BeBytes2`: `cItem::lv8` (item.h), `SND_RND::b` (snd_drv.h) |
| 0017 `room_jmp.cpp` | `getIndexNum` liest das niedrige Byte des u32-Raumzählers über seinen Wert statt über Offset 3 |
| 0018 `motion.cpp`, `r117.cpp` | Lokale Unions: `FccF32`/`FccS16` setzen Floats aus big-endian Byteströmen zusammen (Bytereihenfolge für little-endian umgedreht), `R117Work::stepMode` |
| 0019 `dvd.cpp` | `cDvd::Init` kopiert alle `FileTbl`-Namen in beschreibbaren Speicher (`strdup`): `read.cpp` benennt Einträge an Ort und Stelle um (`SET_DRS_NAME`: `.das` → `.drs`), auf der GameCube lagen Stringliterale in beschreibbarem RAM |
| 0020 `math_sub.h`, `math_sub.cpp` | C-Fassungen von `fabsf`, `SQRTF`, `SINF`, `COSF`, `LIMIT_ANGLE` (siehe M4.5) |
| 0021 `trans.cpp`, `shape.cpp`, `dbmodule.cpp`, `Espgen42.cpp`, `espgen45.cpp`, `pendulum.cpp` | Paired-Single-Laden/Speichern mit GQR-Quantisierung und Skinning `CalcSk1_x/x2` in C, Locked Cache als Host-Puffer |
| 0022 `dbg_tool.h` | `asm("li %0,0")` → `z = 0` |
| 0023 `em.h` | `EmRoutineSetW` setzt die vier Zustandsbytes einzeln statt über ein u32 |
| 0024 `pl_sub.cpp` | Bit 0x40 im ersten Byte von `cMotBase::pMod` (ungecachter Spiegel der GameCube) entfällt; auf little-endian träfe es ein anderes Objekt |
| 0025 `read.cpp` | `GetDataExt` meldet jeden Fund an `re4_formats_get_data` (Abdeckungs-Log) |
| 0026 `title.cpp` | Debug-Start: `RE4_START_ROOM` wählt den Raum im Debug-Startmenü vor, Titelmenü auf „Neues Spiel“ |
| 0028 `dvd.cpp` | `MemorySwap` meldet am Ende den getauschten Bereich (`re4_formats_memory_swapped`): Ereignisse leihen sich so den Archivspeicher eines Gegners, Untermenüs den Heap; die Herkunftsangaben der Brücke wandern mit |
| 0029 `scroll.cpp` | `setObj` meldet für jedes Kulissen-Objekt, welchen SMD-Eintrag (Raum oder Block, Nummer) es zeigt (`re4_scroll_placed`): Die Brücke gibt das weiter, die App lässt versteckte und nicht geladene Kulisse weg, verschiebt bewegte und zeichnet Kulisse aus Raumskripten (r100: das Polizeiauto) wie die übrigen Modelle |
| 0027 `model.cpp`, `objRocket.cpp` | Mit `-O2` (Android) gibt clang Inline-Funktionen, die nur in einer `.cpp` definiert sind, nicht aus; Module brauchen sie: `cModel::getPartsPtr` ohne `inline`, `cManager<cObj>::destroyNow` ausdrücklich instanziiert. Ohne den Patch zwei Platzhalter mehr als bei `-O0` |
| 0030 `GXInit.c`, `GXTransform.c`, `GXLight.c`, `GXMisc.c` (SDK) | GX-Bibliothek im Port (G1): Register-Blöcke über `re4_gx_register_block` statt fester Hardware-Adressen, `IsWriteGatherBufferEmpty` ohne Assembler (immer leer), Matrix-/Projektions-/Licht-Ladefunktionen in C statt Paired-Single-Assembler (schreiben über `re4_gx_put_f32` in den FIFO), `GXPeek*`/`GXPoke*` über `re4_gx_efb_peek` statt EFB-Adresse. Dazu `-include re4_sdk_intrinsics.h` (`__rlwimi`, `__cntlzw`) für die SDK-Dateien und `-std=c99` für `GXDraw.c` |
| 0031 `GXAttr.c`, `GXTexture.c` (SDK) | Zeiger an den Grafikprozessor über `re4_gx_physical` statt Maskieren (`GXSetArray`, `GXInitTexObj`, `GXInitTexObjData`, `GXInitTlutObj`): MEM1 wie auf der GameCube, Speicher des Ports außerhalb von MEM1 (statische Felder, `filter02.cpp CLUTdata`) über ein Fenster ab `0x03000000` (G2) |
| 0032 `eff_sys.cpp`, `Espgen42.cpp`, `Espgen43.cpp`, `espgen45.cpp` | Display-Listen, die das Spiel mit CPU-Schreibzugriffen baut (Anzahl, Indizes, Floats in Host-Reihenfolge), werden vor `DCStoreRange` mit `re4_gx_host_order_list` angemeldet; der Decoder liest sie in Host-Reihenfolge, solange ihr Inhalt gleich bleibt (G2) |
| 0033 `GXTev.c`, `GXPixel.c`, `GXLight.c` (SDK) | Farben (`GXColor`) als Wort mit R im höchsten Byte zusammensetzen statt `*(u32*)&color` (TEV-, Konstant-, S10-, Material-/Umgebungs-, Nebel- und Lichtfarbe); little-endian vertauschte sonst die Kanäle |
| 0034 `card.cpp`, `tv_mode.cpp`, `event.cpp` | Ein-/Ausblendfarben, die als `u32` an `FadeSet` gehen, in GameCube-Byte-Reihenfolge schreiben (`RE4_BE`) |
| 0035 `sce_sys.cpp` | `SceSleep` außerhalb einer Szenario-Aufgabe wartet auf das nächste Halbbild (`re4_wait_interrupt`): Warteschleifen wie `SndStrStopBlock` (Ende des Funkspruchs) kommen sonst nie voran, weil der Ton-Interrupt nur an Unterbrechungspunkten läuft |
| 0036 `GXTev.c` | Die Tabellen von `GXSetTevOp` (`TEVCOpTableST0/1`, `TEVAOpTableST0/1`) waren Bitfeld-Strukturen, die als ganze Wörter gelesen werden; MWCC (Big-Endian) belegt Bitfelder vom höchsten Bit an, Compiler für Little-Endian vom niedrigsten. Die Wörter werden jetzt Feld für Feld gebaut (`RE4_TEVC_WORD`, `RE4_TEVA_WORD`), Felder und Werte wie im Original |
| 0038 `GXFifo.h` | `GXFifoObj` (ein Byte-Feld, das das SDK als `__GXFifoObj` mit Zeigern und Zählern beschreibt) ist 32-Byte-ausgerichtet; vorher lag das statische `FifoObj` von `GXInit` im Android-Build an einer ungeraden Adresse, und die zusammengefassten Wort-Speicherbefehle (`strd`) brachen auf dem Retroid mit SIGBUS ab |
| 0037 `main_mem.h`, `pl_class.cpp` | `VALID_PTR`/`VALID_PTR2` (Zeigerprüfung gegen MEM1) nehmen zusätzlich `re4_valid_port_ptr` (runtime/os_thread.cpp): Host-Stapel der OS-Threads und die statischen Daten der Spielbibliotheken (`libre4_*`), die auf dem GameCube ebenfalls in MEM1 lägen |
| ~~0039 `card.cpp`~~ | **entfernt in M10:** `CardFirstCheck` übersprang die Speicherkarten-Prüfung beim Start, solange der Port keine Speicherkarte hatte (`RE4_CARD_CHECK=1` behielt sie); seit M10 prüft das Spiel die echte Karte, die Test-Tastenfolgen setzen `RE4_MEMCARD=none` |
| 0040 `esp_sub.cpp` | `Esp1b_SpTrans` findet die Daten von `cEsp1b` über `RE4_OWN(cEsp, Esp1bSpWork, 0xF8)` statt über `&pad_EC[0xC]`: MWCC legt den vptr ans Ende von `cEsp`, die Compiler des Ports an den Anfang; vorher las es die Punktzahl 4 Bytes daneben (Absturz im GX-Decoder auf dem Retroid nach 153 s). Auf LP64 beginnen die Daten in den Füllbytes von `cEsp` (0x10C von 0x110; bis 18.10.2026 `sizeof(cEsp)`, Hänger in r120 auf 64 bit) |
| 0041 `at_sub.cpp`, `atari.cpp`, `card.cpp` | Das Attributwort einer Kollisionsfläche (`SAT_POLY::m_Status`) wird als Wort gelesen statt als zwei Halbwörter in GameCube-Reihenfolge (vorher vertauschte Hälften: Zäune als Fallkanten, Wände ohne Kollision, Boden als Wasser); `CardID::save` liest `nPoint` als Wert statt Byte 3 |
| 0042 `src/lib/GXGeometry.c` | `GXSetCullMode` übernimmt das Ergebnis von `__rlwimi` (`hwMode = __rlwimi(...)`): MWCC fügt bei diesem Befehl in den ersten Operanden ein, die C-Fassung des Ports gibt das Ergebnis zurück und die Zeile verwarf es. Aus `GX_CULL_FRONT` (Standard aller Modelle) wurde „keine Rückseiten weglassen“: alles wurde beidseitig gezeichnet, die Doppelschichten von Leons Haar verdeckten sich gegenseitig (dunkle Flecken) |
| 0043 `title.cpp` | Das Debug-Startmenü nach „New Game“ (`titleExit`, Raumauswahl) meldet sich bei der Laufzeit an (`re4_title_room_menu`, runtime/dolphin_os.cpp): solange es offen ist, lässt die Debug-Anzeigen-Sperre der App (`RE4_DEBUG_HUD=0`) `debug_mode` mindestens 1, damit seine Zeilen (Debug-Text) zu sehen sind; `RE4_ROOM_MENU=0` überspringt es wie ein sofortiges A. Übersprungen behält der „New Game“-Ton seine Kennung: das Ende von `titleExit` wartet wie in der Verkaufsfassung, bis er ausgeklungen ist (die Debug-Fassung setzt sie vor dem Menü auf 0; mit dem Menü klang der Ton aus, während es wartete, ohne Menü schnitt der Abbau des Titels ihn ab; gemeldet vom Projektinhaber, 17.10.2026) |
| 0044 `cam_ctrl.cpp` | `CameraControl::roomInit` füllt den Platz für Kamera-Objekte (`m_Free`, Muster 9) vor `Check()` statt danach: `Check()` kann dort die Fall-Kamera bauen, deren vtable-Zeiger das Füllen sonst überschrieb; der nächste Kamera-Schnitt (`switchCamera` → `delete m_pProc`) stürzte ab (Direktstart von r100, Totenköpfe untersuchen) |
| 0045 `snd.cpp` | `SndSoftReset` wartet auf die Bestätigung des Ton-Resets durch die Abspieler im Audio-Interrupt; die Warteschleife ruft `re4_wait_interrupt()` (runtime/vi.cpp), damit der Port die Interrupts ausliefert (vorher Stillstand beim Soft-Reset nach dem Tod, schwarzer Bildschirm) |
| 0046 `include/cam_extra.h` | `~cCamera` übergibt das Objekt an `re4_keep_vtable` (runtime/dolphin_os.cpp, tut nichts), damit clang den vtable-Zeiger von `cCamera` wieder schreibt, nachdem der Destruktor einer Unterklasse das Objekt mit Muster 9 gefüllt hat; `move()` hat einen leeren Rumpf statt rein virtuell zu sein, weil clang den löschenden Destruktor einer abstrakten Klasse als Haltebefehl erzeugt. `CameraControl` löscht eine beendete Kamera ein zweites Mal (`m_pProc` bleibt gesetzt); mit GCC 2.95 harmlos, mit clang Absturz 0x0909090d (Sprung aus dem Fenster, Totenköpfe) |
| 0047 SDK `chorus.c`, `delay.c`, `reverb_hi.c`, `reverb_hi_4ch.c`, `reverb_std.c` | C für die Gekko-Assembler-Funktionen der AXFX-Effekte (DoCrossTalk, HandleReverb ×2, HandleReverbDpl2, do_src1/2); die Vorverzögerung des hohen Halls wirkt wie im Original nur einen Abtastwert; `AXFXDelayInit` gibt das Ergebnis von `AXFXDelaySettings` zurück (im Original fehlt das `return`, der Gekko lieferte es in r3) |
| 0048 CRI `uty_tmr.c`, `uty_ppc.c`, `mpv_lib.c`, `mps_lib.c`, `dct_ac.c`, `sfx_zmv.c` | Plattformteile der CRI-Quellen: Zeitbasis über `re4_cri_timer` (runtime/vi.cpp), kein Sichern der GQR (die Kerne sind C), `HID2` ohne Locked Cache, keine Big-Endian-Selbstprüfung (die Leser stellt 0051 um), Adressen der IDCT-Tabellen in C, `sscanf` wie die C-Bibliothek |
| 0049 CRI `mpv_mc.c`, `mpv_mcy.c` | Bewegungsausgleich 8 × 8 und 16 × 16 als Byte-Schleifen mit denselben Ergebnissen (Original packte Bildpunkte als Wörter in GameCube-Reihenfolge) |
| 0050 CRI `mpv_umc.c`, `dct_fsri.c`, `cftyp422_ppc.c` | Paired-Single-Assembler in C (Halbpixel-Mischen, GQR-Vorbereitung, IDCT; übersetzt von `port/tools/whole_ppc_to_c.py`), Y8/C4:4-Texturumwandlung mit der Wortreihenfolge der GameCube; die ungenutzten ARGB8888-Umwandler melden sich einmal |
| 0051 CRI `mpv_bit.h`, `mpv_emp.c`, `mpvabdec.c`, `mps_dec.c`, `mpv_vlc.c`, `sfh_main.c`, `adx_dcd.c`, `adx_dcd5.c`, `adx_tlk.c`, `adx_sjd.c`, `adx_bsc.c`, `ax_rna.c` | Lesen in GameCube-Reihenfolge (`re4_be16/32`): Bitleser von MPEG-Video und Programmstrom, Sofdec-Kopf, ADX-Kopf und Skalen; Lauf/Stufen-Tabellen in Host-Reihenfolge; AXRNA legt PCM16 big-endian ins ARAM (so liest `dsp.cpp`) und liest die Stimmposition aus ihren zwei Hälften |
| 0052 `include/mwply.h` | `mwPlyGetHdrInf`: Breite und Höhe sind auf little-endian die unteren Hälften der Wörter, die CRI schreibt |
| 0053 CRI `mpv_dec.c`, `sfd_lib.c`, `mwsfdsvr.c`, `adx_sjd.c`, `adx_rnaa.c`, `mwstm.c` | Werte, die auf dem GameCube zufällig im Register standen: Zahl übersprungener Makroblöcke, Bildrate des Sofdec-Zeitgebers, Player der Nachschub-Prüfung, Größe für den ADX-Kopf; zwei Weiterleitungen geben das Ergebnis zurück, das die Aufrufer prüfen |
| 0054 `sofdec.cpp` | Fortschritt des Films ins Log (`re4_movie_status`/`re4_movie_end`, runtime/movie_status.cpp); die vier TEV-Konstantfarben der YUV→RGB-Umrechnung (`setTevPrm`) in GameCube-Byte-Reihenfolge (`RE4_BE`), sonst vertauschte Kanäle |
| 0055 `title.cpp` | Blendfarben der Titelbilder (Logos, Titel, Demo-Filme) als Wort in GameCube-Reihenfolge (`RE4_BE`) wie Patch 0034; sonst ist `0x000000FF` (deckendes Schwarz) ein durchsichtiges Rot und die Blenden fehlen |
| 0056 SDK `synsample.c`, `synpitch.c` | SYN schreibt 32-bit-Werte des AX-Parameterblocks (Adressen, Schleife/Format, Verstärkung, Abtastraten-Verhältnis) mit der hohen Hälfte zuerst wie das Wort des GameCube (`RE4_PB_WORD`, zwei Halbwörter); vorher vertauschte Hälften: SYN-Stimmen außerhalb des ARAM und eine riesige DSP-Schätzung, AX stoppte Stimmen („ISS Voice Drop“) |
| 0057 SDK `CARDBios.c`, `CARDRdwr.c`, `__os.h` | Die CARD-Bibliothek im Port (M10): die ein- und zwei-Byte-Befehle der Karte (0x81 Interrupt an, 0x83 Status, 0x85 Hersteller, 0x89 Status löschen, 0x87/0x88) baut das SDK als u32 und schickt die ersten Bytes im Speicher, auf little-endian die niedrigen (00 00): `re4_card_cmd` legt den Befehl zuerst; die Hersteller-ID kommt höchstes Byte zuerst (`re4_card_be16`). `CARDRdwr.c`: CodeWarriors Cast als Zuweisungsziel (`(u8*) p += n`) als normale Zuweisung; `__os.h`: die `.init`-Asm-Deklarationen nur für CodeWarrior |
| 0058 `card.cpp` | `cCard::MainLoop` meldet je Bild Modus und Schritt des Speicherkarten-Bildschirms an `re4_card_screen` (runtime/memcard.cpp), das Änderungen ins Log schreibt („card: save: saving (file 1, step 5)“, Fehlermeldungen mit Code); nur Diagnose |
| 0059 `snd.cpp` | `SndRoomBgmLoad` wartet beim Raumstart, bis die Musik des Raums davor ausgeblendet ist; die Schleife ruft `re4_wait_interrupt()` (runtime/vi.cpp) wie 0045, sonst hing das Spiel bei „Retry from a continue point“ und beim Laden aus dem Pausemenü (Retroid, 17.10.2026) |
| 0060 `card.cpp` | Die Startprüfung der Karte ohne die Fragen und Meldungen des GameCube (Entscheidungen des Projektinhabers, 17.10.2026): `firstCheck10` zeigt „Systemdatei wird geladen“ nicht mehr, `createSysfile` legt eine fehlende Systemdatei ohne Frage an (wie „Ja“) und ohne „wird gespeichert“/„gespeichert“; `RE4_CARD_BOOT_MESSAGE=1` (runtime/memcard.cpp) stellt das Original wieder her. Fehlermeldungen bleiben |
| 0061 `game.cpp` | Das Debug-Menü (`DbMenuExec`) öffnet auf Wunsch der App (SELECT eine Sekunde gehalten, `re4_core_debug_menu` → `re4_debug_menu_request`) statt mit L + START (Entscheidung des Projektinhabers, 17.10.2026: L2 + START öffnete es versehentlich; SELECT ist keine GameCube-Taste). Die übrigen Bedingungen bleiben (Werbefassung: Controller 2; `DBG_TEST_MODE`) |
| 0062 `se_at.cpp` | Raumgeräusche ohne feste Wartezeit: `rnd_base + Rnd() % rnd_interval` mit `rnd_interval` 0 (r311) ergibt wie auf dem GameCube `Rnd()` (`divw` durch 0: Quotient 0, Rest = Dividend) statt eines Abbruchs auf x86 (`SIGFPE`, PC-Rundgang 17.10.2026) bzw. eines unbestimmten Rests auf ARM; `Rnd()` weiter genau einmal |
| 0063 `r404.cpp` | Startpunkt im Söldner-Raum r404: `&pos[n]` / `&rot[n]` statt `&pos + n * 12 + 0x28`, das auf die Stapel-Anordnung von CodeWarrior baut (Clang legt `rot` anders; Stillstand in `LIMIT_ANGLE`, PC-Rundgang 17.10.2026) |
| 0064 `shape.cpp` | `CalculateShape_new`: ein Kanal, der einen Eintrag **hinter** der Formen-Tabelle nennt (Anzahl im Wort vor der Tabelle), bekommt keine Werte. Das Spiel liest dort die Eckpunkt-Listen als Eintrag; big-endian ist dessen Anzahl negativ (nichts passiert), in Host-Reihenfolge 65535 (r229: Codec-Gesicht schrieb in die Zeichenliste, Funktionszeiger um 0x74 verschoben). Außerdem wird eine Eckpunkt-Nummer außerhalb des Modells übersprungen. Beides einmal gemeldet (`check: shape: …`); richtige Daten haben beides nicht |
| 0065 `filter01.cpp` | Schalter „Depth of field“ des App-Menüs (M11, Wunsch des Projektinhabers, 17.10.2026): ist er aus (`re4_dof_enabled()`, `core_api.cpp`), legt `Filter01Trans` keinen Tiefenschärfe-Durchgang an und löscht die Anfragen dieses Bilds wie nach einem gezeichneten. Standard an. Ist er an und legt das Spiel keinen Fokus an, kommt bei freiem Spieler der Autofokus der App dazu (`re4_dof_autofocus`, OT-Eintrag `re4_dof_autofocus_mark` → BP 0x5A, `runtime/dof.cpp`; kein GameCube-Effekt, Wunsch des Projektinhabers „wie in RE4-Decomp“); `re4_dof_note` schreibt die Durchgänge eines Bilds ins Protokoll, wenn sie sich ändern |
| 0066 `t_option.cpp` | Waffenseite des Debug-Menüs (`tp_pl_weapon`): der Cursor lief über 46 Plätze, `strWepName` hat aber 45 Namen und ein NULL-Ende; über den ersten Eintrag nach oben oder über den letzten nach unten zeigte die Liste den NULL-Namen, `eprintf` las Adresse 0 (GameCube: was dort liegt; Port: Absturz, Retroid 17.10.2026, `eprintf_main`, Fehleradresse 0). Der Cursor läuft jetzt über die 45 Waffen (letzte Seite 20..44); Auswahl und Stufenseite unverändert. Nicht auf dem Gerät geprüft |
| 0067 `room_jmp.cpp`, `title.cpp`, `roomdata.cpp`, `seq.c` | 64 bit (M12): `eprintf` bekommt die Namen aus den Raumtabellen (Strukturen mit fester Form) über `re4_ptr(…)` als echten Zeiger; `re4_ptr` ist auf ILP32 ohne Wirkung. `roomdata.cpp`: die Fehlermeldung beim Lesen eines Raummoduls gibt für `%s` den Dateinamen statt der ganzen Tabellenzeile (die GameCube übergab deren Adresse, ARM und x86 kopieren die Struktur). `SEQGetVolume` mit `s32` wie seine Deklaration in `seq.h` (auf LP64 sonst zwei verschiedene Typen) |
| 0068 `os.h`, `model.h`, `model.cpp`, `shape.cpp` | 64 bit (M12): Adressen, die über einen vorzeichenbehafteten 32-bit-Wert laufen, werden auf LP64 zu `0xFFFFFFFF8…` (alles, was das Spiel erreicht, hat Bit 31 gesetzt). `OS_CACHED_REGION_PREFIX`/`UNCACHED` als `unsigned` (feste OS-Variablen wie `OS_BUS_CLOCK`), `cModel::modelInit` gibt den `cModelInfo*` als `u32` zurück statt `int`, die verschobene Formtabelle (`shape.cpp`) wird als `u32` gelesen. Auf ILP32 gleiche Werte |
| 0069 `GXTexture.c` | 64 bit (M12): `GXTexObj` ist im Spiel 32 Bytes (`u32 dummy[8]`), die SDK-Innensicht `__GXTexObjInt` wurde auf LP64 durch den `userData`-Zeiger 40 Bytes groß: `GXInitTexObj` schrieb über jedes Texturobjekt hinaus. `userData` bleibt 4 Bytes (`RE4_P32`), Größenprüfungen für die vier Innensichten (`_Static_assert`) |
| 0070 CRI-Bibliothek (36 Dateien in `src/lib`, darunter `cri/mpv.h`, `cri/sfd.h`, `cri/sfx.h`) | 64 bit (M12): Sofdec-Filme und ADX-Ton auf LP64. Adressen in `Sint32`-Werten über `Uint32` zurück (36 Stellen); Teilsichten mit GameCube-Platzhaltern (ADX-Dekoder, Player `MWPLY_OBJ`, Sofdec-Handle `SFD_OBJ`, MPEG-Dekoder `MPV_OBJ` in fünf Dateien) bekommen auf LP64 erzeugte Platzhalter an den Abständen der vollen Struktur (`port/tools/m12/gen_views.py`, geprüft mit `group_check.py`); Größen- und Abstandsprüfungen (`_Static_assert`) für Handle-Schritte, Arbeitsflächen und Tabellen; feste Byte-Abstände (`sfd_tim.c` Uhrquellen-Tabelle, `sfd_mpv.c` Bildplatz, Zeitwörter der Bildinformation, die drei übersetzten Rechenkerne in `mpv_umc.c`) über die Glieder; `sfd_mem.c` führt die Puffernummer als Zahl. Jede LP64-Variante steht in `#if __SIZEOF_POINTER__ != 4` oder behält auf ILP32 denselben Wert (Kopf des Patches listet alle Punkte) |
| 0071 `include/mwply.h` | 64 bit (M12): die Spielseite des Filmbilds `MWS_FRM` folgt auf LP64 der Glieder-Liste der Bibliothek (Zeiger `tblsrc`, `usrdat`); `mwPlyGetCurFrm` schrieb sonst über `cSofdec` hinaus |
| 0072 `dbg_button.h`, `db_mod.h`, `db_mod.cpp` | 64 bit (M12): Debug-Werkzeuge. Knopf-Sicht des Sscrn-Debugmenüs mit echtem Text- und Funktionszeiger; Platzhalter des Modellbetrachters `DbModState` je Zeigerbreite mit Abstandsprüfung |
| 0073 `em2d.cpp`, `em32.cpp`, `sce_com.cpp`, `r100.cpp`, `r103.cpp`, `r307.cpp`, `r318.cpp`, `t_camera.cpp`, `t_camera_draw.cpp`, `t_esp.cpp`, `ss_pzzl.cpp` | 64 bit (M12): feste Byte-Abstände in Strukturen, deren Glieder auf LP64 wandern (`(u8*) pe + 0x378`), über `RE4_OFS(T, glied, gc)` (Prelude): der Abstand des Glieds, auf ILP32 beim Übersetzen gegen den GameCube-Wert geprüft. Kamera- und Effekt-Werkzeug über ihre Makros (`TC_TRG`, `FSTORE_AT`). **Auch 32 bit:** `ss_pzzl.cpp` las Zustand und Auswahl von Nachricht 1 an den MWCC-Abständen (vptr hinter den Feldern); im Port lagen dort `be_flag` und ein Byte von `m_selTbl`. Jetzt über `GetMesStatus(1)`/`GetSelectMessage(1)` |
| 0074 `trans.cpp`, `emwindow.h`, `em3e.h`, `TexRender.cpp`, `light_area.cpp`, `objTrolley.cpp`, `r300.cpp`, `r30c.cpp`, `r310.cpp`, `r316.cpp`, `r317.cpp`, `r318.cpp`, `db_work.cpp` | 64 bit (M12): Sichten mit GameCube-Abständen (ein Füllfeld bis zu dem Glied, das sie benennen) über ein Objekt, dessen Grundklassen auf LP64 wachsen, erreichen das Glied jetzt über `RE4_OFS(T, glied, gc)` oder `sizeof` der Grundklasse (auf ILP32 der GameCube-Wert): Skelett (`cPartsWk` → `cParts::pList`), Fenster (`EmObjWork::eff`), `EmMarkView`, Lore (`FREE_EM10::Target_dir`), Ereignis-Kamera (`Event`-Glieder statt `TexRenderEvt`), Raketenwerfer (`cObjLauncher::pRocket`), r300/r30c/r310/r316/r317/r318, Objekt-Betrachter. `GxWork` und `SaveDataPtrs` stattdessen in `fixed_form.json`. Neu im Prelude: `RE4_OWN(B, T, gc)`, der Anfang der eigenen Arbeitsfläche hinter einer Grundklasse (auf LP64 in deren Füllbytes). Gefunden mit `cast_check.py` (jetzt auch C++) |
| 0075 `event.cpp`, `ss_pzzl.cpp`, `ss_shop.cpp`, `eprintf.cpp`, `r332.cpp`, `r20d.cpp` | 64 bit (M12): Zeiger in 4-Byte-Plätzen. `EventMgr::GetRead` (Absturz beim Laden des Autofahrt-Ereignisses r120) und `Event::PFuncTbl` über eine Zeiger-Variable; die bin/tpl-Wörter von `ITEM_PIECE_INFO::model` als u32 geschrieben und gelesen; Debug-Textzeiger in 4-Byte-Plätzen (`RE4_P32`); r332-Makros und r20d mit Index statt GameCube-Byteabstand. Gefunden mit `ptr_slot_check.py` |
| 0076 `db_log.cpp` | Die Spielmeldungen (`pLog->err/warn/mes`, auf dem GameCube Bildschirm und `printf`-Konsole) gehen über `re4_game_log` ins Protokoll des Ports: jede Meldung (nach ihrem Format) die ersten 3 Male, dann jedes 1000. Mal mit Zählung. Auf Android gab es keine Konsole, Meldungen wie „GetPrimBuff() : OVERFLOW“ gingen verloren |
| 0077 `r117.cpp` | 64 bit (M12): Byte 0xF9 der Lichtstrahlen (Byte 1 der eigenen Arbeitsfläche hinter `cEsp`) über `RE4_OWN(cEsp, u8, 0xF8) + 1`; auf LP64 beginnt die Fläche bei 0x10C. Gefunden mit `raw_offset_check.py` (Abstände hinter dem Ende einer Struktur) **Berichtigt 19.10.2026:** der Lichtstrahl ist esp06, dessen Arbeit (mit Zeiger) auf LP64 bei 0x110 beginnt; gelesen wird jetzt `RE4_OWN(cEsp, void*, 0xF8) + 1` = 0x111 statt 0x10D (das Rätsel wartete sonst beim Prüfen des Musters ewig; Tester, POCO X7 Pro). Nachgezogen: `own_start_check.py` |
| 0078 `motion.cpp`, `r320.cpp` | 64 bit (M12): GameCube-Abstände in Summen mit Variablen. `MOT_HIST` (Schlüssel-Verlauf der Wurzel für `MotionGetSpeed` und `MotionGetPosition`) über `RE4_OFS(MOTION_INFO, Key_hist, 8)`: vorher schrieb `HermiteInterpolation` auf LP64 in die obere Hälfte von `pHermite_data` (Absturz des ersten Ganados in r100, Gerät und PC-Prüfung; gefunden mit einem Hardware-Haltepunkt, `runtime/watch.cpp`). `R320_EM_OFS` über `RE4_OFS(R320Work, em, 0xC)` |
| 0079 `cam_extra.cpp` | 64 bit (M12): die Kamera beim Öffnen einer Tür liest die z-Spalte von `FREE_EMDOOR::base_mat` über den Namen statt als `((f32*) em)[0x784 / 4]`. Gefunden mit `raw_offset_check.py --words` |
| 0080 `include/trans.h` | 64 bit (M12): `ModelTexInfo`, die Sicht des Zeichners auf `cModelInfo` ab `flagsDC` (`MODEL_TEX`), legt jedes Glied dorthin, wo `cModelInfo` es hat: auf LP64 4-Byte-Packung und eine Lücke aus den Abständen von `cModelInfo` (auf dem GameCube 2 Bytes, auf LP64 6), `static_assert` je Glied. Vorher las `RefractShaderSetup` auf LP64 `blendTbl` 4 Bytes zu früh (Absturz im ersten Bild von r100, PC-Prüfung 18.10.2026). Gefunden mit `cast_check.py`, seit es den Operator `&` bei libclang abfragt (vorher im Makro übersehen) |
| 0081 `main_mem.cpp`, `rnd.cpp` | Diagnose (M12), ändert das Verhalten des Spiels nicht: jede Speicheranforderung ohne Platz (`mem_alloc` mit jedem `flag`, `Debug_alloc`) ruft `re4_heap_failed` (runtime/log.cpp: „heap: no room for … in heap N (datei(zeile), flag F)“, Zähler für den Zustands-Trace); `re4_rnd_state` gibt den Zustand des Zufallsgenerators für den Trace. Anlass: der Absturz im Speicherbildschirm auf LP64 (PC-Prüfung 18.10.2026), ein zu kleines Speicherbudget (bestätigt durch das Protokoll; seit der Regel speichert und lädt die PC-Prüfung auf 64 bit); Speicherbudgets aus GameCube-Größen sind statisch kaum zu finden, der Vergleichslauf 32/64 bit zeigt jetzt jede Anforderung, die nur auf 64 bit scheitert |
| 0082 `sce_sys.cpp`, `sce_at.cpp`, `r20d.cpp`, `r20e.cpp`, `r21d.cpp` | 64 bit (M12): GameCube-Abstände an einer Adresse als Zahl oder in einer Summe mit Index (`(u32) em + 0x320`, `i * sizeof(T) + (u32) w + 0x2C`, `base += 0x30C`) über `RE4_OFS(T, glied, gc)`: Szenen-Bedingung „Gegner tot“ (`cEm::hp`, LP64 0x3B8), Kamera-Ecken (`SceAtSysWork::pCamAt`, LP64 0x1B0, jetzt zeigerbreit geschrieben: `RAW_CAM_AT`), Schiebepuzzle r20e (Teile LP64 0x18, Felder 0x1C8), Zäune r20d (`fence[i].t`), Fallen r21d (`trap[]`, LP64 0x380). Gefunden mit `raw_offset_check.py` (Adressen als Zahl, Summen) und `sizeof_ptr_check.py` (18.10.2026) |
| 0083 `id_sys.cpp` | Nicht initialisierter Lesezugriff in `IdShimmerTrans` (Flimmer-Effekt der Bildschirm-Elemente, z. B. die WARNING-Schrift): die Tiefe `dot`, die die Stärke der indirekten Verzerrung bestimmt, kam aus `indMtx`, bevor es beschrieben war (so schon im Original), also aus Resten auf dem Stapel, die je Build andere sind (Vergleichslauf 18.10.2026: Bild 301 an den Schriftkanten verschieden, alles andere gleich). `indMtx` beginnt jetzt mit Nullen: `dot` 0 → 1500 (Untergrenze), auf jedem Build gleich; was der GameCube dort las, ist nicht bekannt. Bestätigung: der nächste Vergleichslauf (Bild 301 gleich) |
| 0084 `esp_efm.cpp` | `EfmSetObj04` schrieb Eltern-Modell und `guid` über einen Wortzeiger (`u32* link = (u32*) &w->pMod; link[0] = (u32) parent; link[1] = parent->guid;`): auf LP64 landete die `guid` in der oberen Hälfte von `pMod` (8 Bytes), `Guid_pMod` blieb alt; `cObj04::move` las das Eltern-Modell dann über `guid << 32 | parent` (Tester, POCO X7 Pro, 64 bit, r104, 18.10.2026: SIGSEGV an `0xf81017988`, `x8 = 0xf81017980`). Jetzt über die Felder selbst, auf ILP32 gleich. Gefunden vom erweiterten `ptr_word_check.py` (Wortzeiger in einer Hilfsvariablen). Bestätigung: Lauf an derselben Stelle auf 64 bit |
| 0085 `dvd.cpp` | Der Start ohne Bild-Takt (Wunsch des Projektinhabers, 19.10.2026: kürzerer schwarzer Bildschirm): `cDvd::readProcMain` wartet nach jedem Schritt einer Lesung ein Bild (`TaskSleep(1)`); bis die erste `title.dat` gelesen ist, folgt der nächste Schritt sofort, solange nichts unterwegs ist (0x02000000 DVD, 0x01000000 ARAM), nach je 64 Schritten ein Bild (`runtime/dvd.cpp re4_dvd_boot_fast`); das Ende meldet `re4_dvd_read_done` (Name der Lesung). Danach und mit `RE4_DVD_BOOT_FAST=0` wie im Original. Prüfung: `runtime-test dvdq` (1 / 1 / 14 Bilder) |
| 0086 `pl14.cpp`, `emmine.cpp`, `obj1b.cpp` | `GetWepTargetList2` legt für jeden Trefferbereich mit `YAT_FLAG_THROUGH`, den die Schusslinie kreuzt, einen Eintrag mehr an als `max` (bis 0x14). `isTarget` (Luis, `TARGET_WK list[2]`), der Treffer der Mine und der des Speers (je ein `TARGET_WK`) hatten nur Platz für `max`: der Eintrag dahinter überschrieb den Wachwert des Stack-Schutzes (Absturz „stack corruption detected“ bei Luis' erstem Schuss in der Hütte r11c, zwei Tester, 20.10.2026). Jetzt je 0x14 Einträge; die Aufrufer lesen weiter Eintrag 0 und 1 |
| 0087 `pl_sub.cpp`, `em10.cpp` | Luis (`cSubLuis`) ist kein `cSubChar`: `SetSubDamage` schreibt für ihn über `cSubChar::m_MotTbl2[0]` / das erste Byte von `m_MotBase`, auf der GameCube sein `m_pFunc` (0x584) und `status` (0x58C, 0x40 = `F_KARAMI`); auf LP64 andere Stellen, `m_pFunc` blieb 0 (Absturz, wenn ein Ganado Luis in der Hütte r11c packt, Tester 20.10.2026). Jetzt für id 4 über Luis' eigene Mitglieder; `subem10_NeckHang_Luis` zählt `m_Work0` wie die GameCube in Luis' `routine.pos.y`. Das Bit `F_KARAMI` (von 0024 gestrichen) setzt Luis' Dank-Satz nach dem Packen wieder in Gang |
| 0088 `route_ck.cpp` | `getNearPoint` sortiert die zehn nächsten Wegpunkte in `idx[]`/`dist[]` (Anfang: Stack-Inhalt bzw. 1e16). Ein Punkt weiter als 1e8 kommt nie hinein; liegen weniger als zehn (bzw. `nPoint`) Punkte so nah, behalten die Einträge dahinter ihren Stack-Inhalt, und die Suche nach freier Sicht liest `pt[Müll]` (Absturz weit über 4 GB in `getNearPoint` aus `RouteCkToEm`/`em10RouteCk`, Tester Samsung Galaxy M23, 64 bit, Hütte r11c, 21.10.2026). Jetzt nur die gefüllten Einträge; keiner: -1 („kein Wegpunkt“, jeder Aufrufer geht dann direkt aufs Ziel). Positionen im Raum füllen alle, dort ändert sich nichts. Wessen Position es war (Spieler, Partner, welcher Gegner), meldet „check: route: …“ bis zu viermal. Prüfung: `runtime-test route` |
| 0089 `r10f.cpp` | Die Gondel (`r10f_GondolaGetOn`/`GetOff`) setzt Leon und Ashley über `&((R10fGondolaTbl*) mot)->posA[side]`/`->posB[side]`: eine Sicht auf das lokale `void* mot[2][3]`, die die Variablen `posA`/`posB` dahinter liest, wo CodeWarrior sie ablegte. clang legt sie anders ab und speichert sie bei -O2 gar nicht (nie beim Namen benutzt; armhf und aarch64 nachgeprüft: 0 Zugriffe auf ihre Konstanten); die Sicht liest gesicherte Register. Folge: Ashley nach der Gondel bei (-1469.6, -2.32e35, -2346.42), ihre Höhe die Bits einer Adresse in `libre4_main.so` (zwei Tester, 21.10.2026; mit 0.41.0 Absturz in `getNearPoint`). Jetzt `&posA[side]`/`&posB[side]` (nach dem Patch 4 Zugriffe) |
| 0090 `em3e.cpp` | Die Schießbude (r22c) hält die Skripte ihrer Zieltafeln als `s32`-Felder `{time, flag, X, Y, Z, Befehle…}` im Modul, im Port in Host-Reihenfolge. `cEmMark::init(EmMarkData*)` nahm den Typ als Byte 7 (`EmMarkData::type`), `setNextInstruction` die nächste Routine als Byte 3 des Befehlsworts: auf der GameCube das niedrigste Byte, auf ARM das höchste (0). Folge: alle Ziele Typ 0 (keine Ashley-Tafeln), jede Tafel nach jedem Befehl wieder „begin“ (hochklappen), nie „end“, über ihr Skript hinaus („cEmMark::setNextInst() ERR 300“) und am Ende `emmark_tbl` hinter dem Feld (Absturz pc 0 aus `cEmMark::move`, Tester 21.10.2026). Jetzt das niedrigste Byte des Worts als Wert. `byte_views.json` kannte beide Stellen nicht (Struktur-Zeiger statt `(u8*) &skalar`, Struktur mit Byte-Feld über dem Wort); die Suche nach weiteren `((u8*) ptr)[n]` und Struktur-Sichten über eingebaute Zahlenfelder der Räume fand nur diese |
| 0091 `r221.cpp`, `pl_class.cpp`, `r104.cpp` | Werte, die das Original aus einem Prozessor-Register nahm (Fehlersuche 22.10.2026, `uninit_scan.py`): r221 (Raum des Insekten-Bosses) ruft für die Szene jeder der vier Gasbomben `SceEventStart(evNo)`, das Original übergibt immer 0, gesetzt war es nur im ersten Fall: sonst gingen Gegner, Objekte, Effekte und Schaden nicht in den Szenen-Zustand. `cMot3::set` (Ziel-Überblendung von Pistole, Messer, Granate, Raketenwerfer …) las die Überblendlänge über eine Register-Bindung (`r9`), die der Port weglässt: beliebige Länge. r104: die „schon geöffnet“-Aufgaben der Regale und Kisten bekommen die Nummer als Aufgaben-Argument, das Original deklariert keinen Parameter; beim Wiederkommen stand ein zufälliges (oder kein) Regal offen |
| 0092 `em39.cpp` | Krauser (em39) sucht seinen nächsten EMI-Punkt und überspringt bei einem zweiten Versuch die Punkte, die dem letzten in Byte 1 und 3 (`sub`, `pad_3`) gleichen: `*(u32*) e & 0x00FF00FF`, eine GameCube-Wortsicht; auf ARM/x86 verglich die Maske `type` und `state`. Jetzt `RE4_BE` (Fehlersuche 22.10.2026, `view_scan.py`) |
| 0093 `ss_item.cpp`, `ss_pzzl.cpp` | Z (R1 am Controller, Y mit Alternate Controls) öffnete im Gegenstand-Bildschirm den „Item Maker“ (Steuerkreuz und A geben oder entfernen Gegenstände) und im Koffer den „Case Maker“ (Inventar-Sätze, die das ganze Inventar ersetzen, Koffergröße, Munition, Peseten) der Debug-Fassung, auch im normalen Spiel. Ihr Text ist Debug-Text, den die App ausblendet: unsichtbare Editoren auf dem Steuerkreuz. Jetzt nur mit sichtbaren Debug-Anzeigen (`RE4_DEBUG_HUD=1`, `re4_debug_hud_shown` in `runtime/state_trace.cpp`). Dazu `(u8) wk->debugMode` statt Byte 3 des Wortes (Fehlersuche 22.10.2026) |
| 0094 `ss_shop.cpp` | Das Aufrüsten beim Händler (`LvUpConfirm::move`) schrieb die vier Waffenstufen über `TuneLevel`, vier 4-Bit-Bitfelder über `cItem::lv` (u16). Das übrige Spiel liest sie als Halbbytes des Werts (Feuerkraft << 12, Schussgeschwindigkeit << 8, Nachladen << 4, Kapazität, `item.h`). CodeWarrior legt das erste Bitfeld in die oberen Bits, clang auf ARM/x86 in die unteren: jedes Aufrüsten landete auf einer anderen Stufe (Feuerkraft ↔ Kapazität, Schussgeschwindigkeit ↔ Nachladen). Jetzt als Wert geschrieben, dazu eine Zeile `shop: tune-up of item …` im Protokoll (Tester-Berichte 22.10.2026; `view_scan.py` `bits` findet die Stelle) |
| 0095 `em36.cpp` | **Fehler des Prototyps** (Entscheidung des Projektinhabers, 22.10.2026: solche Fehler werden behoben). Ein Regenerator ohne Beine beißt vom Boden (Routine 1/0x14 `D_Catch`, Halten in 1/0x15 `D_CatchHit` mit `plem36_D_CatchHit`). Leons Seite prüfte die Routine des Bisses im Stehen (1/0xB): Leon wurde im ersten Bild losgelassen, der Biss lief unsichtbar weiter (Leben bis 1, „SHAKE OFF“, dann der tödliche Biss). Jetzt 0x15. `port/tools/scan/catch_scan.py` prüft alle Halte-Rückrufe so |
| 0096 `trans.cpp` | `commonModelTrans` nimmt die Mischart aus `bl[info->blend_mode]`, einer Tabelle mit 5 Zeilen; Modelle in r20a, r20b, r216 und r31d haben `blend_mode` 5 (Sanitizer-Rundgang 22.10.2026, global-buffer-overflow). Die GameCube liest dahinter die ersten vier Wörter von `funcLightParam` (`trans_lit.cpp`, Adressen von `lightSetConstant` … `lightSetSpotlight`); `GXSetBlendMode` behält davon Bit 0 und die unteren 3/3/4 Bit: kein Mischen, Logik-Op 12. Der Port ergänzt diese sechste Zeile; vorher las er die Konstanten, die clang dahinter legte |
| 0097 `esp_app.cpp` | `EffAreaUpdate` setzt je Effekt-Bereich, in dem Leon steht, `1 << area_no`; Bereiche mit Nummer 255 (r102, r103, r119, r120, r227) setzen auf der GameCube (`slw`: untere 6 Bit, 32–63 ergeben 0) und auf armv7 nichts, auf aarch64 (`lslv`, modulo 32) und x86 Bit 31: die Effekte von Bereich 31 liefen dort an. Der Port schreibt die Regel der GameCube aus (Sanitizer-Rundgang 22.10.2026) |
| 0098 `pl_npc.cpp` | `cSubChar::moveDamage` ruft Ashleys Schadens-Handler `pAuxDm` ohne Argument auf; `SetSubBulldozer` setzt ihn auf `objBull.cpp` `Sub_dm_bull(cEm*)` (Ashley am Steuer des Bulldozers, r30f). Auf der GameCube stand in r3 noch `this`; auf ARM64 die Adresse des Handlers selbst: `setStatus` schrieb in den Code (SIGSEGV, SCV40-Tester 22.10.2026, zweimal). Jetzt mit `this`; die übrigen Partner-Handler (`pAux(this)`, `m_pFunc` von `SetSubDamage`/`EmCatchSubSet`: em10, em2b, obj13 lesen `SUB_CHAR()`) geprüft. Test `partnerdmg` |
| 0099 `r318.cpp` | 64 bit: der Laser-Gang zeichnet jeden Strahl von Teil 2 zu Teil 4 des Emitters und liest den Anfang bei `(u8*) parts + 0x70`, wo `cCoord::world` auf der GameCube und ILP32 liegt; auf LP64 liegt `world` bei 0x80, bei 0x70 stehen das Ende der lokalen Matrix und eine Hälfte von `pParent`. Die Strahlen erschienen woanders, als der Treffertest (Teile 2 und 4) sie sah: Leon wurde von unsichtbaren Strahlen zerschnitten (SCV40-Tester, 0.42.1, Video). Jetzt `RE4_OFS(cParts, world, 0x70)`. Die Suche von 0073 sah den Abstand nicht (er hängt an einem `Vec*`, der aus dem `cParts*` gewaschen ist) |
| 0100 `puzzle.cpp`, `ss_pzzl.cpp` | Gegenstände, deren gespeicherter Platz im Koffer belegt ist oder außerhalb liegt (Spielstände, die der unsichtbare „Case Maker“ des Debug-Builds bis 0.41.3 verändert hat), lagen nirgends: unsichtbar, und als ausgerüstete Waffe standen Ausrüstungsmarke „E“ und Rahmen in der Bildmitte, auf allen Seiten des Untermenüs (Projektinhaber, 22.10.2026). `pzlPlayer::init` legt sie nach allen anderen an die erste freie Stelle (Koffer, sonst Ablagefeld; wie gespeichert, sonst gedreht), Log-Zeile `case: item … moved to …`; ohne freie Stelle der Original-Fehler. `pzzlEquipDisp`: keine Marke für eine Waffe ohne Platz. Laufzeittest `casefree` |
| 0101 `sce_at.cpp` | `SceAtExecRoomJump` (Raumwechsel aus Skripten, die Aufzüge) baut eine Tür-Fläche auf dem Stapel und füllt nur das Ziel; `key_id` und die übrigen Felder waren Stapel-Reste. Im Port startete `key_id` 1/2 die Aufgabe `sceInLock` mit einem Zeiger auf die lokale Variable, die ein Bild später ins Leere griff (SIGSEGV am Aufzug in r332, SCV40-Tester, 0.42.1). Die Fläche beginnt jetzt mit Nullen (offene Tür, normale Blende). Schlummernder Fehler des Originalcodes |
| 0102 `include/ctrl.h` | `cCtrl` (die kleinen Raum-Helfer von `ctrl.cpp`, 0x214 Byte) hat seinen Arbeitsbereich `work` bei 0x13, jede Art (`CTRL11_WK`, `CTRL12_FREE`, `CTRL14_FREE`, `LightCtrlWork`, `ctrl01`) liegt also an einer ungeraden Adresse. Die GameCube nimmt das mit `lwz`/`stw`; clang fasst auf armv7 zwei Zugriffe zu `strd`/`ldrd` zusammen (oder nimmt `ldm`/`stm`, `vldr`/`vstr`), die eine durch 4 teilbare Adresse brauchen: `GetCtrlDragon` (r222, der Burgsaal mit den Drachenköpfen) schrieb mit `strd` nach c+0x1F, SIGBUS im armhf-Rundgang (22.10.2026). Ein 64-bit-Kernel korrigiert das für 32-bit-Apps erst ab Linux 6.1, der 32-bit-Kern auf den Geräten stürzt also genauso ab. Der Port legt `work` ein Byte später (Füllbereich 7 statt 6 Byte, Objekt weiter 0x214); keine Disc-Datei und kein Spielstand enthält ein `cCtrl` |
| 0103 `mpv_bdec.c`, `mpv_lib.c` | `UTY_MemcpyDword` (CRI) kopiert wortweise, beim Start des MPEG-Dekoders auch vier Byte-Tabellen (`mpvbdec_bitmsk`, `mpvbdec_zigzag`, `mpvbdec_dfl_iqm`, `mpv_clip_0_255_tbl`); ab 16 Wörtern nimmt der armv7-Build `ldm`/`stm`. Auf der GameCube liegen die Tabellen auf Wortgrenzen, im Port nur zufällig (die Ausrichtungs-Suche fand `mpvbdec_bitmsk` 2 Byte daneben): sonst würde jeder Filmstart auf dem 32-bit-Kern mit SIGBUS enden. Der Port richtet alle vier auf 4 Byte aus |
| 0104 `snd.cpp` | `SndRoomBgmLoad` wartete beim Raumstart nur auf das Ausblenden des Musik-Platzes, den es lädt; die neuen Musik-Bänke liegen aber über denen beider Plätze des alten Raums. Nach Saddlers Ereignis blendete BGM 1 noch aus, als der Aufzug r331 erreichte: sein Sequenzer las die neue Bank (`SND SEQ data error : 07 63 00`, OSPanic, SCV40-Tester, 0.43.0). Der Port lädt Räume viel schneller als die Disc. Jetzt wartet es, bis kein Platz mehr ausblendet (nach 0059) |
| 0105 `main.cpp`, `trans.cpp` | Bildformat „4:3 (ohne Balken)“ (Anbernic RG 477V, 4:3-Bildschirm): solange `re4_letterbox_open()` (`runtime/picture.cpp`: Schalter des App-Menüs, Hauptschleife `Rno0` 3, kein Untermenü, kein Ereignis) gilt, keine Schere auf die Zeilen 56..392 in `Render` (die setzt jeder Raumstart, `SYS_SCISSOR_ON`) und kein `Draw_cinesco`; sonst wie im Original |
| 0106 `ss_main.cpp` | Touch im Untermenü: `SubScreenTask` ruft je Bild vor `cur->move` `re4_touch_sscrn` (`runtime/menu_touch.cpp`) mit dem aktuellen Widget und den vier Haupt-Widgets (jetzt mit 0 vorbelegt, Händler und Funk legen keine an); ein Tipp der App wird zu Cursor und Tasten dieses Bildes (Koffer-Cursor, Befehlsmenü, Frage), ein Wischen zum Reiterwechsel (seit dem Gerätetest 22.10.2026 20:09 ohne Ring, Reiter per Tipp ausgebaut); seit dem Gerätetest 21:22 dazu gleich nach `cur->move`, vor dem Zeichnen, `re4_touch_sscrn_moved`: die Hervorhebung des gewählten Reiters im Bild, in dem die Leiste den Fokus bekommt (aus dem Koffer blinkte sonst „Exit“) |
| 0107 `snd_drv.h` | Anfrage-Speicher des Tontreibers 256 statt 64 Plätze (`SND_REQ_MAX`, Anpassung des Ports, 22.10.2026): auf dem GameCube läuft der Ton-Takt des Spiels alle 5 ms mitten im Spielbild und nimmt die Anfragen dieser 5 ms; im Port laufen die Ton-Takte zwischen zwei Spielbildern (`vi.cpp` `re4_os_idle`), ein Speicher nimmt also alle Anfragen eines Spielbilds (jeder Raumklang je Bild, jeder neue Ton, jedes Stopp). Ein voller Speicher verweigert einen Ton stillschweigend; vermutete Ursache der stummen Todesschreie in großen Kämpfen (Gerätetest 22.10.2026 23:41). Die Zeile `sound: …` (`state_trace.cpp`) zählt Takte mit mehr als 64 Anfragen |
| 0108 `debug.cpp` | `proc_tick` (die eigene Zeitmessung eines Spielbilds, `ProcessTickGet` in `main.cpp`) nicht mehr `static` (Anpassung des Ports, 22.10.2026): `runtime/state_trace.cpp` mittelt daraus die Abschnitte eines Bilds und schreibt alle 600 Spielbilder die Zeile `frame: … logic …, drawing …, rest …, frame end …, waiting …` in den Bericht (wohin die Zeit eines schwachen Geräts geht). Nur die Sichtbarkeit ändert sich, die Reihenfolge in `.bss` bleibt |
| 0109 `em10.cpp` | `em10DmSetWep09` (Schrotflinten, Magnum, Gewehre) fragt vor `Dm_Blow` auch `em10RoofDmCk` (Fehler des Prototyps, 24.10.2026): Ganados von Dach, Seilbahn, Mauer oder Brüstung fallen mit dem Fallschrei (`Dm_Roof`, `Se_tbl[14]`) statt stumm über die Kante; nicht für Explosionen (`mag`) |
| 0110 `em3e.cpp` | Zielscheiben und Tore (`cEmMark`) tragen `EM_STATUS_ASHLEY_NO_HELP` (Fehler des Prototyps, 24.10.2026): in der Schießbude r22c zählten die beiden Tore (em 0x3E, hp 1000, an 0 0 0) als Gegner neben der wartenden Ashley, sie rief alle 151 Bilder „Help!“, sobald Leon mehr als 2 m weg war |
| 0111 `game.cpp`, 0112 `title.cpp` | Instant-Spielstand laden: `gameMainLoop` und `Title_task` rufen `re4_instant_game_tick` / `re4_instant_title_tick` (neu erzeugt mit `whole_make_patch.py`, 26.10.2026; vorher von Hand und nur mit Unschärfe anwendbar) |
| 0113 `dvd.cpp` | Teile-Tabelle prüfen (`re4_dvd_header_check`): eine Tabelle ohne Ende-Eintrag (nur Nullen: Raumdatei auf dem Gerät beschädigt oder stiller Lesefehler) ließ `readInit`/`readMain` über `header_buff` hinauslaufen, das ganze Spiel stand (r222 nach der Tür von r210, Infinix X6725 und Galaxy S20 FE, 25.10.2026; r325 auf dem Xiaomi 2109119DG). Der Port liest die Tabelle bis zu dreimal neu, beendet dann das Lesen mit Fehler, einer Bildschirmzeile „DISC FILE DAMAGED“ und einer ausführlichen Zeile im Fehlerprotokoll. Kein Fehler des Prototyps |
| 0114 `db_light.cpp` | `TOOL_ERR` liest `pTool->Flag`, `pTool` ist ohne geöffneten Licht-Editor leer; das Debug-Werkzeug „SCE ITEM“ stürzte so ab (GameCube: lesbarer Speicher bei Adresse 0x14; Galaxy A02s, Redmi Note 14 Pro). Port-Patch |
| 0115 `at_mod.cpp` | Ein getretener Ganado (em10, Schiebe-Vorrang 3) und eine Tür (id 0x41, Vorrang 3) schieben sich gegenseitig nicht (`priorityCheck`): der Ganado fliegt durch die geschlossene Tür des r100-Hauses (Fehler des Prototyps, in Dolphin bestätigt, 26.10.2026). In der Luft schiebt die Tür ihn wie einen stehenden Ganado (Vorrang 2). Das gemerkte `m_flag` ist 16 bit (die erste Fassung mit `u8` löschte Bits 0x100/0x200: Leon lief durch Türen, Nachtest 26.10.2026); Test `runtime-test em at flags` |
| 0116 `db_menu.cpp` | Das Debug-Menü (SELECT gehalten, Patch 0061) zeigt nur AREA JUMP und DEBUG OPTION, in einer Spalte am linken Rand, immer an derselben Stelle (Wunsch des Projektinhabers, 26.10.2026; das Original listet 34 Werkzeuge in zwei Spalten um x = 176 und lässt sich mit dem C-Stick verschieben). Hoch/runter wählt, A startet, B oder START schließt wie bisher; die Menütabelle bleibt (ein mit `DbMenuSetExecTool` vorgemerktes Werkzeug startet weiter über seinen Namen) |
| 0117 `read.cpp` | Kern-Archiv an einem zweiten Platz, wenn es größer ist als der feste (Verkaufsdaten, `retail-eu-plan.md` R1, 26.10.2026): `etc/core.das` der europäischen Discs hat 0x23AC40 Byte, der Platz der Debug-Version bei 0x80578000 fasst 0x234000 (`CORE_DATA_MAX`) bis zu den Optionsdaten bei 0x807AC000; das Lesen lief hinein („CORE_DATA IS TOO LARGE“). `CoreDataRead` fragt `re4_core_data_place` (`runtime/dvd.cpp`: Größe des Körpers aus der Teile-Tabelle, bei yz2 die entpackte); ein größerer Kern kommt nach 0x81C00000 (4 MB in der Entwicklerhälfte von MEM1, sonst unbenutzt, `RE4_MEM1_CORE_ALT_ADDR`), der Kern der Debug-Version bleibt an seinem Platz. Port-Patch für Verkaufsdaten |
| 0118 `eprintf.cpp` | Ohne die Debug-Schrift `etc/moji8.tpl` (fehlt auf den europäischen Discs) legte `EprintfInit` keine Puffer an; `eprintf` und `EprintfDrawing` prüfen `eprintf_init`, `EprintfBufferClear` (jedes Bild über `EprintfFlush`) nicht und schrieb über einen Nullzeiger: das Spiel endete im ersten Bild. Jetzt bleibt der Debug-Text ohne Schrift aus. Port-Patch für Verkaufsdaten (R1) |
| 0119 `main.cpp` | Ersatz für `debug/roomInfo.dat` (fehlt auf den europäischen Discs): jedes „Neues Spiel“ der Debug-Version läuft über das Debug-Startmenü, das die Tabelle liest (`titleExit`, `cRoomJmp`), und endete dort. `systemRestartInit` legt dann eine Tabelle mit einem Eintrag an: der vorgewählte Raum (`pG->room_id`, r120 wie beim neuen Spiel) ohne Position (wie der r120-Eintrag der Debug-Tabelle), in der Form, die der Umwandler aus der Disc-Datei macht. `re4_room_info_stand_in` (`runtime/dolphin_os.cpp`) meldet das und lehnt einen anderen Debug-Startraum (`RE4_START_ROOM`) ab: das neue Spiel beginnt in r120 und läuft von selbst nach r100. Port-Patch für Verkaufsdaten (R1) |
| 0120 `ss_model.cpp`, `ss_term.cpp` | Funkspruch mit den europäischen Discs (Verkaufsdaten, R2, 26.10.2026): `ss/eng/ss_term.dat` hat dort 10 statt 11 Teile, die zweite Textur der Funkgerät-Teile fehlt (die EU-Dateien hängen sie an jede `ss/cmn/ss_ocNNN.dat` an, Teil 16), die Bewegung liegt auf 13 statt 14. Aus der europäischen `Sscrn.rel` abgelesen: `tel00ModelInit` nimmt die Textur aus der Datei des Gesprächspartners (`pTelDat`, 16), `termMotionCancel` die Bewegung von 13. Vorher endete das Spiel beim ersten Funkspruch in r100. Mit den Debug-Discs unverändert (`re4_disc_retail_eu`). Port-Patch für Verkaufsdaten **US-Discs (28.10.2026, `retail-us-plan.md`):** gilt ebenso, Weiche `re4_disc_retail()`. |
| 0121 `main.cpp`, `card.cpp`, `title.cpp`, `read.cpp` | Spielsprache mit den europäischen Discs (R7, Entscheidungen des Projektinhabers, 26.10.2026: Wahl im Ingame-Menü der App, bis dahin die Handy-Sprache, „LANGUAGE SELECT“ raus aus dem Titelmenü; 27.10.2026: ein Wechsel startet das Spiel nach einer Frage neu). Die EU-Fassung nimmt in `setLanguage` die Systemsprache der Konsole (2 Englisch … 6 Italienisch), diese Version fest 1 (US: keine Untertitel in Filmsequenzen, zwei Schwierigkeitsgrade). `setLanguage` und das Lesen der Systemdatei (`card.cpp`, sie trägt die Sprache, mit der sie gespeichert wurde) nehmen die Sprache der App, nach dem Zurückholen aus dem Reset-Speicher (`main`) ebenso; ein Wechsel startet das Spiel neu (Frage im App-Menü, Soft-Reset über `runtime/game_language.cpp`, Entscheidung des Projektinhabers vom 27.10.2026). `titleMenuInit` blendet die Einheiten 11 und 12 der europäischen `title.dat` aus (Textur 248, „LANGUAGE SELECT“, für Code, den diese Version nicht hat). `systemWorkInit` (jeder Start und Soft-Reset) nimmt sie ebenso. Größere `SS/<lang>/option.dat` (Deutsch, Französisch, Spanisch, Italienisch: 0x44000–0x4C2C0 statt höchstens 0x40000 Byte; sie liefen in Leons Daten) kommen an einen eigenen Platz (`OptionDataRead`, `re4_option_data_place`). Mit den Debug-Discs bleibt die Sprache 1, auch nach einer Systemdatei der EU-Discs. Port-Patch für Verkaufsdaten **US-Discs:** nicht nötig, Sprache 1 wie die Debug-Fassung (nur `ss/Eng`). |
| 0122 `mes.cpp` | Einsprachige Texttabellen der europäischen Discs (R7): die Untermenü-Texte liegen dort je Sprache in `ss/<lang>/*.dat` (145 Tabellen) als ein einzelner Textblock ohne den Kopf der Sechs-Sprachen-Tabellen (6, Offsets ab 0x1C). Die EU-Fassung von `getAddr`/`getMesNum` (main.dol 0x80023564/0x800235b4) liest so eine Tabelle direkt; diese Version las sie als sechs Sprachen und nahm Text von falschen Stellen. Jede Sechs-Sprachen-Tabelle beider Fassungen beginnt mit 6, 0x1C (217 Debug, 184 EU), daran entscheidet `mesBlock`. Der Umwandler (`formats_mes.cpp` `convertMdt`) dreht solche Tabellen als einen Block. Mit den Debug-Discs unverändert. Port-Patch für Verkaufsdaten |
| 0123 `mes.cpp` | Textsprachen Deutsch, Französisch, Spanisch, Italienisch (R7). `MessageControl::setLayout` nimmt Schriftgröße, Zeichen- und Zeilenabstand aus einer Tabelle mit zwei Zeilen (Japanisch, Englisch), ausgewählt nach der Textsprache; 2–5 lasen dahinter, die Untertitel des Funkspruchs kamen als riesige, verschmierte Buchstaben. Die EU-Fassung (main.dol 0x80023610, Tabelle 0x801F8E04, ebenfalls zwei Zeilen) nimmt für `pSys->language` 0 die japanische, sonst die englische Zeile; so jetzt auch hier. `MessageControl::setLanguage` wählt den Block der Sechs-Sprachen-Tabellen: die der EU-Discs sind Japanisch, Englisch, Französisch, Deutsch, Italienisch, Spanisch (Blockgrößen der Funktexte; im französischen Lauf kam der Funkspruch deutsch), die EU-Fassung (0x800236d4) ordnet 3 → 3, 4 → 2, 5 → 5, 6 → 4 zu, diese Version 3–6 → 2–5. Die Debug-Discs haben in den Blöcken 2–5 nur Platzhalter und spielen mit Sprache 1: mit ihnen unverändert. Zugleich ein Fehler des Prototyps (sein Debug-Menü bietet die Sprachen 2–7 an) `MessageData::getAddr`: die EU-Kern-Texttabelle hat ab 0x56 15 Texte mehr; mit den EU-Discs wird jede Nummer ab 0x56 um 15 verschoben (Optionsmenü zeigte „ZOOM“, Gerätetest 26.10.2026) **US-Discs (28.10.2026, `retail-us-plan.md`):** die Verschiebung um 15 gilt ebenso (168 Kern-Texte, dieselben 15 neuen ab 0x56), Weiche `re4_disc_retail()`; Sprache bleibt 1. |
| 0124 `ss_map.cpp` (Sscrn) | Karte im Untermenü mit den EU-Discs: deren Bildschirmbeschreibung (`SS/<lang>/ss_map.dat` Eintrag 3) führt Element 253 (Kopie des Bildschirms) zuletzt statt vor dem Hintergrund; mit dieser Version blieb die Karte leer (Gerät und PC, 26.10.2026). Die EU-Fassung (`SsMapMain::init`, Sscrn.rel Text 0x111ac) blendet es außer in r22c aus; so jetzt mit den EU-Discs. Mit den Debug-Discs unverändert. Port-Patch für Verkaufsdaten **US-Discs (28.10.2026, `retail-us-plan.md`):** gilt ebenso, Weiche `re4_disc_retail()`. |
| 0125 `cockpit.cpp` | Namensschilder der Lebensanzeige mit den EU-Discs: sechs (0x40–0x45) in beiden Fassungen, diese Version blendet 0x40–0x42 aus; auf den EU-Discs tragen 0x43–0x45 Namen und lagen über Leons („KiLeonr“, Retroid 26.10.2026). Wie die EU-Fassung (`LifeMeter::roomInit`, main.dol 0x800c05a8): alle ausblenden, 0x45 für Spielertyp 3, 0x43 für 4, 0x44 für 5. Mit den Debug-Discs unverändert. Port-Patch für Verkaufsdaten **US-Discs (28.10.2026, `retail-us-plan.md`):** gilt ebenso, Weiche `re4_disc_retail()`. |
| 0126 `sscrn.cpp`, `ss_term.cpp` (Sscrn) | Funkspruch mit den EU-Discs wie die EU-Fassung (R3, 27.10.2026): (1) `SubScreenExec` setzt die vier gemeinsamen Id-Tabellen aus `ss_cmmn.dat` (Peseten, Hauptmenü, Sonstiges, Hintergrund) beim Funkspruch (`open_flag` 0x20) nicht (EU: main.dol 0x8005a920), `SsTermMain::init` kein Hauptmenü; sonst war das `IdSub` (128 Plätze) voll und die dunkle Leiste hinter dem Untertitel fehlte („IDSystem::set() work full“). (2) Die EU-Funkspruch-Tabelle (`ss_term.dat` Teil 9, 78 statt 46 Bausteine) hat drei neue Bildgruppen 0x15–0x17 beim Partnerbild; die EU-Fassung blendet sie beim Start aus (0x16 nicht bei Funkspruch 14) und schaltet sie und 0x12 in `OpeMesMove` bei einem Bild des Funkspruchs um (13: 0x12 bei Bild 600; 14: 0x16 → 0x15 bei 5; 15: 0x17 bei 1215; 17: 0x12 bei 444; Tabelle im EU-Datenabschnitt 20 +0x1200). Ohne (2) lagen sie über Hunnigan, sobald (1) ihnen Platz gab (Gerät und PC, 27.10.2026; drei vorherige Fassungen verworfen). Mit den Debug-Discs unverändert. Port-Patch für Verkaufsdaten **US-Discs (28.10.2026, `retail-us-plan.md`):** gilt ebenso, Weiche `re4_disc_retail()`. |
| 0127 `r101.cpp` | Fehler des Prototyps: nach dem Aufheben des Schlüssels zur Tür nach r102 zeigt `r101_checkDoor102KeyUse` Raumtext 0xB („Das Regal ist an die Wand genagelt …“, in beiden Fassungen); „Du hast … benutzt“ ist 0x14. Die EU-Fassung (`st1_1.rel`) nimmt 0x14, jetzt auch diese (beide Fassungen) **US-Discs (28.10.2026):** der US-Code (`st1_1.rel`) fragt noch 0xB wie der Prototyp; der Patch gilt mit allen Discs, mit US-Discs also eine Abweichung zum Besseren. |
| 0128 `r221.cpp` | Raumtexte von r221 mit den EU-Discs (R3): die EU-Tabelle hat ab Text 6 alles einen Platz tiefer (Text für Text verglichen); der Code dieser Version fragt 6 („C'mon, c'mon!“ am Aufzug), 7 (nur Japanisch), 8 (Stickstofftank), 9 (Rollladen-Schalter) und 0xA (nach dem Rollladen), mit EU-Daten kamen ein leerer Text, der Tank-Text am Schalter, ein Wartehinweis danach. Die EU-Fassung (`st2_3.rel`) fragt an denselben Stellen 5–9; mit den EU-Discs jetzt ebenso. Mit den Debug-Discs unverändert. Port-Patch für Verkaufsdaten **US-Discs:** nicht nötig, ihre r221-Tabelle ist die der Debug-Fassung. |
| 0129 `ss_map.cpp` (Sscrn) | Händler-Markierungen der Karte mit den EU-Discs (R3, 27.10.2026): die EU-Kartendaten (`ss_map.dat` Teil 6) haben eine achte Händler-Markierung (Baustein 0xB mit „H“) für den Burg-Händler nach seinem Umzug (`SCF_R204_MERCHANT_MOVE`). Die EU-Fassung zeigt acht Markierungen (`getMerchantMarkNo` 7 → 0xB) und blendet in Bereich 2 Markierung 2 nach dem Umzug aus, Markierung 7 davor (ihr `GLOBAL_WK` hat `Scenario_flg` 0xC weiter hinten). Diese Version setzte Baustein 0xB nie: ein „H“ stand mitten auf der Karte und blieb beim Verschieben stehen (Video des Projektinhabers). Jetzt wie die EU-Fassung. Mit den Debug-Discs unverändert. Port-Patch für Verkaufsdaten **US-Discs (28.10.2026, `retail-us-plan.md`):** gilt ebenso, Weiche `re4_disc_retail()`. |
| 0130 `item.cpp` | Cheat-Menü der App (`port/docs/cheat-menu-plan.md`, Entscheidung 6 des Projektinhabers, 27.10.2026): mit `DBG_INF_BULLET` antwortete `cItemMgr::bulletNum(cItem*)` für jeden Platz 100, die Anzeige unten rechts zeigte „100“. Jetzt die echte Magazinzahl (`runtime/cheats.cpp` hält das Magazin der ausgerüsteten Waffe voll, das Bit selbst lässt `trigger` nichts verbrauchen); 100 nur, wo die echte Zahl 0 ist, wie bisher. Ohne das Bit unverändert. Port-Patch für das Cheat-Menü |
| 0131 `game.cpp` | Stage 4 mit den EU-Discs (Raum-Rundgang 10b, 27.10.2026): das Raummodul `rel/st4_0.rel` (r400–r411, darunter die Söldner-Räume) liegt bei der EU nur auf Disc 2, bei den Debug-Discs auf beiden. `gameStageInit` wechselt in dieser Version nur für Stage 3 auf Disc 2; ein Stage-4-Raum mit eingelegter Disc 1 endete in „RelDataReadError! rel/st4_0.rel“ (18 Räume im EU-Rundgang). Die EU-Fassung (main.dol 0x80118a28) wechselt für Stage 3 und 4; mit den EU-Discs jetzt ebenso. Mit den Debug-Discs unverändert. Port-Patch für Verkaufsdaten **US-Discs:** nicht nötig, Stage 4 liegt wie bei Debug auf beiden Discs. |
| 0132 `event.cpp` | Ereignis-Untertitel auf Englisch mit den EU-Discs (10b): die Debug-Version zeigt bei Sprache 1 (US-Englisch) keine; mit den EU-Discs ist Englisch Sprache 2 (`runtime/game_language.cpp`), und ihre Untertitel-Dateien haben keine englischen Texte. `Event::MesSet` zeigte sie trotzdem an: `Message::init` meldete „Msg[04] Address Error“ (15 Räume im EU-Rundgang) und zeigte Text 0 der Kerntabelle („14-09LE“ unter Leon und Luis in r104, GX-Bild). Die EU-Fassung (main.dol 0x8010f968) zeigt bei Sprache 2 keine; mit den EU-Discs jetzt ebenso. Deutsch zeigt weiter seine Untertitel („Vom Regen in die Traufe.“). Mit den Debug-Discs unverändert. Port-Patch für Verkaufsdaten **US-Discs:** nicht nötig, US-`Event::MesSet` vergleicht mit Sprache 1 wie die Debug-Fassung. |
| 0133 `r204.cpp` | Umzug des Burg-Händlers mit den EU-Discs (10b): beide Fassungen lassen den Händler in r204 nach Raum-Merkmal 1 an einer neuen Stelle stehen (Gegnerliste 3: Eintrag 0x59 weg, 0x55 bleibt). Die EU-Fassung (`R204Init`, `st2_1.rel`) setzt dabei die Szenario-Merkmale 88 (`SCF_R204_MERCHANT_MOVE`) und 174; ihre Karte fragt 88 für die achte Händler-Markierung (Patch 0129). Diese Version setzte sie nie: mit den EU-Discs zeigte die Karte den Händler nach dem Umzug an der alten Stelle. Jetzt wie die EU-Fassung. Mit den Debug-Discs unverändert. Port-Patch für Verkaufsdaten **US-Discs (28.10.2026, `retail-us-plan.md`):** gilt ebenso, Weiche `re4_disc_retail()`. |
| 0134 `global.h` | Mercenaries mit Hunk stürzte auf jeder Karte ab (Testerbericht 26.10.2026, `HermiteInterpolation` nach „Invalid parts“). Der Code der Maschinenpistole (`wep/objMachinegun.cpp` `setMotion`, gemeinsam für Leons TMP `wep11` und Hunks `wep29`) holt die Nacken-Bewegungen aus den Waffen-Archiv-Plätzen 0x34..0x39; `wep29` hat nur 48 Einträge (Plätze 4..0x33), in beiden Fassungen. Hinter der Tabelle stehen die Kennungen der Einträge („EFF\0“ …): der GameCube liest sie big-endian als Abstände von etwa 1 GB, der Zeiger liegt außerhalb des Speichers, `VALID_PTR2` verwirft ihn (Kopf bleibt still); die App las sie little-endian als wenige MB, also als Bewegung. `PL_ARC_PTR` geht jetzt über `re4_arc_entry` (`re4_whole_prelude.h`): ein Platz hinter der Tabelle eines Archivkopfs (Anzahl < 0x1000, zwei Nullwörter) liest sein Wort wie der GameCube, einmal je Stelle im Protokoll („past its table“). Plätze in der Tabelle unverändert. Fehler des Ports (Byte-Reihenfolge), kein Fehler des Prototyps |
| 0135 `option.cpp` | Ergebnis-Bildschirme mit den EU-Discs wie ihr Code (`retail_code.py dis`): Kapitelende (`ChapterEnd::move`, EU 0x800422AC) mit vierstelliger Gegnerzahl, Tausender in Baustein 0x1B (Kapitel) bzw. 0x1C (gesamt), führende Nullen aus, Fortsetzungen höchstens 999; vorher liefen die zwei Bausteine durch alle Zeichen (Testervideos 27.10.2026). Spielende (`GameResult::move`, EU 0x80041E64): Fortsetzungen höchstens 999, Baustein 0 nie sichtbar. Mercenaries-Ergebnis in beiden Fassungen gleich. Mit den Debug-Discs unverändert. Port-Patch für Verkaufsdaten **US-Discs (28.10.2026, `retail-us-plan.md`):** Kapitelende und Baustein 0 des Spielendes ebenso (`re4_disc_retail()`; EU-Vorlagen `chapNN.dat`, US-`GameResult::move` blendet Baustein 0 immer aus), die 999-Grenze des Spielendes nur EU (US-Code hat sie nicht). |
| 0136 `sofdec.cpp` | Untertitel im Intro-Film mit den EU-Discs (Projektinhaber, 28.10.2026: fehlten). Nur der EU-`movie/opening.sfd` trägt im Kopf (privater Strom 0xBF) eine Tabelle `CRITAGS`/`USRINFS` Typ `SUBTITLE`: je Sprache ein Kanal (3 Deutsch … 6 Italienisch; Englisch hat keinen), je Eintrag Startzeit, Dauer, Textnummer; die Texte in `etc/movie.das` (MDT mit sechs Sprachen, dazu eine japanische Schrift). Wie die EU-Fassung (`cSofdec` 0x80161170 ff., `retail_code.py`): `initApp` lädt `etc/movie.das` als Meldungstabelle 2 und setzt das Layout von Meldung 0, `initWork` lässt die Meldungen während des Films an (`DPF_MESSAGE`), `draw` zeigt zur Filmzeit den Text (`MesSet` x 100 unten, Attribut 0x01001054) und nimmt ihn danach weg, `finishMovie` räumt auf. Ihre neuere CRI-Bibliothek sucht die Zeit im laufenden Film; hier liest `runtime/movie_status.cpp` die Tabelle aus dem Kopf, den `initApp` ohnehin liest, und vergleicht mit `mwPlyGetTime`. Abweichung: Tabelle 2 wird danach zurückgesetzt (die EU-Fassung zeigt danach auf die freigegebenen Texte). Dazu `FNT`-Einträge in Archiven umwandeln (`formats_arc.cpp`). Mit den Debug-Discs unverändert. Port-Patch für Verkaufsdaten **US-Discs:** nicht nötig, keine Untertitel-Tabelle und kein `etc/movie.das`. |
| 0137 `trans_ot.cpp` | 60 FPS ohne Flackern, Schritt S1 (`60fps-plan.md`, Entscheidung des Projektinhabers 28.10.2026): `ExecOt` meldet vor jedem Eintrag einer Ordnungstabelle Objekt, Funktion und Tabelle (`runtime/gx_owner.cpp` → `gx_fifo.cpp` `re4_gx_put_owner`, Port-Befehl 0x02 im GX-Strom → `RE4_GX_OP_OWNER` im Chunk; bei Teilchen aus dem Effekt-Pool dazu Effekt-Nummer und Lebenszähler), nach der Tabelle „kein Objekt“. Spiellogik und Bild unverändert; ältere Renderer überspringen die Nachricht. Kosten: in r204 etwa 290 Marken je Bild (4–5 KB). Port-Patch |
| 0138 `r20b.cpp` | Labyrinth mit den EU-Discs (Testerbericht 28.10.2026): ihre Daten sperren die Treppe vom Podest für Easy (Bereich 0x1C als Wand, Modellgruppe 0xA6 als Brüstung; die Debug-Discs haben beides nicht). Wie die EU-Fassung von `R20bInit` (`st2_1.rel` 0x10F44): über Easy (`game_mode > 3`, nicht in den Bonusspielen) Bereich 0x1C aus und Gruppe 0xA6 unsichtbar; auf Easy Raum-Flag 1 (Mondstein-Tor offen), Gruppen 0x86–0x8A unsichtbar, Bereiche 0x80–0x82 und 0x88 aus, Kisten 9/10/14/15 weg, kein Funkspruch, keine Eingangskamera, keine Gegner. Mit den Debug-Discs unverändert. Port-Patch für Verkaufsdaten |
| 0139 `title.cpp` | Schwierigkeitsauswahl mit den EU-Discs wie ihr `titleMain`: „New Game“ fragt immer Professional (nur freigeschaltet) / Normal / Easy, A oder START, Cursor rundum, B zurück; die japanische Rückfrage danach nicht. Nicht beim Debug-Start (`RE4_START_ROOM`). Mit den Debug-Discs unverändert. Port-Patch für Verkaufsdaten |
| 0140 `game.cpp` | `gameStageInit` setzt mit den EU-Discs `game_country = game_mode > 3` wie ihre Fassung (das EU-„Easy-Byte“ `pG+0x4F9F` ist `game_country`): Bereichs-Flaggen JPN/USA, Schwierigkeitswert-Untergrenze und Raumzweige richten sich nach Easy/Normal statt nach der Sprache. Mit den Debug-Discs unverändert. Port-Patch für Verkaufsdaten |
| 0141 `r203.cpp`, `r211.cpp`, `ss_map.cpp` | Easy-/Normal-Zweige der EU-Räume, die dem Debug-Code fehlen: r203 (Normal Meldungsbereich 6 an Tür 5 aus; Easy Tür 5 zu, kein Schlüssel nötig, weniger Ganados), r211 (Easy erster Kelch und Gruppe 0x20 mit Kollision; Normal Gruppe 0x20 aus), Karte auf Easy aus `SS/Esy`. Mit den Debug-Discs unverändert. Port-Patch für Verkaufsdaten |
| 0150 `sofdec.cpp` | Vorfilm-Untertitel mit den JP-/KR-Discs (Nutzermeldung 30.10.2026: falsche Zeichen). Zu 0136 der japanische Zweig ihres `cSofdec`: bei Sprache 0 macht `initApp` (G4BP08 0x80161210, G4BJ08 0x80160b50) die Schrift aus `etc/movie.das` (Eintrag 5, 28 × 28) zur Schrift 2 der Untertitel, `finishMovie` (G4BJ08 0x801610b0) setzt die Stage-Schrift `pG->pStFnt` zurück. Debug-, EU- und US-Discs unverändert. Port-Patch für Verkaufsdaten |
| 0151 `pl_wep.cpp`, `cam_extra.cpp` | „Zielgeschwindigkeit“ des Ingame-Menüs (Wunsch des Projektinhabers 30.10.2026, Seite Steuerung): `PlWepLockCtrl` (Waffen, Messer, Bogen) und `CameraScope::move` (Zielfernrohr) multiplizieren die Stick-Werte beim Zielen mit `re4_aim_speed()` (`runtime/aim_options.cpp`, 0,5–3, 1 wie das Spiel); Steuerkreuz unverändert. „Vertikal umkehren“ ist die Spieloption `CFG_AIM_REVERSE`. Port-Patch |
| 0152 `em10.cpp` | Handcannon gegen Gegner mit `Wep_type` 4 (Kettensägen-Mann u. a.): `em10SetDmVal` nimmt die Waffe 0x0F wie die EU-Fassung von der 1000er-Grenze aus (Nutzerbericht 30.10.2026). Fehler des Prototyps, beide Disc-Arten |
| 0153 `snd_iss4.cpp`, `snd_seq3.cpp`, `snd_str4.cpp` | Seite AUDIO des Ingame-Menüs (Wunsch des Projektinhabers 30.10.2026): Ausgabelautstärke × `re4_sound_volume()` (`runtime/sound_volume.cpp`, Musik bzw. Effekte in %, 100 wie das Spiel). Port-Patch |
| 0154 `r40e.cpp` | Chicago Typewriter nach Assignment Ada (Nutzermeldung 30.10.2026): `gameResult` prüft und setzt `EXT_GET_TOMPSON` statt `EXT_GET_SW500`, wie EU und US (`st4_0.rel`). Fehler des Prototyps, alle Discs |
| 0155 `r222.cpp` | Drachenhalle: die sechs Treffer-Kästen der Drachen mit 2000 statt 7000 Lebenspunkten, wie EU (`R222Init`, Nutzermeldung 30.10.2026). Fehler des Prototyps, alle Discs |
| 0156 `player.cpp` | „Immer laufen“ des Ingame-Menüs (Wunsch des Projektinhabers 30.10.2026): die vier Lauf-Abfragen (`KEY_B` gehalten) in `pl_R1_Wait`, `pl_R1_Walk`, `pl_R1_Run` über `re4_run_held` (`runtime/aim_options.cpp`), mit dem Schalter umgekehrt. Port-Patch |

Neuen Patch erzeugen: Datei aus `whole_prepare.rewrite()` kopieren, ändern,
`port/tools/whole_make_patch.py NAME pfad/im/repo kopie` ausführen. Mehrere
Patches dürfen dieselbe Datei ändern, solange sich ihre Stellen nicht
überschneiden (`patch` verschiebt die Zeilennummern).

Compiler-Schalter zusätzlich seit M4: `-fno-strict-return`. Fünf
Originalfunktionen fallen ohne `return` aus einer Nicht-void-Funktion
(`cDvd::ReadCheck` → Patch 0011, `cItemMgr::available`, `MercSysSetAddTime`,
eine em30- und eine emwindow-Funktion, deren Rückgabewert niemand nutzt). Ohne
den Schalter setzt clang dort einen Absturzbefehl.

**SDK-Auszug (`whole_prepare.py`):** Aus den Mathe-Dateien `mtx`, `mtxvec`,
`mtx44`, `mtx44vec`, `vec`, `quat` von `src/lib` werden nur die portablen
C-Funktionen (`C_*`) nach `prepared/sdk_c/` übernommen; die
Paired-Single-Assembler-Fassungen (`PS*`) fallen weg und `runtime/mtx_ps.c`
leitet die `PS*`-Namen auf die C-Fassungen um.

## Linken und Module (M3)

**Module als eigene Bibliotheken** (Entscheidung des Projektinhabers,
13.10.2026). Jedes Modul wird mit `RTLD_LOCAL` geladen und mit `-Bsymbolic`
gelinkt. So behält es wie auf der GameCube seine eigenen Kopien doppelter
Dateien (286 Symbole sind in mehreren Modulen definiert). Modul-IDs aus
`rel.json` stehen in der erzeugten Tabelle `kRe4Modules`.

**Aufrufe zwischen Modulen:** 24 Module rufen zusammen 893 Funktionen in
Nachbarmodulen auf. Das betrifft vor allem die Stufenmodule `st1_*`, `st2_*`
und `st3_*`, die gegenseitig ihre Räume registrieren, die Debug-Werkzeuge
(`Tools`, `t_*`) und einzelne Gegner/Waffen (`em27`, `em3e`, `wep17`). Diese
Abhängigkeiten sind zyklisch. Die GameCube löste sie beim Linken jedes Moduls
nach. ELF-Bibliotheken können Zyklen nicht vorab auflösen (Android kennt kein
„lazy binding“). Deshalb bekommt jedes Modul erzeugte Weiterleitungen
(`generated/imports/<modul>.S`, Assembler für i686 und armv7). Sie suchen das
Ziel beim ersten Aufruf in den geladenen Modulen und springen mit unveränderten
Argumenten weiter. Für ARM64 (M12) braucht es eine dritte Assembler-Vorlage.

**Einzige Datenreferenz zwischen Modulen:** `Tools` liest `DbgFileList` aus
`t_event`. Das Debug-Werkzeug bekommt eine eigene, leere Kopie. Das ist nur
für das Debug-Menü relevant.

**Platzhalter (`generated/stubs.cpp`):** Ein Probe-Link aller Objekte gegen
die C/C++-Laufzeit des Zielsystems ermittelt, was niemand liefert. Funktionen
schreiben beim ersten Aufruf `stub called: <Name>` ins Log und geben 0 zurück.
Datenobjekte sind genullt; Größe aus `config/G4BE08/symbols.txt`, sonst 4 KB.

| Platzhalter | M3 | M4 Teil A | M4 Teil B |
|---|---|---|---|
| Funktionen | 363 | 234: GX 97 (M6), AX/AXFX/AXART/MIX/SYN/SEQ/AI/ADX 77 (M8), CARD 17 (M10), mwPly 12 (M9), PAD 7 (M7), PPC-Leistungszähler 8, SN-PC-Host-I/O 7 (nur Entwicklerkonsole), OS 8 (IPL-Schrift `OSInitFont`/`OSGetFontEncode`/`OSGetFontTexture`, Stoppuhren, `OSResetSystem`), `TEXGet` | 209: wie Teil A ohne CARD, PAD und `TEXGet` (ersetzt durch `card.cpp`, `pad.cpp`, `tex_palette.cpp`) |
| Daten | 9 | 1: `GXWGFifo` (GX-Schreib-FIFO, `mes.cpp` schreibt Eckpunkte direkt hinein; M6) | 1: unverändert |

Seit G1 (15.10.2026): 76 Funktionen (Ton 59 für M8: ADX, AI, AXART, AXFX, MIX, SEQ, SYN, `AXSetCompressor`; OS 7: IPL-Schrift, Stoppuhren, `OSResetSystem`; SN-PC-Host-I/O 7; PPC-Leistungszähler 7), 0 Daten. GX, `mwPly` (`movie.cpp`) und die übrigen OS-Funktionen sind ersetzt.

Seit M8 (16.10.2026): 27 Funktionen. Seit M9 (16.10.2026): 45 Funktionen und 1
Datenobjekt. Die mitkompilierten CRI-Quellen verweisen aus totem Code auf 22
Funktionen, die es im Original nicht mehr gab (der Linker hatte sie
entfernt: `ADXF_GetFnameRangeEx`, `ADXSJD_*` 4, `ADXSTM_GetNumSctIbuf`,
`MWSFCRE_ATTACH_PICUSRBUF`, `SFD_Finish`, `SFD_GetPaStat`, `SFX_*` 9,
`mwPlyEntryFnameCore/Sub`, `mwPlyReleaseSub`, `mwPlyStartSub`) und das
Datenobjekt `sjuni_vtbl` (`sj_uni.c`, nur von toten Funktionen benutzt); dazu
OS 8, SN-PC-Host-I/O 7, PPC 8. Im Filmtest wird keiner davon aufgerufen.

Seit M10 (16.10.2026): 47 Funktionen und 1 Datenobjekt. Dazu kommen
`__CARDGetStatusEx` und `__CARDSetStatusExAsync`: `CARDNet.c` ruft sie aus
Funktionen, die das Spiel nicht nutzt (der Original-Linker hatte beide
entfernt). Deshalb liegen die CARD-Dateien in `libre4_main` (wie die CRI-Dateien),
wo die erzeugten Platzhalter gelten. `OSGetFontEncode` (Platzhalter, gibt 0
= ANSI wie eine US-Konsole) liefert `CARDInit` die Kodierung der Karte.

Die erwartete Liste steht seit M9 in `port/whole/stubs-expected.txt`;
`port/tools/whole_stub_check.py` prüft jeden Build dagegen (`whole_build.sh`
streng, `whole_build_android.sh` nur auf zusätzliche Platzhalter). Vorher
verglich der Android-Build mit dem ersten Linux-Build-Ordner unter
`port/build`; auf dem PC war das ein alter Stand von vor M8 (76
Platzhalter), deshalb brach der erste Gerätelauf von M9 mit „only stubbed on
Android“ für die 22 CRI-Namen ab, obwohl der Kern fehlerfrei gebaut war.
Ändert sich die Liste absichtlich, im selben Commit
`whole_stub_check.py BUILD/generated/link_report.json --write` ausführen und
die neuen Einträge kommentieren.

**Echter Ersatz statt Platzhalter** (`runtime/sn_runtime.cpp`): `_ctors`/`_dtors`
(leer, weil der ELF-Lader Konstruktoren selbst ausführt), `__builtin_new`/
`__builtin_delete` (GCC-2.95-Namen von `operator new/delete`),
`memset_asm`/`memclr_asm`, seit M9 `__cvt_fp2unsigned` (Kommazahl → u32 in den
CRI-Quellen, Ergebnis wie die Gekko-Umwandlung `fctiwz`, auch ab 2^31).

## Plattformschicht (M4 Teil A)

**Hauptspeicher an der Originaladresse** (`runtime/mem1.cpp`). Das Spiel
rechnet mit der GameCube-Speicherkarte: `SystemMemInit` legt die Heaps über die
festen Adressen `0x80974000–0x817F4000`, `read.cpp` lädt Kern-, Spieler- und
Waffenarchive an feste Adressen, `OS_BUS_CLOCK` liest `0x800000F8`,
`OSPhysicalToCached` addiert `0x80000000`, und rund 60 Stellen prüfen Zeiger
auf `0x80000000–0x82FFFFFF` (teils, um Offsets von Zeigern zu unterscheiden).
Darum wird `0x80000000–0x83000000` (48 MB wie eine Entwicklerkonsole) per
`mmap` genau dort reserviert, und die Startwerte des IPL stehen in den
OS-Globals (Disc-ID, Speichergröße 24 MB, Konsolentyp Retail, Arena, FST,
Takte). Ohne diese Adresse läuft der Port nicht (Abbruch mit Logmeldung).
Statische Objekte in den Bibliotheken liegen außerhalb dieses Bereichs; Prüfungen,
die sie treffen, müssen einzeln behandelt werden (bisher keine aufgetreten).

| Datei | Ersetzt |
|---|---|
| `dolphin_os.cpp` | `OSInit`, `OSReport` (zeilenweise ins Log), `OSPanic`, Konsolentyp/Speichergröße, `OSGetTime`/`OSGetTick` (40,5 MHz, Epoche 2000, seit M10 Ortszeit wie die Uhr der Konsole), Kalenderzeit, Interrupt-Sperren, Caches (no-op); seit G1 Unterbrechungs-Rückrufe des SDK (`__OSSetInterruptHandler`, `re4_os_hw_interrupt`), Kontexte, Reset-Funktionen, Versionsnamen und die Gekko-Register `HID2`/`WPAR` als No-op |
| SDK `OSAlloc.c`, `OSArena.c` | unverändert aus `src/lib`: Heap und Arena; `main_mem.cpp` liest die Heap-Deskriptoren direkt |
| SDK `Padclamp.c` (M7) | unverändert aus `src/lib`: `PADClamp`, die Totzonen und Bereiche des Original-Controllers; `PadRead` des Spiels ruft es auf den Rohwerten von `pad.cpp` auf |
| `os_thread.cpp` | `OSThread`, `OSSemaphore`: ein Host-Thread, strikte Prioritäten, Wechsel nur in OS-Aufrufen; jeder Thread hat einen Host-Stack (`port/src/stack_context.cpp` aus dem alten Port, neu mit ucontext für Linux x86, Boost.Context-Assembler für Linux ARM wie auf Android); ein Verteiler auf dem Stack des Startprogramms (`re4_os_run`). Spielsichtbar wie im SDK: `state`, `suspend`, Priorität, Warteschlangen, Stack-Magie `0xDEADBABE` |
| `vi.cpp` | VI-Retrace alle 1/59,94 s Echtzeit, zugestellt an Interrupt-Punkten (Verteiler im Leerlauf, `VIWaitForRetrace`, Patch 0012, später lange Hintergrundarbeit); Thread-Wechsel aus dem Rückruf erst am Interrupt-Ende. `RE4_VI_FAST=1`: ohne Warten (Tests). Seit M9 `re4_cri_timer`: die Zeitbasis der CRI-Uhr (`OSGetTime`, mit `RE4_VI_FAST` die zugestellten Halbbilder), die Halbbilder folgen dem Vsync des Bildschirms der App (`re4_vi_vsync`, 60/120 Hz; `RE4_VI_VSYNC=0` aus), und `VISetBlack` gilt ab dem nächsten `VIFlush` (Bildstücke mit `RE4_GX_CHUNK_BLACK`) |
| SDK AX, MIX, SYN, SEQ, AXART, AXFX (M8) | unverändert aus `src/lib` bis auf Patch 0047 (Effekte in C); ersetzt `ax.cpp` (K2) |
| `dsp.cpp` (M8) | der DSP mit dem AX-Microcode: Befehlslisten von `AXCL.c`, Stimmen dekodieren und mischen, Aux-Busse, Ausgabe (siehe „M8 Audio“) |
| `ai.cpp` (M8) | AI: 200 Tonblöcke je Sekunde Spielzeit mit den VI-Halbbildern, Ring für die Tonausgabe der App (`re4_audio_read`), `RE4_AUDIO_WAV` |
| CRI-Middleware (M9) | unverändert aus `src/lib` bis auf die Patches 0048–0053: ADX, ADXF, SJ, SVM, Sofdec, MPEG (siehe „M9 Videos“); ersetzt `movie.cpp` (K2) |
| `movie_status.cpp` (M9) | Fortschritt jedes Films ins Log (Aufruf aus `sofdec.cpp`, Patch 0054) |
| `gx_fifo.cpp` (G1) | GX-FIFO und Register-Blöcke CP/PE/PI/MEM: Display-Listen aufnehmen, PE-Token/fertig als Unterbrechung (`re4_gx_deliver_interrupts`), Speicher wie ihn der Grafikprozessor sieht (MEM1 und Fenster für Port-Speicher, `re4_gx_physical`), EFB-Lesen antwortet „leer“ |
| `gx_decode.cpp` (G2) | Befehlsdecoder: Zustand CP/XF/BP/TLUT, Punkte, Indizes, Texturen mit Inhalts-Schlüssel, Bildpuffer-Kopien, Bildstücke mit Warteschlange für den Renderer (`re4_gx_frame.h`, `re4_core_gx_acquire/release/detach`) |
| SDK `GX*.c` (G1) | unverändert aus `src/lib` bis auf Patch 0030: die gesamte GX-Bibliothek |
| `dvd.cpp` | `DVD*` über einen Ordner mit entpackten Disc-Dateien (siehe unten); Lesen sofort, Rückruf vor der Rückkehr; aus einem Interrupt heraus (Audio-Rahmen: Nachladen der Sprach-/Musik-Streams) bleibt der Auftrag „beschäftigt“, bis der Interrupt-Rückruf zurückgekehrt ist (`os_thread.cpp re4_os_defer`), Abbruch liefert dann −3; FST der eingelegten Disc oben in MEM1, Disc-ID bei `0x80000000`; Disc-Wechsel lädt den anderen Ordner; seit M9 `DVDCancel` (Abbruch über den verzögerten Rückruf, Ergebnis „abgebrochen“); seit 19.10.2026 `re4_dvd_boot_fast` / `re4_dvd_read_done` für Patch 0085 (der Start ohne Bild-Takt) |
| `aram.cpp` | ARAM (16 MB) als Host-Puffer, `ARInit`/`ARAlloc` (Stapel ab `0x4000`), ARQ kopiert sofort und ruft den Rückruf (aus einem Interrupt heraus erst nach dessen Ende, wie `dvd.cpp`) |
| `os_link.cpp` | `OSLink`/`OSUnlink` (siehe unten) |
| `yz2_decode.cpp` + `port/src/yz2_kernel.cpp` | `yz2Decode_Decode`: der geprüfte Kern des alten Ports unverändert, ein Adapter verbindet ihn mit `yz2code.cpp` des Spiels |
| `mtx_ps.c` + `sdk_c/` | Matrix-/Vektor-/Quaternion-Mathematik (C-Fassungen des SDK) |
| `byte_order.cpp` | Byte-Tausch-Helfer für Format-Umwandler |
| `newlib_ctype.c` | newlib-Zeichentabelle `_ctype_` (heißt seit K2 `re4_newlib_ctype_`, Prelude-Regel) |
| SDK `CARD*.c` (M10) | unverändert aus `src/lib` bis auf Patch 0057: die gesamte Speicherkarten-Bibliothek, gelinkt in `libre4_main` (wie im Original Teil von `main.dol`); ersetzt `card.cpp` (M4 B: beide Schächte leer) |
| `exi.cpp` (M10) | EXI (die Schnittstelle der Speicherkarten-Schächte) in C wie `EXIBios.c`: Sperren mit Warteschlange, Auswahl, Sofort- und DMA-Übertragungen, Probe, Anstecken, Kennung; das Ende einer Übertragung und der „fertig“-Interrupt der Karte kommen an den Interrupt-Punkten (`re4_exi_deliver`: Leerlauf, `OSSleepThread`), im Interrupt-Kontext |
| `memcard.cpp` (M10) | die Karte in Schacht A: Memory Card 251 (2 MB, 8-KB-Blöcke) als Datei mit den Befehlen der echten Karte (Lesen 0x52, Seite schreiben 0xF2, Block löschen 0xF1, alles löschen 0xF4, Status, Hersteller); Status „entsperrt“ (kein DSP-Handshake); jede Änderung sofort in die Datei, 1 s danach `fsync`; SRAM (`__OSLockSram*`, Flash-ID aus dem Kartenkopf abgeleitet); `RE4_MEMCARD` (siehe „M10“); Log der Dateien auf der Karte und der Schritte des Kartenbildschirms (Patch 0058) |
| `host_file.cpp` (M10) | Dateizugriffe für Code mit den Schaltern des Spiels (Kartendatei): Öffnen samt Ordnern, Lesen/Schreiben an einer Stelle, `fsync`, Schließen; dort wirken die Umbenennungen der C-Bibliothek (64-bit-Dateistellen, Android-Fortify) |
| `card_files.cpp` (M11) | Dateien auf dem Kartenabbild für das Menü der App: Liste, Lesen, Schreiben/Ersetzen wie die Bibliothek (zwei Kopien von Verzeichnis und FAT, Blöcke wie `__CARDAllocBlock`) |
| `gyro.cpp` (19.10.2026) | Gyro-Zielen (Wunsch des Projektinhabers, wie im ersten Android-Port): `re4_core_gyro` (Drehraten der App, rad/s) und ein Bild-Haken in `PADRead`, der nur beim Zielen mit einer Schusswaffe (Hauptschleife, kein Untermenü/Ereignis, Routine `pl_R1_Weapon`, nicht das Messer) Körperdrehung `pPL->ang.y` und Neigung `m3r` samt Mitte des Wackelns (`cPlWep::pitch`) dreht, mit Zielfernrohr `CameraScope::angle_x`; 35 % der Gerätedrehung, Zoom wie der Stick. Als Spielcode übersetzt (`re4_gamehooks_obj`), kein Patch |
| `gci.cpp` (M11) | `.gci` für Dolphin/GameCube: Spielstände und Systemdatei Feld für Feld in GameCube-Reihenfolge und zurück (`save_layout.inc` aus den Headern, `port/tools/save_layout.py`), CRCs neu |
| `alarm.cpp` (M10) | `OSAlarm` (Zeitüberwachung der CARD-Befehle: 100 ms je Seite, 2 s je Block), fällig an den Interrupt-Punkten |
| `pad.cpp` (M4 B) | Controller: Kanal 0 angeschlossen, Kanäle 1–3 leer (ein zweiter Controller ließe den Titel die Logos überspringen). Tasten aus der Testfolge `RE4_PAD_SCRIPT` (`TASTEN@BILD` drückt 3 Bilder lang, `TASTEN@VON-BIS/ALLE` wiederholt; Tasten `A B X Y Z L R START UP DOWN LEFT RIGHT`, mit `+` verbunden; Bild = Zahl der `PADRead`-Aufrufe). Seit K2 zusätzlich `re4_pad_live` (Controller der App, sobald kein Testskript läuft) und Bildhaken `re4_add_frame_hook` (bis 4, je `PADRead`). Seit M7: Rohwerte wie die Hardware (das eigene No-op-`PADClamp` ist entfernt, siehe SDK `Padclamp.c`), `PADControlMotor` merkt den Rüttelmotor je Bild (`re4_core_rumble`) |
| `core_api.cpp` (K2) | Einbettungs-Schnittstelle `re4_core.h` für die App: Kern-Thread, Status, Controller, Dreifachpuffer der Schnappschüsse; seit M11 Anhalten für das Menü (`re4_core_pause`), Debug-Menü auf Wunsch, Kartenfunktionen nur bei stehendem Spiel und freier Karte; Testausgaben `RE4_BRIDGE_DUMP`, `RE4_BRIDGE_SNAPSHOT`, `RE4_GX_DUMP` (GX-Bildstücke als Dateien, `re4_gx_frame_io.h`) |
| `core_bridge.cpp` (K2) | Schnappschuss des Spielzustands (Kamera, Raum, Modelle aus EmMgr/ObjMgr mit Teile-Matrizen und Disc-Herkunft); mit den Schaltern des Spielcodes gebaut |
| `log.cpp` | Log (Linux: stderr, Android: logcat-Tag `re4`), seit K2 mit den letzten 24 Zeilen für das Overlay der App (`re4_log_tail`) |
| `tex_palette.cpp` (M4 B) | `TEXGet` des SDK (`src/lib/texPalette.c` braucht sonst den SDK-Dateicache) |
| `mem1.cpp` (M4 B) | zusätzlich `re4_check_game_address`: bricht mit Logmeldung ab, wenn `libre4_main` oder ein Modul unter `0x80000000` geladen wurde |

**Disc-Ordner** (`re4_host --data DIR`, sonst `$RE4_DATA_DIR`, sonst
`<re4_host>/data`): `DIR/disc0` und `DIR/disc1` mit `sys/boot.bin`,
`sys/fst.bin` und `files/…`, genau wie Dolphins „Gesamte Disc extrahieren“.
`port/tools/whole_disc_folder.py` legt ihn an: aus den Disc-Abbildern
(`--disc d1.iso --disc d2.iso --out DIR`, mit dem geprüften Disc-Leser
`original_import/gcm.py` des alten Imports) oder aus dem privaten Speicher von
`import_original.py` (`--store STORE --out DIR`, jede Datei gegen ihre SHA-256
geprüft); `--check DIR` prüft einen vorhandenen Ordner. Ziel immer außerhalb
des Repositorys.

**OSLink/OSUnlink:** Der REL-Kopf kommt big-endian von der Disc;
`re4_rel_header_to_host` dreht ihn an Ort und Stelle (erkennbar an der
Versionsnummer, wiederholbar). Dann Modul-ID → `re4_module_name` →
`re4_module_load`, und `_prolog`/`_epilog`/`_unresolved` der Bibliothek werden
in den Kopf geschrieben (`DLL_PROLOG` ruft sie). Zustand wie auf der GameCube:
Wird derselbe Kopf nach `OSUnlink` wieder gelinkt (Raum pausiert/fortgesetzt,
`cRoomData::stopRelData`/`restartRelData`), bleibt der Modulzustand erhalten; ein
neu von Disc gelesenes REL startet mit dem Anfangszustand der Bibliothek (sie
wird neu geladen). `OSUnlink` führt die statischen Destruktoren des Moduls aus
(das Original-Epilog tat das über `_dtors`; betrifft `Sscrn` und Debug-Werkzeuge).
Beim Entladen eines Moduls lösen die Weiterleitungen der anderen neu auf.
Bekannte Abweichung: Lädt das Spiel dasselbe Modul zweimal gleichzeitig, teilen
sich beide eine Bibliothek (Logmeldung).

## Formatumwandler (M4 Teil B)

**Entscheidung des Projektinhabers (13.10.2026): Umwandlung einmal beim
Laden, an einer zentralen Stelle.** Disc-Daten sind big-endian, der
Originalcode liest sie als Strukturen. Jede Datei wird deshalb unmittelbar
nach dem Laden Feld für Feld in Host-Reihenfolge gedreht:

- `re4_formats_part_loaded` (Patch 0015): am Ende jedes fertig gelesenen
  Dateiteils der Lade-Warteschlange (`cDvdQueue::readMain`), mit Dateiname,
  Verschachtelungstiefe, Teilnummer, `DvdHeader` (Typ, Größe) und Adresse
  (MRAM oder ARAM-Puffer). Nur die Tondaten-Streams (`snd_str*.cpp`) lesen
  an der Warteschlange vorbei (Audio, M8).
- `re4_formats_unpacked` (`yz2_decode.cpp`): nach dem Yz2-Entpacken. Einziger
  Nutzer ist `ReadAreaData`, das Ergebnis ist ein Raumarchiv.
- Auswahl: zuerst nach `DvdHeader`-Typ (Typ 1 Tonbank, Typ 2 Tonproben), dann
  nach Dateiname (`fnmatch`), in Archiven nach dem 3-Zeichen-Kürzel jedes
  Eintrags. Unbekanntes wird einmal geloggt
  (`formats: … no converter, left big-endian`) und bleibt unverändert.
- Die Umwandler werden mit den Schaltern und Headern des Spielcodes
  übersetzt (`re4_formats_obj`, Teil von `libre4_platform`): Feldlisten und
  Größen kommen aus den Originalstrukturen (`RE4_SWAP(blob, base, T, feld)`,
  `static_assert` auf die Strukturgrößen). Jeder Zugriff wird gegen die
  Blockgröße geprüft; passt eine Struktur nicht, bricht der Umwandler mit
  Logmeldung ab, statt fremden Speicher zu verändern. Gemeinsam genutzte
  Unterstrukturen werden nur einmal gedreht (`Seen`).
- Big-endian bleibt, was nur Grafik- oder Tonhardware liest: Texel und
  CLUT-Einträge von TPLs, Display-Listen, Tonproben. Darum kümmern sich
  Renderer (M6) und Audio (M8).

| Datei / Kürzel | Format (Quelle im Originalcode) | Umwandler |
|---|---|---|
| `bgm/bio4str.hed` | Stream-Blöcke: Offsets, `SND_RIT`, `SND_SHD` (snd_sub3.cpp) | `formats_snd.cpp` |
| `bgm/bio4midi.hed` | u32-Offsets der BGM-Dateien | `convertWords` |
| `bgm/doorse.hed` | `SndDoorTbl`, u32-Tabelle, u16-Zähler (snd.cpp) | `formats_snd.cpp` |
| `bgm/bgmtbl.dat` | `SndBgmTbl`, `SndBgmRoom`, u16-Raumliste | `formats_snd.cpp` |
| Teiltyp 1 (Tonbank) | Kopf, ISS-Block, `SND_SIT`, Wavetable (`WTFILEHEADER`, `WTREGION`, `WTART`, `WTSAMPLE`, `WTADPCM`), Sequenz-Tabelle (Sequenzen selbst sind Byteströme), Zufalls-Tabelle der Geräusche (Kopfwort 1, `seRandomCheck`: nur die Gruppen, die die SITs nennen; die Wavetable endet vor ihr) | `formats_snd.cpp` |
| `etc/sizetbl.dat` | Zähler + Name/Größe (das Spiel liest es nicht) | `formats.cpp` |
| `*.tpl`, `TPL` | `TEXPalette`, `TEXDescriptor`, `TEXHeader`, `CLUTHeader` (tpl.h) | `formats_tex.cpp` |
| `font/*.fnt` | Kopf (TPL-Offset, Breitentabelle in Bytes) + TPL (mes.cpp) | `formats_tex.cpp` |
| `MDT` | Sprachblöcke `MesTblBlock` + u16-Nachrichtencodes (mes.cpp) | `formats_mes.cpp` |
| `op/op*.das` | Funkgespräche: Archiv bei +0x400 im Teil (ss_term.cpp `opArc`) | `formats_arc.cpp` `convertCodecArchive` |
| `*.das`, `*.drs`, `ss/*.dat`, entpackte Räume | Archiv: Zähler, REL-Offset, Offsets, Kürzel; eingebettetes REL bekommt `re4_rel_header_to_host` (read.cpp `GetDataExt`). Ein Eintrag mit demselben Offset wie ein späterer ist leer, die Daten gehören dem letzten | `formats_arc.cpp` |
| `UWF` | `ID_FILE_HEADER`, `ID_DATA_V1/V2`, B-Spline `FUNC_PATH_PTR`, `HERMITE_1_PTR` (id_sys.cpp) | `formats_id.cpp` |
| `EFF` | `ESP_EFF_HEADER` (Version 0xB): Id-Listen, TPL- und `cAnm`-Tabellen, `cEspSeqHead` + `cEspSeqTbl`, Pfade (`Path`), `EffEfmEnt` (eff_sys.cpp) | `formats_eff.cpp` (Effektmodelle, -motionen, Shape-Köpfe noch nicht) |
| `TEX` | `TexData` (Version 3), Id-Liste, TPLs, `cAnm` (texture.cpp) | `formats_eff.cpp` |
| `VIB` | `VibDataTbl`, `VIB_INFO`, `VIB_DATA` (pad.cpp) | `formats_eff.cpp` |
| `debug/roominfo.dat` | Stufen-Offsets, `CRoomInfo` (room_jmp.h) | `formats_dbg.cpp` |
| `BIN` | Modell `cModelData` (Versionen 0x20010801/0x20030817/0x20030818): Kopf, Teile-Datensätze, Positionen/Normalen/Texturkoordinaten (Host), Gewichte `cWeightExt`, Primitiv-Köpfe, Shape-, Blend-, Flip-Tabellen; Display-Listen und Farben bleiben big-endian. Im Kernarchiv auch der Lichtpfad (lightPath.cpp) | `formats_model.cpp` |
| `FCV`, Kurven in EVD/ETM | `MotionData`: Zähler und Bildnummern Host, Schlüsselwerte bleiben big-endian (`FccF32`/`FccS16`, Patch 0018) | `formats_model.cpp` |
| `SEQ` | Sequenztabelle | `formats_model.cpp` |
| `LIT` | `cLit`, `cLightEnv`, `cLightWork` mit Spot-Block und Bewegungsdaten je Id (light01..10) | `formats_lit.cpp` |
| `CAM` | `CAM_FILE_HEADER` B402–B404: `CUT_INFO`, `AREA_DATA` + Eckpunkte, `CAMERA_DATA` + Schlüssel, `LERP_DATA` (B400/B401 und Daten ohne Versionskennung bleiben unverändert, Log) | `formats_event.cpp` |
| `EVD`, `event/evd/*.evd`, `EVS` | `EvtHeader`, Pakete mit Zahlenparametern je Typ, Anhänge nach Verwendung im Paket (Kamera/Motion/Shape-Kurven, LIT, Nebel, Fokus, EFF, MDT, Modelle, Texturen) oder Endung | `formats_event.cpp` |
| `SMD`, `SMX`, `SHD`, `ETM`, `ITM` | Streumodelle mit BIN/TPL/Motion-Tabellen, Objektparameter (Typen 1/2 mit Feldliste), Schatten, Etc-Archiv (benannte Dateien), Item-Modelle | `formats_scene.cpp` |
| `SAT`, `EAT` | `cSatFile`/`cSatHeader`, Vektoren, `SAT_POLY`, Blockkette (andere Versionen als 0x20/0xFF/Blockliste bleiben unverändert, Log) | `formats_scene.cpp` |
| `MAP` (`SS/cmn/map_objNN.dat`) | Räume der Karte (Sscrn `ss_map.cpp` `mapRoomNum`/`mapBinAddr`/`mapHitAddr`): u32 Anzahl (Modelle + 2), drei Wörter, Offsets; Modelle wie `BIN`, zwei Böden wie `SAT` | `formats_scene.cpp` `convertMapRoom` |
| `CNS`, `ETS`, `EMI`, `SAR`, `EAR`, `FSE`, `ESE`, `DSE`, `STB`, `BLK`, `RTP`, `OSD`, `AEV`, `ITA`, `etc/*.esl` | Raumtabellen aus gameRoomInit/ReadAreaData (Quelle je Format im Kopf von `formats_room.cpp`) | `formats_room.cpp` |
| Effektmodelle in `EFF` | BIN/TPL/Motion der Effekte | `formats_eff.cpp` |
| `rel/*.rel` | REL-Kopf | `OSLink` (`re4_rel_header_to_host`) |
| `debug/*.txt` | Text | nichts zu tun |

Bekannte Lücken (werden beim ersten Auftreten geloggt): SMX-Objekttypen
außer 1 und 2 mit nicht leerem Arbeitsbereich, Bereichstypen ohne
Nutzlast-Feldliste (`area type N … payload has no field list`), Kameradateien
B400/B401. Ob „Error: Can't calc Inverse Matrix !“ im Titel verschwunden ist,
zeigt erst der nächste Lauf mit Disc-Daten.

## Byte-Sichten (Endian-abhängiger Spielcode)

Unabhängig von den Disc-Daten liest der Spielcode an manchen Stellen einzelne
Bytes eines größeren Feldes. Auf der GameCube liegt das höchstwertige Byte
vorn, auf ARM/x86 hinten.

- **Unions** mit Byte-Struktur über einem u16/u32 (z. B. `room_id` =
  `stage_no << 8 | room_no`): Patch 0016/0018 dreht die Byte-Reihenfolge der
  Struktur auf little-endian um. Beide Sichten stimmen dann beim Lesen und
  Schreiben, für umgewandelte Disc-Daten wie für Spielvariablen. Gefunden mit
  einem Suchskript über alle Unions in `include/` und `src/` (20 Treffer,
  `gx.h` ist der GX-FIFO und bleibt, `cModelInfo::colorWord` siehe offene
  Punkte).
- **Lesen über Byte-Offsets** (`*((s8*) p + ofs + 3)`): einzeln per Patch
  (0017).
- **Wort-Zugriffe auf Byte-Strukturen** (`*(u32*) &farbe = 0xFF`): seit M4.5
  über `byte_views.json` (85 Stellen, siehe „M4.5“).
- **Schmale Sichten über Zeiger** (`((u16*) poly)[8]`): das Suchskript
  findet nur `&x`; die 21 Stellen mit Zeigervariablen sind von Hand
  durchgesehen (Patch 0041, siehe „Vierter Gerätelauf mit GX“).
- **Byte-Sichten in Disc-Strukturen** muss der Umwandler als Wort drehen
  (Patch 0016 legt sie für Host-Wörter aus): `ModelDataHead::x0` (gefunden
  vom Formatvergleich), `CRoomInfo::roomNo`, `ETS_DATA::no`, `SstList::no`,
  `EspGenPrm`, `cAnm::Frames` sind so umgesetzt.

## Adressen der Spielbibliotheken

**Entscheidung des Projektinhabers (13.10.2026): natürliche Lage + Prüfung.**
Das Spiel unterscheidet an manchen Stellen über Bit 31 einer Adresse eigene
Daten von Offsets und Listenverweisen; so speichern die Zeichenlisten
(`libgpu.cpp` `ClearOTagR`/`DrawOTag`) Verweise mit gelöschtem Bit 31. Die
Listen selbst sind feste Variablen (`MainOt`, `ActBtn`, `SceSys` …) und
liegen in den Bibliotheken. Auf Android liegen die Bibliotheken einer 32-bit-App
bei `0xC0000000` und höher (gemessen, siehe unten), unter i686-Linux bei
`0xF7…`, unter qemu-arm bei `0x40…` (dort stürzte `DrawOTag` ab).

- Der Linux-Vorlader `re4_host` (`host/loader.c`) belegt vor dem Laden jede
  freie Lücke unter `0x83000000` mit einer unzugänglichen Reservierung
  (`PROT_NONE`, `MAP_NORESERVE`); die Bibliotheken landen darüber, auch unter
  qemu-arm (`libre4_host.so` bei `0x83018000`). Den MEM1-Teil übernimmt
  `re4_mem1_init` (Umgebungsvariable `RE4_MEM1_PLACEHOLDER`).
- `re4_check_game_address` prüft `libre4_main` (vor dem Spielstart) und jedes
  Modul (nach `dlopen`) und bricht unterhalb von `0x80000000` mit Logmeldung
  ab. Auf Android muss der JNI-Start dieselbe Prüfung für `libre4_main`
  aufrufen.
- Prüfungen auf den MEM1-Bereich `0x80000000–0x82FFFFFF`, die feste
  Variablen treffen, bleiben Einzelfälle (bisher keiner).

## Adressbereich auf dem Retroid (gemessen 13.10.2026)

Retroid Pocket Flip 2, Android 13, Kernel 4.19 aarch64, `ro.zygote=zygote64_32`,
ABIs `arm64-v8a,armeabi-v7a,armeabi`. Die alte Port-App `org.re4.port` ist als
arm64 installiert. Gemessen wurde deshalb ein echter 32-bit-App-Prozess aus
dem 32-bit-Zygote: `am instrument --abi armeabi-v7a -e debug true
org.re4.port.test/androidx.test.runner.AndroidJUnitRunner` startet
`org.re4.port` als `app_process32` und lässt ihn auf den Debugger warten;
`run-as org.re4.port` liest `/proc/<pid>/maps`; danach `am force-stop
org.re4.port`. Alles unter der ADB-Sperre (`Retroid-Adb-Exclusive.ps1`), nur
eigene Pakete, Seriennummer `dbdcacc`.

| Probe | Abbildungen | Überlappung mit `0x80000000–0x83000000` | Freie Lücke um `0x80000000` | Bibliotheken |
|---|---|---|---|---|
| 1 (PID 18115) | 2150 | keine | `0x7532A000–0xC3B25000` (1255 MB) | `0xD1D25000–0xF07E2000` |
| 2 (PID 18233) | 2151 | keine | gleich | gleich |
| 3 (PID 18289) | 2150 | keine | gleich | gleich |

Weitere Belegung: ART-Hauptbereich `0x12C00000–0x32C00000`, Boot-Image um
`0x70D06000–0x7532A000`, Stack `0xFF3F4000`. Ergebnis: **MEM1 an der
Originaladresse ist in einer 32-bit-App frei**, und die Bibliotheken liegen
oberhalb von `0x80000000`. Die drei Proben stammen aus demselben Zygote-Start
(gleiche Zufallsverteilung); nach einem Geräteneustart verschiebt sich die
Lage, die Lücke von 1,2 GB lässt aber viel Spielraum. Ungeprüft: das
tatsächliche `mmap` in einer Port-App (M6/M11, sobald der NDK-Build läuft).
Der Platz sollte früh reserviert werden (beim Laden von `libre4_platform`).

## Offene Punkte für M5 und später

- **M5 abschließen (lokal):** `port/scripts/test-r100-core.ps1` mit den
  Disc-Daten; Ziel: Formatvergleich ohne Abweichung, 10 000 Bilder ohne
  Absturz, Abdeckung für r100 ohne „nein“. Abweichungen und Abstürze aus dem
  Bericht beheben. **Erreicht im dritten Lauf** (siehe oben); armhf mit
  Disc-Daten (`-Arm`, qemu) steht noch aus.
- **Null-Füllung durch C++11:** `whole_value_init.py` findet jede
  Konstruktion, die noch mit Nullen füllt (mit und ohne Platzierung, auch
  Member-Initialisierer `x()`). Nach Änderungen an `whole_prepare.py` oder
  den Ersatz-Headern erneut laufen lassen; erwartet sind die vier Stellen aus
  dem Kopf des Skripts.
- **Formate ohne App-Leser im Vergleich** (Liste unter „Formatvergleich mit
  der App“): bei Auffälligkeiten im Lauf weitere Leser der App heranziehen
  (`room_blocks.cpp` BLK, `room_shadows.cpp` SHD, `sound_footsteps.cpp` FSE,
  `sound_ambient.cpp` ESE, `room_area_effects.cpp` EAR, `room_items.cpp` ITA/ITM).
- **SQRTF** weicht in den letzten Bits ab (siehe M4.5); falls Kollision oder
  Kamera sichtbar anders reagieren, die `frsqrte`-Tabelle nachbilden.
- **Von der CPU gebaute Display-Listen** (`EspInit`: `*(u16*) d = 4`) sind
  Host-Reihenfolge, Display-Listen von der Disc big-endian: der GX-Ersatz (M6)
  muss beide kennen.
- **Feste Variablen als DVD-Ziel:** `etc/emleon00.esl` wird in einen Puffer in
  `libre4_main` gelesen (`0xF7…`); DVD und Umwandler kommen damit zurecht,
  MEM1-Bereichsprüfungen darauf wären ein Einzelfall.
- **IPL-Schrift:** `OSInitFont` braucht die Schrift aus dem IPL-ROM (nicht auf
  der Disc), nur für die Fehlerbildschirme. Platzhalter liefert 0.
- **Platzhalter (209 Funktionen):** GX 97 (M6), AX/AXFX/AXART/MIX/SYN/SEQ/AI/ADX
  77 (M8), mwPly 12 (M9), PPC-Leistungszähler 8, SN-PC-Host-I/O 7,
  OS 8 (IPL-Schrift, Stoppuhren, `OSResetSystem`). CARD und PAD sind ersetzt.
- **Weitere Leerlauf-Schleifen** auf Interrupt-Zähler wie in `main.cpp` können
  auftauchen; dort ebenfalls `re4_wait_interrupt()` einsetzen.
- **Hintergrund-Task (iTask):** läuft ohne Unterbrechung bis zum Ende. Für
  echtes Zeitscheiben-Verhalten `re4_interrupt_point()` in lange Schleifen
  (z. B. den Yz2-Kern über seinen Checkpoint) einbauen.
- **Android-Einbindung:** erledigt im Cloud-Teil von K2 (siehe dort);
  `_ctype_` über die Prelude-Regel umbenannt. Gerätetest steht aus.

## Bekannte Risiken für die Laufzeit (M4/M5)

- Mit `-w` sind Warnungen ausgeblendet. Bei Abstürzen lohnt ein Lauf ohne `-w`
  für die betroffene Datei.
- Funktionsaliase mit abweichender Signatur (z. B. `EvtFlgOnStatus` →
  `Event::FlgOnStatus`) funktionieren auf ARM32 und i686, weil `this` dort wie
  ein erstes Argument übergeben wird.
- Die Task-Threads bekommen je 1 MB Host-Stack (das Original: 6–12 KB im
  Spiel-Heap), der Boot-Thread 8 MB.
- Lesen von Disc ist sofort fertig: Code, der auf einen späteren Abschluss
  wartet oder den Fortschritt misst, verhält sich anders als mit Laufwerk.
- Der Formatumwandler kennt Formate nur so weit, wie der Spielcode sie beim
  Booten las. Ein falsch gedrehtes Feld fällt erst auf, wenn es gelesen wird;
  bei Abstürzen zuerst die `formats:`-Logzeilen der betroffenen Datei prüfen.
- Unions mit Byte-Sichten in Dateien, die das Suchskript nicht erfasst
  (benannte Strukturtypen als Union-Mitglieder), können fehlen; `EspGenPrm`
  wurde von Hand gefunden.
- Klassen mit virtuellen Funktionen: MWCC legt den vptr hinter die Felder
  der Klasse, in der er entsteht (`cEsp`: 0xF4), die Compiler des Ports an
  den Anfang. Direkte Feldzugriffe stimmen trotzdem; Code, der über feste
  Abstände oder Füllfelder zu anderen Feldern rechnet, nicht (Patch 0040).
  `sizeof` bleibt gleich, wenn der vptr in die Klasse fällt.
- PowerPC-Befehle als Funktionen (`re4_sdk_intrinsics.h`): MWCC schreibt bei
  `__rlwimi` in den ersten Operanden, die C-Fassung gibt das Ergebnis nur
  zurück. Ein Aufruf als eigene Anweisung (Ergebnis nicht zugewiesen) wirkt im
  Port nicht. In `src/` gab es genau eine solche Stelle (Patch 0042).
