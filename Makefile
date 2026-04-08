CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic -std=c11 -O2 -D_XOPEN_SOURCE=700 -D_DEFAULT_SOURCE
OBJ = wserver.o request.o io_helper.o

all: wserver

wserver: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ)

wserver.o: wserver.c request.h io_helper.h
request.o: request.c request.h io_helper.h
io_helper.o: io_helper.c io_helper.h

clean:
	rm -f $(OBJ) wserver
