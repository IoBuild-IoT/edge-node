#!/usr/bin/env bash
# Pruebas en PC (sin placa): compila el firmware con stubs de Arduino y simula sensores y broker.
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build
[ -f build/ArduinoJson.h ] || curl -sSLf -o build/ArduinoJson.h \
  https://github.com/bblanchon/ArduinoJson/releases/download/v7.2.1/ArduinoJson-v7.2.1.h
cp ../firmware/iobuild_edge_node/secrets.example.h build/secrets.h
g++ -std=c++17 -Ibuild -Istubs test_edge_node.cpp -o build/test_edge_node
g++ -std=c++17 -Ibuild -Istubs test_local.cpp -o build/test_local
./build/test_edge_node | tail -1
./build/test_local | tail -1
