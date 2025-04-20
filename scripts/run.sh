#!/bin/bash
echo "rEstoy en: $(pwd)"
./build/dhl_server

# for ((i=0; i<10; i++)) 
# do
#    ./build/dhl_client
# done
wait