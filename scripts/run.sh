#!/bin/bash
case "$1" in
    client)
        # for i in $(seq 0 1999); do # (seq 1 2000)
        #     ./build/dhl_client "$i" &
        # done

        ./build/dhl_client 0 &
        ./build/dhl_client 1 &
        ./build/dhl_client 5 &
        ./build/dhl_client 7 &

        # ./build/dhl_client 0
        wait
        ;;
    server)
        ./build/dhl_server
        ;;
    *)
    echo "Invalid TARGET: $1"
    exit 1
    ;;
esac
