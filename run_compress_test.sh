#!/usr/bin/env bash

die() {
	echo $1
	exit 1
}

if [[ $# -eq 0 ]];
then
    echo "Usage $0 [FILE]...";
    exit 0;
fi;

make debug || (echo "Failed to build." && exit 1)
rm -f *.txt.gz *.check

for file in "$@";
do
    filename=$(basename $(realpath $file))
    output1=${filename}.gz
    output2=${filename}.check
    ./build/debug/compress $file $output1 || die "error: compress failed";
    gzip -d -k -c $output1 > $output2 || die "error: gzip failed";
    diff $output2 $file || die "!!! FAILED !!!";
    echo "Passed."
done
