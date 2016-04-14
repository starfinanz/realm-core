TAG=v0.97.3
FULLTAG=tags/$(TAG)
CC=g++
CFLAGS=-Werror -Wextra -Wall -pedantic -std=c++11 \
	-I../core-builds/$(FULLTAG)/include
LDFLAGS= -lrealm \
	-L../core-builds/$(FULLTAG)/lib

all: bench.bin

bench.bin: main.cpp
	cd .. && ./util/build-core.sh $(FULLTAG)
	$(CC) $(CFLAGS) $? -o $@ $(LDFLAGS)

clean:
	rm -f *.bin

.PHONY: all clean
