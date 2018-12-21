#------------------------------------------------------------
# @file Makefile
# @author Klaus Hahnenkamp <e11775823@student.tuwien.ac.at>
# @date 15.12.2018
# program: intmul
#------------------------------------------------------------

params = -std=c99 -pedantic -Wall -D_DEFAULT_SOURCE -D_BSD_SOURCE -D_SVID_SOURCE -D_POSIX_C_SOURCE=200809L

all: intmul

intmul: intmul.o
	gcc $(params) -g -o intmul intmul.o

intmul.o: intmul.c
	gcc $(params) -g -o intmul.o -c intmul.c

clean:
	rm -rf *.o intmul

