#!/bin/bash
set -e
echo "INICIANDO BUILD.SH"
echo "PWD: $(pwd)"
rm -rf ./build/ external/
echo "Estoy en: $(pwd)"
mkdir -p ./build/
echo "Estoy en: $(pwd)"
cd ./build/ || exit 1
echo "Target: $1"
case "$1" in
    client)
        cmake .. -DBUILD_TARGET=client
        make
        ;;
    server)
        cmake .. -DBUILD_TARGET=server
        make
        ;;
    *)
    echo "Invalid TARGET: $1"
    exit 1
    ;;
esac
