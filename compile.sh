#!/bin/bash

mkdir build 2>/dev/null
g++ src/*.cpp -o build/jpegreader -Wall -ljpeg -pthread
