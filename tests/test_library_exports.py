"""Verify the Linux runtime exports and optional private runtime boundary."""

import argparse
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("library")
p.add_argument("--bundled", action="store_true")
p.add_argument("--engine", action="store_true")
a = p.parse_args()
exports = subprocess.check_output(["nm", "-D", "--defined-only", a.library], text=True)
names = {line.split()[-1] for line in exports.splitlines()}
expected = {
    "_ZN4fosu14runtime_engineEv",
    "fosu_c_abi_version",
    "fosu_c_backend_name",
    "fosu_c_new",
    "fosu_c_free",
    "fosu_c_parse",
    "fosu_c_parse_file",
    "fosu_c_get_view",
    "fosu_c_last_error",
}
if a.engine:
    expected = {"fosu_engine_v2"}
assert names == expected, names ^ expected
if a.bundled:
    dynamic = subprocess.check_output(["readelf", "-d", a.library], text=True)
    assert "libstdc++" not in dynamic and "libgcc_s" not in dynamic
print("Runtime exports and runtime dependencies passed")
