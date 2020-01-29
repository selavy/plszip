#!/bin/bash

# set -x

die() {
	echo $1
	exit 1
}

#BUILD=../build/debug/

if [[ $# -gt 0 ]];
then
    BUILD=$1
else
    BUILD=../build/debug
fi;

if [[ $# -gt 1 ]];
then
    TESTDIR=$2
else
    TESTDIR=.
fi;

PLZIP=${BUILD}/plzip

ninja -C ${BUILD} || die "Failed to compile"

run_test() {
    input=$1
    TEST=${TESTDIR}/${input}
    ORIG=${TEST}.txt
    COMPRESSED=${BUILD}/${input}.txt.gz
    OUTPUT=${BUILD}/${input}.output
	echo -n "$TEST... "
    gzip -c $ORIG > $COMPRESSED || die "Failed to compress with gzip"
    if [[ $2 -ne 0 ]];
    then
        $PLZIP $COMPRESSED $OUTPUT || die "Failed to decompress with plzip"
    else
        $PLZIP $COMPRESSED $OUTPUT > /dev/null 2> /dev/null || die "Failed to compress with plzip"
    fi;
    diff $ORIG $OUTPUT > /dev/null && echo "Passed." || die "Diff failed"
    rm -f $COMPRESSED
    rm -f $OUTPUT
}

if [[ $# -gt 2 ]];
then
    run_test $3 1
    exit 0;
fi;

TESTS=(test1 test2 test3 test4 test5 test6 test7 test8 test9 test10 test11
test12 test13 test14 test15 test16 test17 test18 test19 test20 test21 test22
test23 test24 test25 test26)
for input in ${TESTS[@]};
do
    run_test $input
done

echo "Passed all tests!"
exit 0
