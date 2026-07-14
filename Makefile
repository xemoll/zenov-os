.PHONY: all clean test

all:
	./build.sh

test:
	./build.sh --clean --test

clean:
	rm -rf build
