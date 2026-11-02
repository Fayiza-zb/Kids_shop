#!/bin/sh
# Build for Linux / macOS (Render uses the Dockerfile instead)
gcc -O2 -Wall server.c -o server && echo "Built ./server"
