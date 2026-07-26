CXX ?= g++
PKG_CONFIG ?= pkg-config
FFMPEG_PACKAGES = libavformat libavcodec libavutil libswscale libswresample
FFMPEG_CFLAGS := $(shell $(PKG_CONFIG) --cflags $(FFMPEG_PACKAGES))
FFMPEG_LIBS := $(shell $(PKG_CONFIG) --libs $(FFMPEG_PACKAGES))
CXXFLAGS = -O3 -I../avisynth -I../extras -I. -ffast-math -Wall -Wshadow -Wempty-body -std=gnu++11 -fomit-frame-pointer -fno-tree-vectorize $(FFMPEG_CFLAGS)
LDLIBS = -pthread $(FFMPEG_LIBS)
OBJS = chapter_exe.o mvec.o
PROGRAM = chapter_exe

ifeq ($(OS),Windows_NT)
  PROGRAM := $(PROGRAM).exe
  CXXFLAGS += -finput-charset=UTF-8 -fexec-charset=CP932 -DSYS_WINDOWS=1 -msse4.1
else
  UNAME_MACHINE := $(shell uname -m)
  UNAME_SYSTEM := $(shell uname -s)

  ifeq ($(UNAME_MACHINE),aarch64)
    CXXFLAGS += -DSYS_ARM64
    ifeq ($(UNAME_SYSTEM),Linux)
      HAVE_NEON := $(shell $(CXX) -mfpu=neon -dM -E - </dev/null >/dev/null 2>&1 && echo yes || echo no)
      ifeq ($(HAVE_NEON),yes)
        CXXFLAGS += -mfpu=neon
      endif
    endif
  endif

  ifeq ($(UNAME_SYSTEM),Linux)
    LDLIBS += -ldl
  endif
endif

.SUFFIXES: .cpp .o

all: $(PROGRAM)

$(PROGRAM): $(OBJS)
	$(CXX) -o $(PROGRAM) $^ $(LDLIBS)

.cpp.o:
	$(CXX) $(CXXFLAGS) -c $<

.PHONY: all clean
clean:
	$(RM) $(PROGRAM) $(OBJS)
