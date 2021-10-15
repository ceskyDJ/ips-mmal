CFLAGS=-std=gnu99 -Wall -Wextra -g
#CFLAGS=-std=gnu99 -Wall -Wextra -g -DNDEBUG
UNAME_S := $(shell uname -s)

test_mmal: mmal.o test_mmal.o
	gcc -o bin/$@ $^

test: test_mmal testrun

testrun:
ifeq ($(UNAME_S),Linux)
		@if setarch `uname -m` -R true 2>/dev/null; then setarch `uname -m` -R ./bin/test_mmal; else ./bin/test_mmal; fi
else
		./bin/test_mmal
endif

mmal.o: src/mmal.c src/mmal.h
	gcc $(CFLAGS) -c $<
test_mmal.o: test/test_mmal.c src/mmal.h
	gcc $(CFLAGS) -c $<

clean:
	-rm mmal.o test_mmal.o bin/test_mmal
