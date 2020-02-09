#!/usr/bin/env bash

# set -x

BENCHS=.
BUILD=../build/release
PROGS=(plzip inflate)
TESTS=(berlioz.txt dracula.txt latin_verse.txt random.txt)

if [[ $# -gt 0 ]];
then
    BUILD=$1
fi;


if [[ $# -gt 1 ]];
then
    BENCHS=$2
fi;

cleanup() {
    rm -f *.out
    rm -f *.gz
}

die() {
    cleanup
	echo $1
	exit 1
}

run_bench() {
    PROG=$1
    BENCH=$2
    CORRECT=$3
    OUTPUT=${BENCH}.out
    echo ""
    echo ""
    echo "-------------------------------------------------"
    echo "| Benchmark: $PROG $BENCH"
    echo "-------------------------------------------------"
    time $PROG $BENCH $OUTPUT || die "$PROG failed"
    echo "-------------------------------------------------"
    diff $CORRECT $OUTPUT || die "$PROG failed to inflate correctly"
    rm $OUTPUT
}

echo "Build directory: $BUILD"
echo "Bench directory: $BENCHS"
echo "Programs: ${PROGS[@]}"

cleanup

ninja -C $BUILD || die "Failed to build!"


for fname in ${TESTS[@]};
do
    gzip -k $fname
    cat $fname.gz > /dev/null
    for prog in ${PROGS[@]};
    do
        run_bench $BUILD/$prog $fname.gz $fname
    done;
done;

cleanup
