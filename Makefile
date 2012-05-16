CFLAGS = -Wall -pedantic -O3
LOADLIBES = -lm -lrt

all: traffic

traffic: traffic.c

clean:
	rm -f traffic

run: traffic
	./traffic 5001 1000 30 1.4 &
	./traffic 5001 localhost 10

.PHONY: clean run
