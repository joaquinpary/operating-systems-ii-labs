#!/bin/bash
rm -rf ../build/ external/
mkdir -p ../build/
cd ../build/ || exit 1
cmake .. -DBUILD_TARGET=client
make
cmake .. -DBUILD_TARGET=server
make
cmake .. -DRUN_COVERAGE=1
make
cd ..
