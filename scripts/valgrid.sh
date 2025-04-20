#!/bin/bash

cd ..
valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./build/dhl_client 

valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --log-file=valgrind.log ./build/dhl_client