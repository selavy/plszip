#!/bin/bash

ninja -C ../build/debug/ plzip || exit 1
../build/debug/bin/plzip $@
