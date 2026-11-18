#!/bin/bash
# realtime-shadows study: the release-candidate session's turning pictures (debug discs, ~/prc/shots/rROOM-c19d/p100)
# drawn without and with the shadow prototype; per room ~/pshd/batch/rROOM/{off,on}/NN.png, NN-cmp.png and a line
# "rROOM NN changed-pixels" in ~/pshd/batch/rROOM/diff.txt. Extra env (RE4_SHD_*) passes through.
#   batch.sh ROOM...      (JOBS=6 in parallel)
R=~/re4-shd/port/build/app-host-gl/gx_chunk_render
out=${OUT:-$HOME/pshd/batch}
one() {
  room=$1
  src=~/prc/shots/r$room-c19d/p100
  [ -d "$src/gx" ] || src=~/prc/shots/r$room/p100
  [ -d "$src/gx" ] || { echo "r$room: no dumps"; return; }
  freeAt=$(grep -a -m1 "run: player free (no event" "$src/log.txt" | sed -E 's/.* at frame ([0-9]+).*/\1/')
  [ -n "$freeAt" ] || { echo "r$room: never free"; return; }
  files=""
  for f in $src/gx/gx-*.bin; do
    n=$(basename $f .bin); n=$((10#${n#gx-}))
    [ $n -gt $freeAt ] && [ $n -le $((freeAt + 400)) ] && files="$files $f"
  done
  d=$out/r$room
  rm -rf $d; mkdir -p $d/off $d/on $d/mask
  # the room's first foot shadow light (lit_survey: "t1(ax,ay)")
  foot=$(grep "^r$room.das" ~/pshd/lit_all.txt | grep -o "t1([0-9]*,[0-9]*)" | head -1 | sed -E 's/^t1\(([0-9]+),([0-9]+)\)$/\1,\2/')
  [ -n "$foot" ] && export RE4_SHD_FOOT=$foot || unset RE4_SHD_FOOT
  echo "r$room foot ${foot:-none}" > $d/foot.txt
  env RE4_GX_API=gles RE4_GX_SCALE=${SCALE:-2} $R $d/off $files > $d/off.txt 2>&1
  env RE4_SHD_PROTO=1 RE4_SHD_LOG=1 RE4_GX_API=gles RE4_GX_SCALE=${SCALE:-2} $R $d/on $files > $d/on.txt 2>&1
  env RE4_SHD_PROTO=1 RE4_SHD_SHOW=1 RE4_GX_API=gles RE4_GX_SCALE=${SCALE:-2} $R $d/mask $files > $d/mask.txt 2>&1
  i=0
  for ppm in $(ls $d/off/*.ppm | sort); do
    b=$(basename $ppm .ppm); nn=$(printf %02d $i)
    convert $d/off/$b.ppm $d/off/$nn.png; convert $d/on/$b.ppm $d/on/$nn.png; convert $d/mask/$b.ppm $d/mask/$nn.png
    rm -f $d/off/$b.ppm $d/on/$b.ppm $d/mask/$b.ppm
    ae=$(compare -metric AE -fuzz 2% $d/off/$nn.png $d/on/$nn.png null: 2>&1)
    echo "r$room $nn $b $ae" >> $d/diff.txt
    convert $d/off/$nn.png $d/on/$nn.png $d/mask/$nn.png +append -resize 50% $d/$nn-cmp.png
    i=$((i + 1))
  done
  echo "r$room: $i pictures, free at $freeAt"
}
export -f one
export R out
printf "%s\n" "$@" | xargs -P ${JOBS:-6} -I{} bash -c 'one {}'
