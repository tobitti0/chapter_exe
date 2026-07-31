@echo off
mingw32-make clean all OS=Windows_NT WITH_AVISYNTH=yes WITH_DTVINDEX=yes PKG_CONFIG_LIBS="--static --libs"
