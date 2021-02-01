# Target library
lib := libuthread.a
objs := sem.o queue.o thread.o tps.o
CC := gcc
CFLAGS := -Wall -Wextra -Werror
all: $(lib)

deps := $(patsubst %.o,%.d,$(objs))
-include $(deps)

%.o : %.c
	@echo "CC $@"
	@$(CC) $(CFLAGS) -c -o $@ $< $(DEPSFLAGS)

$(lib) : $(objs)
	ar rcs $(lib) $^	

clean: 
	rm -f $(lib) sem.o tps.o $(deps)
	
## TODO: Phase 1 and Phase 2
