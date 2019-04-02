#!/bin/bash

PLZIP=../build/src/plzip

ORIG=test1.txt
COMPRESSED=test1.txt.gz
OUTPUT=${ORIG}.output
echo "Test #1" && gzip -c $ORIG > $COMPRESSED && $PLZIP $COMPRESSED $OUTPUT > /dev/null && diff $ORIG $OUTPUT && echo "Passed." || (echo "Failed" && exit 1)
rm -f $COMPRESSED
rm -f $OUTPUT
