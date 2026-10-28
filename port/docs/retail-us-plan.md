# Retail-Discs (US) – Analyse und Umsetzung

Stand: 28.10.2026. Auftrag: CubeEvil4 spielt auch mit den US-Verkaufs-Discs von Resident Evil 4 (GameCube),
parallel zur Debug-Disc (Referenz) und zu den EU-Verkaufs-Discs (`G4BP08`, `retail-eu-plan.md`). Branch `retail-us`.
Geprüft mit `Resident Evil 4 (USA) (Disc 1).iso`/`(Disc 2).iso` und den RVZ-Dateien daneben
(`E:\Development\Android\RE4-DiscdataUS`), lokal unter WSL (i686). Werkzeuge: `retail_compare.py`,
`retail_messages.py`, `retail_code.py`, `make_disc_catalogs.py` (alle in `port/`), Berichte außerhalb des
Repositorys (`~/re4-us` in WSL).

## Erkennung: US-Disc oder Debug-Disc?

Beide tragen die Kennung `G4BE08`. Sie unterscheiden sich am Disc-Kopf eindeutig an der Lage der Dateitabelle (FST):

| Disc | Kennung | Name im Kopf | FST bei | FST-Größe | `main.dol` |
| --- | --- | --- | --- | --- | --- |
| Debug 2004-11-25 | `G4BE08` | resident evil 4 disc1/2 | 0x28FE00 | 0x69A5 / 0x6400 | 2 554 432 Byte |
| US-Verkauf | `G4BE08` | resident evil 4 disc1/2 | **0x231A00** | 0x5B9C / 0x55DF | 2 168 544 Byte |
| EU-Verkauf | `G4BP08` | resident evil 4 game disc 1/2 | 0x236500 | 0x7668 / 0x71F6 | 2 187 552 Byte |

Die Regel stand schon seit M13 in `port/tools/disc_release.py` und `runtime/dvd.cpp` (`G4BE08` mit FST bei
0x28FE00 = Debug, sonst US); neu ist nur, dass `retail-us` jetzt angenommen wird. Die App prüft zusätzlich die
Größe von `main.dol` und die SHA-1 aller Code-Stücke gegen `references-us.tsv`. Revision beider US-Discs: 0.

## Unterschiede zu Debug und EU

**US ist fast überall wie EU aufgebaut**, nur ohne die vier weiteren Sprachen:

| Bereich | US-Discs | wie |
| --- | --- | --- |
| Debug-Dateien (`moji8.tpl`, `debug/roomInfo.dat`, `config.txt`, `Tools.rel`, `t_*.rel`) | fehlen | EU |
| Kern-Archiv `etc/core.das` | 0x269300 Byte, passt in den festen Platz der Debug-Fassung (kein Umzug im Log) | Debug (Patch 0117 greift nur bei Überlänge) |
| umsortierte Archive (`pl0a.drs`, `ss_wep40.dat`, `ss_term.dat`, `map_obj4g.dat`) | dieselbe Ordnung | EU (`ss_wep40.dat` bytegleich) |
| Zusatzteile am Ende (`pl11/15.drs`, `ss_ocNNN.dat`, r20f, r30c, r30e, r330) | dieselben | EU |
| REL-Module | 128 / 124, alle mit denselben Modulnummern | EU und Debug |
| Sprachen | nur `ss/Eng` (kein Ger/Fra/Esp/Ita/Esy, kein `etc/movie.das`, keine `movie_j.fnt`) | Debug |
| `ss/eng/title.dat`, `ss/eng/option.dat` | bytegleich mit Debug (kein „LANGUAGE SELECT“) | Debug |
| Stage 4 (`rel/st4_0.rel`, `st4/*.das`, `adaend_*.sfd`) | auf beiden Discs | Debug |
| Kern-Texte (`core.das` Tabelle 6) | 168 Texte, dieselben 15 neuen ab 0x56 | EU (0123) |
| Raumtexte r221 | Tabelle wie Debug (Text 5 noch da), `st2_3.rel` fragt dieselben Nummern | Debug |
| r101, Schlüssel zur Tür nach r102 | `st1_1.rel` fragt Text 0xB wie der Prototyp (EU: 0x14) | Debug (Patch 0127 gilt für alle Discs) |
| Textnummern der übrigen Raummodule | wie EU (`retail_messages.py calls`: nur `st2_4`, `st3_2`, `st4_0` anders als Debug, dieselben Stellen wie bei EU) | EU |
| übrige Text-Tabellen | wie EU: nur überarbeitete Texte oder Texte am Ende | EU |
| Kartenlayout `ss_map.dat` | Baustein 253 an der EU-Stelle; Händler-Markierung 0xB | EU |
| Lebensanzeige (Namensschilder 0x43–0x45) | Code wie EU (`LifeMeter::roomInit` bei 0x800bf4b8, jede Anweisung gleich) | EU (0125) |
| Funkgerät `ss_term.dat` (Teile 2, 5, 9) | bytegleich mit EU | EU (0120, 0126) |
| Kapitelende `ss/eng/chapNN.dat` | EU-Vorlagen (vierstellige Gegnerzahl); `ChapterEnd::move` wie EU | EU (0135) |
| Spielende `ss/eng/result.dat` | Layout wie Debug; `GameResult::move` blendet Baustein 0 immer aus, keine 999-Grenze | gemischt (0135) |
| Ereignis-Untertitel | `Event::MesSet` vergleicht mit Sprache 1 (wie Debug); US spielt Sprache 1 | Debug |
| Händler-Markierungen (Sscrn.rel) | `markMerchantDisp`, `getMerchantMarkNo` wie EU | EU (0129, 0133) |
| Filme | dieselben wie Debug (`Movie/*.sfd`), keine Untertitel-Tabelle | Debug |

## Was eingebaut wurde

Weichen im Kern (`runtime/dvd.cpp`): `re4_disc_retail()` = EU oder US, `re4_disc_retail_us()`; `re4_disc_retail_eu()`
bleibt für das, was nur die EU-Discs haben.

| Patch / Stelle | mit US-Discs |
| --- | --- |
| 0117 Kern-Archiv (bei US nicht nötig), 0118 ohne `moji8.tpl`, 0119 ohne `roomInfo.dat`, 0122 einsprachige MDT | gelten ohnehin (prüfen die Daten selbst) |
| 0120, 0126 Funkgerät, 0123 Kern-Texte +15, 0124 Karte, 0125 Lebensanzeige, 0129 achte Händler-Markierung, 0133 r204 | jetzt `re4_disc_retail()` |
| 0135 Ergebnis-Bildschirme | Kapitelende und Baustein 0 des Spielendes `re4_disc_retail()`, 999-Grenze des Spielendes nur EU |
| 0121 Sprache, 0128 r221, 0131 Stage 4, 0132 Ereignis-Untertitel, 0136 Film-Untertitel | nur EU (US wie Debug) |
| 0127 r101 (Fehler des Prototyps) | gilt für alle Discs; mit US zeigt CubeEvil4 den richtigen Text, das US-Original den falschen |
| `formats.cpp` Umsortieren `pl0a.drs`/`ss_wep40.dat` | EU und US |
| Debug-Menü (Kern `re4_core_debug_menu`, App `CoreActivity`) | aus mit EU und US |
| Sprachwahl im Ingame-Menü | nur EU |
| Spielstände | US und Debug haben dieselbe Kennung `G4BE08`: Spielstände und `.gci` gehen ohne Umweg zwischen ihnen |

App: `GameCubeDisc` nimmt US-Discs im Kernmodus an (`release=us` in `ready.properties`), Prüfkataloge
`references-us.tsv` (134 Code-Stücke), `rooms-us.tsv` (142 Räume), `blocks-us.tsv` (gleich den Debug-Blöcken),
`files-us.tsv` (1 975 Dateien), erzeugt mit `make_disc_catalogs.py --iso … --data … --suffix=-us`. Gemischte Discs
(z. B. US-Disc 1 mit Debug-Disc 2) erkennt `DiscInstaller.sameRelease` wie bei EU. Prüfstrecke:
`whole_disc_folder.py` und `whole_r100_check.sh` nehmen `retail-us` an (Rundgang-Ergebnisse in
`rundgang-<Ziel>-us.json`, verglichen mit den Debug-Discs).

## Prüfungen

Siehe `whole-program-status.md`, Abschnitt „Retail-Discs (US)“.
