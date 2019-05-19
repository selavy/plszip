#!/bin/bash

ninja -C ../build/debug/ compress || exit 1
../build/debug/bin/compress $@
