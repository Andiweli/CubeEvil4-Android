# Die Paketdatei `.c4tex` (CubeEvil4-Texturpaket)

Für die Akten: wie die Datei aufgebaut ist, die das PC-Programm „CubeEvil4 Texture Packer“
(`port/tools/texpack/`) schreibt und die App liest. Plan: `port/docs/textur-lader-plan.md`. Die genaue Form steht im
Code in `port/tools/texpack/c4tex.h`; beide Seiten nutzen diese eine Datei.

## Aufbau

```
Kopf (128 Byte) | Info-Text | Bilddaten (jedes Bild auf 16 Byte ausgerichtet) | Inhaltsverzeichnis
```

Alle Zahlen sind little-endian (wie auf Handys und PCs). Die Bilddaten liegen so, wie OpenGL ES und Vulkan sie
erwarten: ETC2-/EAC-Blöcke von 4 × 4 Bildpunkten, Zeile für Zeile, jeder 64-Bit-Block mit dem höchsten Byte zuerst.

### Kopf

| Byte | Inhalt |
|---|---|
| 0–7 | Kennung `C4TEXPAK` |
| 8 | Version (1) |
| 12 | Größe des Kopfs (128) |
| 16 | Anzahl der Bilder |
| 20 | Größe eines Eintrags im Inhaltsverzeichnis (48) |
| 24 | wo das Inhaltsverzeichnis beginnt |
| 32 | wo die Bilddaten beginnen |
| 40 | Größe der ganzen Datei (eine kürzere Datei ist unvollständig kopiert) |
| 48 | Stufe (512 oder 1024) |
| 52 | Prüfsumme (CRC-32) des Inhaltsverzeichnisses |
| 56, 60 | wo der Info-Text liegt, wie lang er ist |
| 64 | Prüfsumme der Bytes 0–63 |

### Info-Text

Zeilen `Name=Wert` in UTF-8, für die Anzeige im Menü: `name`, `tool`, `created`, `size`, `source` (Name des
Paket-Ordners, ohne Pfad), `choices` (gewählte Varianten), `images`.

### Inhaltsverzeichnis

Ein Eintrag (48 Byte) je Ersatzbild, sortiert, damit die App mit einer Binärsuche oder einer Tabelle sofort findet:

| Byte | Inhalt |
|---|---|
| 0 | Kennung des Bildes (Dolphins XXH64 über die erste Bildstufe in ganzen Kacheln) |
| 8 | Kennung des benutzten Teils der Farbtabelle (bei Bildern mit Farbtabelle, sonst 0) |
| 16, 18 | Breite und Höhe des Originalbildes im Spiel (aus dem Dolphin-Namen) |
| 20 | GX-Bildformat (0–14) |
| 21 | Merkmale: `_m` im Namen, mit Farbtabelle, Farbtabelle „beliebig“ (`$`), eigene Verkleinerungsstufen (`_arb`) |
| 22 | Art: 1 ETC2 ohne Durchsichtigkeit, 2 ETC2 mit „ganz oder gar nicht durchsichtig“, 3 ETC2 mit EAC-Durchsichtigkeit |
| 23 | Anzahl der Verkleinerungsstufen (größte zuerst) |
| 24, 26 | Breite und Höhe des Ersatzbildes |
| 28 | Prüfsumme der Bilddaten |
| 32 | wo die Bilddaten liegen |
| 40 | wie viele Bytes |

Ein Bild kann zweimal da sein, mit und ohne `_m`: Dolphin sucht für ein Bild, das das Spiel mit Verkleinerungsstufen
benutzt, den Namen mit `_m`. Im HD Project gibt es 216 solche Paare, manche mit verschiedenem Inhalt.

## Was ein Leser prüft

- Kennung, Version, Prüfsumme des Kopfs, Dateigröße.
- Prüfsumme des Inhaltsverzeichnisses; dass es in der Datei liegt.
- Jeden Eintrag (`c4tex::EntryValid`): Art bekannt, Größen und Stufen sinnvoll, Datenmenge passt genau zu Größe, Art
  und Stufen, Daten liegen ganz zwischen Datenbeginn und Inhaltsverzeichnis. Ein Eintrag, der das nicht erfüllt, wird
  nicht benutzt.
- Auf Wunsch die Prüfsumme der Bilddaten (das PC-Programm mit `-check` immer).

Eine falsche oder beschädigte Datei führt zu einer Meldung, nie zu einem Absturz.
