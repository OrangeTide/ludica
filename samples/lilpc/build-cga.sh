#!/bin/sh
# Build CGA test .COM files for lilpc demodisk
# Made by a machine. PUBLIC DOMAIN (CC0-1.0)
set -e

DIR=$(dirname "$0")
OUT="$DIR/cga-out"
mkdir -p "$OUT"

# compile packers
cc -O2 -I"$DIR/../../src/thirdparty" "$DIR/cgapack.c" -o "$OUT/cgapack" -lm
cc -O2 -I"$DIR/../../src/thirdparty" "$DIR/cga256pack.c" -o "$OUT/cga256pack" -lm

# convert each image to mode 4, mode 6, and CGA-256 raw dumps
for img in lake cabin; do
    "$OUT/cgapack" --mode4 "$DIR/${img}.png" "$OUT/${img}_m4.raw"
    "$OUT/cgapack" --mode6 "$DIR/${img}.png" "$OUT/${img}_m6.raw"
    "$OUT/cga256pack" "$DIR/${img}.png" "$OUT/${img}_256.raw"
done

# assemble .COM files
for img in lake cabin; do
    nasm -f bin -DMODE=4 "-DIMGFILE=\"$OUT/${img}_m4.raw\"" \
         -o "$OUT/${img}4.com" "$DIR/viewer.asm"
    nasm -f bin -DMODE=6 "-DIMGFILE=\"$OUT/${img}_m6.raw\"" \
         -o "$OUT/${img}6.com" "$DIR/viewer.asm"
    nasm -f bin "-DIMGFILE=\"$OUT/${img}_256.raw\"" \
         -o "$OUT/${img}256.com" "$DIR/viewer256.asm"
done

echo "Built: $OUT/lake4.com lake6.com lake256.com cabin4.com cabin6.com cabin256.com"

# copy onto the demo disk if mtools is available
DISK="$DIR/disk/demodisk.img"
if command -v mcopy >/dev/null 2>&1 && [ -f "$DISK" ]; then
    for img in lake cabin; do
        for com in "${img}4.com" "${img}6.com" "${img}256.com"; do
            mcopy -i "$DISK" -o "$OUT/$com" "::$com"
        done
    done
    echo "Copied .COM files onto $DISK"
fi
