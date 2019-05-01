#!/bin/sh

FILE=$1
GZIPPED=${FILE}.gz
echo "Running for $FILE"
./compress.sh $1 || (echo "Failed to compress" && exit 1)
ninja -C ../build/debug/ || exit 1
../build/debug/bin/plzip $GZIPPED output
