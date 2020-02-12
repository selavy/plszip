#!/usr/bin/env bash

OUT=big.txt

rm -f $OUT
i=300;
while [[ i -gt 0 ]];
do
    cat berlioz.txt >> $OUT;
    cat dracula.txt >> $OUT;
    i=$(expr $i - 1);
done

gzip -k big.txt
