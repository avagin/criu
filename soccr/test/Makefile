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

test: tcp-constructor
	python run.py ./$(RUN)

.PHONY: test

