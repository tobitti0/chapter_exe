#ifndef __DTVINDEX_SOURCE__
#define __DTVINDEX_SOURCE__

#include "dtvindex/dtvindex.hpp"
#include "ffmpeg_source.h"

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>

class DtvIndexSource : public FFmpegSource {
private:
    std::string _media_path;
    std::string _index_path;
    dtvindex::Index _index;
    std::unique_ptr<dtvindex::VideoReader> _reader;

public:
    DtvIndexSource()
        : FFmpegSource(), _index(), _reader() {
    }

    void init(const char *infile) {
        FFmpegSource::init(infile);
        if (!has_video()) {
            return;
        }

        _media_path = infile;
        _index_path = dtvindex::Index::default_index_path(_media_path);
        bool created = false;
        _index = dtvindex::Index::load_or_build(
            _media_path, _index_path, &created);

        const dtvindex::StreamInfo &stream = _index.stream();
        if (stream.width != _ip.format->biWidth ||
            stream.height != _ip.format->biHeight) {
            throw std::runtime_error(
                "dtvindex dimensions do not match FFmpegSource");
        }
        if (stream.frame_rate.numerator <= 0 ||
            stream.frame_rate.denominator <= 0) {
            throw std::runtime_error(
                "dtvindex does not contain a usable frame rate");
        }

        _ip.rate = stream.frame_rate.numerator;
        _ip.scale = stream.frame_rate.denominator;
        _ip.n = static_cast<int>(
            std::min<std::uint64_t>(_index.frames().size(), INT_MAX));
        _reader.reset(new dtvindex::VideoReader(
            _media_path, _index));

        printf(" -DtvIndexSource: %s %s (%d frames)\n",
               created ? "created" : "reused",
               _index_path.c_str(),
               _ip.n);
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
};

#endif
