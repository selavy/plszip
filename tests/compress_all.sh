#!/usr/bin/env bash

for i in *.txt; do gzip -k $i; done
