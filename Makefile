# CC=gcc-8
# CXX=g++-8
CC=gcc
CXX=g++

all: debug release

build:
	mkdir -p build

debug: build
	mkdir -p build/debug
	cd build/debug && cmake -DCMAKE_BUILD_TYPE=Debug -GNinja ../..
	ninja -C build/debug

release: build
	mkdir -p build/release
	cd build/release && cmake -DCMAKE_BUILD_TYPE=Release -GNinja -DCMAKE_C_COMPILER=${CC} -DCMAKE_CXX_COMPILER=${CXX} ../..
	ninja -C build/release

test: debug
	./tests/run_tests.sh ./build/debug ./tests

.PHONY: clean
clean:
	ninja -C build/debug clean
	ninja -C build/release clean

.PHONY: fullclean
fullclean:
	rm -rf build/
