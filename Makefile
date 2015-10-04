all: main

LIB=-lsphinxbase -I/Users/evgeny/linux-share/sphinx/pocketsphinx/../sphinxbase/include -I/Users/evgeny/linux-share/sphinx/pocketsphinx/../sphinxbase/include/sphinxbase

main: main.o
	gcc -g -O0 main.o $(LIB) -o main

main.o: main.c
	gcc -c -g -O0 $(LIB) main.c
	
clean:
	rm -rf main.o main.c
