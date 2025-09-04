all: initksocket library user1 user2

library: libksocket.a

libksocket.a: ksocket.o
	ar rcs libksocket.a ksocket.o

ksocket.o: ksocket.c ksocket.h
	gcc -c ksocket.c -o ksocket.o

initksocket: initksocket.c ksocket.h
	gcc -o initksocket initksocket.c

user1: user1.c libksocket.a
	gcc -o user1 user1.c -L. -lksocket

user2: user2.c libksocket.a
	gcc -o user2 user2.c -L. -lksocket

clean:
	rm -f *.o *.a initksocket user1 user2