#!/bin/bash
# Script para buildear servidor y cliente para testing

set -e  # Salir si hay error

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"

echo "=== Building Server and Client for Testing ==="

# Limpiar build anterior (opcional, comentar si quieres mantener)
# rm -rf "$BUILD_DIR"

# Crear directorio build si no existe
mkdir -p "$BUILD_DIR"

cd "$BUILD_DIR"

echo ""
echo "--- Building Server ---"
cmake "$PROJECT_ROOT" -DBUILD_TARGET=server
make -j$(nproc)

echo ""
echo "--- Building Client ---"
echo "--- NOT BUILDING THE CLIENT"
#cmake "$PROJECT_ROOT" -DBUILD_TARGET=client
#make -j$(nproc)

echo ""
echo "=== Build completed successfully! ==="
echo "Server: $BUILD_DIR/dhl_server"
#echo "Client: $BUILD_DIR/dhl_client"

