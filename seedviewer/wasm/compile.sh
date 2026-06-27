#!/bin/bash
set -e

CUBIOMES_DIR="${CUBIOMES_DIR:-../../cubiomes}"
OUT_DIR="${OUT_DIR:-./out}"

mkdir -p "$OUT_DIR"

CUBIOMES_SRC="$CUBIOMES_DIR/biomenoise.c $CUBIOMES_DIR/biomes.c $CUBIOMES_DIR/finders.c $CUBIOMES_DIR/generator.c $CUBIOMES_DIR/layers.c $CUBIOMES_DIR/noise.c $CUBIOMES_DIR/util.c"

emcc -O3 \
  -I"$CUBIOMES_DIR" \
  $CUBIOMES_SRC \
  cubiomes_wrapper.c \
  -o "$OUT_DIR/cubiomes.js" \
  -s WASM=1 \
  -s MODULARIZE=1 \
  -s EXPORT_ES6=1 \
  -s EXPORTED_FUNCTIONS='["_cubiomes_init","_cubiomes_sample_tile","_cubiomes_gen_tile","_malloc","_free"]' \
  -s EXPORTED_RUNTIME_METHODS='["HEAPU8","HEAP32"]' \
  -s ALLOW_MEMORY_GROWTH=1 \
  -s INITIAL_MEMORY=64MB \
  -s NO_FILESYSTEM=1 \
  -s NO_EXIT_RUNTIME=1 \
  -fno-exceptions

echo "WASM build complete: $OUT_DIR/cubiomes.js + $OUT_DIR/cubiomes.wasm"
ls -la "$OUT_DIR/"
