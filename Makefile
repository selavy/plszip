all: debug release

build:
	mkdir -p build

debug: build
	mkdir -p build/debug
	cd build/debug && cmake -DCMAKE_BUILD_TYPE=Debug -GNinja ../..
	ninja -C build/debug

release: build
	mkdir -p build/release
	cd build/release && cmake -DCMAKE_BUILD_TYPE=Release -GNinja ../..
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
