PYTHON ?= python3
VENV := build/venv

.PHONY: test test-native test-python test-official python-install

# Every check of parser behaviour that runs on this machine. CI also covers
# other platforms and build configurations, sanitizers, fuzzing and packaging.
test: test-native test-python test-official

test-native:
	cmake -S . -B build/native
	cmake --build build/native --target check -j4

# Reinstall on every run so Python checks see native changes.
python-install:
	test -x $(VENV)/bin/python || $(PYTHON) -m venv $(VENV)
	$(VENV)/bin/python -m pip install -q '.[test]'

test-python: python-install
	$(VENV)/bin/python -m pytest -q tests/test_python.py
	FOSU_BACKEND=scalar $(VENV)/bin/python -m pytest -q tests/test_python.py

# Requires the .NET 10 SDK; the first run fetches the pinned osu! revision.
test-official: python-install
	sh tests/reference/official/build.sh
	$(VENV)/bin/python tests/test_official.py
	FOSU_BACKEND=scalar $(VENV)/bin/python tests/test_official.py
	$(VENV)/bin/python tests/test_official.py --corpus build/official-osu --report build/official-acceptance.json
	FOSU_BACKEND=scalar $(VENV)/bin/python tests/test_official.py --corpus build/official-osu --report build/official-acceptance-scalar.json
	$(VENV)/bin/python tests/test_official_values.py --corpus build/official-osu --report build/official-values.json
	FOSU_BACKEND=scalar $(VENV)/bin/python tests/test_official_values.py --corpus build/official-osu --report build/official-values-scalar.json
