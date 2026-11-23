monitor: main.o
	gcc main.o -lstdc++ -o monitor

main.o: main.cpp
	gcc -c main.cpp -std=c++17 -o main.o
