CXX ?= g++
PKG_CONFIG ?= pkg-config
FFMPEG_PACKAGES = libavformat libavcodec libavutil libswscale libswresample
FFMPEG_CFLAGS := $(shell $(PKG_CONFIG) --cflags $(FFMPEG_PACKAGES))
FFMPEG_LIBS := $(shell $(PKG_CONFIG) --libs $(FFMPEG_PACKAGES))
DTVINDEX_DIR ?= ../../dtvindex
DTVINDEX_LIBRARY = $(DTVINDEX_DIR)/build/libdtvindex.a
CXXFLAGS = -O3 -I../avisynth -I../extras -I. -I$(DTVINDEX_DIR)/include -ffast-math -Wall -Wshadow -Wempty-body -std=gnu++11 -fomit-frame-pointer -fno-tree-vectorize $(FFMPEG_CFLAGS)
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

$(PROGRAM): $(OBJS) dtvindex
	$(CXX) -o $(PROGRAM) $(OBJS) $(DTVINDEX_LIBRARY) $(LDLIBS)

.PHONY: dtvindex
dtvindex:
	$(MAKE) -C $(DTVINDEX_DIR) build/libdtvindex.a

chapter_exe.o: dtvindex_source.h ffmpeg_source.h source.h

.cpp.o:
	$(CXX) $(CXXFLAGS) -c $<

.PHONY: all clean
clean:
	$(RM) $(PROGRAM) $(OBJS)
