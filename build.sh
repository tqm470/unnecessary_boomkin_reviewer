#!/usr/bin/env bash
# Cross-compile the Windows exe with MinGW-w64 and run the core tests natively.
#   sudo apt install g++-mingw-w64-x86-64
#   ./build.sh            -> build/UnnecessaryBoomkinReviewer.exe
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build

CXX_WIN=${CXX_WIN:-x86_64-w64-mingw32-g++-posix}
WINDRES=${WINDRES:-x86_64-w64-mingw32-windres}
CORE="src/core/spells.cpp src/core/wcl.cpp src/core/analysis.cpp src/core/i18n.cpp src/core/report.cpp src/core/demo.cpp"
WIN="src/win/main.cpp src/win/winutil.cpp src/win/http_win.cpp src/win/graph.cpp"

echo "== tests (native)"
g++ -std=c++17 -O1 -Wall -Wextra -o build/tests tests/test_main.cpp $CORE
./build/tests

echo "== resources"
(cd res && "$WINDRES" app.rc -O coff -o ../build/app.res)

echo "== exe"
"$CXX_WIN" -std=c++17 -O2 -Wall -Wextra -DUNICODE -D_UNICODE -municode -mwindows \
    -static -static-libgcc -static-libstdc++ \
    $CORE $WIN build/app.res \
    -lwinhttp -lcomctl32 -lshell32 -ladvapi32 -lgdi32 -luser32 \
    -s -o build/UnnecessaryBoomkinReviewer.exe
ls -la build/UnnecessaryBoomkinReviewer.exe
