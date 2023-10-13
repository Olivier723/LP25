FLAGS=-lm -W -Wall -Wextra -O3 -ggdb
BIN_DIR=./bin/

lp25-project : main.o analysis.o configuration.o direct_fork.o fifo_processes.o mq_processes.o reducers.o utility.o
	gcc $(BIN_DIR)*.o -o lp25-project $(FLAGS)

main.o : main.c
	gcc -c main.c -o $(BIN_DIR)main.o $(FLAGS)

analysis.o : analysis.c
	gcc -c analysis.c -o $(BIN_DIR)analysis.o $(FLAGS)

configuration.o : configuration.c
	gcc -c configuration.c -o $(BIN_DIR)configuration.o $(FLAGS)

direct_fork.o : direct_fork.c
	gcc -c direct_fork.c -o $(BIN_DIR)direct_fork.o $(FLAGS)

fifo_processes.o : fifo_processes.c
	gcc -c fifo_processes.c -o $(BIN_DIR)fifo_processes.o $(FLAGS)

mq_processes.o : mq_processes.c
	gcc -c mq_processes.c -o $(BIN_DIR)mq_processes.o $(FLAGS)

reducers.o : reducers.c
	gcc -c reducers.c -o $(BIN_DIR)reducers.o $(FLAGS)

utility.o : utility.c
	gcc -c utility.c -o $(BIN_DIR)utility.o $(FLAGS)

clean :
	rm ./bin/*.o
	rm ./temp/*
	rm lp25-project

run : lp25-project
	clear
	./lp25-project -f ./config.txt
