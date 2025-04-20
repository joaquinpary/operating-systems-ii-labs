#!/bin/bash
set -e
echo "INICIANDO BUILD.SH"
echo "PWD: $(pwd)"
rm -rf ./build/ external/
echo "Estoy en: $(pwd)"
mkdir -p ./build/
echo "Estoy en: $(pwd)"
cd ./build/ || exit 1
#cmake .. -DBUILD_TARGET=client
#make
cmake .. -DBUILD_TARGET=server
make
#cmake .. -DRUN_TEST=1 -DTEST_TARGET=client
#make
#cmake .. -DRUN_TEST=1 -DTEST_TARGET=server
#make
#cmake .. -DRUN_COVERAGE=1 -DTEST_TARGET=common
#make
cd ..
