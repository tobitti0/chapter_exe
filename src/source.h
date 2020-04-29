// 入力クラス
#ifndef __SOURCE__
#define __SOURCE__

#undef UNICODE
#ifdef _WIN32
  #include <windows.h>
#else
  #include <dlfcn.h>
  #include <limits.h>

  #define LoadLibrary(x) dlopen(x, RTLD_NOW | RTLD_LOCAL)
  #define GetProcAddress dlsym
  #define FreeLibrary dlclose
#endif
#include <string>
#include <algorithm>
#include <cstdio>
#include <string.h>
#include "input.h"

using namespace std;

class Source {
public:
	virtual int add_ref() = 0;
	virtual int release() = 0;

	virtual void init(char *infile) = 0;

	virtual bool has_video() = 0;
	virtual bool has_audio() = 0;
	virtual INPUT_INFO &get_input_info() = 0;
	virtual void set_rate(int rate, int scale) = 0;

	virtual bool read_video_y8(int frame, unsigned char *luma) = 0;
	virtual int read_audio(int frame, short *buf) = 0;
};

// 空のソース
class NullSource : public Source {
protected:
	NullSource() : _ref(1) { memset(&_ip, 0, sizeof(_ip)); }
	virtual ~NullSource() { }

	INPUT_INFO _ip;
	int _ref;
public:

	int add_ref() { return ++_ref; }
	int release() { int r = --_ref; if (r <= 0) delete this; return r; }

	bool has_video() { return (_ip.flag & INPUT_INFO_FLAG_VIDEO) != 0; };
	bool has_audio() { return (_ip.flag & INPUT_INFO_FLAG_AUDIO) != 0; };
	INPUT_INFO &get_input_info() { return _ip; };
	void set_rate(int rate, int scale) {
		_ip.rate = rate;
		_ip.scale = scale;
	}

	// must implement
	void init(char *infile) { };
	bool read_video_y8(int frame, unsigned char *luma) { return false; };
	int read_audio(int frame, short *buf) { return 0; };
};

#ifdef _WIN32
typedef INPUT_PLUGIN_TABLE* (__stdcall  *GET_PLUGIN_TABLE)(void);
#else
typedef INPUT_PLUGIN_TABLE* (*GET_PLUGIN_TABLE)(void);
#endif

// auiを使ったソース
class AuiSource : public NullSource {
protected:
	string _in, _plugin;

	void* _dll;

	INPUT_PLUGIN_TABLE *_ipt;
	INPUT_HANDLE _ih;
	//INPUT_INFO _ip;

public:
	AuiSource(void) : NullSource(), _dll(NULL) { }
	virtual ~AuiSource() {
		if (_dll) {
			FreeLibrary(_dll);
		}
	}

	virtual void init(const char *infile) {
		_in = infile;
		_plugin = "avsinp.aui";

		int p = _in.find("://");
		if (p != _in.npos) {
			_plugin = _in.substr(0, p);
			_in = _in.substr(p+3);
		}

		printf(" -%s\n", _plugin.c_str());

		_dll = LoadLibrary(_plugin.c_str());
		if (_dll == NULL) {
			throw "   plugin loading failed.";
		}

		GET_PLUGIN_TABLE f = (GET_PLUGIN_TABLE)GetProcAddress(_dll, "GetInputPluginTable");
		if (f == NULL) {
			throw "   not Aviutl input plugin error.";
		}
		_ipt = (INPUT_PLUGIN_TABLE*)f();
		if (_ipt == NULL) {
			throw "   not Aviutl input plugin error.";
		}
		if (_ipt->func_init) {
			if (_ipt->func_init() == false) {
				throw "   func_init() failed.";
			}
		}

		_ih = _ipt->func_open((char*)_in.c_str());
		if (_ih == NULL) {
			throw "   func_open() failed.";
		}

		if (_ipt->func_info_get(_ih, &_ip) == false) {
			throw "   func_info_get() failed...";
		}
	}

	bool has_video() {
		return (_ip.flag & INPUT_INFO_FLAG_VIDEO) != 0;
	}
	bool has_audio() {
		return (_ip.flag & INPUT_INFO_FLAG_AUDIO) != 0;
	}

	INPUT_INFO &get_input_info() {
		return _ip;
	}

	bool read_video_y8(int frame, unsigned char *luma) {
		int h = _ip.format->biHeight;
		int w = _ip.format->biWidth;
		unsigned char *buf = (unsigned char *)malloc(2 * h * w);

		int ret = _ipt->func_read_video(_ih, frame, buf);
		if (ret == 0) {
			return false;
		}

		int skip_w = w & 0x0F;
		w = w - skip_w;

		unsigned char *p = buf;
		for (int i=0; i<w; i++) {
			for (int j=0; j<h; j++) {
				*luma = *p;

				luma++;
				p += 2;
			}
			p += skip_w * 2;
		}
		free(buf);
		return true;
	}

	int read_audio(int frame, short *buf) {
		int start = (int)((double)frame * _ip.audio_format->nSamplesPerSec / _ip.rate * _ip.scale);
		int end = (int)((double)(frame + 1) * _ip.audio_format->nSamplesPerSec / _ip.rate * _ip.scale);
		return _ipt->func_read_audio(_ih, start, end - start, buf);
	}
};

// *.wavソース
class WavSource : public NullSource {
	string _in;

	FILE *_f;
	int64_t _start;
	int64_t _end;
	WAVEFORMATEX _fmt;

public:
	WavSource() : NullSource(), _f(NULL), _start(0) { }
	~WavSource() {
		if (_f) {
			fclose(_f);
		}
	}

	void init(const char *infile) {
		printf(" -WavSource\n");
		_f = fopen(infile, "rb");
		if (_f == NULL) {
			throw "   wav open failed.";
		}

		char buf[1000];
		if (fread(buf, 1, 4, _f) != 4 || strncmp(buf, "RIFF", 4) != 0) {
			throw "   no RIFF header.";
		}
		fseek(_f, 4, SEEK_CUR);
		if (fread(buf, 1, 4, _f) != 4 || strncmp(buf, "WAVE", 4) != 0) {
			throw "   no WAVE header.";
		}

		// chunk
		while(fread(buf, 1, 4, _f) == 4) {
			if (ftell(_f) > 1000000) {
				break;				
			}

			int size = 0;
			fread(&size, 4, 1, _f);
			if (strncmp(buf, "fmt ", 4) == 0) {
				if (fread(&_fmt, min(size, (int)sizeof(_fmt)), 1, _f) != 1) {
					throw "   illegal WAVE file.";
				}
				if (_fmt.wFormatTag != WAVE_FORMAT_PCM) {
					throw "   only PCM supported.";
				}
				int diff = size - sizeof(_fmt);
				if (diff > 0) {
					fseek(_f, size - sizeof(_fmt), SEEK_CUR);
				}
			} else if (strncmp(buf, "data", 4) == 0){
				fseek(_f, 4, SEEK_CUR);
#ifdef _WIN32
				_start = _ftelli64(_f);
#else
				_start = ftello(_f);
#endif
				break;
			} else {
				fseek(_f, size, SEEK_CUR);
			}
		}
		if (_start == 0) {
			fclose(_f);
			throw "   maybe not wav file.";
		}

		memset(&_ip, 0, sizeof(_ip));
		_ip.flag |= INPUT_INFO_FLAG_AUDIO;
		_ip.audio_format = &_fmt;
		_ip.audio_format_size = sizeof(_fmt);
		_ip.audio_n = -1;
	}

	int read_audio(int frame, short *buf) {
		int64_t start = (int)((double)frame * _ip.audio_format->nSamplesPerSec / _ip.rate * _ip.scale);
		int64_t end = (int)((double)(frame + 1) * _ip.audio_format->nSamplesPerSec / _ip.rate * _ip.scale);

#ifdef _WIN32
		_fseeki64(_f, _start + start * _fmt.nBlockAlign, SEEK_SET);
#else
		fseeko(_f, _start + start * _fmt.nBlockAlign, SEEK_SET);
#endif

		return fread(buf, _fmt.nBlockAlign, (size_t)(end - start), _f);
	}
};


// *.avsソース
#include "avs_internal.c"

class AvsSource : public NullSource {
protected:
	avs_hnd_t avs_h;
	AVS_VideoInfo *inf;

  BITMAPINFOHEADER format;
  WAVEFORMATEX audio_format;

public:
  AvsSource(void) 
    : NullSource()
    , avs_h({0})
    , format()
    , audio_format()
  {}
	~AvsSource() {
    if(avs_h.library)
        internal_avs_close_library(&avs_h);
	}

  virtual void init(const char *infile) {
		int interlaced = 0;
    int tff = 0;
		if(internal_avs_load_library(&avs_h) < 0) {
        throw "error: failed to load avisynth.dll";
    }

    avs_h.env = avs_h.func.avs_create_script_environment(AVS_INTERFACE_25);
    if(avs_h.func.avs_get_error) {
        const char *error = avs_h.func.avs_get_error(avs_h.env);
        if(error) {
            throw error;
        }
    }

    AVS_Value arg = avs_new_value_string(infile);
    AVS_Value res = avs_h.func.avs_invoke(avs_h.env, "Import", arg, NULL);
    if(avs_is_error(res)) {
        throw avs_as_string(res);
    }
		/* check if the user is using a multi-threaded script and apply distributor if necessary.
		adapted from avisynth's vfw interface */
    AVS_Value mt_test = avs_h.func.avs_invoke(avs_h.env, "GetMTMode", avs_new_value_bool(0), NULL);
    int mt_mode = avs_is_int(mt_test) ? avs_as_int(mt_test) : 0;
    avs_h.func.avs_release_value(mt_test);
    if( mt_mode > 0 && mt_mode < 5 ) {
        AVS_Value temp = avs_h.func.avs_invoke(avs_h.env, "Distributor", res, NULL);
        avs_h.func.avs_release_value(res);
        res = temp;
    }
    if(!avs_is_clip(res)) {
        throw "error: inputfile didn't return a video clip";
    }
    avs_h.clip = avs_h.func.avs_take_clip(res, avs_h.env);
    inf = avs_h.func.avs_get_video_info(avs_h.clip);
    if(!avs_has_video(inf)) {
        throw "error: inputfile has no video data";
    }
    /* if the clip is made of fields instead of frames, call weave to make them frames */
    if(avs_is_field_based(inf)) {
        fprintf(stderr, "detected fieldbased (separated) input, weaving to frames\n");
        AVS_Value tmp = avs_h.func.avs_invoke(avs_h.env, "Weave", res, NULL);
        if(avs_is_error(tmp)) {
            throw "error: couldn't weave fields into frames";
        }
        res = internal_avs_update_clip(&avs_h, &inf, tmp, res);
        interlaced = 1;
        tff = avs_is_tff(inf);
    }

    if( avs_is_planar(inf) == false )
    {
			fprintf(stderr, "converting input clip to Y420\n");

			const char *arg_name[2] = {NULL, "interlaced"};
			AVS_Value arg_arr[2] = {res, avs_new_value_bool(interlaced)};
			AVS_Value tmp = avs_h.func.avs_invoke(avs_h.env, "ConvertToY420", avs_new_value_array(arg_arr, 2), arg_name);
			if(avs_is_error(tmp)) {
					throw "error: couldn't convert input clip to Y420";
			}
			res = internal_avs_update_clip(&avs_h, &inf, tmp, res);
    }
    if (inf->num_audio_samples > 0 && avs_bytes_per_channel_sample(inf) != 2) {
      fprintf(stderr, "converting input clip to 16bit audio\n");

			AVS_Value tmp = avs_h.func.avs_invoke(avs_h.env, "ConvertAudioTo16bit", res, NULL);
			if(avs_is_error(tmp)) {
					throw "error: couldn't convert input clip to 16bit audio";
			}
			res = internal_avs_update_clip(&avs_h, &inf, tmp, res);
    }

    _ip.flag = INPUT_INFO_FLAG_VIDEO_RANDOM_ACCESS | INPUT_INFO_FLAG_VIDEO;
    if (inf->num_audio_samples > 0) {
        _ip.flag |= INPUT_INFO_FLAG_AUDIO;
    }
    _ip.rate = inf->fps_numerator;
    _ip.scale = inf->fps_denominator;
    _ip.n = inf->num_frames;
    _ip.format = &format;
    format.biHeight = inf->height;
    format.biWidth = inf->width;
    // 48kHzで12時間を超えるとINT_MAXを越えてしまうが表示にしか使っていないのでOK
    _ip.audio_n = (int)min((INT64)INT_MAX, inf->num_audio_samples);
    _ip.audio_format = &audio_format;
    audio_format.nChannels = inf->nchannels;
    audio_format.nSamplesPerSec = inf->audio_samples_per_second;

    avs_h.func.avs_release_value(res);
  }

  bool has_video() {
    return (_ip.flag & INPUT_INFO_FLAG_VIDEO) != 0;
  }
  bool has_audio() {
    return (_ip.flag & INPUT_INFO_FLAG_AUDIO) != 0;
  }

  INPUT_INFO &get_input_info() {
    return _ip;
  }

  bool read_video_y8(int frame, unsigned char *luma) {
    AVS_VideoFrame *f = avs_h.func.avs_get_frame(avs_h.clip, frame);
    const char *err = avs_h.func.avs_clip_get_error(avs_h.clip);
    if(err) {
      throw err;
    }
    static const int planes[] = {AVS_PLANAR_Y, AVS_PLANAR_U, AVS_PLANAR_V};
    int pitch = avs_get_pitch_p(f, planes[0]);
    const unsigned char* data = avs_get_read_ptr_p(f, planes[0]);

    int w = _ip.format->biWidth & 0xFFFFFFF0;
    int h = _ip.format->biHeight & 0xFFFFFFF0;

		//avs_h.func.avs_bit_blt(avs_h.env, luma, w, data, pitch, w, h);
    for (int i=0; i<h; i++) {
      const unsigned char* p = data + pitch*i;
			for (int j=0; j<w; j++) {
				*luma = *p;

				luma++;
				p ++;
			}
		}
    avs_h.func.avs_release_video_frame(f);
    return true;
  }

  int read_audio(int frame, short *buf) {
    int64_t start = (int64_t)((double)frame * _ip.audio_format->nSamplesPerSec / _ip.rate * _ip.scale);
		int64_t end = (int64_t)((double)(frame + 1) * _ip.audio_format->nSamplesPerSec / _ip.rate * _ip.scale);
    if(end >= inf->num_audio_samples){
			return 0;
		}
    avs_h.func.avs_get_audio(avs_h.clip, buf, start, end - start);
    const char *err = avs_h.func.avs_clip_get_error(avs_h.clip);
    if(err) {
      throw err;
    }

    return int(end - start);
  }
};

#endif
