#!/usr/bin/env bash

if [[ $# -gt 0 ]];
then
    FILE=$1
else
    echo "Usage $0 [FILE]"
    exit 0
fi;

FILENAME=$(basename $(realpath $FILE))
OUTPUT=${FILENAME}.gz

make debug || (echo "Failed to build." && exit 1)
./build/debug/compress $FILE $OUTPUT
gzip -d -k -c $OUTPUT

if [[ $? -ne 0 ]];
then
    echo "!!! FAILED !!!"
    exit 1
fi;
