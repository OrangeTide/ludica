#!/bin/sh
# Build CGA test .COM files for lilpc demodisk
# Made by a machine. PUBLIC DOMAIN (CC0-1.0)
set -e

DIR=$(dirname "$0")
OUT="$DIR/cga-out"
mkdir -p "$OUT"

# compile cgapack
cc -O2 -I"$DIR/../../src/thirdparty" "$DIR/cgapack.c" -o "$OUT/cgapack" -lm

# convert each image to mode 4 and mode 6 raw dumps
for img in lake cabin; do
    "$OUT/cgapack" --mode4 "$DIR/${img}.png" "$OUT/${img}_m4.raw"
    "$OUT/cgapack" --mode6 "$DIR/${img}.png" "$OUT/${img}_m6.raw"
done

# assemble .COM files
for img in lake cabin; do
    nasm -f bin -DMODE=4 "-DIMGFILE=\"$OUT/${img}_m4.raw\"" \
         -o "$OUT/${img}4.com" "$DIR/viewer.asm"
    nasm -f bin -DMODE=6 "-DIMGFILE=\"$OUT/${img}_m6.raw\"" \
         -o "$OUT/${img}6.com" "$DIR/viewer.asm"
done

echo "Built: $OUT/lake4.com lake6.com cabin4.com cabin6.com"
