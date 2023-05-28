CC = gcc
CFLAGS = -Wall `pkg-config fuse3 --cflags --libs` -DDEBUG

.PHONY: clean

all: chatfs

chatfs: chatfs.c
	$(CC) $(CFLAGS) chatfs.c -o chatfs

clean:
	rm -f chatfs
