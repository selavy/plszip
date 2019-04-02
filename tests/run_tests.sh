#!/bin/bash

PLZIP=../build/src/plzip

TESTS=(test1 test2 test3)

# TEST=test1
for TEST in ${TESTS[@]};
do
    ORIG=${TEST}.txt
    COMPRESSED=${TEST}.txt.gz
    OUTPUT=${TEST}.output
    echo -n "$TEST... " && gzip -c $ORIG > $COMPRESSED && $PLZIP $COMPRESSED $OUTPUT > /dev/null && diff $ORIG $OUTPUT && echo "Passed." || (echo "Failed" && exit 1)
    rm -f $COMPRESSED
    rm -f $OUTPUT
done
