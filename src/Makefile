CC = gcc
CFLAGS = -w -O3 -I../avisynth -I../extras -ffast-math -Wall -Wshadow -Wempty-body -I. -std=gnu99 -fpermissive -fomit-frame-pointer -s -fno-tree-vectorize
LDLAGS = -lstdc++ -pthread
OBJS = chapter_exe.o mvec.o
PROGRAM = chapter_exe

ifeq ($(OS),Windows_NT)
  PROGRAM := $(PROGRAM).exe
  CFLAGS += -static -finput-charset=UTF-8 -fexec-charset=CP932 -DSYS_WINDOWS=1 -msse4.1
  LDLAGS += -static -static-libstdc++
else
  UNAME_MACHINE:=$(shell uname -m)
  ifeq ($(UNAME_MACHINE),aarch64)
    CFLAGS += -mfpu=neon -DSYS_ARM64
  endif
  LDLAGS += -ldl
endif

.SUFFIXES: .c .o

$(PROGRAM): $(OBJS)
	$(CC) -o $(PROGRAM) $^ $(LDLAGS)

.cpp.o:
	$(CC) $(CFLAGS) -c $<

.PHONY: clean
clean:
	$(RM) $(PROGRAM) $(OBJS)
