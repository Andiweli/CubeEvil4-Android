#!/bin/bash
# realtime-shadows study: GX dumps of other sessions' runs drawn without / with the shadow prototype / its mask.
#   dirs.sh NAME DUMP...   -> ~/pshd/dirs/NAME/{off,on,mask}/NN.png, NN-cmp.png, diff.txt, on.txt (the prototype's log)
R=~/re4-shd/port/build/app-host-gl/gx_chunk_render
name=$1; shift
d=~/pshd/dirs/$name
rm -rf $d; mkdir -p $d/off $d/on $d/mask
env RE4_GX_API=gles RE4_GX_SCALE=${SCALE:-2} RE4_GX_TIME=1 $R $d/off "$@" > $d/off.txt 2>&1
env RE4_SHD_PROTO=1 RE4_SHD_LOG=1 RE4_SHD_TIME=1 RE4_GX_TIME=1 RE4_GX_API=gles RE4_GX_SCALE=${SCALE:-2} $R $d/on "$@" > $d/on.txt 2>&1
env RE4_SHD_PROTO=1 RE4_SHD_SHOW=1 RE4_GX_API=gles RE4_GX_SCALE=${SCALE:-2} $R $d/mask "$@" > $d/mask.txt 2>&1
i=0
for ppm in $(ls $d/off/*.ppm | sort); do
  b=$(basename $ppm .ppm); nn=$(printf %02d $i)
  for m in off on mask; do [ -f $d/$m/$b.ppm ] && convert $d/$m/$b.ppm $d/$m/$nn.png && rm -f $d/$m/$b.ppm; done
  ae=$(compare -metric AE -fuzz 2% $d/off/$nn.png $d/on/$nn.png null: 2>&1)
  echo "$name $nn $b $ae" >> $d/diff.txt
  convert $d/off/$nn.png $d/on/$nn.png $d/mask/$nn.png +append -resize 50% $d/$nn-cmp.png
  i=$((i + 1))
done
echo "$name: $i pictures"
