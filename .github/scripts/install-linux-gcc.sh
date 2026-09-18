#!/bin/sh
set -eu

version=16.2.0
checksum=e6738e29597f733270731aa90600f37ffdc045079dfc27ec7e8192cc81085c3e
install_dir="/opt/fosu-gcc-${version}"
work_dir="$(mktemp -d)"
trap 'rm -rf "${work_dir}"' EXIT

if command -v dnf >/dev/null; then
  dnf install -y gcc-c++ gmp-devel libmpc-devel mpfr-devel zlib-devel >/dev/null
elif command -v apk >/dev/null; then
  apk add --no-cache build-base curl gmp-dev mpc1-dev mpfr-dev xz zlib-dev >/dev/null
else
  echo "unsupported Linux build image" >&2
  exit 1
fi

curl --fail --location --silent --show-error \
  "https://ftp.gnu.org/gnu/gcc/gcc-${version}/gcc-${version}.tar.xz" \
  --output "${work_dir}/gcc.tar.xz"
echo "${checksum}  ${work_dir}/gcc.tar.xz" | sha256sum -c
tar -xf "${work_dir}/gcc.tar.xz" -C "${work_dir}"
mkdir "${work_dir}/build"

cd "${work_dir}/build"
"${work_dir}/gcc-${version}/configure" \
  --prefix="${install_dir}" \
  --disable-bootstrap \
  --disable-libsanitizer \
  --disable-multilib \
  --disable-nls \
  --enable-languages=c,c++ \
  --with-system-zlib >/dev/null
if ! make -j"$(getconf _NPROCESSORS_ONLN)" >build.log 2>&1; then
  tail -n 200 build.log >&2
  exit 1
fi
make install-strip >/dev/null
"${install_dir}/bin/g++" --version | head -n 1
