.PHONY: all clean test

all:
	bash build.sh

test:
	bash build.sh --clean --test

clean:
	rm -rf build
