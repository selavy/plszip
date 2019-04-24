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

test5.txt.gz: ./tests/test5.txt
	gzip -c ./tests/test5.txt > test5.txt.gz

test: debug test5.txt.gz
	./build/debug/bin/plzip test5.txt.gz output

test2: debug
	./build/debug/bin/plzip ./tests/sample.txt.gz output

.PHONY: clean
clean:
	ninja -C build/debug clean
	ninja -C build/release clean

.PHONY: fullclean
fullclean:
	rm -rf build/
