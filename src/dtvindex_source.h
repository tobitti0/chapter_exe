#ifndef __DTVINDEX_SOURCE__
#define __DTVINDEX_SOURCE__

#include "dtvindex/dtvindex.hpp"
#include "ffmpeg_audio_reader.h"

#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>

class DtvIndexSource : public NullSource {
private:
    std::string _media_path;
    std::string _index_path;
    dtvindex::Index _index;
    std::unique_ptr<dtvindex::VideoReader> _reader;
    BITMAPINFOHEADER _format;
    FFmpegAudioReader _audio;

public:
    DtvIndexSource()
        : NullSource(),
          _index(),
          _reader(),
          _format(),
          _audio() {
    }

    void init(const char *infile) {
        _media_path = infile;
        _index_path =
            dtvindex::Index::default_index_path(_media_path);
        bool created = false;
        _index = dtvindex::Index::load_or_build(
            _media_path, _index_path, &created);

        const dtvindex::StreamInfo &stream = _index.stream();
        if (stream.width <= 0 || stream.height <= 0 ||
            stream.frame_rate.numerator <= 0 ||
            stream.frame_rate.denominator <= 0) {
            throw std::runtime_error(
                "dtvindex contains invalid video metadata");
        }

        std::int64_t video_start_time_us = AV_NOPTS_VALUE;
        if (!_index.frames().empty()) {
            const dtvindex::FrameRecord &first = _index.frames().front();
            const std::int64_t timestamp =
                (first.flags & dtvindex::kFramePtsValid) != 0
                    ? first.pts
                    : (first.flags & dtvindex::kFrameDtsValid) != 0
                          ? first.dts
                          : AV_NOPTS_VALUE;
            if (timestamp != AV_NOPTS_VALUE) {
                const AVRational time_base = {
                    stream.time_base.numerator,
                    stream.time_base.denominator};
                video_start_time_us = av_rescale_q(
                    timestamp, time_base, AV_TIME_BASE_Q);
            }
        }
        _audio.init(
            infile, stream.stream_index, video_start_time_us);

        memset(&_format, 0, sizeof(_format));
        _format.biSize = sizeof(_format);
        _format.biWidth = stream.width;
        _format.biHeight = stream.height;
        _format.biPlanes = 1;
        _format.biBitCount = 8;

        _ip.flag |= INPUT_INFO_FLAG_VIDEO |
                    INPUT_INFO_FLAG_VIDEO_RANDOM_ACCESS;
        _ip.rate = stream.frame_rate.numerator;
        _ip.scale = stream.frame_rate.denominator;
        _ip.n = static_cast<int>(
            std::min<std::uint64_t>(
                _index.frames().size(), INT_MAX));
        _ip.format = &_format;
        _ip.format_size = sizeof(_format);
        _reader.reset(new dtvindex::VideoReader(
            _media_path, _index));

        fprintf(stderr,
                " -DtvIndexSource: %s %s, stream %d (%d frames)\n",
                created ? "created" : "reused",
                _index_path.c_str(),
                stream.stream_index,
                _ip.n);

        if (_audio.has_audio()) {
            _ip.flag |= INPUT_INFO_FLAG_AUDIO;
            const int64_t sample_count = _audio.sample_count();
            _ip.audio_n =
                sample_count < 0
                    ? -1
                    : static_cast<int>(
                          std::min<int64_t>(sample_count, INT_MAX));
            _ip.audio_format = _audio.format();
            _ip.audio_format_size = sizeof(*_ip.audio_format);
        }
    }

    bool read_video_y8(int frame, unsigned char *luma) {
        if (!_reader.get() || frame < 0 || frame >= _ip.n) {
            return false;
        }

        const dtvindex::DecodedLumaFrame decoded =
            _reader->read_luma(static_cast<std::uint64_t>(frame));
        const int width = _ip.format->biWidth & 0xFFFFFFF0;
        const int height = _ip.format->biHeight & 0xFFFFFFF0;
        if (decoded.width < width || decoded.height < height) {
            throw std::runtime_error(
                "dtvindex returned an undersized video frame");
        }
        for (int y = 0; y < height; ++y) {
            memcpy(
                luma + static_cast<size_t>(y) * width,
                &decoded.pixels[static_cast<size_t>(y) * decoded.width],
                width);
        }
        return true;
    }

    int read_audio(int frame, short *buf) {
        return _audio.read(frame, _ip.rate, _ip.scale, buf);
    }
};

#endif
