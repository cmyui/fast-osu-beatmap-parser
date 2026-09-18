#!/bin/sh
set -eu

if command -v dnf >/dev/null; then
  dnf install -y gcc-toolset-15-gcc gcc-toolset-15-gcc-c++
  ln -sf /opt/rh/gcc-toolset-15/root/usr/bin/gcc /usr/local/bin/gcc
  ln -sf /opt/rh/gcc-toolset-15/root/usr/bin/g++ /usr/local/bin/g++
elif command -v apk >/dev/null; then
  apk add --no-cache --upgrade \
    --repository=https://dl-cdn.alpinelinux.org/alpine/v3.23/main \
    gcc g++
else
  echo "unsupported Linux build image" >&2
  exit 1
fi

case "$(gcc -dumpversion)" in
  15 | 15.*) ;;
  *) exit 1 ;;
esac
