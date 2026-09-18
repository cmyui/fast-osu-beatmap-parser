#!/bin/sh
set -eu

if command -v dnf >/dev/null; then
  dnf install -y gcc-toolset-15-gcc gcc-toolset-15-gcc-c++
  compiler_dir=/opt/rh/gcc-toolset-15/root/usr/bin
elif command -v apk >/dev/null; then
  apk add --no-cache --upgrade \
    --repository=https://dl-cdn.alpinelinux.org/alpine/v3.23/main \
    gcc g++
  compiler_dir=/usr/bin
else
  echo "unsupported Linux build image" >&2
  exit 1
fi

ln -sf "$compiler_dir/gcc" /usr/local/bin/gcc
ln -sf "$compiler_dir/g++" /usr/local/bin/g++

case "$(/usr/local/bin/gcc -dumpversion)" in
  15 | 15.*) ;;
  *) exit 1 ;;
esac
