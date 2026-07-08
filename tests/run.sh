#!/usr/bin/env bash
# 编译并运行 luaaa 全部测试(含 ASan/UBSan)。
# 用法: bash tests/run.sh
set -u
cd "$(dirname "$0")/.."

# --- 定位 lua 头文件与库 ---
LUA_CFLAGS="$(pkg-config --cflags lua-5.5 2>/dev/null || echo -I/opt/homebrew/include/lua)"
LUA_LIBS="$(pkg-config --libs lua-5.5 2>/dev/null || echo '-L/opt/homebrew/lib -llua -lm')"
CXX="${CXX:-g++}"
STD="${STD:-c++11}"
SAN="-fsanitize=address,undefined -fno-omit-frame-pointer"
COMMON="-std=$STD -g $SAN -I. $LUA_CFLAGS $LUA_LIBS"

export ASAN_OPTIONS=detect_leaks=0
export UBSAN_OPTIONS=halt_on_error=0

run_one() {
  local src="$1"; local extra="$2"; local bin="/tmp/$(basename "$src" .cpp)"
  echo "############################################################"
  echo "# $src"
  echo "############################################################"
  if ! $CXX $COMMON $extra "$src" -o "$bin" 2>/tmp/cc.log; then
    echo "  编译失败:"; cat /tmp/cc.log; return 1
  fi
  "$bin"; local rc=$?
  echo "  (exit=$rc)"
  echo
  return 0
}

run_one tests/test_functional.cpp ""
run_one tests/test_fixes.cpp ""
run_one tests/test_callbacks.cpp ""
run_one tests/test_bugs.cpp ""
run_one tests/test_edge.cpp ""
run_one tests/test_extend.cpp ""
run_one tests/test_numeric.cpp ""
