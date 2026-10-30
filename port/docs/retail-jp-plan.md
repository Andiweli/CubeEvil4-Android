# Retail-Discs (Japan und Korea) – Analyse und Plan

Stand: 29.10.2026 abends, lokal (Branch `golden-master`). Auftrag des Projektinhabers: die japanische Verkaufsfassung
von Resident Evil 4 („biohazard4“, GameCube) und eine inoffizielle koreanische Übersetzung, die auf ihr beruht,
als weitere Importversionen von CubeEvil4. Geprüft mit `E:\Development\Android\RE4-DiscdataJP\RE4-Japan-Disc1/2.iso`
und `RE4-Korea-Disc1/2.iso`, ausgepackt nach `~/re4-jp/disc` und `~/re4-kr/disc` (WSL; Disc-Daten nie ins
Repository). Werkzeuge: `retail_compare.py`, `retail_code.py`, `disc_release.py`; Hilfsskripte nur in der Sitzung.

## Erkennung

| Disc | Kennung | Name im Kopf | FST bei | FST-Größe | `main.dol` |
| --- | --- | --- | --- | --- | --- |
| Debug 2004-11-25 | `G4BE08` | resident evil 4 disc1/2 | 0x28FE00 | 0x69A5 / 0x6400 | 2 554 432 Byte |
| US-Verkauf | `G4BE08` | resident evil 4 disc1/2 | 0x231A00 | 0x5B9C / 0x55DF | 2 168 544 Byte |
| EU-Verkauf | `G4BP08` | resident evil 4 game disc 1/2 | 0x236500 | 0x7668 / 0x71F6 | 2 187 552 Byte |
| **Japan** | **`G4BJ08`** | biohazard4 game disc 1/2 | **0x235300** | 0x5E18 / 0x57F8 | 2 182 944 Byte |
| **Korea (Fan)** | `G4BJ08` | wie Japan | wie Japan | wie Japan | wie Japan, **6 Byte anders** |

Japan und Korea haben denselben Disc-Kopf (`boot.bin`, `bi2.bin`, `apploader.img` bytegleich). Unterscheiden lassen
sie sich an `main.dol` (SHA-1, die App prüft ohnehin die Code-Stücke gegen eine Referenzliste) oder an den
Schriften (`font/*_j.fnt`). `disc_release.py` und `runtime/dvd.cpp` kennen `G4BJ08` noch nicht („unknown“), die
App lehnt die Discs heute ab.

## Korea gegenüber Japan

- 302 von 1 158 Dateien geändert, alle mit gleicher Größe (die Übersetzung schreibt in den vorhandenen Platz):
  Ereignisse (`evd/*.evd`, 128), Raumarchive (Texte, 127), `ss/jpn` (17), Schriften (`font/*_j.fnt`, 12),
  `etc/core.das`, `etc/memcard.das`, `etc/movie.das`, `op/op01..03.das`, 8 Gegnerarchive `em/em10..17.drs`,
  `fst.bin` (80/89 Byte: andere Lage geänderter Dateien).
- **Code:** nur zwei Befehle in `main.dol`: bei 0x80017b7c und 0x8002ccd4 wird aus `lbz rX, 9(r9)`
  (`pSys->eff_country`) ein `li rX, 1`. Die Funktionen entsprechen `emWepPlHeadLost` (`game/emwep.cpp`) und
  `obj16PlHeadLost` (`game/obj16.cpp`) der Debug-Fassung: der Tod durch Enthauptung (Kettensäge u. ä.), den die
  japanische Fassung für `eff_country == 0` (Japan) zensiert. Die Übersetzung schaltet dort auf das US-Verhalten.
  Der Port führt den Debug-Code aus; für Korea müssten diese zwei Stellen dasselbe tun (kleiner Patch).

## Japan gegenüber Debug und EU

- **Aufbau wie EU/US:** dieselben drei umsortierten Archive (`em/pl0a.drs` Krauser, `ss/cmn/ss_wep40.dat` Adas
  Gewehr, `ss/cmn/map_obj4g.dat`), dieselben angehängten Einträge (`pl11/15.drs`, `ss_oc*.dat`, `r20f`, `r330` …),
  alle 128/124 REL-Module mit denselben Modulnummern, keine Debug-Dateien (`moji8.tpl`, `roomInfo.dat`,
  `Tools.rel`, `t_*.rel`). Gegen EU nur 233 Dateien anders (vor allem Texte: Ereignisse, Räume, Filme, `core.das`).
- **Sprache:** nur Japanisch: `ss/Jpn` (97 Dateien statt 81 in `ss/eng`), `etc/Jpn`, Schriften `*_j.fnt`
  (`dvd_j`, `system_j` dazu), Filmuntertitel `etc/movie.das` wie EU. Kein `ss/Esy`.
- **Der Debug-Code kennt Japan:** `pSys->language` 0 und `pSys->eff_country` 0 sind Japan (61 Stellen mit
  `eff_country`: Speicherkarte, DVD-Fehler, Zensur, Titel). Es fehlt also kein Grundgerüst, sondern – wie bei EU/US –
  die Abweichungen der Verkaufsfassung.

## Plan (Schritte J0–J7)

| Schritt | Inhalt |
| --- | --- |
| J0 | ✅ Branch `retail-jp` von `golden-master` (Entscheidung des Projektinhabers, 29.10.2026: `retail-us` bleibt, wie es ist); die Weiche `re4_disc_retail()` wie auf `retail-us` neu angelegt (EU, US oder Japan) |
| J1 | Erkennung `G4BJ08` (Japan, Korea per `main.dol`) in `disc_release.py`, `runtime/dvd.cpp`, App-Import (Referenzlisten `references-jp.tsv`, `references-kr.tsv`, RVZ) |
| J2 | Prüfstrecke mit JP-Daten (`whole_r100_check.sh`, mit `--room-info` der Debug-Disc), Formatvergleich, Absturzstellen |
| J3 | Sprache: `language`/`eff_country` 0, `ss/Jpn`, japanische Schriften und Texte (Kern-Texte, Raumtexte, Ereignis- und Filmuntertitel), Ingame-Menü ohne Sprachwahl |
| J4 | Code-Abweichungen der JP-Fassung an den Stellen der EU/US-Patches (0120–0145) mit `retail_code.py` prüfen und die Weichen auf Japan erweitern |
| J5 | Korea: die zwei `PlHeadLost`-Stellen wie die Übersetzung, Schriften/Texte aus den Daten |
| J6 | Speicherkarte (`bh4` mit `G4BJ08`), Titel, Schwierigkeit (JP-Titelmenü lesen) |
| J7 | Raum-Rundgang 10b mit JP-Daten, Gerätetest auf dem Retroid (beide Fassungen) |

Nicht geprüft bis hier: der JP-Code selbst an den Patch-Stellen, ein Spiellauf mit JP-Daten (der Kern lehnt die Discs
heute ab), Gerät.


## Umsetzung auf `retail-jp` (29./30.10.2026, lokal)

**J1 Erkennung.** Kern: `RE4_DISC_RETAIL_JP` (4) für `G4BJ08`, Korea als Spielart davon (`koreanOf`: die zwei
Befehle in `sys/main.dol`), `re4_disc_retail_jp()`, `re4_disc_retail_kr()`, `re4_disc_retail()` (EU, US oder
Japan: kein Debug-Menü, umsortierte Archive in Debug-Reihenfolge); das Spiel sieht wie bei EU die Kennung `G4BE08`
(Speicherkarte). Werkzeuge: `disc_release.py` (`retail-jp`), `whole_disc_folder.py`, `whole_r100_check.sh`. App:
`GameCubeDisc` erkennt `G4BJ08`, Korea an `main.dol` (Freigaben `jp`/`kr`, eigene Kataloge `references-jp/kr.tsv`,
`files-…`, `rooms-…`, `blocks-…`, aus den Abbildern mit `make_disc_catalogs.py`), `DiscInstaller`, Startseite,
kein Debug-Menü mit allen Verkaufs-Discs. Geprüft: Laufzeittest `disc release` (Japan, Korea), Java-`DiscTests`
(24 Prüfungen) und der echte Import beider Abbildpaare mit dem App-Code (je 4 082 Prüfungen, `release=jp` / `kr`).

**J3 Sprache.** `re4_game_language_value()` gibt mit JP-Discs 0: Sprache, Land und `game_country` 0 (Japan); der
Debug-Code kennt Japan (Schriften `*_j.fnt`, Ordner `jpn`). Erster Lauf: Systemdatei „biohazard4 システムファイル“.

**J4 Patches.** Code der Verkaufsfassungen an den Patch-Stellen verglichen (EU-Funktion → JP-Funktion, Befehle
ohne Adressanteile):

| Patch | JP gleich EU? | jetzt |
| --- | --- | --- |
| 0120, 0126 Funkgerät, 0123 Kern-Texte (+15, gleiche MDT-Blöcke), 0124 Karte, 0125 Lebensanzeige, 0129 Händler-Markierung, 0133 r204, 0135 Kapitelende/Ergebnis | ja | `re4_disc_retail()` (wie auf `retail-us`) |
| 0128 r221, 0138 r20b, 0141 r203/r211/Karte, 0145 r214/r20e (Raummodule wortgleich), 0140 Schwierigkeitsklasse (JP `game_mode == 3`, EU `> 4`: gleich für 3/5/6), 0136 Filmuntertitel (JP-Film hat eine Untertitelspur, Kanal 0) | ja | EU oder JP |
| 0121 Sprachwahl, 0122, 0131 Stage 4 (JP wie Debug auf Disc 1), 0132 Ereignis-Untertitel (JP vergleicht mit Sprache 1 wie Debug), 0139 Titelmenü (JP ganz anders, keine Schwierigkeitsabfrage), 0142 EMPTY (JP ohne Sprachbilder) | nein | nur EU |

Auf Easy lädt der EU-Code Kartenobjekte aus `SS/Esy`, das die JP-Discs nicht haben; Japan bietet kein Easy an
(Titelmenü), die Klasse bleibt „über Easy“.

**J5 Korea.** Patch 0147: `emWepPlHeadLost`/`obj16PlHeadLost` gehen mit den koreanischen Discs den US-Weg.

**Vorfilm-Untertitel (30.10.2026, Nutzermeldung Korea).** Zu 0136 fehlte der japanische Zweig: bei Sprache 0 nimmt
`cSofdec` die Schrift aus `etc/movie.das` (Eintrag 5) als Schrift 2 und setzt danach die Stage-Schrift zurück; ohne
sie falsche Zeichen (Kana-Gemisch, sinnloses Hangul). Patch 0150, am PC mit JP-, KR- und EU-Discs geprüft.
