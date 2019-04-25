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

# test5.txt:
test5.txt.gz: ./tests/test5.txt
	gzip -c ./tests/test5.txt > test5.txt.gz

test5: debug test5.txt.gz
	./build/debug/bin/plzip test5.txt.gz output

# test3.txt:
test3.txt.gz: ./tests/test3.txt
	gzip -c ./tests/test3.txt > test3.txt.gz

test3: debug test3.txt.gz
	./build/debug/bin/plzip test3.txt.gz output

.PHONY: clean
clean:
	ninja -C build/debug clean
	ninja -C build/release clean
	rm -f test5.txt.gz

.PHONY: fullclean
fullclean:
	rm -rf build/
