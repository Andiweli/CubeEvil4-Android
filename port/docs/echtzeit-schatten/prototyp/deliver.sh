#!/bin/bash
# realtime-shadows study: the comparison pictures for the owner, E:\Development\Android\builds\realtime-shadows
# (left the game as today, right the prototype; problem pictures with the shadow mask as a third panel)
O=/mnt/e/Development/Android/builds/realtime-shadows
mkdir -p $O
rm -f $O/*.png
lab() {  # lab IN LABEL OUT: the picture with a label bar on top
  convert "$1" -gravity North -background "#202020" -fill white -font DejaVu-Sans -pointsize 26 -splice 0x44 \
    -annotate +0+8 "$2" "$3"
}
pair() {  # pair NAME DIR NN TITLE [mask]
  name=$1; d=$2; nn=$3; title=$4
  lab $d/off/$nn.png "Original (heute)" /tmp/shd-a.png
  lab $d/on/$nn.png "Mit Echtzeit-Schatten (Versuch)" /tmp/shd-b.png
  args="/tmp/shd-a.png /tmp/shd-b.png"
  if [ -n "$5" ]; then lab $d/mask/$nn.png "Schattenmaske (dunkel = Schatten)" /tmp/shd-c.png; args="$args /tmp/shd-c.png"; fi
  convert $args +append /tmp/shd-row.png
  convert /tmp/shd-row.png -gravity North -background black -fill "#ffd060" -font DejaVu-Sans -pointsize 30 \
    -splice 0x56 -annotate +0+10 "$title" "$O/$name.png"
  echo "$name.png"
}
pair 01-r10e-door ~/pshd/final/r10e 06 "r10e (Stollen): Leons Schatten fällt auf das Eisentor"
pair 02-r207-wall ~/pshd/final/r207 02 "r207 (Schloss): Leons Schatten an der Wand"
pair 03-r11b-table ~/pshd/final/r11b 03 "r11b (Hafen-Lager): Leons Schatten auf dem Tisch"
pair 04-r101-villager ~/pshd/dirs/r101foot2 05 "r101 (Dorfplatz): echter Schatten der Dorfbewohnerin statt rundem Fleck"
pair 05-r101-ganados ~/pshd/dirs/r101foot2 02 "r101 (Dorfplatz, Kampf): Schatten an den Füßen der Ganados" mask
pair 06-r403-ganado ~/pshd/final/r403 00 "r403 (Dorf): Ganado und Leon, Licht aus der Raum-Lichttabelle" mask
pair 07-problem-self-shadow-r30f ~/pshd/batch2/r30f 02 "Problem: Streifen auf den Figuren, wenn sie sich selbst beschatten (r30f)" mask
pair 08-problem-continue-screen ~/pshd/dirs/r101foot 06 "Problem: Schatten über dem Fortsetzen-Bildschirm (muss ausgeschlossen werden)" mask
pair 09-r100-forest ~/pshd/batch/r100 01 "r100 (Wald): Licht kommt von hinten oben, Schatten kaum zu sehen" mask
ls -la $O
