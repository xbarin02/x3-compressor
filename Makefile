CFLAGS+=-std=c99 -pedantic -Wall -Wextra -fopenmp
LDFLAGS+=-fopenmp
LDLIBS+=-lm

BIN=x3

ifeq ($(BUILD),debug)
	CFLAGS+=-Og -g
	LDFLAGS+=-rdynamic
endif

ifeq ($(BUILD),release)
	CFLAGS+=-march=native -O3 -DNDEBUG
endif

ifeq ($(BUILD),profile-generate)
	CFLAGS+=-march=native -O3 -DNDEBUG -fprofile-generate
	LDFLAGS+=-fprofile-generate
endif

ifeq ($(BUILD),profile-use)
	CFLAGS+=-march=native -O3 -DNDEBUG -fprofile-use
endif

ifeq ($(BUILD),profile)
	CFLAGS+=-Og -g -pg
	LDFLAGS+=-rdynamic -pg
endif

.PHONY: all
all: $(BIN)

x3: x3.o backend.o file.o dict.o gr.o tag_pair.o utils.o bio.o context.o ac.o

.PHONY: clean
clean:
	-$(RM) -- *.o $(BIN)

.PHONY: distclean
distclean: clean
	-$(RM) -- *.gcda gmon.out cachegrind.out.* callgrind.out.*

.PHONY: check
check: all
	for f in $(wildcard data/*); do ./x3 -zf $$f $$f.x3; ./x3 -df $$f.x3 $$f.x3.out; diff $$f $$f.x3.out; rm $$f.x3 $$f.x3.out; done
