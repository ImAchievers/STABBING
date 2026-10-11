CC      = gcc
CFLAGS  = -O3 -march=native -fwrapv -Wall -Ilib
LDFLAGS = -lm -lpthread

CORE_SRCS = lib/generator.c lib/layers.c lib/biomenoise.c lib/biomes.c lib/noise.c
CORE_OBJS = $(CORE_SRCS:.c=.o)

all: STABBING

STABBING: STABBING.c $(CORE_OBJS)
	$(CC) $(CFLAGS) STABBING.c $(CORE_OBJS) -o STABBING $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f STABBING STABBING.exe $(CORE_OBJS)

.PHONY: all clean
