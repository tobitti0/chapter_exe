CXX ?= g++
AR ?= ar
PKG_CONFIG ?= pkg-config
PKG_CONFIG_LIBS ?= --libs

PROGRAM := chapter_exe
OBJS := chapter_exe.o mvec.o mvec_simd.o

WITH_AVISYNTH ?= auto
WITH_DTVINDEX ?= auto
DTVINDEX_DIR ?=
SIMD_BACKEND ?= auto

FFMPEG_PACKAGES := libavformat libavcodec libavutil libswscale libswresample
VALID_FEATURE_VALUES := auto yes no
VALID_SIMD_BACKENDS := auto sse2 neon scalar

ifeq ($(filter $(WITH_AVISYNTH),$(VALID_FEATURE_VALUES)),)
  $(error WITH_AVISYNTH must be auto, yes, or no)
endif
ifeq ($(filter $(WITH_DTVINDEX),$(VALID_FEATURE_VALUES)),)
  $(error WITH_DTVINDEX must be auto, yes, or no)
endif
ifeq ($(filter $(SIMD_BACKEND),$(VALID_SIMD_BACKENDS)),)
  $(error SIMD_BACKEND must be auto, sse2, neon, or scalar)
endif

CPPFLAGS := -I../extras -I.
CXXFLAGS := -O3 -ffast-math -Wall -Wshadow -Wempty-body \
	-std=gnu++11 -fomit-frame-pointer -fno-tree-vectorize
LDFLAGS :=
LDLIBS := -pthread
DTVINDEX_DEPS :=

ifeq ($(SIMD_BACKEND),sse2)
  CPPFLAGS += -DMVEC_SIMD_FORCE_SSE2=1
  CXXFLAGS += -msse2
else ifeq ($(SIMD_BACKEND),neon)
  CPPFLAGS += -DMVEC_SIMD_FORCE_NEON=1
else ifeq ($(SIMD_BACKEND),scalar)
  CPPFLAGS += -DMVEC_SIMD_FORCE_SCALAR=1
endif

# chapter_exe keeps the AviSynth C API header in the repository and loads the
# runtime library dynamically. The development library is not required.
AVISYNTH_FOUND := $(strip $(if \
	$(and $(wildcard ../avisynth/avs_internal.c),\
		$(wildcard ../extras/avisynth_c.h)),yes,no))
ifeq ($(WITH_AVISYNTH),auto)
  ENABLE_AVISYNTH := $(AVISYNTH_FOUND)
else
  ENABLE_AVISYNTH := $(WITH_AVISYNTH)
endif
ifeq ($(ENABLE_AVISYNTH),yes)
  ifneq ($(AVISYNTH_FOUND),yes)
    $(error WITH_AVISYNTH=yes, but the bundled AviSynth C API files were not found)
  endif
  CPPFLAGS += -DHAVE_AVISYNTH=1 -I../avisynth
else
  CPPFLAGS += -DHAVE_AVISYNTH=0
endif

ifeq ($(WITH_DTVINDEX),no)
  PKG_CONFIG_FOUND := no
else
  PKG_CONFIG_FOUND := $(strip $(shell \
	$(PKG_CONFIG) --version >/dev/null 2>&1 \
	&& echo yes || echo no))
endif
DTVINDEX_PKG_FOUND := $(strip $(if $(filter yes,$(PKG_CONFIG_FOUND)),\
	$(shell $(PKG_CONFIG) --exists dtvindex libswresample \
		&& echo yes || echo no),no))
FFMPEG_FOUND := $(strip $(if $(filter yes,$(PKG_CONFIG_FOUND)),\
	$(shell $(PKG_CONFIG) --exists $(FFMPEG_PACKAGES) \
		&& echo yes || echo no),no))

LOCAL_DTVINDEX_LIBRARY := $(CURDIR)/libdtvindex.a
LOCAL_DTVINDEX_INCLUDE := $(CURDIR)/include
LOCAL_DTVINDEX_FOUND := $(strip $(if \
	$(and $(wildcard $(LOCAL_DTVINDEX_LIBRARY)),\
		$(wildcard $(LOCAL_DTVINDEX_INCLUDE)/dtvindex/dtvindex.hpp)),\
	yes,no))

SIBLING_DTVINDEX_DIR := $(abspath ../../dtvindex)
SIBLING_DTVINDEX_FOUND := $(strip $(if \
	$(and $(wildcard $(SIBLING_DTVINDEX_DIR)/Makefile),\
		$(wildcard $(SIBLING_DTVINDEX_DIR)/include/dtvindex/dtvindex.hpp)),\
	yes,no))

DTVINDEX_MODE :=
DTVINDEX_SOURCE_DIR :=

ifneq ($(strip $(DTVINDEX_DIR)),)
  DTVINDEX_SOURCE_DIR := $(abspath $(DTVINDEX_DIR))
  ifneq ($(and $(wildcard $(DTVINDEX_SOURCE_DIR)/Makefile),\
		$(wildcard $(DTVINDEX_SOURCE_DIR)/include/dtvindex/dtvindex.hpp)),)
    ifeq ($(FFMPEG_FOUND),yes)
      DTVINDEX_MODE := source
    endif
  else ifneq ($(WITH_DTVINDEX),no)
    $(error DTVINDEX_DIR does not contain a dtvindex source tree)
  endif
else ifeq ($(LOCAL_DTVINDEX_FOUND)-$(FFMPEG_FOUND),yes-yes)
  DTVINDEX_MODE := local
else ifeq ($(DTVINDEX_PKG_FOUND),yes)
  DTVINDEX_MODE := pkg-config
else ifeq ($(SIBLING_DTVINDEX_FOUND)-$(FFMPEG_FOUND),yes-yes)
  DTVINDEX_MODE := source
  DTVINDEX_SOURCE_DIR := $(SIBLING_DTVINDEX_DIR)
endif

ifeq ($(WITH_DTVINDEX),auto)
  ENABLE_DTVINDEX := $(if $(DTVINDEX_MODE),yes,no)
else
  ENABLE_DTVINDEX := $(WITH_DTVINDEX)
endif

ifeq ($(ENABLE_DTVINDEX),yes)
  ifeq ($(DTVINDEX_MODE),)
    $(error WITH_DTVINDEX=yes, but dtvindex and its FFmpeg dependencies were not found)
  endif
  CPPFLAGS += -DHAVE_DTVINDEX=1

  ifeq ($(DTVINDEX_MODE),pkg-config)
    CPPFLAGS += $(shell $(PKG_CONFIG) --cflags dtvindex libswresample)
    LDLIBS += $(shell $(PKG_CONFIG) $(PKG_CONFIG_LIBS) dtvindex libswresample)
  else ifeq ($(DTVINDEX_MODE),local)
    CPPFLAGS += -I$(LOCAL_DTVINDEX_INCLUDE) \
		$(shell $(PKG_CONFIG) --cflags $(FFMPEG_PACKAGES))
    LDLIBS += $(LOCAL_DTVINDEX_LIBRARY) \
		$(shell $(PKG_CONFIG) $(PKG_CONFIG_LIBS) $(FFMPEG_PACKAGES))
    DTVINDEX_DEPS += $(LOCAL_DTVINDEX_LIBRARY)
  else ifeq ($(DTVINDEX_MODE),source)
    DTVINDEX_LIBRARY := $(DTVINDEX_SOURCE_DIR)/build/libdtvindex.a
    CPPFLAGS += -I$(DTVINDEX_SOURCE_DIR)/include \
		$(shell $(PKG_CONFIG) --cflags $(FFMPEG_PACKAGES))
    LDLIBS += $(DTVINDEX_LIBRARY) \
		$(shell $(PKG_CONFIG) $(PKG_CONFIG_LIBS) $(FFMPEG_PACKAGES))
    DTVINDEX_DEPS += $(DTVINDEX_LIBRARY)
  endif
else
  CPPFLAGS += -DHAVE_DTVINDEX=0
endif

ifneq ($(MAKECMDGOALS),clean)
  ifeq ($(ENABLE_AVISYNTH)-$(ENABLE_DTVINDEX),no-no)
    $(error Neither AviSynth nor dtvindex input support is available)
  endif
endif

ifeq ($(OS),Windows_NT)
  PROGRAM := $(PROGRAM).exe
  CXXFLAGS += -finput-charset=UTF-8 -fexec-charset=CP932 \
	-DSYS_WINDOWS=1 -msse2
  LDFLAGS += -static -static-libgcc -static-libstdc++
else
  UNAME_SYSTEM := $(shell uname -s)

  ifeq ($(UNAME_SYSTEM),Linux)
    LDLIBS += -ldl
  endif
endif

$(info chapter_exe input support: AviSynth=$(ENABLE_AVISYNTH) dtvindex=$(ENABLE_DTVINDEX)$(if $(filter yes,$(ENABLE_DTVINDEX)),$(if $(DTVINDEX_MODE), ($(DTVINDEX_MODE)))))
$(info chapter_exe motion SIMD: $(SIMD_BACKEND))

.SUFFIXES: .cpp .o

.PHONY: all clean

all: $(PROGRAM)

$(PROGRAM): $(OBJS) $(DTVINDEX_DEPS)
	$(CXX) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

ifeq ($(DTVINDEX_MODE),source)
$(DTVINDEX_LIBRARY):
	$(MAKE) -C "$(DTVINDEX_SOURCE_DIR)" build/libdtvindex.a \
		CXX="$(CXX)" \
		AR="$(AR)" \
		OS="$(OS)" \
		PKG_CONFIG="$(PKG_CONFIG)" \
		PKG_CONFIG_LIBS="$(PKG_CONFIG_LIBS)"
endif

chapter_exe.o: source.h mvec_simd.h
ifeq ($(ENABLE_AVISYNTH),yes)
chapter_exe.o: ../avisynth/avs_internal.c ../extras/avisynth_c.h
endif
ifeq ($(ENABLE_DTVINDEX),yes)
chapter_exe.o: dtvindex_source.h ffmpeg_audio_reader.h
endif
mvec.o mvec_simd.o: mvec_simd.h

.cpp.o:
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $<

clean:
	$(RM) chapter_exe chapter_exe.exe $(OBJS)
