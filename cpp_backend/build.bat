@echo off
REM Build script for MahJong RLAI C++ Backend
REM Requirements: MinGW-w64 (g++) with C++20 support

if not exist build mkdir build

echo Compiling...
g++ -std=c++20 -O2 -Wall -Wextra -Isrc src/main.cpp -o build/server.exe -lws2_32

if %ERRORLEVEL% EQU 0 (
    echo.
    echo Build successful! Binary: build/server.exe
    echo.
    echo Usage:
    echo   build\server.exe -H 0.0.0.0 -P 9999 -A 0 -ob
    echo.
    echo Options:
    echo   -A N    Number of AI players (0-4)
    echo   -P N    Port (default: 9999)
    echo   -ob     Allow observers
    echo   -f      Fast mode (skip delays)
    echo   -d      Debug logging
) else (
    echo.
    echo Build failed!
)
