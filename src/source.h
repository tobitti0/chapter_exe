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
#include <avisynth.h>
#include <stdio.h>
#include <dlfcn.h>

const AVS_Linkage *AVS_linkage = nullptr;

class AvsSource : public NullSource {
protected:
  VideoInfo inf;
  IScriptEnvironment *env;
  PClip clip;
  BITMAPINFOHEADER format;
  WAVEFORMATEX audio_format;

public:
  AvsSource(void) 
    : NullSource()
    , format()
    , audio_format()
  {}

  virtual void init(const char *infile) {
    int interlaced = 0;
    int tff = 0;
    if (env) env->DeleteScriptEnvironment();
  
    void *handle = dlopen("libavisynth.so", RTLD_LAZY);
    if (handle == NULL) {
      fprintf(stdout, "Cannot load libavisynth.so\r\n");
    }
  
    typedef IScriptEnvironment * (* func_t)(int);
    void *mkr = dlsym(handle, "CreateScriptEnvironment");
    if(mkr == NULL) {
      fprintf(stdout, "Cannot find CreateScriptEnvironment\r\n");
    }
  
    func_t CreateScriptEnvironment = (func_t)mkr;
    env = CreateScriptEnvironment(AVISYNTH_INTERFACE_VERSION);

    AVS_linkage = env->GetAVSLinkage(); // e.g. for VideoInfo.BitsPerComponent, etc..

    try {
      AVSValue arg = infile;
      AVSValue res = env->Invoke("Import", arg);
      int mt_mode = res.IsInt() ? res.AsInt() : 0;
      if( mt_mode > 0 && mt_mode < 5 ) {
        AVSValue temp = env->Invoke("Distributor", res);
        // need release old res
        res = temp;
      }
      if(!res.IsClip()){
        throw "error: inputfile didn't return a video clip";
      }
  
      clip = res.AsClip();
      inf = clip->GetVideoInfo();
  
      
      if(!inf.HasVideo()){
        throw "error: inputfile has no video data";
      }
      /* if the clip is made of fields instead of frames, call weave to make them frames */
      if(inf.IsFieldBased()){
      	fprintf(stderr, "detected fieldbased (separated) input, weaving to frames\n");
        throw "error: couldn't weave fields into frames";
        //AVSValue tmp = env->Invoke("Weave", res);
        // need release old res
        //res = tmp;
        //interlaced = 1;
        //tff = inf.IsTFF();
      }
      
      if(inf.IsPlanar()==false){
        fprintf(stderr, "converting input clip to Y420\n");
        //char *arg_name[2] = {NULL, "interlaced"};
        //AVSValue arg_arr[2] = {res, bool(interlaced)};
        //AVSValue tmp = env->Invoke("ConvertToY420", (arg_arr, 2), arg_name);
        throw "error: input file isn't Y420";
      }
  
      if(inf.num_audio_samples > 0 && inf.BytesPerChannelSample() !=2){
        AVSValue tmp = env->Invoke("ConvertAudioTo16bit", res);
        res = tmp;
      	clip = res.AsClip();
      	inf = clip->GetVideoInfo();
	fprintf(stderr, "converting input clip to 16bit audio\n");
      }
    }
    catch (const AvisynthError &err) {
      fprintf(stdout,"Avisynth ERROR: %s\r\n", err.msg);
    }
    _ip.flag = INPUT_INFO_FLAG_VIDEO_RANDOM_ACCESS | INPUT_INFO_FLAG_VIDEO;
    if (inf.num_audio_samples > 0) {
        _ip.flag |= INPUT_INFO_FLAG_AUDIO;
    }
    _ip.rate = inf.fps_numerator;
    _ip.scale = inf.fps_denominator;
    _ip.n = inf.num_frames;
    _ip.format = &format;
    format.biHeight = inf.height;
    format.biWidth = inf.width;
    // 48kHzで12時間を超えるとINT_MAXを越えてしまうが表示にしか使っていないのでOK
    _ip.audio_n = (int)min((int64_t)INT_MAX, inf.num_audio_samples);
    _ip.audio_format = &audio_format;
    audio_format.nChannels = inf.nchannels;
    audio_format.nSamplesPerSec = inf.audio_samples_per_second;

    //env_->DeleteScriptEnvironment();
    //fprintf(stdout,"Avisynth Script Environment deleted\r\n");
    
    //dlclose(handle);
    //fprintf(stdout,"Ready to exit\r\n");
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
    PVideoFrame f = clip->GetFrame(frame,env); 
    static const int planes[] = {PLANAR_Y, PLANAR_U, PLANAR_V};
    int pitch = f->GetPitch(planes[0]);
    const unsigned char* data = f->GetReadPtr(planes[0]);

    int w = _ip.format->biWidth & 0xFFFFFFF0;
    int h = _ip.format->biHeight & 0xFFFFFFF0;

    //avs_h.func.avs_bit_blt(avs_h.env, luma, w, data, pitch, w, h);
    //env->BitBlt(luma, w, data, pitch, w, h);
    for (int i=0; i<h; i++) {
      const unsigned char* p = data + pitch*i;
			for (int j=0; j<w; j++) {
				*luma = *p;

				luma++;
				p ++;
			}
		}
    return true;
  }

  int read_audio(int frame, short *buf) {
    int64_t start = (int64_t)((double)frame * _ip.audio_format->nSamplesPerSec / _ip.rate * _ip.scale);
		int64_t end = (int64_t)((double)(frame + 1) * _ip.audio_format->nSamplesPerSec / _ip.rate * _ip.scale);
    if(end >= inf.num_audio_samples){
			return 0;
		}
    clip->GetAudio(buf, start, end - start, env);

    return int(end - start);
  }
};

#endif
