#!/bin/bash
rm -rf ../build/ external/
mkdir -p ../build/
cd ../build/ || exit 1
cmake .. -DBUILD_TARGET=client
make
cmake .. -DBUILD_TARGET=server
make
cmake .. -DRUN_TEST=1 -DTEST_TARGET=client
make
cmake .. -DRUN_TEST=1 -DTEST_TARGET=server
make
cmake .. -DRUN_COVERAGE=1 -DTEST_TARGET=common
make
cd ..
