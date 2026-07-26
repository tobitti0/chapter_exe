#ifndef __FFMPEG_SOURCE__
#define __FFMPEG_SOURCE__

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstring>
#include <deque>
#include <stdexcept>
#include <string>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/mathematics.h>
#include <libavutil/pixdesc.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

class FFmpegSource : public NullSource {
private:
    struct VideoCacheEntry {
        int index;
        std::vector<unsigned char> pixels;
    };

    std::string _input_path;

    BITMAPINFOHEADER _format;
    WAVEFORMATEX _audio_format;

    AVFormatContext *_video_format_context;
    AVCodecContext *_video_codec_context;
    AVPacket *_video_packet;
    AVFrame *_video_frame;
    int _video_stream_index;
    bool _video_demux_eof;
    bool _video_flush_sent;
    int _video_decode_warnings;
    int64_t _video_decoded_index;
    std::deque<VideoCacheEntry> _video_cache;
    std::vector<unsigned char> _gray_frame;
    SwsContext *_sws_context;

    AVFormatContext *_audio_format_context;
    AVCodecContext *_audio_codec_context;
    AVPacket *_audio_packet;
    AVFrame *_audio_frame;
    int _audio_stream_index;
    bool _audio_demux_eof;
    bool _audio_flush_sent;
    bool _resampler_flushed;
    int _audio_decode_warnings;
    int _audio_channels;
    int _audio_sample_rate;
    SwrContext *_swr_context;
    std::vector<int16_t> _audio_fifo;
    size_t _audio_fifo_offset;
    int64_t _audio_front_sample;
    int64_t _audio_timeline_start;
    int64_t _audio_fallback_next_sample;
    int64_t _video_start_time_us;

    static std::runtime_error make_error(const char *operation, int error_code) {
        char error_text[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(error_code, error_text, sizeof(error_text));
        return std::runtime_error(std::string(operation) + ": " + error_text);
    }

    static bool is_missing_stream_error(int error_code) {
        return error_code == AVERROR_STREAM_NOT_FOUND;
    }

    static void report_recoverable_decode_error(const char *stream_name,
                                                int error_code,
                                                int *warning_count) {
        if (*warning_count < 10) {
            char error_text[AV_ERROR_MAX_STRING_SIZE] = {0};
            av_strerror(error_code, error_text, sizeof(error_text));
            fprintf(stderr,
                    "warning: FFmpeg skipped invalid %s data: %s\n",
                    stream_name,
                    error_text);
            if (*warning_count == 9) {
                fprintf(stderr,
                        "warning: further %s decode warnings are suppressed\n",
                        stream_name);
            }
        }
        ++*warning_count;
    }

    bool open_stream(const char *infile,
                     AVMediaType media_type,
                     AVFormatContext **format_context,
                     AVCodecContext **codec_context,
                     int *stream_index) {
        int ret = avformat_open_input(format_context, infile, NULL, NULL);
        if (ret < 0) {
            throw make_error("FFmpeg could not open input", ret);
        }

        ret = avformat_find_stream_info(*format_context, NULL);
        if (ret < 0) {
            throw make_error("FFmpeg could not read stream information", ret);
        }

        const AVCodec *decoder = NULL;
        ret = av_find_best_stream(*format_context, media_type, -1, -1, &decoder, 0);
        if (is_missing_stream_error(ret)) {
            avformat_close_input(format_context);
            return false;
        }
        if (ret < 0) {
            throw make_error("FFmpeg could not find a decoder", ret);
        }

        *stream_index = ret;
        AVStream *stream = (*format_context)->streams[*stream_index];
        if (media_type == AVMEDIA_TYPE_VIDEO &&
            (stream->disposition & AV_DISPOSITION_ATTACHED_PIC) != 0) {
            avformat_close_input(format_context);
            *stream_index = -1;
            return false;
        }

        *codec_context = avcodec_alloc_context3(decoder);
        if (*codec_context == NULL) {
            throw std::runtime_error("FFmpeg could not allocate a decoder context");
        }

        ret = avcodec_parameters_to_context(*codec_context, stream->codecpar);
        if (ret < 0) {
            throw make_error("FFmpeg could not copy decoder parameters", ret);
        }

        ret = avcodec_open2(*codec_context, decoder, NULL);
        if (ret < 0) {
            throw make_error("FFmpeg could not open the decoder", ret);
        }
        return true;
    }

    int64_t estimate_video_frames(AVStream *stream, AVRational frame_rate) const {
        if (stream->nb_frames > 0) {
            return stream->nb_frames;
        }

        const AVRational frame_duration = av_inv_q(frame_rate);
        if (stream->duration > 0 && stream->duration != AV_NOPTS_VALUE) {
            return av_rescale_q(stream->duration, stream->time_base, frame_duration);
        }
        if (_video_format_context->duration > 0 &&
            _video_format_context->duration != AV_NOPTS_VALUE) {
            return av_rescale_q(_video_format_context->duration,
                                AV_TIME_BASE_Q,
                                frame_duration);
        }
        return 0;
    }

    int64_t estimate_audio_samples(AVStream *stream) const {
        if (stream->duration > 0 && stream->duration != AV_NOPTS_VALUE) {
            return av_rescale_q(stream->duration,
                                stream->time_base,
                                AVRational{1, _audio_sample_rate});
        }
        if (_audio_format_context->duration > 0 &&
            _audio_format_context->duration != AV_NOPTS_VALUE) {
            return av_rescale_q(_audio_format_context->duration,
                                AV_TIME_BASE_Q,
                                AVRational{1, _audio_sample_rate});
        }
        return -1;
    }

    int64_t stream_start_in_microseconds(AVFormatContext *format_context,
                                         int stream_index) const {
        AVStream *stream = format_context->streams[stream_index];
        if (stream->start_time != AV_NOPTS_VALUE) {
            return av_rescale_q(stream->start_time,
                                stream->time_base,
                                AV_TIME_BASE_Q);
        }
        if (format_context->start_time != AV_NOPTS_VALUE) {
            return format_context->start_time;
        }
        return AV_NOPTS_VALUE;
    }

    void initialize_audio_timeline() {
        _audio_fifo.clear();
        _audio_fifo_offset = 0;
        _audio_front_sample = 0;
        _audio_fallback_next_sample = _audio_timeline_start;
    }

    void configure_video_info() {
        AVStream *stream = _video_format_context->streams[_video_stream_index];
        AVRational frame_rate =
            av_guess_frame_rate(_video_format_context, stream, NULL);
        if (frame_rate.num <= 0 || frame_rate.den <= 0) {
            frame_rate = stream->avg_frame_rate;
        }
        if (frame_rate.num <= 0 || frame_rate.den <= 0) {
            frame_rate = stream->r_frame_rate;
        }
        if (frame_rate.num <= 0 || frame_rate.den <= 0) {
            throw std::runtime_error("FFmpeg could not determine the video frame rate");
        }

        int64_t frame_count = estimate_video_frames(stream, frame_rate);
        if (frame_count <= 0) {
            throw std::runtime_error(
                "FFmpeg could not determine the video frame count");
        }

        memset(&_format, 0, sizeof(_format));
        _format.biSize = sizeof(_format);
        _format.biWidth = _video_codec_context->width;
        _format.biHeight = _video_codec_context->height;
        _format.biPlanes = 1;
        _format.biBitCount = 8;

        _ip.flag |= INPUT_INFO_FLAG_VIDEO |
                    INPUT_INFO_FLAG_VIDEO_RANDOM_ACCESS;
        _ip.rate = frame_rate.num;
        _ip.scale = frame_rate.den;
        _ip.n = (int)std::min<int64_t>(frame_count, INT_MAX);
        _ip.format = &_format;
        _ip.format_size = sizeof(_format);

        _video_packet = av_packet_alloc();
        _video_frame = av_frame_alloc();
        if (_video_packet == NULL || _video_frame == NULL) {
            throw std::runtime_error(
                "FFmpeg could not allocate video decode buffers");
        }
    }

    AVChannelLayout normalized_channel_layout(
        const AVChannelLayout &source_layout) const {
        AVChannelLayout layout = {};
        if (source_layout.order == AV_CHANNEL_ORDER_UNSPEC) {
            av_channel_layout_default(&layout, source_layout.nb_channels);
        } else {
            int ret = av_channel_layout_copy(&layout, &source_layout);
            if (ret < 0) {
                throw make_error("FFmpeg could not copy the audio channel layout",
                                 ret);
            }
        }
        return layout;
    }

    void configure_audio_info() {
        AVStream *stream = _audio_format_context->streams[_audio_stream_index];
        _audio_channels = _audio_codec_context->ch_layout.nb_channels;
        _audio_sample_rate = _audio_codec_context->sample_rate;
        if (_audio_channels <= 0 || _audio_sample_rate <= 0) {
            throw std::runtime_error("FFmpeg reported an invalid audio format");
        }

        AVChannelLayout input_layout =
            normalized_channel_layout(_audio_codec_context->ch_layout);
        AVChannelLayout output_layout = normalized_channel_layout(input_layout);
        int ret = swr_alloc_set_opts2(&_swr_context,
                                      &output_layout,
                                      AV_SAMPLE_FMT_S16,
                                      _audio_sample_rate,
                                      &input_layout,
                                      _audio_codec_context->sample_fmt,
                                      _audio_sample_rate,
                                      0,
                                      NULL);
        av_channel_layout_uninit(&input_layout);
        av_channel_layout_uninit(&output_layout);
        if (ret < 0) {
            throw make_error("FFmpeg could not configure audio conversion", ret);
        }

        ret = swr_init(_swr_context);
        if (ret < 0) {
            throw make_error("FFmpeg could not initialize audio conversion", ret);
        }

        memset(&_audio_format, 0, sizeof(_audio_format));
        _audio_format.wFormatTag = WAVE_FORMAT_PCM;
        _audio_format.nChannels = (unsigned short)_audio_channels;
        _audio_format.nSamplesPerSec = _audio_sample_rate;
        _audio_format.wBitsPerSample = 16;
        _audio_format.nBlockAlign =
            _audio_format.nChannels * (_audio_format.wBitsPerSample / 8);
        _audio_format.nAvgBytesPerSec =
            _audio_format.nSamplesPerSec * _audio_format.nBlockAlign;

        int64_t sample_count = estimate_audio_samples(stream);
        _ip.flag |= INPUT_INFO_FLAG_AUDIO;
        _ip.audio_n =
            sample_count < 0 ? -1 : (int)std::min<int64_t>(sample_count, INT_MAX);
        _ip.audio_format = &_audio_format;
        _ip.audio_format_size = sizeof(_audio_format);

        _audio_timeline_start = 0;
        _video_start_time_us = AV_NOPTS_VALUE;
        if (_video_format_context != NULL && _video_stream_index >= 0) {
            int64_t video_start = stream_start_in_microseconds(
                _video_format_context, _video_stream_index);
            int64_t audio_start = stream_start_in_microseconds(
                _audio_format_context, _audio_stream_index);
            _video_start_time_us = video_start;
            if (video_start != AV_NOPTS_VALUE &&
                audio_start != AV_NOPTS_VALUE) {
                _audio_timeline_start = av_rescale_q(
                    audio_start - video_start,
                    AV_TIME_BASE_Q,
                    AVRational{1, _audio_sample_rate});
            }
        }
        initialize_audio_timeline();

        _audio_packet = av_packet_alloc();
        _audio_frame = av_frame_alloc();
        if (_audio_packet == NULL || _audio_frame == NULL) {
            throw std::runtime_error(
                "FFmpeg could not allocate audio decode buffers");
        }
    }

    bool decode_next_video_frame() {
        av_frame_unref(_video_frame);
        for (;;) {
            int ret = avcodec_receive_frame(_video_codec_context, _video_frame);
            if (ret == 0) {
                return true;
            }
            if (ret == AVERROR_EOF) {
                return false;
            }
            if (ret == AVERROR_INVALIDDATA) {
                report_recoverable_decode_error(
                    "video", ret, &_video_decode_warnings);
                continue;
            }
            if (ret != AVERROR(EAGAIN)) {
                throw make_error("FFmpeg video decoding failed", ret);
            }

            if (_video_demux_eof) {
                if (_video_flush_sent) {
                    return false;
                }
                ret = avcodec_send_packet(_video_codec_context, NULL);
                if (ret < 0 && ret != AVERROR_EOF) {
                    throw make_error("FFmpeg video decoder flush failed", ret);
                }
                _video_flush_sent = true;
                continue;
            }

            for (;;) {
                ret = av_read_frame(_video_format_context, _video_packet);
                if (ret < 0) {
                    if (ret != AVERROR_EOF) {
                        throw make_error("FFmpeg video packet reading failed",
                                         ret);
                    }
                    _video_demux_eof = true;
                    break;
                }

                if (_video_packet->stream_index != _video_stream_index) {
                    av_packet_unref(_video_packet);
                    continue;
                }

                ret = avcodec_send_packet(_video_codec_context, _video_packet);
                av_packet_unref(_video_packet);
                if (ret == AVERROR_INVALIDDATA) {
                    report_recoverable_decode_error(
                        "video", ret, &_video_decode_warnings);
                    continue;
                }
                if (ret < 0) {
                    throw make_error("FFmpeg video packet submission failed",
                                     ret);
                }
                break;
            }
        }
    }

    void reset_video_decoder() {
        AVStream *stream =
            _video_format_context->streams[_video_stream_index];
        int64_t start_time =
            stream->start_time == AV_NOPTS_VALUE ? 0 : stream->start_time;
        int ret = av_seek_frame(_video_format_context,
                                _video_stream_index,
                                start_time,
                                AVSEEK_FLAG_BACKWARD);
        if (ret < 0) {
            throw make_error("FFmpeg could not rewind the video stream", ret);
        }
        avcodec_flush_buffers(_video_codec_context);
        _video_demux_eof = false;
        _video_flush_sent = false;
        _video_decoded_index = -1;
        _video_cache.clear();
    }

    void convert_video_frame(std::vector<unsigned char> &output) {
        const int output_width = _format.biWidth & 0xFFFFFFF0;
        const int output_height = _format.biHeight & 0xFFFFFFF0;
        if (_video_frame->width < output_width ||
            _video_frame->height < output_height) {
            throw std::runtime_error(
                "FFmpeg video dimensions changed while decoding");
        }

        output.resize((size_t)output_width * output_height);
        const AVPixelFormat pixel_format =
            (AVPixelFormat)_video_frame->format;
        const AVPixFmtDescriptor *descriptor =
            av_pix_fmt_desc_get(pixel_format);
        const bool direct_luma =
            descriptor != NULL &&
            (descriptor->flags &
             (AV_PIX_FMT_FLAG_RGB | AV_PIX_FMT_FLAG_PAL |
              AV_PIX_FMT_FLAG_BAYER | AV_PIX_FMT_FLAG_HWACCEL)) == 0 &&
            descriptor->nb_components > 0 &&
            descriptor->comp[0].plane == 0 &&
            descriptor->comp[0].step == 1 &&
            descriptor->comp[0].offset == 0 &&
            descriptor->comp[0].depth == 8;

        if (direct_luma) {
            for (int y = 0; y < output_height; ++y) {
                memcpy(&output[(size_t)y * output_width],
                       _video_frame->data[0] +
                           (ptrdiff_t)y * _video_frame->linesize[0],
                       output_width);
            }
            return;
        }

        _sws_context = sws_getCachedContext(_sws_context,
                                            _video_frame->width,
                                            _video_frame->height,
                                            pixel_format,
                                            _video_frame->width,
                                            _video_frame->height,
                                            AV_PIX_FMT_GRAY8,
                                            SWS_BILINEAR,
                                            NULL,
                                            NULL,
                                            NULL);
        if (_sws_context == NULL) {
            throw std::runtime_error(
                "FFmpeg could not create the video conversion context");
        }

        _gray_frame.resize(
            (size_t)_video_frame->width * _video_frame->height);
        uint8_t *destination_data[4] = {_gray_frame.data(), NULL, NULL, NULL};
        int destination_linesize[4] = {
            _video_frame->width, 0, 0, 0};
        int ret = sws_scale(_sws_context,
                            _video_frame->data,
                            _video_frame->linesize,
                            0,
                            _video_frame->height,
                            destination_data,
                            destination_linesize);
        if (ret != _video_frame->height) {
            throw std::runtime_error("FFmpeg video conversion failed");
        }

        for (int y = 0; y < output_height; ++y) {
            memcpy(&output[(size_t)y * output_width],
                   &_gray_frame[(size_t)y * _video_frame->width],
                   output_width);
        }
    }

    void append_converted_audio_frame() {
        if (_audio_frame->sample_rate != 0 &&
            _audio_frame->sample_rate != _audio_sample_rate) {
            throw std::runtime_error(
                "FFmpeg audio sample rate changed while decoding");
        }

        int64_t delay = swr_get_delay(_swr_context, _audio_sample_rate);
        int output_capacity = (int)av_rescale_rnd(
            delay + _audio_frame->nb_samples,
            _audio_sample_rate,
            _audio_sample_rate,
            AV_ROUND_UP);
        std::vector<int16_t> converted(
            (size_t)output_capacity * _audio_channels);
        uint8_t *output_data[1] = {
            reinterpret_cast<uint8_t *>(converted.data())};
        const uint8_t **input_data =
            const_cast<const uint8_t **>(_audio_frame->extended_data);

        int converted_samples = swr_convert(_swr_context,
                                            output_data,
                                            output_capacity,
                                            input_data,
                                            _audio_frame->nb_samples);
        if (converted_samples < 0) {
            throw make_error("FFmpeg audio conversion failed",
                             converted_samples);
        }

        converted.resize((size_t)converted_samples * _audio_channels);
        int64_t frame_start = _audio_fallback_next_sample;
        AVStream *stream =
            _audio_format_context->streams[_audio_stream_index];
        int64_t frame_timestamp = _audio_frame->best_effort_timestamp;
        if (frame_timestamp == AV_NOPTS_VALUE) {
            frame_timestamp = _audio_frame->pts;
        }
        if (frame_timestamp != AV_NOPTS_VALUE &&
            _video_start_time_us != AV_NOPTS_VALUE) {
            int64_t frame_time_us = av_rescale_q(
                frame_timestamp, stream->time_base, AV_TIME_BASE_Q);
            frame_start = av_rescale_q(
                frame_time_us - _video_start_time_us,
                AV_TIME_BASE_Q,
                AVRational{1, _audio_sample_rate});
        }
        _audio_fallback_next_sample = frame_start + converted_samples;

        int64_t current_end =
            _audio_front_sample + available_audio_samples();
        int64_t skip_samples = 0;
        if (frame_start < current_end) {
            skip_samples = std::min<int64_t>(
                current_end - frame_start, converted_samples);
        } else if (frame_start > current_end) {
            _audio_fifo.resize(
                _audio_fifo.size() +
                    (size_t)(frame_start - current_end) *
                        (size_t)_audio_channels,
                0);
        }

        if (skip_samples < converted_samples) {
            _audio_fifo.insert(
                _audio_fifo.end(),
                converted.begin() +
                    (ptrdiff_t)(skip_samples * _audio_channels),
                converted.end());
        }
    }

    void flush_resampler() {
        if (_resampler_flushed) {
            return;
        }
        _resampler_flushed = true;

        for (;;) {
            int64_t delay = swr_get_delay(_swr_context, _audio_sample_rate);
            if (delay <= 0) {
                break;
            }
            int output_capacity =
                (int)std::min<int64_t>(delay, INT_MAX);
            std::vector<int16_t> converted(
                (size_t)output_capacity * _audio_channels);
            uint8_t *output_data[1] = {
                reinterpret_cast<uint8_t *>(converted.data())};
            int converted_samples = swr_convert(_swr_context,
                                                output_data,
                                                output_capacity,
                                                NULL,
                                                0);
            if (converted_samples < 0) {
                throw make_error("FFmpeg audio converter flush failed",
                                 converted_samples);
            }
            if (converted_samples == 0) {
                break;
            }
            converted.resize((size_t)converted_samples * _audio_channels);
            _audio_fifo.insert(_audio_fifo.end(),
                               converted.begin(),
                               converted.end());
        }
    }

    bool decode_next_audio_frame() {
        av_frame_unref(_audio_frame);
        for (;;) {
            int ret = avcodec_receive_frame(_audio_codec_context, _audio_frame);
            if (ret == 0) {
                append_converted_audio_frame();
                return true;
            }
            if (ret == AVERROR_EOF) {
                flush_resampler();
                return false;
            }
            if (ret == AVERROR_INVALIDDATA) {
                report_recoverable_decode_error(
                    "audio", ret, &_audio_decode_warnings);
                continue;
            }
            if (ret != AVERROR(EAGAIN)) {
                throw make_error("FFmpeg audio decoding failed", ret);
            }

            if (_audio_demux_eof) {
                if (_audio_flush_sent) {
                    flush_resampler();
                    return false;
                }
                ret = avcodec_send_packet(_audio_codec_context, NULL);
                if (ret < 0 && ret != AVERROR_EOF) {
                    throw make_error("FFmpeg audio decoder flush failed", ret);
                }
                _audio_flush_sent = true;
                continue;
            }

            for (;;) {
                ret = av_read_frame(_audio_format_context, _audio_packet);
                if (ret < 0) {
                    if (ret != AVERROR_EOF) {
                        throw make_error("FFmpeg audio packet reading failed",
                                         ret);
                    }
                    _audio_demux_eof = true;
                    break;
                }

                if (_audio_packet->stream_index != _audio_stream_index) {
                    av_packet_unref(_audio_packet);
                    continue;
                }

                ret = avcodec_send_packet(_audio_codec_context, _audio_packet);
                av_packet_unref(_audio_packet);
                if (ret == AVERROR_INVALIDDATA) {
                    report_recoverable_decode_error(
                        "audio", ret, &_audio_decode_warnings);
                    continue;
                }
                if (ret < 0) {
                    throw make_error("FFmpeg audio packet submission failed",
                                     ret);
                }
                break;
            }
        }
    }

    int64_t available_audio_samples() const {
        if (_audio_channels <= 0 ||
            _audio_fifo.size() < _audio_fifo_offset) {
            return 0;
        }
        return (int64_t)((_audio_fifo.size() - _audio_fifo_offset) /
                         (size_t)_audio_channels);
    }

    bool ensure_audio_through(int64_t end_sample) {
        while (_audio_front_sample + available_audio_samples() < end_sample) {
            if (!decode_next_audio_frame()) {
                return _audio_front_sample + available_audio_samples() >=
                       end_sample;
            }
        }
        return true;
    }

    void compact_audio_fifo() {
        if (_audio_fifo_offset == 0) {
            return;
        }
        if (_audio_fifo_offset < 65536 &&
            _audio_fifo_offset * 2 < _audio_fifo.size()) {
            return;
        }
        _audio_fifo.erase(
            _audio_fifo.begin(),
            _audio_fifo.begin() + (ptrdiff_t)_audio_fifo_offset);
        _audio_fifo_offset = 0;
    }

    int64_t discard_audio_samples(int64_t sample_count) {
        int64_t discarded =
            std::min<int64_t>(sample_count, available_audio_samples());
        _audio_fifo_offset +=
            (size_t)discarded * (size_t)_audio_channels;
        _audio_front_sample += discarded;
        compact_audio_fifo();
        return discarded;
    }

    void reset_audio_decoder() {
        AVStream *stream =
            _audio_format_context->streams[_audio_stream_index];
        int64_t start_time =
            stream->start_time == AV_NOPTS_VALUE ? 0 : stream->start_time;
        int ret = av_seek_frame(_audio_format_context,
                                _audio_stream_index,
                                start_time,
                                AVSEEK_FLAG_BACKWARD);
        if (ret < 0) {
            throw make_error("FFmpeg could not rewind the audio stream", ret);
        }
        avcodec_flush_buffers(_audio_codec_context);
        swr_close(_swr_context);
        ret = swr_init(_swr_context);
        if (ret < 0) {
            throw make_error("FFmpeg could not reset audio conversion", ret);
        }
        _audio_demux_eof = false;
        _audio_flush_sent = false;
        _resampler_flushed = false;
        initialize_audio_timeline();
    }

public:
    FFmpegSource()
        : NullSource(),
          _format(),
          _audio_format(),
          _video_format_context(NULL),
          _video_codec_context(NULL),
          _video_packet(NULL),
          _video_frame(NULL),
          _video_stream_index(-1),
          _video_demux_eof(false),
          _video_flush_sent(false),
          _video_decode_warnings(0),
          _video_decoded_index(-1),
          _sws_context(NULL),
          _audio_format_context(NULL),
          _audio_codec_context(NULL),
          _audio_packet(NULL),
          _audio_frame(NULL),
          _audio_stream_index(-1),
          _audio_demux_eof(false),
          _audio_flush_sent(false),
          _resampler_flushed(false),
          _audio_decode_warnings(0),
          _audio_channels(0),
          _audio_sample_rate(0),
          _swr_context(NULL),
          _audio_fifo_offset(0),
          _audio_front_sample(0),
          _audio_timeline_start(0),
          _audio_fallback_next_sample(0),
          _video_start_time_us(AV_NOPTS_VALUE) {
    }

    ~FFmpegSource() {
        sws_freeContext(_sws_context);
        swr_free(&_swr_context);

        av_frame_free(&_video_frame);
        av_packet_free(&_video_packet);
        avcodec_free_context(&_video_codec_context);
        avformat_close_input(&_video_format_context);

        av_frame_free(&_audio_frame);
        av_packet_free(&_audio_packet);
        avcodec_free_context(&_audio_codec_context);
        avformat_close_input(&_audio_format_context);
    }

    void init(const char *infile) {
        _input_path = infile;
        printf(" -FFmpegSource\n");

        bool has_video_stream = open_stream(infile,
                                            AVMEDIA_TYPE_VIDEO,
                                            &_video_format_context,
                                            &_video_codec_context,
                                            &_video_stream_index);
        if (has_video_stream) {
            configure_video_info();
        }

        bool has_audio_stream = open_stream(infile,
                                            AVMEDIA_TYPE_AUDIO,
                                            &_audio_format_context,
                                            &_audio_codec_context,
                                            &_audio_stream_index);
        if (has_audio_stream) {
            configure_audio_info();
        }

        if (!has_video_stream && !has_audio_stream) {
            throw std::runtime_error(
                "FFmpeg input contains neither video nor audio");
        }
    }

    bool read_video_y8(int frame, unsigned char *luma) {
        if (!has_video() || frame < 0 || frame >= _ip.n) {
            return false;
        }

        const int width = _format.biWidth & 0xFFFFFFF0;
        const int height = _format.biHeight & 0xFFFFFFF0;
        const size_t output_size = (size_t)width * height;
        for (std::deque<VideoCacheEntry>::reverse_iterator entry =
                 _video_cache.rbegin();
             entry != _video_cache.rend();
             ++entry) {
            if (entry->index == frame &&
                entry->pixels.size() == output_size) {
                memcpy(luma, entry->pixels.data(), output_size);
                return true;
            }
        }

        if (frame <= _video_decoded_index) {
            reset_video_decoder();
        }

        while (_video_decoded_index < frame) {
            if (!decode_next_video_frame()) {
                if (_video_decoded_index < 0 ||
                    _video_frame->data[0] == NULL) {
                    return false;
                }
                // Match LWLibavVideoSource(repeat=true) at a short stream
                // tail: retain the last decoded image through the declared
                // CFR duration.
                _video_decoded_index = frame;
                break;
            }
            ++_video_decoded_index;
        }

        _video_cache.push_back(VideoCacheEntry());
        VideoCacheEntry &entry = _video_cache.back();
        entry.index = frame;
        convert_video_frame(entry.pixels);
        while (_video_cache.size() > 32) {
            _video_cache.pop_front();
        }
        memcpy(luma, entry.pixels.data(), output_size);
        return true;
    }

    int read_audio(int frame, short *buf) {
        if (!has_audio() || _ip.rate <= 0 || _ip.scale <= 0 || frame < 0) {
            return 0;
        }

        int64_t start = av_rescale_rnd(
            frame,
            (int64_t)_audio_sample_rate * _ip.scale,
            _ip.rate,
            AV_ROUND_DOWN);
        int64_t end = av_rescale_rnd(
            (int64_t)frame + 1,
            (int64_t)_audio_sample_rate * _ip.scale,
            _ip.rate,
            AV_ROUND_DOWN);
        if (end <= start) {
            return 0;
        }

        if (start < _audio_front_sample) {
            reset_audio_decoder();
        }

        if (start > _audio_front_sample) {
            ensure_audio_through(start);
            int64_t samples_to_discard = start - _audio_front_sample;
            if (discard_audio_samples(samples_to_discard) <
                samples_to_discard) {
                return 0;
            }
        }

        ensure_audio_through(end);
        int64_t requested = end - start;
        int64_t readable =
            std::min<int64_t>(requested, available_audio_samples());
        if (readable <= 0) {
            return 0;
        }

        size_t scalar_count =
            (size_t)readable * (size_t)_audio_channels;
        memcpy(buf,
               &_audio_fifo[_audio_fifo_offset],
               scalar_count * sizeof(int16_t));
        discard_audio_samples(readable);
        return (int)readable;
    }
};

#endif
