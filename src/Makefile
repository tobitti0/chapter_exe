CC = gcc
CFLAGS = -w -O3 -I../avisynth -I../extras -ffast-math -Wall -Wshadow -Wempty-body -I. -std=gnu99 -fpermissive -fomit-frame-pointer -s -fno-tree-vectorize
LDLAGS = -lstdc++ -pthread
OBJS = chapter_exe.o mvec.o
PROGRAM = chapter_exe

ifeq ($(OS),Windows_NT)
  PROGRAM := $(PROGRAM).exe
  CFLAGS += -finput-charset=UTF-8 -fexec-charset=CP932 -DSYS_WINDOWS=1 -msse4.1
else
  UNAME_MACHINE := $(shell uname -m)
  UNAME_SYSTEM := $(shell uname -s)

  ifeq ($(UNAME_MACHINE),aarch64)
    CFLAGS += -DSYS_ARM64
    ifeq ($(UNAME_SYSTEM),Linux)
      HAVE_NEON := $(shell $(CC) -mfpu=neon -dM -E - </dev/null >/dev/null 2>&1 && echo yes || echo no)
      ifeq ($(HAVE_NEON),yes)
        CFLAGS += -mfpu=neon
      endif
    endif
  endif

  ifeq ($(UNAME_SYSTEM),Linux)
    LDLAGS += -ldl
  endif
endif

.SUFFIXES: .c .o

$(PROGRAM): $(OBJS)
	$(CC) -o $(PROGRAM) $^ $(LDLAGS)

.cpp.o:
	$(CC) $(CFLAGS) -c $<

.PHONY: clean
clean:
	$(RM) $(PROGRAM) $(OBJS)
