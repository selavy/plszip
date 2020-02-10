#!/usr/bin/env bash

OUT=big.txt

i=200;
while [[ i -gt 0 ]];
do
    cat berlioz.txt >> $OUT;
    cat dracula.txt >> $OUT;
    i=$(expr $i - 1);
done
