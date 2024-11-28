
CC = cc
CFLAGS = -O3
LINK = -lpthread
EXEC = main
INCLUDE = -I./lib

DEPS = ./lib/event_loop.c ./lib/buffer.c ./lib/fs.c ./lib/socket_server.c globals.c protocol.c aggregator.c

ifeq ($(DEBUG), 1)
	CFLAGS += -DDEBUG
endif

run: build
	./$(EXEC)

rebuild: clean build	
main.o: 
	$(CC) $(CFLAGS) $(DEPS) main.c -o $(EXEC) $(LINK) $(INCLUDE)

build: main.o

profBuild: 
	$(CC) -pg -g $(CFLAGS) $(DEPS) main.c -o $(EXEC) $(LINK) $(INCLUDE)

debug:
	DEBUG=1 make build

valgrind: build	
	valgrind --leak-check=full ./main

callgrind: profBuild	
	sudo valgrind --tool=callgrind "sudo ./main"

gprof: profBuild
	sudo ./$(EXEC)
	gprof $(EXEC) > gprof.txt



# $(python3-config --cflags) $(python3-config --ldflags) -lpython3.10


show_net:
	@echo $(lsof -i :8080)

clean:
	rm -f *.o main

