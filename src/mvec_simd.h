#ifndef CHAPTER_EXE_MVEC_SIMD_H
#define CHAPTER_EXE_MVEC_SIMD_H

namespace mvec_simd {

const char *backend_name();

int dist(const unsigned char *p1,
         const unsigned char *p2,
         int stride,
         int dist_limit,
         int block_height);

int maxmin_block(const unsigned char *pixels,
                 int stride,
                 int block_height);

int avgdist(int *average,
            const unsigned char *pixels,
            int stride,
            int block_height);

// Exact scalar equivalents of the optimized functions. These are both the
// portable fallback and the reference implementation for SIMD tests.
namespace reference {

int dist(const unsigned char *p1,
         const unsigned char *p2,
         int stride,
         int dist_limit,
         int block_height);

int maxmin_block(const unsigned char *pixels,
                 int stride,
                 int block_height);

int avgdist(int *average,
            const unsigned char *pixels,
            int stride,
            int block_height);

}  // namespace reference
}  // namespace mvec_simd

#endif
