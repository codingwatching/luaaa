#!/usr/bin/env bash
# ============================================================================
#  Build & run the luaaa examples.
#     bash example/build.sh            # build + run both stories
#     bash example/build.sh desktop    # only the full std-lib example
#     bash example/build.sh embedded   # only the no-stdlib example
#
#  Override the toolchain / Lua location if auto-detection misses:
#     CXX=g++ LUA_CFLAGS=-I/usr/include/lua5.4 LUA_LIBS=-llua5.4 bash example/build.sh
# ============================================================================
set -u
cd "$(dirname "$0")"          # run from the example/ folder (examples read *.lua here)

# --- locate lua headers & libs -------------------------------------------------
detect() { pkg-config --"$1" "$2" 2>/dev/null; }
LUA_PKG="$(for p in lua5.4 lua-5.4 lua5.3 lua-5.3 lua5.2 lua5.1 luajit lua; do
             pkg-config --exists "$p" 2>/dev/null && { echo "$p"; break; }; done)"
LUA_CFLAGS="${LUA_CFLAGS:-$(detect cflags "$LUA_PKG" || echo '-I/opt/homebrew/include/lua -I/usr/local/include')}"
LUA_LIBS="${LUA_LIBS:-$(detect libs "$LUA_PKG" || echo '-L/opt/homebrew/lib -L/usr/local/lib -llua -lm')}"
CXX="${CXX:-c++}"
STD="${STD:-c++14}"           # c++11 also works; c++14 enables the tuple fast path

echo "CXX=$CXX  STD=$STD"
echo "LUA_CFLAGS=$LUA_CFLAGS"
echo "LUA_LIBS=$LUA_LIBS"
echo

build_run() {
  local name="$1"; shift
  echo "############################################################"
  echo "# $name"
  echo "############################################################"
  if ! $CXX -std="$STD" -Wall "$@" -I. $LUA_CFLAGS "${name}.cpp" $LUA_LIBS -o "/tmp/luaaa_${name}"; then
    echo "  build failed"; return 1
  fi
  "/tmp/luaaa_${name}"; echo "  (exit=$?)"; echo
}

what="${1:-all}"
case "$what" in
  all)      build_run example
            build_run embedded -fno-exceptions -fno-rtti ;;
  desktop)  build_run example ;;
  embedded) build_run embedded -fno-exceptions -fno-rtti ;;
  *)        echo "usage: build.sh [all|desktop|embedded]"; exit 2 ;;
esac
