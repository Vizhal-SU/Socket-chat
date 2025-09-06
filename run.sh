#!/bin/bash
set -e

# Step 1: configure and build
if [ ! -d build ]; then
    mkdir build
fi
cd build
cmake ..
make -j
cd ..

# # Step 2: open server and clients in separate xterm windows
# xterm -hold -e ./build/server &
# sleep 1
# xterm -hold -e ./build/client &
# xterm -hold -e ./build/client &
