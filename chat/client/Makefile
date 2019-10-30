CC = clang
main = client.c
out = client
flags = -lncurses -lpthread -o $(out)

all: $(main)
	@make compile && make run && make clean

compile:
	@$(CC) $(flags) $(main)

run:
	@./$(out)

clean:
	@-rm $(out)