# 🍓 Raspberry Pi Controller

This directory contains the C++ source code for the edge controller of the apple sorting system.

## Files
- `apple_sorter2.cpp`: The main C++ application. Handles GPIO pin manipulation, threading, camera control, and HTTP requests.
- `CMakeLists.txt`: Build configuration file.
- `bin/applesortv2`: Pre-compiled ARM64 ELF binary for convenience.

## Dependencies
- `pigpio`: For hardware-timed PWM (controlling servos without jitter) and GPIO reads.
- `libcurl`: For making POST requests to the cloud server.
- `pthread`: For multithreaded non-blocking servo actuation.
- System utilities: `rpicam-jpeg`, `fswebcam`.

For build instructions, see the main [SOFTWARE_SETUP.md](../docs/SOFTWARE_SETUP.md).
