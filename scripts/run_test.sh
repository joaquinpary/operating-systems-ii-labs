#!/bin/bash
# Script para ejecutar servidor y 10 clientes para testing

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
SERVER_BIN="$BUILD_DIR/dhl_server"
CLIENT_BIN="$BUILD_DIR/dhl_client"

# Verificar que los binarios existen
if [ ! -f "$SERVER_BIN" ]; then
    echo "Error: Server binary not found. Run ./scripts/build_test.sh first"
    exit 1
fi

if [ ! -f "$CLIENT_BIN" ]; then
    echo "Error: Client binary not found. Run ./scripts/build_test.sh first"
    exit 1
fi

# Verificar que la DB está corriendo
if ! docker ps | grep -q postgres; then
    echo "Warning: PostgreSQL container might not be running."
    echo "Start it with: docker-compose up -d db"
    read -p "Continue anyway? (y/n) " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        exit 1
    fi
fi

# Limpiar procesos anteriores si existen
pkill -f "$SERVER_BIN" 2>/dev/null || true

echo "=== Starting Server ==="
cd "$PROJECT_ROOT"
"$SERVER_BIN" &
SERVER_PID=$!

# Esperar un poco para que el servidor inicie
sleep 2

# Verificar que el servidor está corriendo
if ! kill -0 $SERVER_PID 2>/dev/null; then
    echo "Error: Server failed to start"
    exit 1
fi

echo "Server started with PID: $SERVER_PID"
echo ""

# Función para limpiar al salir
cleanup() {
    echo ""
    echo "=== Cleaning up ==="
    kill $SERVER_PID 2>/dev/null || true
    wait $SERVER_PID 2>/dev/null || true
    echo "Server stopped"
}

trap cleanup EXIT INT TERM

echo "=== Starting 10 Clients ==="
CLIENT_PIDS=()

# Ejecutar 10 clientes
for i in $(seq -f "%04g" 1 10); do
    echo "Starting client_$i..."
    "$CLIENT_BIN" --config "$i" > /tmp/client_$i.log 2>&1 &
    CLIENT_PIDS+=($!)
    sleep 0.2  # Pequeño delay entre clientes para evitar race conditions
done

echo ""
echo "All clients started. Waiting for them to complete..."
echo "Press Ctrl+C to stop server and clients"

# Esperar a que todos los clientes terminen
for pid in "${CLIENT_PIDS[@]}"; do
    wait $pid 2>/dev/null || true
done

echo ""
echo "All clients finished"

