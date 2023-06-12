CFLAGS += -Wall -Wextra -O3 -march=native -D_GNU_SOURCE

main: main.o
	$(CC) $^ -o $@ $(LDFLAGS)
