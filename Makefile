.PHONY: all clean

all:
	cmake -S . -B build/switch -DCMAKE_BUILD_TYPE=Release
	cmake --build build/switch --parallel
	cmake -E copy_if_different build/switch/switch-drive.nro switch-drive.nro

clean:
	cmake -E remove_directory build/switch
	cmake -E rm -f switch-drive.nro
