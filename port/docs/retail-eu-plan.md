# Meilenstein 13 „Retail-Discs (EU)“ – Plan

Stand: 26.10.2026. Als **M13** in die Meilensteinliste von `CLAUDE.md` aufgenommen (Entscheidung des
Projektinhabers, 26.10.2026).

**Fortschritt:** R0 ✅ und R1 ✅ (26.10.2026, Branch `retail-eu`): die r100-Prüfung läuft mit den
unveränderten EU-Discs ohne Absturz durch, die Debug-Discs bleiben „ALLES OK“. Einzelheiten und offene
Punkte: `whole-program-status.md`, Abschnitt „Retail-Discs (EU), Schritte R0 und R1“. Befunde 2–4 der
Tabelle unten sind behoben (Patches 0117–0119), Befund 12 (`config.txt`) ist ohne Folge.
R2 ✅ (26.10.2026): Befunde 5, 8 und 9 behoben (Patch 0120; Umsortierung von `pl0a.drs` und `ss_wep40.dat`
beim Laden), Befund 10 ohne Folge; der hängende Speicherbildschirm lag an der Prüfstrecke (Speichern in r120),
nicht an den EU-Daten. Neu für R3: die Untertitel-Leiste des Funkspruchs fehlt („IDSystem work full“). Neu für
R4/R8: Spielstände tragen die Disc-Kennung (G4BP08/G4BE08), Austausch zwischen den Fassungen ungeprüft.

Ziel: CubeEvil4 spielt das Spiel auch mit den europäischen Verkaufs-Discs (`G4BP08`, Disc 1 und 2),
nicht nur mit der Debug-Disc (`G4BE08`, 2004-11-25). Der Spielcode bleibt der der Debug-Version
(`src/`); angepasst werden nur Import, Plattformschicht und kleine geprüfte Patches.

## Ausgangslage (Analyse vom 26.10.2026)

Geprüft mit `Resident Evil 4 (Europe) (Disc 1).iso` und `(Disc 2).iso`, lokal unter WSL (i686).
Werkzeuge und Berichte der Analyse lagen außerhalb des Repositorys (`~/re4-eu` in WSL).

**Was passt**

| Prüfung | Ergebnis |
| --- | --- |
| Raum-Archive `St*/r*.das` | alle gleich aufgebaut; r20f, r30c, r30e, r330 haben nur Teile am Ende zusätzlich |
| Programm-Module (REL) | alle 127 / 124 Spielmodule mit derselben Modulnummer an derselben Stelle |
| Formatvergleich `re4_format_compare --data` | 407 Dateien, 0 Fehler |
| Filme (`Movie/*.sfd`) | laufen über die CRI-Middleware ohne Fehler |
| Disc-Schicht des Kerns (`runtime/dvd.cpp`) | liest die EU-Discs ohne Änderung (Disc-Wechsel nach Disc-Nummer) |
| Sprachen | die EU-Disc hat echte deutsche, französische, spanische und italienische Texte (`ss/Ger`, `ss/Fra`, `ss/Esp`, `ss/Ita`); der Debug-Code kennt diese Ordner bereits |

**Was fehlt (in der Reihenfolge, in der es im Testlauf auftrat)**

| # | Befund | Folge | Beleg |
| --- | --- | --- | --- |
| 1 | App-Import verlangt `G4BE08` und die SHA-1-Prüfsummen der Debug-Version (`GameCubeDisc`, `ReferenceCatalog`, `DiscInstaller.ready`) | EU-Discs werden abgelehnt | Code |
| 2 | `etc/core.das` der EU ist 0x23AC40 Byte groß, der feste Platz im Debug-Code 0x234000 (`src/game/read.cpp` `CORE_DATA_MAX`) | Überlauf in den Optionsbereich, „CORE_DATA IS TOO LARGE“ | Testlauf |
| 3 | `etc/moji8.tpl` (Debug-Schrift) fehlt | Absturz in `EprintfFlush` bei jedem Bild | Testlauf |
| 4 | `Debug/roomInfo.dat` fehlt; im Debug-Code führt jedes „Neues Spiel“ über das Raumauswahl-Menü (`titleExit`, `cRoomJmp`) | Absturz beim Spielstart | Testlauf |
| 5 | `ss/eng/ss_term.dat`: andere Reihenfolge der Teile (Debug 11 Teile, EU 10), `tel00ModelInit` und Nachbarn greifen auf Plätze 13/14 | Absturz beim ersten Funkspruch in r100 | Testlauf |
| 6 | Menü-Baustein-Speicher des `IDSystem` zu klein für die EU-Menüs | „IDSystem::set() work full“, Menüteile fehlen | Testlauf |
| 7 | Menütexte (MDT) in `ss/<Sprache>/*.dat` sind bei der EU einsprachig aufgebaut, bei Debug mit sechs Sprachplätzen | falsche oder fehlende Texte, mögliche Abstürze in Menüs | Testlauf (Umwandler-Meldungen) und Dateivergleich |
| 8 | `ss/cmn/ss_wep40.dat` (Adas Gewehr im Inventar): ein Teil eingefügt | falsches Modell/Bewegung, möglicher Absturz | Archivvergleich |
| 9 | `em/pl0a.drs` (Krauser, Mercenaries): Modelle und Texturen anders sortiert ab Platz 11 | Grafikfehler oder Absturz mit Krauser | Archivvergleich |
| 10 | `ss/cmn/map_obj4g.dat` (Inselkarte): leere Plätze gefüllt | vermutlich harmlos, prüfen | Archivvergleich |
| 11 | `Evd/r204s00.evd`, `s204s00.evd`: ein Lichtdatensatz fehlt | Szene in r204 prüfen | Archivvergleich |
| 12 | Debug-Werkzeuge (`Rel/Tools.rel`, `Rel/t_*.rel`), `Debug/config.txt` fehlen | Debug-Menü-Werkzeuge nicht nutzbar; Grundeinstellungen statt config.txt | Dateivergleich |

Nicht geprüft: alles nach dem ersten Funkspruch in r100 (der Rest des Spiels), die US-Verkaufs-Disc,
die japanische Disc.

## Grundsatzentscheidungen (Projektinhaber, 26.10.2026)

1. **Parallel:** Die Debug-Disc bleibt Referenz und voll unterstützt; die EU-Discs kommen als zweite
   Quelle dazu. Beide Discs müssen aus derselben Version stammen (kein Mischen von Debug-Disc 1 mit
   EU-Disc 2).
2. **Debug-Menü mit EU-Discs ausblenden** (SELECT 1 Sekunde öffnet dann nichts). Später soll ein
   Cheat-Menü im Ingame-Menü der App kommen; das ist ein eigenes Vorhaben, nicht Teil dieses
   Meilensteins.
3. **Alle Sprachen der EU-Discs sollen spielbar sein** (Deutsch, Französisch, Spanisch, Italienisch,
   Englisch); das gehört fest zum Meilenstein, reicht aber in Schritt R7.
4. **Aufnahme als M13** in `CLAUDE.md`.

## Schritte

Aufwand in Arbeitssitzungen (eine Sitzung = ein Chat mit Bauen und Testen). „Lokal“ heißt
Windows-PC mit WSL und Disc-Daten; die Cloud hat keine Disc-Daten.

| Schritt | Inhalt | Wo | Aufwand | Fertig, wenn |
| --- | --- | --- | --- | --- |
| R0 Prüfstrecke | `whole_disc_folder.py` und `whole_r100_check.sh` nehmen `G4BP08` an; Archivvergleich Debug/EU als Werkzeug in `port/tools/` (Teile-Reihenfolge, Event-Namen, REL-Nummern); Laufzeit erkennt die Version aus `sys/boot.bin` (`re4_disc_is_retail()`) | lokal | 1 | Prüfstrecke läuft mit EU-Daten bis zum ersten Absturz, Archivvergleich reproduziert die Tabelle oben |
| R1 Starten | Patches: Kern-Archiv bei Überlänge an einen größeren Platz (Befund 2); Debug-Texte ohne `moji8.tpl` abschalten statt abstürzen (3); „Neues Spiel“ ohne `roomInfo.dat` direkt in r100 wie die Verkaufsversion (4); Grundwerte ohne `config.txt` prüfen | lokal | 1–2 | r100-Prüfung mit unveränderten EU-Daten erreicht die Hauptschleife; Debug-Disc-Prüfung unverändert grün |
| R2 Archiv-Plätze | Funkspruch (`ss_term.dat`, 5), Adas Gewehr (`ss_wep40.dat`, 8), Krauser (`pl0a.drs`, 9), Inselkarte (10): bei EU-Daten die richtigen Plätze nehmen (Patch mit Versionsweiche oder Umsortieren beim Laden im Umwandler) | lokal | 1–2 | Funkspruch in r100 läuft durch, Leon wird frei; Inventar mit Adas Gewehr und Mercenaries mit Krauser ohne Fehler (GX-Bilder) |
| R3 Menüs und Texte | einsprachige MDT der EU (7) lesen; `IDSystem`-Speicher vergrößern (6); Titel, Optionen, Inventar, Karte, Dateien, Händler, Rätsel per GX-Bild mit der Debug-Version vergleichen | lokal | 2–3 | alle Untermenüs ohne Fehlermeldung, Texte vollständig (Bildvergleich) |
| R4 App-Import | `G4BP08` annehmen; eigener Prüfkatalog für die EU-Discs (Prüfsummen gegen beschädigte Abbilder, wie `rooms.tsv`); Version in `ready.properties`; Mischen der Versionen ablehnen; Sprachordner mit importieren | Cloud (Java) + Retroid | 1 | Import beider EU-Discs auf dem Retroid; Debug-Import unverändert |
| R5 Raum-Rundgang EU | `--rooms all` mit EU-Daten, Ergebnis gegen den Rundgang mit Debug-Daten; neue Abstürze beheben (u. a. r204, Befund 11) | lokal | 2–4 (offen) | jeder Raum mit EU-Daten so weit wie mit Debug-Daten |
| R6 Debug-Menü | mit EU-Discs ausblenden (Entscheidung 2) | lokal | 0,5 | SELECT-Halten mit EU-Daten öffnet nichts und führt nicht zum Absturz |
| R7 Sprachen | alle Sprachen der EU-Discs (Entscheidung 3): Spielsprache aus der App (`pSys->language`), Schriften, Speicherdaten, Untertitel der Filme und Ereignisse prüfen | lokal + Retroid | 2 | jede Sprache spielbar von Titel bis r101 mit vollständigen Texten |
| R8 Gerät und Freigabe | ARM32 und ARM64 auf dem Retroid mit EU-Discs, Release-Text | Retroid | 1 | Spielstart, Speichern/Laden und ein Kapitel mit EU-Discs auf dem Gerät |

Gesamt: etwa 12–16 Sitzungen mit R7 (R0 und R1 erledigt). R5 ist am schwersten abzuschätzen: Die
Analyse hat nur den Anfang des Spiels gespielt.

## Regeln für die Umsetzung

- `src/` und `include/` bleiben unverändert; alle Änderungen als Patch in `port/whole/patches/`
  (im Patch-Kopf „retail data“ statt „a bug of the prototype“) und Eintrag in
  `port/docs/whole-program-status.md`.
- Jede Weiche fragt die Version der eingelegten Disc ab; mit der Debug-Disc bleibt das Verhalten
  bytegleich zum heutigen Stand (Prüfstrecke mit Debug-Daten nach jedem Schritt).
- Keine Dateien der Debug-Disc (`roomInfo.dat`, `moji8.tpl`) in App oder Repository.
