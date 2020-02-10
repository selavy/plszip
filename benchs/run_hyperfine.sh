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

cleanup
ninja -C $BUILD || die "Failed to build"
for fname in ${TESTS[@]};
do
    cmds=()
    gzip -k $fname
    for prog in ${PROGS[@]};
    do
        cmds+=("$BUILD/$prog $fname.gz $fname.gz.out")
    done;
    cmds+=("gzip -k -d -c $fname.gz > /dev/null")
    cat $fname.gz > /dev/null
    cat $fname.gz > /dev/null
    cat $fname.gz > /dev/null
    cat $fname.gz > /dev/null
    # hyperfine --warmup 30 "${cmds[@]}"
    hyperfine --prepare 'sync; echo 3 | sudo tee /proc/sys/vm/drop_caches' "${cmds[@]}"
done;
cleanup
