#include "mvec_simd.h"

#include <algorithm>
#include <climits>
#include <cstdlib>

#if defined(MVEC_SIMD_FORCE_SSE2)
#define MVEC_SIMD_USE_SSE2 1
#elif defined(MVEC_SIMD_FORCE_NEON)
#define MVEC_SIMD_USE_NEON 1
#elif defined(MVEC_SIMD_FORCE_SCALAR)
#define MVEC_SIMD_USE_SCALAR 1
#elif defined(__aarch64__) || defined(__ARM_NEON) || defined(__ARM_NEON__)
#define MVEC_SIMD_USE_NEON 1
#elif defined(__SSE2__) || defined(_M_X64) || \
    (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#define MVEC_SIMD_USE_SSE2 1
#else
#define MVEC_SIMD_USE_SCALAR 1
#endif

#if defined(MVEC_SIMD_USE_SSE2)
#include <emmintrin.h>
#elif defined(MVEC_SIMD_USE_NEON)
#include <arm_neon.h>
#endif

namespace mvec_simd {
namespace reference {

int dist(const unsigned char *p1,
         const unsigned char *p2,
         int stride,
         int dist_limit,
         int block_height) {
    int sum = 0;
    for (int y = 0; y < block_height; ++y) {
        for (int x = 0; x < 16; ++x) {
            sum += std::abs(static_cast<int>(p1[x]) -
                            static_cast<int>(p2[x]));
        }
        if (block_height != 8 && sum > dist_limit) {
            break;
        }
        p1 += stride;
        p2 += stride;
    }
    return sum;
}

int maxmin_block(const unsigned char *pixels,
                 int stride,
                 int block_height) {
    unsigned char minimum = UCHAR_MAX;
    unsigned char maximum = 0;
    for (int y = 0; y < block_height; ++y) {
        for (int x = 0; x < 16; ++x) {
            minimum = std::min(minimum, pixels[x]);
            maximum = std::max(maximum, pixels[x]);
        }
        pixels += stride;
    }
    return static_cast<int>(maximum) - static_cast<int>(minimum);
}

int avgdist(int *average,
            const unsigned char *pixels,
            int stride,
            int block_height) {
    const unsigned char *row = pixels;
    int sum = 0;
    for (int y = 0; y < block_height; ++y) {
        for (int x = 0; x < 16; ++x) {
            sum += row[x];
        }
        row += stride;
    }

    const int pixel_count = block_height * 16;
    const unsigned char block_average =
        static_cast<unsigned char>((sum + pixel_count / 2) / pixel_count);

    row = pixels;
    sum = 0;
    for (int y = 0; y < block_height; ++y) {
        for (int x = 0; x < 16; ++x) {
            sum += std::abs(static_cast<int>(row[x]) -
                            static_cast<int>(block_average));
        }
        row += stride;
    }

    *average = block_average;
    return sum;
}

}  // namespace reference

#if defined(MVEC_SIMD_USE_SSE2)

namespace {

int sad16(const unsigned char *p1, const unsigned char *p2) {
    const __m128i left =
        _mm_load_si128(reinterpret_cast<const __m128i *>(p1));
    const __m128i right =
        _mm_loadu_si128(reinterpret_cast<const __m128i *>(p2));
    const __m128i sum = _mm_sad_epu8(left, right);
    return _mm_extract_epi16(sum, 0) + _mm_extract_epi16(sum, 4);
}

}  // namespace

const char *backend_name() {
    return "SSE2";
}

int dist(const unsigned char *p1,
         const unsigned char *p2,
         int stride,
         int dist_limit,
         int block_height) {
    int sum = 0;
    for (int y = 0; y < block_height; ++y) {
        sum += sad16(p1, p2);
        if (block_height != 8 && sum > dist_limit) {
            break;
        }
        p1 += stride;
        p2 += stride;
    }
    return sum;
}

int maxmin_block(const unsigned char *pixels,
                 int stride,
                 int block_height) {
    __m128i minimum =
        _mm_load_si128(reinterpret_cast<const __m128i *>(pixels));
    __m128i maximum = minimum;
    pixels += stride;

    for (int y = 1; y < block_height; ++y) {
        const __m128i row =
            _mm_load_si128(reinterpret_cast<const __m128i *>(pixels));
        minimum = _mm_min_epu8(minimum, row);
        maximum = _mm_max_epu8(maximum, row);
        pixels += stride;
    }

    const __m128i zero = _mm_setzero_si128();
    __m128i high = _mm_unpackhi_epi8(minimum, zero);
    __m128i low = _mm_unpacklo_epi8(minimum, zero);
    minimum = _mm_min_epi16(high, low);
    high = _mm_unpackhi_epi8(maximum, zero);
    low = _mm_unpacklo_epi8(maximum, zero);
    maximum = _mm_max_epi16(high, low);

    high = _mm_unpackhi_epi16(minimum, zero);
    low = _mm_unpacklo_epi16(minimum, zero);
    minimum = _mm_min_epi16(high, low);
    high = _mm_unpackhi_epi16(maximum, zero);
    low = _mm_unpacklo_epi16(maximum, zero);
    maximum = _mm_max_epi16(high, low);

    high = _mm_unpackhi_epi32(minimum, zero);
    low = _mm_unpacklo_epi32(minimum, zero);
    minimum = _mm_min_epi16(high, low);
    high = _mm_unpackhi_epi32(maximum, zero);
    low = _mm_unpacklo_epi32(maximum, zero);
    maximum = _mm_max_epi16(high, low);

    high = _mm_unpackhi_epi64(minimum, zero);
    low = _mm_unpacklo_epi64(minimum, zero);
    minimum = _mm_min_epi16(high, low);
    high = _mm_unpackhi_epi64(maximum, zero);
    low = _mm_unpacklo_epi64(maximum, zero);
    maximum = _mm_max_epi16(high, low);

    return _mm_extract_epi16(maximum, 0) -
           _mm_extract_epi16(minimum, 0);
}

int avgdist(int *average,
            const unsigned char *pixels,
            int stride,
            int block_height) {
    __m128i comparison = _mm_setzero_si128();
    int sum = 0;
    unsigned char block_average = 0;

    for (int pass = 0; pass < 2; ++pass) {
        const unsigned char *row = pixels;
        __m128i result = _mm_setzero_si128();
        for (int y = 0; y < block_height; ++y) {
            const __m128i values =
                _mm_loadu_si128(reinterpret_cast<const __m128i *>(row));
            result = _mm_add_epi32(
                result, _mm_sad_epu8(values, comparison));
            row += stride;
        }
        sum = _mm_extract_epi16(result, 0) +
              _mm_extract_epi16(result, 4);

        if (pass == 0) {
            const int pixel_count = block_height * 16;
            block_average = static_cast<unsigned char>(
                (sum + pixel_count / 2) / pixel_count);
            comparison = _mm_set1_epi8(
                static_cast<char>(block_average));
        }
    }

    *average = block_average;
    return sum;
}

#elif defined(MVEC_SIMD_USE_NEON)

namespace {

int horizontal_sum_u8(uint8x16_t values) {
    const uint16x8_t sum16 = vpaddlq_u8(values);
    const uint32x4_t sum32 = vpaddlq_u16(sum16);
    const uint64x2_t sum64 = vpaddlq_u32(sum32);
    return static_cast<int>(vgetq_lane_u64(sum64, 0) +
                            vgetq_lane_u64(sum64, 1));
}

int sad16(const unsigned char *p1, const unsigned char *p2) {
    const uint8x16_t left = vld1q_u8(p1);
    const uint8x16_t right = vld1q_u8(p2);
    return horizontal_sum_u8(vabdq_u8(left, right));
}

unsigned char horizontal_min_u8(uint8x16_t values) {
    uint8x8_t result =
        vmin_u8(vget_low_u8(values), vget_high_u8(values));
    result = vpmin_u8(result, result);
    result = vpmin_u8(result, result);
    result = vpmin_u8(result, result);
    return vget_lane_u8(result, 0);
}

unsigned char horizontal_max_u8(uint8x16_t values) {
    uint8x8_t result =
        vmax_u8(vget_low_u8(values), vget_high_u8(values));
    result = vpmax_u8(result, result);
    result = vpmax_u8(result, result);
    result = vpmax_u8(result, result);
    return vget_lane_u8(result, 0);
}

}  // namespace

const char *backend_name() {
    return "NEON";
}

int dist(const unsigned char *p1,
         const unsigned char *p2,
         int stride,
         int dist_limit,
         int block_height) {
    int sum = 0;
    for (int y = 0; y < block_height; ++y) {
        sum += sad16(p1, p2);
        if (block_height != 8 && sum > dist_limit) {
            break;
        }
        p1 += stride;
        p2 += stride;
    }
    return sum;
}

int maxmin_block(const unsigned char *pixels,
                 int stride,
                 int block_height) {
    uint8x16_t minimum = vld1q_u8(pixels);
    uint8x16_t maximum = minimum;
    pixels += stride;

    for (int y = 1; y < block_height; ++y) {
        const uint8x16_t row = vld1q_u8(pixels);
        minimum = vminq_u8(minimum, row);
        maximum = vmaxq_u8(maximum, row);
        pixels += stride;
    }

    return static_cast<int>(horizontal_max_u8(maximum)) -
           static_cast<int>(horizontal_min_u8(minimum));
}

int avgdist(int *average,
            const unsigned char *pixels,
            int stride,
            int block_height) {
    uint8x16_t comparison = vdupq_n_u8(0);
    int sum = 0;
    unsigned char block_average = 0;

    for (int pass = 0; pass < 2; ++pass) {
        const unsigned char *row = pixels;
        sum = 0;
        for (int y = 0; y < block_height; ++y) {
            sum += horizontal_sum_u8(
                vabdq_u8(vld1q_u8(row), comparison));
            row += stride;
        }

        if (pass == 0) {
            const int pixel_count = block_height * 16;
            block_average = static_cast<unsigned char>(
                (sum + pixel_count / 2) / pixel_count);
            comparison = vdupq_n_u8(block_average);
        }
    }

    *average = block_average;
    return sum;
}

#else

const char *backend_name() {
    return "scalar";
}

int dist(const unsigned char *p1,
         const unsigned char *p2,
         int stride,
         int dist_limit,
         int block_height) {
    return reference::dist(
        p1, p2, stride, dist_limit, block_height);
}

int maxmin_block(const unsigned char *pixels,
                 int stride,
                 int block_height) {
    return reference::maxmin_block(pixels, stride, block_height);
}

int avgdist(int *average,
            const unsigned char *pixels,
            int stride,
            int block_height) {
    return reference::avgdist(average, pixels, stride, block_height);
}

#endif

}  // namespace mvec_simd
