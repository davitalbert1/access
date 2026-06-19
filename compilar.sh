#!/bin/bash
set -e

echo "=== Limpando cache antigo ==="
rm -f CMakeCache.txt
rm -rf CMakeFiles build

echo "=== Configurando CMake ==="
mkdir -p build
cd build
cmake -G "MSYS Makefiles" ..

echo "=== Compilando ==="
make -j$(nproc)

echo "=== Copiando executavel ==="
cp lmstudio_chat_client.exe ..

echo "=== Pronto! Execute: ./lmstudio_chat_client.exe ==="
