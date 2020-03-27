#!/usr/bin/env bash

# for i in *.txt; do gzip -k $i; done

die() {
    echo $1
    exit 1
}

DO_LZ4=0
DO_GZIP_FAST=0
DO_GZIP_SLOW=0
DO_GZIP_FAST_1=0
DO_GZIP_FAST_2=0
DO_GZIP_FAST_3=0

rm -rf *.txt.gz
rm -rf *.txt.gz.fast
rm -rf *.txt.gz.slow
rm -rf *.txt.gz.out
rm -rf *.txt.lz4

rm -rf *.txt.gz.fast.1
rm -rf *.txt.gz.fast.2
rm -rf *.txt.gz.fast.3

for i in *.txt;
do
    # BASENAME=$(basename $i)
    FILENAME=$(echo $i | sed 's/.txt//')
    TARNAME=${FILENAME}.txt.gz.out
    echo "file = $i tar = $TARNAME"

    ../build/debug/compress --slow --level=9 $i $TARNAME > /dev/null 2> /dev/null || die "failed to compress $i"

    gzip -k -9 $i || die "gzip failed on $i"

    if [[ $DO_LZ4 -ne 0 ]];
    then
        lz4 $i || die "lz4 failed on $i"
    fi;

    if [[ $DO_GZIP_FAST -ne 0 ]];
    then
        gzip -1 -k -c $i > $i.gz.fast || die "gzip fast failed on $i"
    fi;

    if [[ $DO_GZIP_FAST_1 -ne 0 ]];
    then
        gzip -1 -k -c $i > $i.gz.fast.1 || die "gzip fast failed on $i"
    fi;
    if [[ $DO_GZIP_FAST_2 -ne 0 ]];
    then
        gzip -2 -k -c $i > $i.gz.fast.2 || die "gzip fast failed on $i"
    fi;
    if [[ $DO_GZIP_FAST_3 -ne 0 ]];
    then
        gzip -3 -k -c $i > $i.gz.fast.3 || die "gzip fast failed on $i"
    fi;

    if [[ $DO_GZIP_SLOW -ne 0 ]];
    then
        gzip -9 -k -c $i > $i.gz.slow || die "gzip slow failed on $i"
    fi;

done;
