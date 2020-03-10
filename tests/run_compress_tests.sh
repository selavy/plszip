#!/usr/bin/env bash

# set -x

die() {
    echo ""
    echo ""
    echo "====================================================================="
    echo "!!! ERRORS !!!"
    echo "====================================================================="
	echo $1
    echo "====================================================================="
    echo ""
	exit 1
}

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

COMPRESS=${BUILD}/compress

ninja -C ${BUILD} || die "Failed to compile"

run_test() {
    PROG=$1
    BASENAME=$2
    INPUT=${TESTDIR}/${BASENAME}.txt
    OUTPUT=${BUILD}/${BASENAME}.txt.gz
    GUNZIP_OUTPUT=${BUILD}/${BASENAME}.txt
    echo -n "$BASENAME..."
    $PROG $INPUT $OUTPUT > /dev/null 2> /dev/null || die "Failed to compress $INPUT with $PROG"
    gunzip $OUTPUT || die "Failed to inflate $INPUT"
    diff $INPUT $GUNZIP_OUTPUT || die "Diff failed"
    echo " Passed!"
    rm -f $GUNZIP_OUTPUT
}

if [[ $# -gt 2 ]];
then
    run_test $COMPRESS $3
    exit 0;
fi;

TESTS=(test1 test2 test3 test4 test5 test6 test7 test8 test9 test10 test11
test12 test13 test14 test15 test16 test17 test18 test19 test20 test21 test22
test23 test24 test25 test26 blank)
for input in ${TESTS[@]};
do
    run_test $COMPRESS $input
done

echo "Passed all tests!"
exit 0
