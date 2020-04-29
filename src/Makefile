PROGRAM = chapter_exe
OBJS = chapter_exe.o mvec.o

CC = gcc
CFLAGS = -O3 -I../avisynth -ffast-math -Wall -Wshadow -Wempty-body -I. -std=gnu99 -fpermissive -fomit-frame-pointer -s -fno-tree-vectorize
LDLAGS = -ldl -lstdc++

.SUFFIXES: .c .o

$(PROGRAM): $(OBJS)
	$(CC) $(LDLAGS) -o $(PROGRAM) $^

.cpp.o:
	$(CC) $(CFLAGS) -c $<

.PHONY: clean
clean:
	$(RM) $(PROGRAM) $(OBJS)
