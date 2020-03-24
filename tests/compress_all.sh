#!/usr/bin/env bash

# for i in *.txt; do gzip -k $i; done

die() {
    echo $0
    exit 1
}

rm -rf *.txt.gz
rm -rf *.txt.gz.mine

for i in *.txt;
do
    # BASENAME=$(basename $i)
    FILENAME=$(echo $i | sed 's/.txt//')
    TARNAME=${FILENAME}.txt.gz.mine
    echo "file = $i tar = $TARNAME"

    ../build/debug/compress $i $TARNAME > /dev/null 2> /dev/null || die "failed to compress $i"
    gzip -k $i || die "gzip failed on $i"
done;
