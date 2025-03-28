#!/bin/bash

# set the script to exit on any command that returns a non-zero status
set -e

# First version:
# * Current working directory must be depot root.
# * Run as `./hacks/chk.sh`
# TODO:
# * [ ] Automatically set proper working directory (depot root, parent of script directory)
# * [ ] Add cmd-line option `--clean` to do clean build
# * [ ] Add cmd-line option `--translations` to generate updated translation files
# * [ ] Detect errors and stop script when detected
# * [ ] Maybe set bash to exit on errors ... `set -e`?

# update CMake for both rp2040 builds and rp2350 builds
cmake -S . -B build_rp2040 -DPICO_SDK_FETCH_FROM_GIT=TRUE                            || exit 11
cmake -S . -B build_rp2350 -DPICO_SDK_FETCH_FROM_GIT=TRUE -DBP_PICO_PLATFORM=rp2350  || exit 12

# optionally, do a clean build each time
# How to make build fail on all warnings?
cmake --build ./build_rp2040 --parallel --target clean || exit 31
cmake --build ./build_rp2350 --parallel --target clean || exit 32

# optionally, update the translation files
# TODO: Run `./src/translation/json2h.py` to generate updated translation files
python ./src/translation/json2h.py || exit 51

# build everything
cmake --build ./build_rp2040 --parallel --target all || exit 71
cmake --build ./build_rp2350 --parallel --target all || exit 72

