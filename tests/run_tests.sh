#!/bin/bash

PLZIP=../build/debug/bin/plzip

ninja -C ../build/debug/

TESTS=(test1 test2 test3 test4 test5 test6 test7 test8 test9 test10 test11
test12 test13 test14 test15 test16 test17 test18)
for TEST in ${TESTS[@]};
do
    ORIG=${TEST}.txt
    COMPRESSED=${TEST}.txt.gz
    OUTPUT=${TEST}.output
    # echo -n "$TEST... " && gzip -c $ORIG > $COMPRESSED && $PLZIP $COMPRESSED $OUTPUT > /dev/null && diff $ORIG $OUTPUT && echo "Passed." || (echo "Failed" && exit 1)
    echo -n "$TEST... " && gzip -c $ORIG > $COMPRESSED && $PLZIP $COMPRESSED $OUTPUT > /dev/null 2> /dev/null && diff $ORIG $OUTPUT && echo "Passed." || (echo "Failed" && exit 1)
    rm -f $COMPRESSED
    rm -f $OUTPUT
done
