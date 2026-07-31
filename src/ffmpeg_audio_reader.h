#ifndef CHAPTER_EXE_FFMPEG_AUDIO_READER_H
#define CHAPTER_EXE_FFMPEG_AUDIO_READER_H

#include <algorithm>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/mathematics.h>
#include <libswresample/swresample.h>
}

class FFmpegAudioReader {
private:
    WAVEFORMATEX _format;
    AVFormatContext *_format_context;
    AVCodecContext *_codec_context;
    AVPacket *_packet;
    AVFrame *_frame;
    int _stream_index;
    int _video_stream_index;
    int _video_width;
    int _video_height;
    bool _has_video_stream;
    bool _demux_eof;
    bool _flush_sent;
    bool _resampler_flushed;
    int _decode_warnings;
    int _channels;
    int _sample_rate;
    SwrContext *_swr_context;
    std::vector<int16_t> _fifo;
    size_t _fifo_offset;
    int64_t _front_sample;
    int64_t _timeline_start;
    int64_t _fallback_next_sample;
    int64_t _video_start_time_us;
    int64_t _sample_count;

    static std::runtime_error make_error(
        const char *operation, int error_code) {
        char error_text[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(error_code, error_text, sizeof(error_text));
        return std::runtime_error(
            std::string(operation) + ": " + error_text);
    }

    static bool is_missing_stream_error(int error_code) {
        return error_code == AVERROR_STREAM_NOT_FOUND;
    }

    void report_recoverable_decode_error(int error_code) {
        if (_decode_warnings < 10) {
            char error_text[AV_ERROR_MAX_STRING_SIZE] = {0};
            av_strerror(error_code, error_text, sizeof(error_text));
            fprintf(stderr,
                    "warning: FFmpeg skipped invalid audio data: %s\n",
                    error_text);
            if (_decode_warnings == 9) {
                fprintf(stderr,
                        "warning: further audio decode warnings are suppressed\n");
            }
        }
        ++_decode_warnings;
    }

    int64_t stream_start_in_microseconds(int stream_index) const {
        AVStream *stream = _format_context->streams[stream_index];
        if (stream->start_time != AV_NOPTS_VALUE) {
            return av_rescale_q(
                stream->start_time, stream->time_base, AV_TIME_BASE_Q);
        }
        if (_format_context->start_time != AV_NOPTS_VALUE) {
            return _format_context->start_time;
        }
        return AV_NOPTS_VALUE;
    }

    int64_t estimate_samples(AVStream *stream) const {
        if (stream->duration > 0 && stream->duration != AV_NOPTS_VALUE) {
            return av_rescale_q(
                stream->duration,
                stream->time_base,
                AVRational{1, _sample_rate});
        }
        if (_format_context->duration > 0 &&
            _format_context->duration != AV_NOPTS_VALUE) {
            return av_rescale_q(
                _format_context->duration,
                AV_TIME_BASE_Q,
                AVRational{1, _sample_rate});
        }
        return -1;
    }

    AVChannelLayout normalized_channel_layout(
        const AVChannelLayout &source_layout) const {
        AVChannelLayout layout = {};
        if (source_layout.order == AV_CHANNEL_ORDER_UNSPEC) {
            av_channel_layout_default(&layout, source_layout.nb_channels);
        } else {
            int ret = av_channel_layout_copy(&layout, &source_layout);
            if (ret < 0) {
                throw make_error(
                    "FFmpeg could not copy the audio channel layout", ret);
            }
        }
        return layout;
    }

    void initialize_timeline() {
        _fifo.clear();
        _fifo_offset = 0;
        _front_sample = 0;
        _fallback_next_sample = _timeline_start;
    }

    void configure_audio() {
        AVStream *stream = _format_context->streams[_stream_index];
        _channels = _codec_context->ch_layout.nb_channels;
        _sample_rate = _codec_context->sample_rate;
        if (_channels <= 0 || _sample_rate <= 0) {
            throw std::runtime_error(
                "FFmpeg reported an invalid audio format");
        }

        AVChannelLayout input_layout =
            normalized_channel_layout(_codec_context->ch_layout);
        AVChannelLayout output_layout =
            normalized_channel_layout(input_layout);
        int ret = swr_alloc_set_opts2(
            &_swr_context,
            &output_layout,
            AV_SAMPLE_FMT_S16,
            _sample_rate,
            &input_layout,
            _codec_context->sample_fmt,
            _sample_rate,
            0,
            NULL);
        av_channel_layout_uninit(&input_layout);
        av_channel_layout_uninit(&output_layout);
        if (ret < 0) {
            throw make_error(
                "FFmpeg could not configure audio conversion", ret);
        }

        ret = swr_init(_swr_context);
        if (ret < 0) {
            throw make_error(
                "FFmpeg could not initialize audio conversion", ret);
        }

        memset(&_format, 0, sizeof(_format));
        _format.wFormatTag = WAVE_FORMAT_PCM;
        _format.nChannels = static_cast<unsigned short>(_channels);
        _format.nSamplesPerSec = _sample_rate;
        _format.wBitsPerSample = 16;
        _format.nBlockAlign =
            _format.nChannels * (_format.wBitsPerSample / 8);
        _format.nAvgBytesPerSec =
            _format.nSamplesPerSec * _format.nBlockAlign;

        _sample_count = estimate_samples(stream);
        _timeline_start = 0;
        _video_start_time_us = AV_NOPTS_VALUE;
        if (_has_video_stream) {
            const int64_t video_start =
                stream_start_in_microseconds(_video_stream_index);
            const int64_t audio_start =
                stream_start_in_microseconds(_stream_index);
            _video_start_time_us = video_start;
            if (video_start != AV_NOPTS_VALUE &&
                audio_start != AV_NOPTS_VALUE) {
                _timeline_start = av_rescale_q(
                    audio_start - video_start,
                    AV_TIME_BASE_Q,
                    AVRational{1, _sample_rate});
            }
        }
        initialize_timeline();

        _packet = av_packet_alloc();
        _frame = av_frame_alloc();
        if (_packet == NULL || _frame == NULL) {
            throw std::runtime_error(
                "FFmpeg could not allocate audio decode buffers");
        }
    }

    void append_converted_frame() {
        if (_frame->sample_rate != 0 &&
            _frame->sample_rate != _sample_rate) {
            throw std::runtime_error(
                "FFmpeg audio sample rate changed while decoding");
        }

        const int64_t delay =
            swr_get_delay(_swr_context, _sample_rate);
        const int output_capacity = static_cast<int>(av_rescale_rnd(
            delay + _frame->nb_samples,
            _sample_rate,
            _sample_rate,
            AV_ROUND_UP));
        std::vector<int16_t> converted(
            static_cast<size_t>(output_capacity) * _channels);
        uint8_t *output_data[1] = {
            reinterpret_cast<uint8_t *>(converted.data())};
        const uint8_t **input_data =
            const_cast<const uint8_t **>(_frame->extended_data);

        const int converted_samples = swr_convert(
            _swr_context,
            output_data,
            output_capacity,
            input_data,
            _frame->nb_samples);
        if (converted_samples < 0) {
            throw make_error(
                "FFmpeg audio conversion failed", converted_samples);
        }

        converted.resize(
            static_cast<size_t>(converted_samples) * _channels);
        int64_t frame_start = _fallback_next_sample;
        AVStream *stream = _format_context->streams[_stream_index];
        int64_t frame_timestamp = _frame->best_effort_timestamp;
        if (frame_timestamp == AV_NOPTS_VALUE) {
            frame_timestamp = _frame->pts;
        }
        if (frame_timestamp != AV_NOPTS_VALUE &&
            _video_start_time_us != AV_NOPTS_VALUE) {
            const int64_t frame_time_us = av_rescale_q(
                frame_timestamp, stream->time_base, AV_TIME_BASE_Q);
            frame_start = av_rescale_q(
                frame_time_us - _video_start_time_us,
                AV_TIME_BASE_Q,
                AVRational{1, _sample_rate});
        }
        _fallback_next_sample = frame_start + converted_samples;

        const int64_t current_end =
            _front_sample + available_samples();
        int64_t skip_samples = 0;
        if (frame_start < current_end) {
            skip_samples = std::min<int64_t>(
                current_end - frame_start, converted_samples);
        } else if (frame_start > current_end) {
            _fifo.resize(
                _fifo.size() +
                    static_cast<size_t>(frame_start - current_end) *
                        static_cast<size_t>(_channels),
                0);
        }

        if (skip_samples < converted_samples) {
            _fifo.insert(
                _fifo.end(),
                converted.begin() +
                    static_cast<ptrdiff_t>(
                        skip_samples * _channels),
                converted.end());
        }
    }

    void flush_resampler() {
        if (_resampler_flushed) {
            return;
        }
        _resampler_flushed = true;

        for (;;) {
            const int64_t delay =
                swr_get_delay(_swr_context, _sample_rate);
            if (delay <= 0) {
                break;
            }
            const int output_capacity = static_cast<int>(
                std::min<int64_t>(delay, INT_MAX));
            std::vector<int16_t> converted(
                static_cast<size_t>(output_capacity) * _channels);
            uint8_t *output_data[1] = {
                reinterpret_cast<uint8_t *>(converted.data())};
            const int converted_samples = swr_convert(
                _swr_context,
                output_data,
                output_capacity,
                NULL,
                0);
            if (converted_samples < 0) {
                throw make_error(
                    "FFmpeg audio converter flush failed",
                    converted_samples);
            }
            if (converted_samples == 0) {
                break;
            }
            converted.resize(
                static_cast<size_t>(converted_samples) * _channels);
            _fifo.insert(
                _fifo.end(), converted.begin(), converted.end());
        }
    }

    bool decode_next_frame() {
        av_frame_unref(_frame);
        for (;;) {
            int ret = avcodec_receive_frame(_codec_context, _frame);
            if (ret == 0) {
                append_converted_frame();
                return true;
            }
            if (ret == AVERROR_EOF) {
                flush_resampler();
                return false;
            }
            if (ret == AVERROR_INVALIDDATA) {
                report_recoverable_decode_error(ret);
                continue;
            }
            if (ret != AVERROR(EAGAIN)) {
                throw make_error("FFmpeg audio decoding failed", ret);
            }

            if (_demux_eof) {
                if (_flush_sent) {
                    flush_resampler();
                    return false;
                }
                ret = avcodec_send_packet(_codec_context, NULL);
                if (ret < 0 && ret != AVERROR_EOF) {
                    throw make_error(
                        "FFmpeg audio decoder flush failed", ret);
                }
                _flush_sent = true;
                continue;
            }

            for (;;) {
                ret = av_read_frame(_format_context, _packet);
                if (ret < 0) {
                    if (ret != AVERROR_EOF) {
                        throw make_error(
                            "FFmpeg audio packet reading failed", ret);
                    }
                    _demux_eof = true;
                    break;
                }

                if (_packet->stream_index != _stream_index) {
                    av_packet_unref(_packet);
                    continue;
                }

                ret = avcodec_send_packet(_codec_context, _packet);
                av_packet_unref(_packet);
                if (ret == AVERROR_INVALIDDATA) {
                    report_recoverable_decode_error(ret);
                    continue;
                }
                if (ret < 0) {
                    throw make_error(
                        "FFmpeg audio packet submission failed", ret);
                }
                break;
            }
        }
    }

    int64_t available_samples() const {
        if (_channels <= 0 || _fifo.size() < _fifo_offset) {
            return 0;
        }
        return static_cast<int64_t>(
            (_fifo.size() - _fifo_offset) /
            static_cast<size_t>(_channels));
    }

    bool ensure_through(int64_t end_sample) {
        while (_front_sample + available_samples() < end_sample) {
            if (!decode_next_frame()) {
                return _front_sample + available_samples() >= end_sample;
            }
        }
        return true;
    }

    void compact_fifo() {
        if (_fifo_offset == 0) {
            return;
        }
        if (_fifo_offset < 65536 &&
            _fifo_offset * 2 < _fifo.size()) {
            return;
        }
        _fifo.erase(
            _fifo.begin(),
            _fifo.begin() + static_cast<ptrdiff_t>(_fifo_offset));
        _fifo_offset = 0;
    }

    int64_t discard_samples(int64_t sample_count) {
        const int64_t discarded =
            std::min<int64_t>(sample_count, available_samples());
        _fifo_offset +=
            static_cast<size_t>(discarded) *
            static_cast<size_t>(_channels);
        _front_sample += discarded;
        compact_fifo();
        return discarded;
    }

    void reset_decoder() {
        AVStream *stream = _format_context->streams[_stream_index];
        const int64_t start_time =
            stream->start_time == AV_NOPTS_VALUE ? 0 : stream->start_time;
        int ret = av_seek_frame(
            _format_context,
            _stream_index,
            start_time,
            AVSEEK_FLAG_BACKWARD);
        if (ret < 0) {
            throw make_error(
                "FFmpeg could not rewind the audio stream", ret);
        }
        avcodec_flush_buffers(_codec_context);
        swr_close(_swr_context);
        ret = swr_init(_swr_context);
        if (ret < 0) {
            throw make_error(
                "FFmpeg could not reset audio conversion", ret);
        }
        _demux_eof = false;
        _flush_sent = false;
        _resampler_flushed = false;
        initialize_timeline();
    }

public:
    FFmpegAudioReader()
        : _format(),
          _format_context(NULL),
          _codec_context(NULL),
          _packet(NULL),
          _frame(NULL),
          _stream_index(-1),
          _video_stream_index(-1),
          _video_width(0),
          _video_height(0),
          _has_video_stream(false),
          _demux_eof(false),
          _flush_sent(false),
          _resampler_flushed(false),
          _decode_warnings(0),
          _channels(0),
          _sample_rate(0),
          _swr_context(NULL),
          _fifo_offset(0),
          _front_sample(0),
          _timeline_start(0),
          _fallback_next_sample(0),
          _video_start_time_us(AV_NOPTS_VALUE),
          _sample_count(-1) {
    }

    ~FFmpegAudioReader() {
        swr_free(&_swr_context);
        av_frame_free(&_frame);
        av_packet_free(&_packet);
        avcodec_free_context(&_codec_context);
        avformat_close_input(&_format_context);
    }

    void init(const char *infile) {
        int ret = avformat_open_input(
            &_format_context, infile, NULL, NULL);
        if (ret < 0) {
            throw make_error("FFmpeg could not open input", ret);
        }
        ret = avformat_find_stream_info(_format_context, NULL);
        if (ret < 0) {
            throw make_error(
                "FFmpeg could not read stream information", ret);
        }

        ret = av_find_best_stream(
            _format_context,
            AVMEDIA_TYPE_VIDEO,
            -1,
            -1,
            NULL,
            0);
        if (ret >= 0) {
            AVStream *video_stream = _format_context->streams[ret];
            if ((video_stream->disposition &
                 AV_DISPOSITION_ATTACHED_PIC) == 0) {
                _video_stream_index = ret;
                _video_width = video_stream->codecpar->width;
                _video_height = video_stream->codecpar->height;
                _has_video_stream = true;
            }
        } else if (!is_missing_stream_error(ret)) {
            throw make_error(
                "FFmpeg could not inspect the video stream", ret);
        }

        const AVCodec *decoder = NULL;
        ret = av_find_best_stream(
            _format_context,
            AVMEDIA_TYPE_AUDIO,
            -1,
            -1,
            &decoder,
            0);
        if (is_missing_stream_error(ret)) {
            avformat_close_input(&_format_context);
            return;
        }
        if (ret < 0) {
            throw make_error(
                "FFmpeg could not find an audio decoder", ret);
        }

        _stream_index = ret;
        AVStream *audio_stream =
            _format_context->streams[_stream_index];
        _codec_context = avcodec_alloc_context3(decoder);
        if (_codec_context == NULL) {
            throw std::runtime_error(
                "FFmpeg could not allocate an audio decoder context");
        }
        ret = avcodec_parameters_to_context(
            _codec_context, audio_stream->codecpar);
        if (ret < 0) {
            throw make_error(
                "FFmpeg could not copy audio decoder parameters", ret);
        }
        ret = avcodec_open2(_codec_context, decoder, NULL);
        if (ret < 0) {
            throw make_error(
                "FFmpeg could not open the audio decoder", ret);
        }

        configure_audio();
        fprintf(stderr,
                " -Audio: FFmpeg (%d channels, %d Hz)\n",
                _channels,
                _sample_rate);
    }

    bool has_video_stream() const {
        return _has_video_stream;
    }

    int video_width() const {
        return _video_width;
    }

    int video_height() const {
        return _video_height;
    }

    bool has_audio() const {
        return _stream_index >= 0;
    }

    WAVEFORMATEX *format() {
        return &_format;
    }

    int64_t sample_count() const {
        return _sample_count;
    }

    int read(int frame, int rate, int scale, short *buf) {
        if (!has_audio() || rate <= 0 || scale <= 0 || frame < 0) {
            return 0;
        }

        const int64_t start = av_rescale_rnd(
            frame,
            static_cast<int64_t>(_sample_rate) * scale,
            rate,
            AV_ROUND_DOWN);
        const int64_t end = av_rescale_rnd(
            static_cast<int64_t>(frame) + 1,
            static_cast<int64_t>(_sample_rate) * scale,
            rate,
            AV_ROUND_DOWN);
        if (end <= start) {
            return 0;
        }

        if (start < _front_sample) {
            reset_decoder();
        }

        if (start > _front_sample) {
            ensure_through(start);
            const int64_t samples_to_discard = start - _front_sample;
            if (discard_samples(samples_to_discard) <
                samples_to_discard) {
                return 0;
            }
        }

        ensure_through(end);
        const int64_t requested = end - start;
        const int64_t readable =
            std::min<int64_t>(requested, available_samples());
        if (readable <= 0) {
            return 0;
        }

        const size_t scalar_count =
            static_cast<size_t>(readable) *
            static_cast<size_t>(_channels);
        memcpy(
            buf,
            &_fifo[_fifo_offset],
            scalar_count * sizeof(int16_t));
        discard_samples(readable);
        return static_cast<int>(readable);
    }

private:
    FFmpegAudioReader(const FFmpegAudioReader &);
    FFmpegAudioReader &operator=(const FFmpegAudioReader &);
};

#endif
