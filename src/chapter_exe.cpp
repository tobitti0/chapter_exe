// chapter_exe.cpp : コンソール アプリケーションのエントリ ポイントを定義します。
//

#include "stdafx.h"
#include "source.h"
#include "faw.h"

// mvec.c
#define FRAME_PICTURE	1
#define FIELD_PICTURE	2
int mvec(int *mvec1,int *mvec2,int *flag_sc,unsigned char* current_pix,unsigned char* bef_pix,int lx,int ly,int threshold,int pict_struct, int nframe);

// １回の無音期間内に保持する最大シーンチェンジ数
#define DEF_SCMAX 100

int proc_scene_change(
	Source *video,int *lastmute_scpos,int *lastmute_marker,FILE *fout,unsigned char *pix0,unsigned char *pix1,
	int w,int h,int start_fr,int seri,int setseri,int breakmute,int extendmute,int debug,int idx);


// 通常の出力
void write_chapter(FILE *f, int nchap, int frame, TCHAR *title, INPUT_INFO *iip) {
	LONGLONG t,h,m;
	double s;

	t = (LONGLONG)frame * 10000000 * iip->scale / iip->rate;
	h = t / 36000000000;
	m = (t - h * 36000000000) / 600000000;
	s = (t - h * 36000000000 - m * 600000000) / 10000000.0;

	fprintf(f, "CHAPTER%02d=%02d:%02d:%06.3f\n", nchap, (int)h, (int)m, s);
	fprintf(f, "CHAPTER%02dNAME=%s\n", nchap, title);
	fflush(f);
}
// 解析用の出力
void write_chapter_debug(FILE *f, int nchap, int frame, TCHAR *title, INPUT_INFO *iip) {
	LONGLONG t,h,m;
	double s;

	t = (LONGLONG)frame * 10000000 * iip->scale / iip->rate;
	h = t / 36000000000;
	m = (t - h * 36000000000) / 600000000;
	s = (t - h * 36000000000 - m * 600000000) / 10000000.0;

	fprintf(f, "CHAPTER%02d=%02d:%02d:%06.3f from:%d\n", nchap, (int)h, (int)m, s, frame);   // add "from:%d" for debug
//	fprintf(f, "CHAPTER%02d=%02d:%02d:%06.3f\n", nchap, (int)h, (int)m, s);
	fprintf(f, "CHAPTER%02dNAME=%s\n", nchap, title);
	fflush(f);
}

int _tmain(int argc, _TCHAR* argv[])
{
	// メモリリークチェック
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);

	printf(_T("chapter.auf pre loading program.\n"));
	printf(_T("usage:\n"));
	printf(_T("\tchapter_exe.exe -v input_avs -o output_txt\n"));
	printf(_T("params:\n\t-v 入力画像ファイル\n\t-a 入力音声ファイル（省略時は動画と同じファイル）\n\t-m 無音判定閾値（1～2^15)\n\t-s 最低無音フレーム数\n\t-b 無音シーン検索間隔数\n"));
	printf(_T("\t-e 無音前後検索拡張フレーム数\n"));

	TCHAR *avsv = NULL;
	TCHAR *avsa = NULL;
	TCHAR *out =  NULL;
	short setmute = 50;
	int setseri = 10;
	int breakmute = 60;
	int extendmute = 1;
	int thin_audio_read = 1;
	int debug = 0;

	for(int i=1; i<argc-1; i++) {
		char *s	= argv[i];
		if (s[0] == '-') {
			switch(s[1]) {
			case 'v':
				avsv = argv[i+1];
				if (strlen(s) > 2 && s[2] == 'a') {
					avsa = argv[i+1];
				}
				i++;
				break;
			case 'a':
				avsa = argv[i+1];
				i++;
				break;
			case 'o':
				out = argv[i+1];
				i++;
				break;
			case 'm':
				setmute = atoi(argv[i+1]);
				i++;
				break;
			case 's':
				setseri = atoi(argv[i+1]);
				i++;
				break;
			case 'b':
				breakmute = atoi(argv[i+1]);
				i++;
				break;
			case 'e':
				extendmute = atoi(argv[i+1]);
				i++;
				break;
			case '-':
				if (strcmp(&s[2], _T("debug")) == 0){
					debug = 1;
				}
				else if (strcmp(&s[2], _T("thin")) == 0){
					thin_audio_read = 2;
				}
				else if (strcmp(&s[2], _T("serial")) == 0){
					thin_audio_read = -1;
				}
				break;
			default:
				printf("error: unknown param: %s\n", s);
				break;
			}
		} else {
			printf("error: unknown param: %s\n", s);
		}
	}

	// 音声入力が無い場合は動画内にあると仮定
	if (avsa == NULL) {
		avsa = avsv;
	}

	if (out == NULL) {
		printf("error: no output file path!");
		return -1;
	}

	printf(_T("Setting\n"));
	printf(_T("\tvideo: %s\n\taudio: %s\n\tout: %s\n"), avsv, (strcmp(avsv, avsa) ? avsa : "(within video source)"), out);
	printf(_T("\tmute: %d seri: %d bmute: %d emute: %d\n"), setmute, setseri, breakmute, extendmute);

	printf("Loading plugins.\n");

	Source *video = NULL;
	Source *audio = NULL;
	try {
		AuiSource *srcv = new AuiSource();
		srcv->init(avsv);
		if (srcv->has_video() == false) {
			srcv->release();
			throw "Error: No Video Found!";
		}
		video = srcv;
		// 同じソースの場合は同じインスタンスで読み込む
		if (strcmp(avsv, avsa) == 0 && srcv->has_audio()) {
			audio = srcv;
			audio->add_ref();
		}

		// 音声が別ファイルの時
		if (audio == NULL) {
			if (strlen(avsa) > 4 && _stricmp(".wav", avsa + strlen(avsa) - 4) == 0) {
				// wav
				WavSource *wav = new WavSource();
				wav->init(avsa);
				if (wav->has_audio()) {
					audio = wav;
					audio->set_rate(video->get_input_info().rate, video->get_input_info().scale);
				} else {
					wav->release();
				}
			} else {
				// aui
				AuiSource *aud = new AuiSource();
				aud->init(avsa);
				if (aud->has_audio()) {
					audio = aud;
					audio->set_rate(video->get_input_info().rate, video->get_input_info().scale);
				} else {
					aud->release();
				}
			}
		}

		if (audio == NULL) {
			throw "Error: No Audio!";
		}
	} catch(char *s) {
		if (video) {
			video->release();
		}
		printf("%s\n", s);
		return -1;
	}

	// 音声がlwinput.auiだった場合は連続読み出しが早く安定するので間引きをせず読み込む
	if (strstr(avsa, "lwinput.aui://") != NULL){
		if (thin_audio_read == 1){
			thin_audio_read = 0;
		}
	}

	FILE *fout;
	if (fopen_s(&fout, out, "w") != 0) {
		printf("Error: output file open failed.");
		video->release();
		audio->release();
		return -1;
	}

	INPUT_INFO &vii = video->get_input_info();
	INPUT_INFO &aii = audio->get_input_info();

	printf(_T("Movie data\n"));
	printf(_T("\tVideo Frames: %d [%.02ffps]\n"), vii.n, (double)vii.rate / vii.scale);
	DWORD fcc = vii.handler;
	printf(_T("\tVideo Format: %c%c%c%c\n"), fcc & 0xFF, fcc >> 8 & 0xFF, fcc >> 16 & 0xFF, fcc >> 24);

	printf(_T("\tAudio Samples: %d [%dHz]\n"), aii.audio_n, aii.audio_format->nSamplesPerSec);

	if (fcc == 0x32424752 || fcc == 0x38344359) {
		printf(_T("Error: Unsupported color RGB/YC48."));
	}

	if (fcc != 0x32595559) {
		printf(_T("warning: only YUY2 is supported. continues...\n"));
		//return -1;
	}

	short buf[4800*2]; // 10fps以上
	int n = vii.n;

	// FAW check
	do {
		CFAW cfaw;
		int faws = 0;

		for (int i=0; i<min(90, n); i++) {
			int naudio = audio->read_audio(i, buf);
			int j = cfaw.findFAW(buf, naudio);
			if (j != -1) {
				cfaw.decodeFAW(buf+j, naudio-j, buf); // test decode
				faws++;
			}
		}
		if (faws > 5) {
			if (cfaw.isLoadFailed()) {
				printf("  Error: FAW detected, but no FAWPreview.auf.\n");
			} else {
				printf("  FAW detected.\n");
				audio = new FAWDecoder(audio);
			}
		}
	} while(0);

	if (thin_audio_read <= 0){
		printf("read audio : serial\n");
	}
	printf(_T("--------\nStart searching...\n"));

	short mute = setmute;
	int seri = 0;
	int cnt_mute = 0;
	int idx = 1;
	int volume;
	int lastmute_scpos = -1;			// -eオプションの検索オーバーラップを考慮して前回位置保持
	int lastmute_marker = -1;			// マーク表示用の起点位置保持

	// start searching
	for (int i=0; i<n; i++) {
		// searching foward frame
		if (seri == 0 && thin_audio_read > 0) {		// 間引きしながら無音確認
			int naudio = audio->read_audio(i+setseri-1, buf);

			bool skip = false;
			for (int j=0; j<naudio; ++j) {
				volume = abs(buf[j]);
				//if (abs(buf[j]) > mute) {
				if (volume > mute) {
					skip = true;
					break;
				}
			}
			if (skip) {
				i += setseri;
			}
		}

		bool nomute = false;
		int naudio = audio->read_audio(i, buf);

		for (int j=0; j<naudio; ++j) {
			volume = abs(buf[j]);
			//if (abs(buf[j]) > mute) {
			if (volume > mute) {
				nomute = true;
				break;
			}
		}

		//
		if (nomute || i == n-1) {
			// owata
			if (seri >= setseri) {
				int start_fr = i - seri;

				printf(_T("mute%2d: %d - %dフレーム\n"), idx, start_fr, seri);

				int w = vii.format->biWidth & 0xFFFFFFF0;
				int h = vii.format->biHeight & 0xFFFFFFF0;
				unsigned char *pix0 = (unsigned char*)_aligned_malloc(1920*1088, 32);
				unsigned char *pix1 = (unsigned char*)_aligned_malloc(1920*1088, 32);

				//--- 区間内のシーンチェンジを取得 ---
				proc_scene_change(video, &lastmute_scpos, &lastmute_marker, fout, pix0, pix1, w, h,
									start_fr, seri, setseri, breakmute, extendmute, debug, idx);


				idx++;

				_aligned_free(pix0);
				_aligned_free(pix1);
			}
			seri = 0;
		} else {
			seri++;
		}
	}

	// 最終フレーム番号を出力（改造版で追加）
	fprintf(fout, "# SCPos:%d %d\n", n-1, n-1);

	// ソースを解放
	video->release();
	audio->release();

	return 0;
}


// 区間内のシーンチェンジを取得・出力
int proc_scene_change(
	Source *video,					// 画像クラス
	int *lastmute_scpos,			// -eオプションの検索オーバーラップを考慮して前回位置保持（上書き更新）
	int *lastmute_marker,			// マーク表示用の起点位置保持（上書き更新）
	FILE *fout,						// 出力ファイル
	unsigned char *pix0,			// 画像データ保持バッファ（１枚目）
	unsigned char *pix1,			// 画像データ保持バッファ（２枚目）
	int w,							// 画像幅
	int h,							// 画像高さ
	int start_fr,					// 開始フレーム番号
	int seri,						// 無音区間フレーム数
	int setseri,					// 最低無音フレーム数
	int breakmute,					// 無音シーン検索間隔フレーム数
	int extendmute,					// 無音前後検索拡張フレーム数
	int debug,						// デバッグ情報表示する時は1
	int idx							// 無音区間通算番号
){
	const int space_sc1  = 5;		// シーンチェンジ間の最低フレーム間隔
	const int space_sc2 = 10;		// シーンチェンジ間の最低フレーム間隔（位置上書きになる場合用１）
	const int space_sc3 = 30;		// シーンチェンジ間の最低フレーム間隔（位置上書きになる場合用２）
	const int THRES_RATE1 = 300;	// 明確なシーンチェンジ判定用の閾値
	const int THRES_RATE2 = 7;		// 変化なし判定用の閾値
	INPUT_INFO &vii = video->get_input_info();
	int n = vii.n;					// フレーム数
	int ncount_sc = 0;				// シーンチェンジ数カウント

	//--- シーンチェンジ情報初期化 ---
	int msel = 0;					// 何番目のシーンチェンジか（0-）
	int d_max_en[DEF_SCMAX];		// シーンチェンジの有効性
	int d_max_flagsc[DEF_SCMAX];	// シーンチェンジ判定フラグ
	int d_max_pos[DEF_SCMAX];		// シーンチェンジ地点フレーム番号
	int d_max_mvec[DEF_SCMAX];		// シーンチェンジ地点動き情報
	int d_maxp_mvec[DEF_SCMAX];		// シーンチェンジ１フレーム前動き情報
	int d_maxn_mvec[DEF_SCMAX];		// シーンチェンジ１フレーム後動き情報
	int d_max_mvec2[DEF_SCMAX];		// シーンチェンジ地点インターレース用動き情報
	int d_maxp_mvec2[DEF_SCMAX];	// シーンチェンジ１フレーム前インターレース用動き情報
	int d_maxn_mvec2[DEF_SCMAX];	// シーンチェンジ１フレーム後インターレース用動き情報
	int d_max_scrate[DEF_SCMAX];	// シーンチェンジ地点のシーンチェンジ判定値（0-100）

	for(int k=0; k<DEF_SCMAX; k++){
		d_max_en[k]     = 0;
		d_max_flagsc[k] = 0;
	}

	//--- 個別シーンチェンジ位置情報取得 ---
	{
		//--- 位置情報設定 ---
		int range_start_fr = start_fr - extendmute - 1;			// 計算開始フレーム
		int valid_start_fr = start_fr - extendmute;				// 検索開始フレーム
		int range_end_fr   = start_fr + seri + extendmute + 1;	// 計算終了フレーム
		int valid_end_fr   = start_fr + seri + extendmute;		// 検索終了フレーム
		if (range_start_fr < 0){
			range_start_fr = 0;
		}
		if (valid_start_fr < 0){
			valid_start_fr = 0;
		}
		if (range_end_fr >= n){
			range_end_fr = n-1;
		}
		if (valid_end_fr >= n){
			valid_end_fr = n-1;
		}

		//--- 前回位置情報取得 ---
		int last_fr = range_start_fr - 1;
		if (last_fr < 0){
			last_fr = 0;
		}
		video->read_video_y8(last_fr, pix0);

		//--- ローカル変数 ---
		int local_end_fr;				// シーンチェンジ確定フレーム位置
		int local_cntsc;				// 指定期間内シーンチェンジ回数
		int pos_lastchange;				// 前回変化位置
		int skip_update;				// 連続フレームシーンチェンジ無視用
		int cmvec;						// インターレースの動き多い側取得用
		int cmvec2;						// インターレースの動き少ない側取得用
		int rate_sc;					// シーンチェンジ判定値（flag_scの元となる値）
		int flag_sc;					// シーンチェンジ判定フラグ
		int flag_sc_hold = 0;			// 保持シーンチェンジ判定フラグ
		int last_cmvec  = 0;			// 前フレームの動き情報記憶用
		int last_cmvec2 = 0;			// 前フレームのインターレース用動き情報記憶用
		int last_rate = 0;				// 直前のシーンチェンジ判定領域値
		int keep_schk = 0;				// 直前シーンチェンジの保持観察フラグ
		int keep_msel = 0;				// 直前シーンチェンジの保持位置

		//--- 検索開始前設定 ---
		local_cntsc = 0;						// 指定期間内シーンチェンジ回数
		local_end_fr = start_fr + breakmute;	// 次のシーンチェンジ確定フレーム指定
		pos_lastchange = -space_sc1;			// 前回変化位置
		if (*lastmute_scpos > 0){				// 前回シーンチェンジが存在する場合
			pos_lastchange = *lastmute_scpos;	// シーンチェンジ直後は間隔をあけるため
		}

		//--- 各フレーム画像データからシーンチェンジ情報を取得 ---
		for (int x=range_start_fr; x<=range_end_fr; x++) {
			//--- データ取得 ---
			video->read_video_y8(x, pix1);
			rate_sc = mvec( &cmvec, &cmvec2, &flag_sc, pix1, pix0, w, h, (100-0)*(100/FIELD_PICTURE), FIELD_PICTURE, x);
			if (d_max_en[msel] > 0){
				if (x == d_max_pos[msel]+1){			// シーンチェンジ１フレーム後の動き情報更新
					d_maxn_mvec[msel]  = cmvec;
					d_maxn_mvec2[msel] = cmvec2;
				}
			}
			//--- シーンチェンジ格納処理 ---
			if (msel < DEF_SCMAX-1){							// 配列が埋まっていないこと前提
				if (flag_sc_hold > 0){							// シーンチェンジ検出切り替え地点
					if (local_cntsc < 1){						// 現地点を残してさらに候補追加可能な時
						local_cntsc ++;
						msel ++;
						flag_sc_hold = 0;
					}
				}
				if (x >= local_end_fr){			// 指定期間無音が続いた場合
					if (local_cntsc == 0 && d_max_en[msel] > 0){	// シーンチェンジがなかったら確定
						msel ++;
					}
					else if (flag_sc_hold > 0){		// シーンチェンジ確定
						msel ++;
					}
					else{
						d_max_en[msel] = 0;
					}
					local_end_fr += breakmute;	// 次のシーンチェンジ確定フレーム指定
					local_cntsc = 0;			// 指定期間内シーンチェンジ回数
					flag_sc_hold = 0;
				}
			}
			//--- シーンチェンジ判定処理 ---
			if (x < valid_start_fr || x > valid_end_fr){		// 検索範囲外
			}
			else{
				//--- シーンチェンジ上書きになる場合の実行を判別 ---
				if (flag_sc > 0 && d_max_flagsc[msel] > 0){			// シーンチェンジありで上書きになる場合
//					printf("overwrite %d,%d,%d -> %d,%d,%d\n", d_max_pos[msel], d_max_mvec[msel], d_max_scrate[msel], x, cmvec, rate_sc);
					if (abs(x - d_max_pos[msel]) <= space_sc2){ 	// 上書き前の位置から間隔が短い場合
						if (d_max_scrate[msel] >= THRES_RATE1 ||	// 前回地点の画面転換割合が大きい場合無効化
							rate_sc < THRES_RATE1 ||				// 今回地点の画面転換割合が小さい場合無効化
							last_rate * 2 > rate_sc){				// 前画面から変化が大きくない場合無効化
							flag_sc = 0;
						}
					}
					if (abs(x - d_max_pos[msel]) <= space_sc3){		// 上書き前の位置から間隔が指定内の場合
						if (d_max_scrate[msel] > rate_sc ||			// 前回より画面転換割合が小さい場合無効化
							last_rate * 2 > rate_sc){				// 前画面から変化が大きくない場合無効化
							flag_sc = 0;
						}
					}
				}
//if (x > 50108 && x < 50118){
//printf("(%d %d %d %d %d)\n", x, flag_sc, rate_sc, last_rate, pos_lastchange);
//}
				//--- シーンチェンジ直後の再シーンチェンジ実行を判別 ---
				// 原則シーンチェンジ検出直後は連続で保持しないよう間隔をあける
				skip_update = 0;
				if (abs(x - pos_lastchange) <= space_sc1 &&
					x - pos_lastchange != 0){					// 前回シーンチェンジ付近のフレームは
					skip_update = 1;							// 連続で保持しない（標準設定）
					if (keep_schk > 0){							// 差し替え可能性から観察必要時
						if (flag_sc > 0 &&						// 今回もシーンチェンジ
							rate_sc >= THRES_RATE1 &&			// 画面転換割合が大きい
							d_max_scrate[keep_msel] * 2 <= rate_sc &&	// 前回地点よりはるかに変化大
							last_rate * 3 <= rate_sc){			// 前画面よりはるかに変化大
							// 上記条件時は例外としてシーンチェンジ設定を行う（上書き設定）
							skip_update = 0;
							if (msel != keep_msel){				// 設定位置が変わっていたら戻す
								msel = keep_msel;
								if (local_cntsc > 0){			// カウントも増えていたら戻す
									local_cntsc --;
								}
							}
						}
					}
				}
				else{											// シーンチェンジから離れたら
					keep_schk = 0;								// 観察終了
				}
				//--- シーンチェンジ更新処理 ---
				if (skip_update == 0){
					int flag_rateup = 0;
					if ((d_max_scrate[msel] < rate_sc && rate_sc >= THRES_RATE2) ||
						(d_max_scrate[msel] == rate_sc && d_max_mvec[msel] < cmvec) ||
						(d_max_scrate[msel] < THRES_RATE2 &&
						 rate_sc < THRES_RATE2 && d_max_mvec[msel] < cmvec)){
						flag_rateup = 1;
					}
					if ((d_max_flagsc[msel] == 0 && flag_rateup > 0)||
							  d_max_en[msel] == 0 || flag_sc > 0) {		// シーンチェンジ地点更新
						d_max_en[msel]     = 1;
						d_max_pos[msel]    = x;
						d_max_mvec[msel]   = cmvec;
						d_maxp_mvec[msel]  = last_cmvec;
						d_maxn_mvec[msel]  = 0;
						d_max_mvec2[msel]  = cmvec2;
						d_maxp_mvec2[msel] = last_cmvec2;
						d_maxn_mvec2[msel] = 0;
						d_max_scrate[msel] = rate_sc;
						if (flag_sc > 0){			// シーンチェンジあり
							flag_sc_hold = 1;
							d_max_flagsc[msel] = 1;
							pos_lastchange = x;						// シーンチェンジ直後は間隔をあけるため位置保持
							if (rate_sc < THRES_RATE1){				// 画面転換量が少ない時は数フレーム要観察
								keep_schk = 1;
								keep_msel = msel;
							}
						}
					}
				}
			}
			//--- 次のフレーム準備 ---
			unsigned char *tmp = pix0;
			pix0 = pix1;
			pix1 = tmp;
			last_cmvec  = cmvec;
			last_cmvec2 = cmvec2;
			last_rate = rate_sc;
//if (x>=48889 && x<=48910) printf("[%d:%d,%d,%d,%d]", x,cmvec,rate_sc,d_max_mvec[msel],d_max_flagsc[msel]);
//					if (x>=9265 && x<=9269){
//						fprintf(fout, "(%d:%d)",x,cmvec);
//					}
		}

		// ２箇所目以降でシーンチェンジがなかったら無効化
		if (flag_sc_hold == 0 && msel > 0){
			if (local_cntsc > 0 || (local_end_fr - breakmute + setseri > range_end_fr)){
				d_max_en[msel] = 0;
			}
		}
	}

	//--- シーンチェンジ情報加工 ---
	int d_maxpre_pos[DEF_SCMAX];		// シーンチェンジ前の位置
	int d_maxrev_pos[DEF_SCMAX];		// シーンチェンジ後の位置
	int msel_max = 0;					// 最大のシーンチェンジ選択
	{
		// add for searching last frame before changing scene
		// シーンチェンジ前後のフレーム番号を取得（インターレース片側変化中を外す）
		for(int k=0; k<=msel; k++){
			if (d_max_en[k] > 0){
				d_maxpre_pos[k] = d_max_pos[k] - 1;		// 通常は１フレーム前がシーンチェンジ前
				if (d_max_mvec[k] < d_maxp_mvec[k] * 2 && d_maxp_mvec[k] > d_maxp_mvec2[k] * 2 &&
					d_max_mvec[k] - d_max_mvec2[k] > d_max_mvec[k] / 16){
					d_maxpre_pos[k] = d_max_pos[k] - 2;
				}
				d_maxrev_pos[k] = d_max_pos[k];			// 通常はシーンチェンジ地点がシーンチェンジ後
				if (d_maxrev_pos[k] - d_maxpre_pos[k] < 2){
					if (d_max_mvec[k] > d_max_mvec2[k] * 2 &&
						d_max_mvec[k] < d_maxn_mvec[k] * 2 &&
						d_maxn_mvec[k] - d_maxn_mvec2[k] > d_maxn_mvec[k] / 16 &&
						(d_maxn_mvec[k] > d_maxn_mvec2[k] * 2 || d_max_mvec[k] < d_maxn_mvec2[k] * 2)){
						d_maxrev_pos[k] = d_max_pos[k] + 1;
					}
				}
				if (d_maxpre_pos[k] < 0){
					d_maxpre_pos[k] = 0;
				}
				if (d_maxrev_pos[k] < 0){
					d_maxrev_pos[k] = 0;
				}
			}
		}
		// 複数候補があり、かつ変化のないシーンチェンジがある場合は削除
		if (msel >= 1){
			// 最大変化位置確認（max_msel値も更新）
			int d_tmpmax      = -1;
			int rate_tmpmax   = -1;
			for(int k=0; k<=msel; k++){
				if (d_max_en[k] > 0){
					if ((d_max_scrate[k] > rate_tmpmax) ||
						(d_max_scrate[k] == rate_tmpmax && d_max_mvec[k] > d_tmpmax)){
						msel_max = k;
						rate_tmpmax = d_max_scrate[k];
						d_tmpmax    = d_max_mvec[k];
					}
				}
			}
			// 最大変化位置を除き、変化のない候補は削除
			for(int k=0; k<=msel; k++){
				if (d_max_en[k] > 0){
					if (d_max_scrate[k] < THRES_RATE2 && k != msel_max){
						d_max_en[k] = 0;
					}
				}
			}
		}

		// 最後のシーンチェンジ位置を記憶（次の無音区間のオーバーラップ検出用）
		int tmp_scpos = -1;
		int tmp_flagsc = 0;
		for(int k=0; k<=msel; k++){
			if (d_max_en[k] > 0){
				if (tmp_scpos < d_max_pos[k]){
					tmp_scpos  = d_max_pos[k];
					tmp_flagsc = d_max_flagsc[k];
				}
			}
		}
		if (tmp_flagsc > 0){			// 最後が明確なシーンチェンジだった時のみ設定
			*lastmute_scpos = tmp_scpos;
		}
		else{
			*lastmute_scpos = -1;
		}
	}

	//--- シーンチェンジ情報表示用 ---
	{
		// 長時間無音シーンチェンジ設定幅があるか確認
		int flag_force_sc = 0;			// 指定期間内で強制的にシーンチェンジを行ったか
		if (msel > 0){		// ２つ以上の間隔
			int msel_s = -1;			// 最初の有効番号
			int msel_e = -1;			// 最後の有効番号
			for(int k=0; k<=msel; k++){
				if (d_max_en[k] > 0){
					msel_e = k;
					if (msel_s < 0){
						msel_s = k;
					}
				}
			}
			if (d_max_pos[msel_e] - d_max_pos[msel_s] > breakmute){
				flag_force_sc = 1;
			}
		}

		// マーク内容と位置を決める
		int msel_mark = msel_max;
		int msel_mknext  = msel_max;
		int mark_type = 0;
		int difmin = 30;
		int last_frame;
		if (*lastmute_marker < 0){					// 最初の無音の前回は無効
			last_frame = -10000;
		}
		else{
			last_frame = *lastmute_marker;
		}
		for(int k=0; k<=msel; k++){
			if (d_max_en[k] == 0) continue;		// シーンチェンジ候補から外れた場合次に
			int dif = abs(d_max_pos[k] - last_frame - 30*15);
			if (dif < difmin){
				difmin = dif;
				msel_mark = k;
				mark_type = 15;				// 15秒間隔
			}
			dif = abs(d_max_pos[k] - last_frame - 30*30);
			if (dif < difmin){
				difmin = dif;
				msel_mark = k;
				mark_type = 30;				// 30秒間隔
			}
			dif = abs(d_max_pos[k] - last_frame - 30*45);
			if (dif < difmin){
				difmin = dif;
				msel_mark = k;
				mark_type = 45;				// 45秒間隔
			}
			dif = abs(d_max_pos[k] - last_frame - 30*60);
			if (dif < difmin){
				difmin = dif;
				msel_mark = k;
				mark_type = 60;				// 60秒間隔
			}
			if (k == msel_mark){			// マーク位置更新時
				msel_mknext = k;			// 次マーク起点も同様に更新
			}
			if (seri > breakmute){			// 無音期間が長い場合
				if (k > msel_mark){			// マークから離れている場合
					if (abs(d_max_pos[k] - d_max_pos[msel_mark]) > breakmute){
						msel_mknext = k;	// 次マーク起点は最後の位置
					}
				}
			}
		}

		//--- 結果表示 ---
		for(int k=0; k<=msel; k++){
			char *mark = "";
			if (d_max_en[k] == 0) continue;		// シーンチェンジ候補から外れた場合次に

			if (d_max_scrate[k] < THRES_RATE2){	// 全く変化のないシーンチェンジ
				mark = "＿";
			}
			else if ((flag_force_sc > 0 && (k != msel_mark || mark_type == 0)) ||	// 指定無音区間内シーンチェンジ地点
					  (d_max_scrate[k] < THRES_RATE2 && k != msel_max)){			// 動きなしで残っている場合
				mark = "○";
			}
			else if (k == msel_mark){
				if (idx > 1 && mark_type == 15) {
					mark = "★";
				} else if (idx > 1 && mark_type == 30) {
					mark = "★★";
				} else if (idx > 1 && mark_type == 45) {
					mark = "★★★";
				} else if (idx > 1 && mark_type == 60) {
					mark = "★★★★";
				}
			}
			else{	// 無音区間内で第2候補シーンチェンジ
					mark = "＠";
			}
			printf("\t SCPos: %d %s\n", d_max_pos[k], mark);
			ncount_sc ++;

			TCHAR title[256];
			sprintf_s(title, _T("%dフレーム %s SCPos:%d %d"), seri, mark, d_maxrev_pos[k], d_maxpre_pos[k]);
			if (debug == 0){		// normal
				write_chapter(fout, idx, start_fr, title, &vii);
			}
			else{					// for debug
				TCHAR tmp_title[256];
				sprintf_s(tmp_title, _T(" Rate:%d"), d_max_scrate[k]);
				strcat(title, tmp_title);
//				sprintf_s(tmp_title, _T(" : [%d %d] [%d %d] [%d %d]"), d_maxn_mvec[k], d_maxn_mvec2[k], d_max_mvec[k], d_max_mvec2[k], d_maxp_mvec[k], d_maxp_mvec2[k]);
//				strcat(title, tmp_title);
				write_chapter_debug(fout, idx, start_fr, title, &vii);
			}
		}
		*lastmute_marker = d_max_pos[msel_mknext];
	}
	return ncount_sc;			// 検出した位置の合計を返す
}
