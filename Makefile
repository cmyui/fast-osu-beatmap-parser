build:
	cmake -S . -B build/native
	cmake --build build/native --target check -j4
