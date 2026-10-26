# Lokalen Stand mit GitHub abgleichen und die App bauen (Windows-PC)

Wunsch des Projektinhabers (22.10.2026): Jede Sitzung schickt nach Änderungen, die in die App
gehören, diese Befehle mit. Ordner: Repository `E:\Development\Android\RE4-Fixed`, Branch
`claude/adoring-pasteur-h5s2fw` (Arbeitsbranch für 0.92.0 seit 26.10.2026, aufgesetzt auf `gui-optimisations`; vorher `beta-optimisations`).

## 1. Stand von GitHub holen (PowerShell)

```powershell
cd E:\Development\Android\RE4-Fixed
git status
git pull --no-rebase origin claude/adoring-pasteur-h5s2fw
```

- `git status` zeigt geänderte Dateien (`modified: …`)? Dann arbeitet gerade ein anderer Chat in
  diesem Ordner. Ihn bitten, seine Änderungen zu committen, mit `git pull --no-rebase origin
  claude/adoring-pasteur-h5s2fw` zu mischen und zu pushen; danach `git pull` hier noch einmal.
  Unversionierte Dateien (`Untracked files: AGENTS.md, core/, …/src/core/assets/`) stören nicht.
- Steht beim `git pull` **CONFLICT** in der Ausgabe: nicht weitermachen, die Ausgabe an den Chat
  schicken.

## 2. Den Spielkern bauen (PowerShell, läuft in WSL)

```powershell
wsl --exec bash -lc "if [ ! -d ~/re4-build/.git ]; then git -c safe.directory='*' clone -q /mnt/e/Development/Android/RE4-Fixed ~/re4-build; fi && cd ~/re4-build && git -c safe.directory='*' fetch -q /mnt/e/Development/Android/RE4-Fixed HEAD && git checkout -q --detach FETCH_HEAD && git log -1 --format='Stand: %h %s' && port/tools/whole_build_android.sh --into /mnt/e/Development/Android/RE4-Fixed/port/android-decomp/app --abi all"
```

- Baut den Kern aus dem Stand, den Schritt 1 geholt hat, in einem eigenen WSL-Ordner
  (`~/re4-build`): der Raum-Rundgang (`~/re4-kern`) wird nicht gestört. Das erste Mal 10–60
  Minuten, danach nur das Geänderte (ohne Änderung am Kern wenige Minuten).
- Die erste Zeile `Stand: …` nennt den Commit; er muss der sein, den der Chat genannt hat.
- Gut ist: zweimal `0 failed build steps` und zweimal `Kern-Bibliotheken …`. Steht dort
  **FEHLER**, die Ausgabe an den Chat schicken.
- Immer ausführen, auch wenn sich nur die App geändert hat: ohne Änderung ist er schnell fertig.

## 3. App bauen und auf den Retroid bringen

In Android Studio wie gewohnt bauen (Variante „core“) und auf den Retroid installieren (Run mit dem
verbundenen Retroid). Laufen gerade Gerätetests eines Chats über ADB, erst danach installieren
(`port/docs/adb-coordination.md`).

## Wechsel auf `claude/adoring-pasteur-h5s2fw` (einmal)

Wer noch auf `beta-optimisations`, `gui-optimisations`, `claude/re4-start-controls` oder `performance` steht (alle sind in
`claude/adoring-pasteur-h5s2fw` enthalten):

```powershell
cd E:\Development\Android\RE4-Fixed
git status
git fetch origin claude/adoring-pasteur-h5s2fw
git switch claude/adoring-pasteur-h5s2fw
git pull --no-rebase origin claude/adoring-pasteur-h5s2fw
```
