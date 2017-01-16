CFLAGS += -Wall -g -I../../
LDFLAGS += -L../ -lsoccr ../libsoccr.a -lnet

RUN ?= tcp-constructor

run:
	./local.sh

tcp-constructor: tcp-constructor.c ../libsoccr.a
	$(CC) $(CFLAGS) tcp-constructor.c -o tcp-constructor $(LDFLAGS)

clean:
	rm -f tcp-constructor

tcp-conn: tcp-conn.c
	$(CC) $(CFLAGS) tcp-conn.c -o tcp-conn $(LDFLAGS)

test: tcp-constructor tcp-conn
	unshare -n sh -c "ip link set up dev lo; ./tcp-conn"
	python run.py ./$(RUN)

.PHONY: test

