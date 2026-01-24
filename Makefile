CC=gcc
CFLAGS=-Wall -Wextra -Wextra
CFLAGS += -DBUILD_TIME="\"$(shell date +%Y-%m-%d_%H:%M:%S)\""

SRC = server.c include/handler_get.c include/handler_put.c include/handler_delete.c include/utils.c
OBJ = $(SRC:.c=.o)

all: server

server: $(OBJ)
	$(CC) $(OBJ) -o $@

%.o: %.c utils.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) server