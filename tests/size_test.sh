#!/usr/bin/env bash

BUILD=../build/debug
TESTDIR=.
TEST=test3.txt

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
fi;

if [[ $# -gt 1 ]];
then
    TESTDIR=$2
fi;

COMPRESS=${BUILD}/compress
INPUT=${TESTDIR}/${TEST}

compress_fast() {
    $COMPRESS --fast --level=$1 $INPUT ${TEST}.fast.$1 > /dev/null 2> /dev/null || die "compress failed"
}
compress_slow() {
    $COMPRESS --slow --level=$1 $INPUT ${TEST}.slow.$1 > /dev/null 2> /dev/null || die "compress failed"
}


ninja -C ${BUILD} || die "Failed to compile"

i=1
while [ $i -lt 10 ]
do
    echo "Running compression level $i..."
    gzip -k -c -$i $INPUT > ${TEST}.gzip.$i || die "gzip failed"
    compress_fast $i
    compress_slow $i
    i=$(( $i + 1 ))
done

compress_fast 10
compress_slow 10

ls -la ${TEST}.* > results
