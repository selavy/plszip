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

run() {
    PROG=$1
    BENCH=$2
    shift 2
    echo "-- Benchmark: $PROG $BENCH"
    /usr/bin/time -f "\t%E real,\t%U user,\t%S sys" $PROG $BENCH $@ > /dev/null || die "$PROG failed"
}

run_bench() {
    PROG=$1
    BENCH=$2
    CORRECT=$3
    shift 3
    echo "Remaining arguments: $@"
    OUTPUT=${BENCH}.out
    run $PROG $BENCH $OUTPUT
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
    echo ""
    echo ""
    gzip -k $fname
    cat $fname.gz > /dev/null
    for prog in ${PROGS[@]};
    do
        run_bench $BUILD/$prog $fname.gz $fname
    done;
    rm $fname
    run gunzip $fname.gz -k -d
done;

cleanup
