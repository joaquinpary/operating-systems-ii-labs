#!/bin/bash

NUM_CLIENTS=${NUM_CLIENTS:-10}
INTERVAL=${INTERVAL:-0.02}

echo "Starting $NUM_CLIENTS client instances..."

for i in $(seq -f "%04g" 1 "$NUM_CLIENTS"); do
    /app/dhl_client --config "$i" &
    
    sleep "$INTERVAL"
done

echo "All $NUM_CLIENTS clients started. Waiting for processes..."

wait

echo "All clients finished."
