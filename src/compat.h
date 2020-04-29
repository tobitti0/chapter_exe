#ifndef __COMPAT__
#define __COMPAT__
#ifndef _WIN32
typedef struct
{
    uint32_t  biSize;
    int32_t   biWidth;
    int32_t   biHeight;
    unsigned short biPlanes;
    unsigned short biBitCount;
    uint32_t  biCompression;
    uint32_t  biSizeImage;
    int32_t   biXPelsPerMeter;
    int32_t   biYPelsPerMeter;
    uint32_t  biClrUsed;
    uint32_t  biClrImportant;
} BITMAPINFOHEADER;

typedef struct
{
    unsigned short        wFormatTag;
    unsigned short        nChannels;
    uint32_t       nSamplesPerSec;
    uint32_t       nAvgBytesPerSec;
    unsigned short        nBlockAlign;
    unsigned short        wBitsPerSample;
    unsigned short        cbSize;
} WAVEFORMATEX;

#define WAVE_FORMAT_PCM	0x0001
#endif
#endif