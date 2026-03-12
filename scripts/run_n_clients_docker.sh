#!/bin/bash

# Get number of clients from environment variable, default to 10
NUM_CLIENTS=${NUM_CLIENTS:-10}

echo "Starting $NUM_CLIENTS client instances..."

# Launch N clients in background
for i in $(seq -f "%04g" 1 "$NUM_CLIENTS"); do
    echo "Starting client instance $i"
    /app/dhl_client --config "$i" &
    
    # Small delay to avoid overwhelming the server on startup
    sleep 0.1
done

echo "All $NUM_CLIENTS clients started. Waiting for processes..."

# Wait for all background processes
wait

echo "All clients finished."
