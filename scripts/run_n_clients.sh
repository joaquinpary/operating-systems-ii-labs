#!/bin/bash

for i in $(seq -f "%04g" 1 10); do
    ./build/dhl_client --config "$i" &
done
wait