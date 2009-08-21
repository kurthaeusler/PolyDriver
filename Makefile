CFLAGS=-Wall
LDFLAGS=

all:    bin/polydriver bin/installer

bin/polydriver:	polydriver.o lzn.o
	gcc polydriver.o lzn.o -o bin/polydriver $(LDFLAGS)

bin/installer:	installer.o lzn.o
	gcc installer.o lzn.o -o bin/installer $(LDFLAGS)

polydriver.o:	src/polydriver.c src/lzn.h
	gcc -c $(CFLAGS) src/polydriver.c

lzn.o:	src/lzn.c src/lzn.h src/atob.h
	gcc -c $(CFLAGS) src/lzn.c

installer.o:	src/installer.c src/lzn.h
	gcc -c $(CFLAGS) src/installer.c

clean:
	-$(RM) *.o
	-$(RM) core

cleanall: clean
	-$(RM) bin/polydriver bin/installer 
