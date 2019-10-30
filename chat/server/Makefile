CC = clang
main = server.c
out = server
flags = -lpthread -o $(out)
libs = list/list.c

all: $(main)
	@make compile && make run && make clean

compile:
	@$(CC) $(flags) $(main) $(libs)

run:
	@./$(out)

clean:
	@-rm $(out)