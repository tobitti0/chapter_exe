// IIR_3DNRフィルタ  by H_Kasahara(aka.HK) より拝借
// シーンチェンジ検出用にアルゴリズム改修 by Yobi


//---------------------------------------------------------------------
//		動き検索処理用
//---------------------------------------------------------------------

#include "stdafx.h"
#include <Windows.h>
#include <stdlib.h>
#include <limits.h>
#include <stdio.h>

#define MAX_LINEOBJ		20		// 固定ライン検出する画面周囲からの検索範囲

#define FRAME_PICTURE	1
#define FIELD_PICTURE	2
#define MAX_SEARCH_EXTENT 32	//全探索の最大探索範囲。+-この値まで。
#define RATE_SCENE_CHANGE 100	//シーンチェンジと判定する割合x1000。指定値/1000以上の変化があればシーンチェンジとする
#define RATE_SCENE_CHGLOW 50	//検出不明が多い時にシーンチェンジと判定する割合x1000。
#define RATE_SCENE_CHGBLK 20	//空白多い時にシーンチェンジと判定する割合のx1000。
#define RATE_SCENE_CHGCBK  8	//空白多い時に中心付近だけを計測してシーンチェンジと判定する割合のx1000。
#define RATE_SCENE_JDGBLK 850	//空白多いと判断する画像が空白の割合のx1000。
#define RATE_SCENE_STAY   100	//シーンチェンジではないと判定する固定割合のx1000。
#define RATE_SCENE_STYLOW 70	//固定箇所が一定以上時にシーンチェンジではないと判定する固定割合のx1000。
#define RATE_SCENE_STYBLK 20	//空白多い時にシーンチェンジではないと判定する固定割合のx1000。
#define RATE_SCENE_STYCBK  8	//空白多い時に中心付近だけを計測してシーンチェンジではないと判定する固定割合のx1000。
#define THRES_STILLDATA    8    //ベタ塗り画像用に誤差範囲と判断させる適当な値

//---------------------------------------------------------------------
//		関数定義
//---------------------------------------------------------------------
//void make_motion_lookup_table();
//BOOL mvec(unsigned char* current_pix,unsigned char* bef_pix,int* vx,int* vy,int lx,int ly,int threshold,int pict_struct,int SC_level);
int mvec(int *mvec1,int *mvec2,int *flag_sc,unsigned char* current_pix,unsigned char* bef_pix,int lx,int ly,int threshold,int pict_struct, int nframe);
int search_change(int* val, unsigned char* pc, unsigned char* pb, int lx, int ly, int x, int y, int thres_fine, int thres_sc, int pict_struct);
int tree_search(unsigned char* current_pix,unsigned char* bef_pix,int lx,int ly,int *vx,int *vy,int search_block_x,int search_block_y,int min,int pict_struct, int method);
int full_search(unsigned char* current_pix,unsigned char* bef_pix,int lx,int ly,int *vx,int *vy,int search_block_x,int search_block_y,int min,int pict_struct, int search_extent);
int dist( unsigned char *p1, unsigned char *p2, int lx, int distlim, int block_height );
int maxmin_block( unsigned char *p, int lx, int block_height );
int avgdist( int *avg, unsigned char *psrc, int lx, int block_height );

//---------------------------------------------------------------------
//		グローバル変数
//---------------------------------------------------------------------
int	block_height, lx2;


//---------------------------------------------------------------------
//		動き誤差判定関数
//---------------------------------------------------------------------
//[ru] 動きベクトルの合計を返す
//返り値はシーンチェンジ判定数値に変更
int tree=0, full=0;
int mvec(
		  int *mvec1,					//インターレースで動きが多い側の動き結果を格納（出力）
		  int *mvec2,					//インターレースで動きが少ない側の動き結果を格納（出力）
          int *flag_sc,					//シーンチェンジフラグ（出力）
		  unsigned char* current_pix, 	//現フレームの輝度。8ビット。
		  unsigned char* bef_pix,		//前フレームの輝度。8ビット。
		  int lx,						//画像の横幅
		  int ly,						//画像の縦幅
		  int threshold,				//検索精度。(100-fp->track[1])*50 …… 50は適当な値。
		  int pict_struct,				//"1"ならフレーム処理、"2"ならフィールド処理
		  int nframe )					// フレーム番号。デバッグのみに使用
{
	int x, y;
	unsigned char *p1, *p2;
	int calc_total_lane_i0, calc_total_lane_i1;
	int rate_sc, rate_sc_all;
	int b_sc, b_sc_all;
	int thr_blank, thr_noobj, thr_mergin;

//関数を呼び出す毎に計算せずにすむようグローバル変数とする
	lx2 = lx*pict_struct;
	block_height = 16/pict_struct;

	// シーンチェンジ検出用の閾値
	thr_blank  = threshold / 100;			// 空白と判定する閾値
	thr_noobj  = threshold / 10;			// 表示物なしと判定する閾値
	thr_mergin = threshold / 8;				// 前後フレーム誤差と判定する閾値

	for(int i=0;i<pict_struct;i++)
	{
		int calc_total = 0;					// 動きベクトルの合計
		int areacnt_blankor = 0;			// 前後どちらかのフレームが空白の合計
		int areacnt_blankand = 0;			// 両フレーム同一輝度で空白の合計
		int areacnt_blank1 = 0;				// 現フレームが空白の合計
		int areacnt_blank2 = 0;				// 前フレームが空白の合計
		int areacnt_noobj1 = 0;				// 現フレームの表示物がない地点合計
		int areacnt_noobj2 = 0;				// 前フレームの表示物がない地点合計
		int cnt_total  = 0;					// 計算領域数
		int cnt_sc = 0;						// シーンチェンジ検出数
		int cnt_detobj = 0;					// 固定地点検出数
		int cnt_center_detobj = 0;			// 固定地点検出数（中心付近の領域）
		int cnt_blank = 0;					// 空白地点検出数
		int cnt_undet = 0;					// 検出状態が微妙で不明な地点検出数
		int cnt_undetobj = 0;				// 検出状態が微妙で不明な地点検出数（表示物は明確に存在）
		int cnt_sc_low1 = 0;				// 表示物消滅によるシーンチェンジ地点検出数
		int cnt_sc_low2 = 0;				// 表示物出現によるシーンチェンジ地点検出数
		int cnt_sc_center_low1 = 0;			// 表示物消滅によるシーンチェンジ地点検出数（中心付近の領域）
		int cnt_sc_center_low2 = 0;			// 表示物出現によるシーンチェンジ地点検出数（中心付近の領域）
		// 固定ライン計算初期化
		int lineobj[4][MAX_LINEOBJ];
		memset(lineobj, '\0', sizeof(int) * 4 * MAX_LINEOBJ);

		for(y=i+16;y<ly-16;y+=16)	//全体縦軸
		{
			p1 = current_pix + y*lx + 16;
			p2 = bef_pix + y*lx + 16;
			for(x=16;x<lx-16;x+=16)	//全体横軸
			{
				int center_area = 0;				// 中心領域でのカウント用
				int invalid_th_fine = 0;			// 一致検索中止フラグ
				int lowtype = 0;					// 表示物の消滅(1)、出現(2)
				int ddist, ddist1, ddist2;			// 中心位置の輝度差情報
				int avg1, avg2;						// 平均値
				int th_fine;						// 一致検索する閾値
				int nrank_sc;						// シーンチェンジ検出結果
				int val_calc;						// 動きベクトル量保持
				unsigned char *pc, *pp;				// 画像先頭位置

				// 中心領域検出
				if (y >= ly*3/16 && y < ly - (ly*3/16) &&
					x >= lx*3/16 && x < lx - (lx*3/16)){
					center_area = 1;
				}

				// １フレーム内の差分絶対値合計取得
				ddist1 = avgdist(&avg1, p1, lx2, block_height);		// 現フレームの平均からの差分絶対値合計
				ddist2 = avgdist(&avg2, p2, lx2, block_height);		// 前フレームの平均からの差分絶対値合計
				// 前後フレームの状態を分類
				if (ddist1 <= threshold && ddist2 > thr_blank && ddist1 * 2 <= ddist2){
					lowtype = 1;								// 現フレームが空白に近い
				}
				else if (ddist2 <= threshold && ddist1 > thr_blank && ddist2 * 2 <= ddist1){
					lowtype = 2;								// 前フレームが空白に近い
				}
				// 前後フレームの空白・表示物なし状態をカウント
				if (ddist1 <= thr_blank || ddist2 <= thr_blank){
					areacnt_blankor ++;							// どちらかのフレームが空白
					if (ddist1 <= thr_blank){
						areacnt_blank1 ++;						// 現フレームが空白
					}
					if (ddist2 <= thr_blank){
						areacnt_blank2 ++;						// 前フレームが空白
					}
					if (ddist1 <= thr_blank && ddist2 <= thr_blank &&
						abs(avg1 - avg2) <= 5){
						areacnt_blankand ++;					// 両フレーム空白で同一輝度
					}
				}
				if (ddist1 <= thr_noobj){
					areacnt_noobj1 ++;							// 現フレームの表示物がない
				}
				if (ddist2 <= thr_noobj){
					areacnt_noobj2 ++;							// 前フレームの表示物がない
				}

				// 前フレーム表示物が消えた場合を検出するため
				// 現フレームだけが空白に近い場合は前フレームの動きを検出
				if (lowtype == 1){					// 前フレーム基準（逆設定）
					ddist = ddist2;
					pc    = p2;
					pp    = p1;
				}
				else{								// 現フレーム基準（通常設定）
					ddist = ddist1;
					pc    = p1;
					pp    = p2;
				}
				// 一致検索する閾値を設定
				th_fine = ddist * 3 / 5;				// 一致とする閾値設定
				if (th_fine > threshold){				// 最大でシーンチェンジ検索
					th_fine = threshold;
				}
				if (th_fine <= thr_mergin){				// 誤差マージン以下の場合
					th_fine = threshold;				// 一致検索を中止する
					invalid_th_fine = 1;
				}

				// シーンチェンジ検出
				nrank_sc = search_change(&val_calc, pc, pp, lx, ly, x, y, th_fine, threshold, pict_struct);

				// シーンチェンジ結果を分類し、分類結果に+1カウント
				// cnt_scは検出に必須、それ以外は微調整用
				if (nrank_sc == 2){						// シーンチェンジ
					cnt_sc ++;
					// 空白部分が多い時のシーンチェンジ検出用
					if (lowtype == 1){
						cnt_sc_low1 ++;
						if (center_area == 1) cnt_sc_center_low1 ++;
					}
					else if (lowtype == 2){
						cnt_sc_low2 ++;
						if (center_area == 1) cnt_sc_center_low2 ++;
					}
				}
				else if (ddist1 <= thr_blank && ddist2 <= thr_blank){	// 両フレーム空白
					cnt_blank ++;
				}
				else if (invalid_th_fine > 0 || nrank_sc > 0){		// 前後フレーム一致状態不明
					cnt_undet ++;
					if (ddist1 > thr_noobj || ddist2 > thr_noobj){
						cnt_undetobj ++;			// 一致状態不明（表示物は明確に存在）
					}
				}
				else{								// 前後フレーム一致
					cnt_detobj ++;
					if (center_area == 1) cnt_center_detobj ++;
					// 固定ライン判断用
					if (center_area == 0){
						int idtmp;
						if (y < ly*3/16){			// 画面上側
							idtmp = y / 16;
							if (idtmp < MAX_LINEOBJ){
								lineobj[0][idtmp] ++;
							}
						}
						if (y >= ly - (ly*3/16)){	// 画面下側
							idtmp = (ly - y - 1) / 16;
							if (idtmp < MAX_LINEOBJ){
								lineobj[1][idtmp] ++;
							}
						}
						if (x < lx*3/16){			// 画面左側
							idtmp = x / 16;
							if (idtmp < MAX_LINEOBJ){
								lineobj[2][idtmp] ++;
							}
						}
						if (x >= lx - (lx*3/16)){	// 画面右側
							idtmp = (lx - x - 1) / 16;
							if (idtmp < MAX_LINEOBJ){
								lineobj[3][idtmp] ++;
							}
						}
					}
				}

				calc_total += val_calc;

				p1+=16;
				p2+=16;
				cnt_total ++;
			}
		}
		// シーンチェンジの割合を計算
		rate_sc = cnt_sc * 1000 / cnt_total;

		//----- ここから微調整開始（なくても動作する） -----
		// 固定ライン検出
		int cnt_lineobj = 0;		// 固定部分の中で固定ラインと認識した数
		if (1){						// 固定ライン検出処理実行
			int linecount_y = 0;
			// 上下左右の合計４か所でライン固定検出を行う
			for(int k1=0; k1<4; k1++){
				int cnt_lineobj_max;
				int cnttmp;
				int cnttmp_max;
				if (k1 < 2){		// 水平ラインの検出
					cnt_lineobj_max = lx / 16;
				}
				else{				// 垂直ラインの検出
					cnt_lineobj_max = ly / 16;
				}
				// ラインの半分以上固定で一番多い所検出を固定ラインとして設定
				cnttmp_max = 0;
				for(int k2=0; k2 < MAX_LINEOBJ; k2++){
					cnttmp = lineobj[k1][k2];
					if (cnttmp > cnt_lineobj_max / 2 && cnttmp > cnttmp_max){
						cnttmp_max = cnttmp;
					}
//if (cnttmp > cnt_lineobj_max / 2){
//printf("(%d:%d,%d,%d)",nframe,cnttmp_max,k1,k2);
//}
				}
				if (cnttmp_max > 1){
					cnt_lineobj += cnttmp_max;
					if (k1 < 2){			// 水平ライン追加カウント
						linecount_y ++;
					}
					else{					// 垂直ライン時は水平ライン重複可能性分を引く
						cnt_lineobj -= linecount_y;
					}
				}
			}
			// 固定ライン扱いのデータを外す
			if (cnt_lineobj > 0){
				cnt_detobj -= cnt_lineobj;
				cnt_total  -= cnt_lineobj;
			}
		}
		// シーンチェンジ以下の割合でもシーンチェンジにする場合の判断
		if (rate_sc < RATE_SCENE_CHANGE){
			int calc_jdgblk = cnt_total * RATE_SCENE_JDGBLK / 1000;
			int cnt_stayobj = cnt_detobj + cnt_undetobj;
			int flag_blankchk = 0;

			//--- 空白地点が多ければ少ない変化でもシーンチェンジにする処理 ---
			// ケース１：表示継続場所と比較して出現消滅シーンチェンジ割合が高い場合チェック実行
			if ((areacnt_blankor >= calc_jdgblk) &&
				(cnt_sc_low1*3/2 >= cnt_stayobj || cnt_sc_low2*3/2 >= cnt_stayobj)){
				flag_blankchk = 1;
			}
			// ケース２：片方のフレームが空白で、片方のフレームが空白以外が多い場合チェック実行
			if ((areacnt_blank1 >= calc_jdgblk || areacnt_blank2 >= calc_jdgblk) &&
				(abs(areacnt_blank1 - areacnt_blank2) >= cnt_total / 3)){
				flag_blankchk = 1;
			}
			// ケース３：片方のフレームの表示物が全くなく、片方のフレームの表示物が多い場合チェック実行
			if ((areacnt_noobj1 >= calc_jdgblk || areacnt_noobj2 >= calc_jdgblk) &&
				(abs(areacnt_noobj1 - areacnt_noobj2) >= cnt_total / 2)){
				flag_blankchk = 1;
			}
			// ケースを満たし確認する場合
			if (flag_blankchk > 0){
				// 空白が多い時のシーンチェンジ閾値設定
				int calc_blank_thres = cnt_total * RATE_SCENE_CHGBLK / 1000;
				// 表示物移動時ではなく、出現または消滅時のみ
				if ((cnt_sc_low1 - (cnt_sc_low2 * 4) >= calc_blank_thres) ||
					(cnt_sc_low2 - (cnt_sc_low1 * 4) >= calc_blank_thres)){
						rate_sc = RATE_SCENE_CHANGE;
				}

				// 中心部分だけ見た場合の設定（全体の約４割エリア）
				int calc_blank_thres_c = cnt_total * RATE_SCENE_CHGCBK / 1000;
				// 表示物移動時ではなく、出現または消滅時のみ
				if ((cnt_sc_center_low1 - (cnt_sc_center_low2 * 4) >= calc_blank_thres_c) ||
					(cnt_sc_center_low2 - (cnt_sc_center_low1 * 4) >= calc_blank_thres_c)){
						rate_sc = RATE_SCENE_CHANGE;
				}
			}

			//--- シーンチェンジから固定箇所を引いて一定以上の割合がある場合の処理 ---
			if (cnt_sc - cnt_detobj/2 >= cnt_total * RATE_SCENE_CHGLOW / 1000 &&
				cnt_detobj <= cnt_total * RATE_SCENE_CHGLOW / 1000 &&
				rate_sc < RATE_SCENE_CHANGE){
				// 検出不明箇所が多ければシーンチェンジを甘めに設定する処理
				if (cnt_undet > cnt_total / 2){			// 検出不明箇所が半分以上
					rate_sc = RATE_SCENE_CHANGE;
				}
				// 空白が多い時も検出不明箇所が多ければ甘めに設定
				else if (areacnt_blankor >= calc_jdgblk &&
						 (cnt_sc - cnt_detobj + cnt_undet >= calc_jdgblk)){
					rate_sc = RATE_SCENE_CHANGE;
				}
			}
		}
		// 固定箇所の割合が閾値以上あれば実質的にシーンチェンジ解除する
		if (cnt_detobj >= cnt_total * RATE_SCENE_STAY / 1000){
			// 中心部分での固定割合が極端に小さければキャンセル
			// 周囲に固定枠模様があってもシーンチェンジ０にはしない対策
			if (cnt_center_detobj < cnt_total * RATE_SCENE_STYCBK / 1000){
			}
			else{
				rate_sc = rate_sc / 10;
			}
		}
		// シーンチェンジ割合を超えていても固定が多ければ解除する処理
		else if (rate_sc >= RATE_SCENE_CHANGE && rate_sc <= RATE_SCENE_CHANGE * 3){
			// 前後フレーム共に空白＋輝度変化なしの領域が半分以上あった時の補正
			int calc_blank_dif  = abs(areacnt_blank1 - areacnt_blank2);
			int calc_blank_base = areacnt_blankand + calc_blank_dif;
			int calc_blank_rev  = (calc_blank_base - cnt_total/2) * 2 * RATE_SCENE_STAY / 1000;
			// 空白＋輝度変化なしが半分以上の場合は固定数補正し閾値以上あればシーンチェンジ解除
			if (areacnt_blankand >= cnt_total / 2 &&
				cnt_detobj >= cnt_total * RATE_SCENE_STYBLK / 1000 &&
				cnt_sc < calc_blank_dif + (cnt_total - calc_blank_base)/2 &&
				((cnt_detobj + calc_blank_rev >= cnt_total * RATE_SCENE_STAY / 1000) ||
				 calc_blank_base > cnt_total * RATE_SCENE_JDGBLK / 1000)){
				rate_sc = RATE_SCENE_CHANGE - 1;
			}
			// 固定箇所の割合がある程度以上ある時はシーンチェンジ厳しめに設定
			else if (cnt_detobj >= cnt_total * RATE_SCENE_STYLOW / 1000){
				if (cnt_sc - cnt_detobj + cnt_total * RATE_SCENE_STYLOW / 1000
					 < cnt_total * RATE_SCENE_CHANGE / 1000){
					rate_sc = RATE_SCENE_CHANGE - 1;
				}
			}
		}
		// 変化が多ければ強いシーンチェンジにする
		if (rate_sc >= RATE_SCENE_CHANGE && rate_sc <= RATE_SCENE_CHANGE * 3){
			if (cnt_sc + cnt_undetobj - (cnt_detobj * 10) >= cnt_total/2){
				rate_sc = RATE_SCENE_CHANGE * 3;
			}
		}
		//----- ここまで微調整終了 -----

		// シーンチェンジ判定
		if (rate_sc >= RATE_SCENE_CHANGE){
			b_sc = 1;
		}
		else{
			b_sc = 0;
		}
		if (0){	// debug
			if (nframe >= 6548 && nframe <= 6553 || nframe >= 19020 && nframe <= 19026 ||
				nframe >= 36396 && nframe <= 36396 || nframe >= 47105 && nframe <= 47123){
				printf("f:%d | %d %d,(%d,%d,[%d,%d,%d]),%d,%d,%d,%d,(%d,%d,%d,%d),%d,%d(%d)\n",
				rate_sc, nframe, cnt_sc, cnt_sc_low1, cnt_sc_low2,
				cnt_sc_center_low1, cnt_sc_center_low2, cnt_center_detobj,
				areacnt_blank1, areacnt_blank2, areacnt_blankand, areacnt_blankor,
				cnt_blank, cnt_detobj, cnt_undet, cnt_undetobj, cnt_lineobj, cnt_total,calc_total);}
		}
		// インターレースはトップ／ボトムで別々に記憶。未計算と区別で最低１以上にするため１を加算
		if (i == 0){
			calc_total_lane_i0 = calc_total+1;
			b_sc_all = b_sc;
			rate_sc_all = rate_sc;
		}
		else{
			calc_total_lane_i1 = calc_total+1;
			if (rate_sc_all < rate_sc){
				b_sc_all = b_sc;
				rate_sc_all = rate_sc;
			}
		}
	}

	if (pict_struct != 2){				// フレーム処理の場合、結果をそのまま代入
		*mvec1 = calc_total_lane_i0;
		*mvec2 = calc_total_lane_i0;
		*flag_sc = b_sc_all;
	}
	else if (calc_total_lane_i0 >= calc_total_lane_i1){	// インターレースでlane0の方が動き大きい時
		*mvec1 = calc_total_lane_i0;
		*mvec2 = calc_total_lane_i1;
		*flag_sc = b_sc_all;
	}
	else{												// インターレースでlane1の方が動き大きい時
		*mvec1 = calc_total_lane_i1;
		*mvec2 = calc_total_lane_i0;
		*flag_sc = b_sc_all;
	}

	/*char str[500];
	sprintf_s(str, 500, "tree:%d, full:%d", tree, full);
	MessageBox(NULL, str, 0, 0);*/

	return rate_sc_all;
}

//---------------------------------------------------------------------
// シーンチェンジを検出
// 出力
//   返り値：0=前後フレーム一致  1=前後フレーム一致不明  2=シーンチェンジ
//   val   ：動きベクトル量
//---------------------------------------------------------------------
int search_change(
	int* val,				//動きベクトル量（結果の値）
	unsigned char* pc,		//検出元フレームの輝度。8ビット。
	unsigned char* pp,		//検出先フレームの輝度。8ビット。
	int lx,					//画像の横幅
	int ly,					//画像の縦幅
	int x,					//検索位置
	int y,					//検索位置
	int thres_fine,			//比較閾値（一致判定）
	int thres_sc,			//比較閾値（シーンチェンジ判定）
	int pict_struct)		//"1"ならフレーム処理、"2"ならフィールド処理
{
	int method = 0;			//検索の簡易化（0:探索多回数 1:２分探索 2:検索省略）
	int n_sc = 0;
	int vx = 0;
	int vy = 0;

	//同位置でのフレーム間の絶対値差。
	int min = dist( pc, pp, lx2, INT_MAX, block_height );
	if (min <= thres_fine){		//フレーム間の絶対値差が最初から小さければ簡略化
		//method = 1;		//動き情報も考慮に入れるならこちら
		method = 2;			//速度優先ならこちら
	}
	// tree_searchは本来一致判定閾値までが正しいが、速度向上のためシーンチェンジ閾値までにする
	if( thres_sc < (min = tree_search( pc, pp, lx, ly, &vx, &vy, x, y, min, pict_struct, method))){
		//フレーム間の絶対値差が大きければ全探索をおこなう
		if ( thres_sc < (min = full_search( pc, &pp[vy * lx + vx], lx, ly, &vx, &vy, x+vx, y+vy, min, pict_struct, max(abs(vx),abs(vy))*2 ))){
			// 最初の検索範囲にかからなかった時のため、離れた範囲を探索
			int vxe = 0;
			int vye = 0;
			if (thres_sc < (min = tree_search( pc, pp, lx, ly, &vxe, &vye, x, y, min, pict_struct, 3))){ 
				//動きベクトルの合計がシーンチェンジレベルを超えていたら、シーンチェンジと判定して大きな値を設定
				vx = MAX_SEARCH_EXTENT * 10;		// 全体の閾値でも検出できなかった場合、大きな値を設定
				vy = MAX_SEARCH_EXTENT * 10;
				n_sc = 2;
			}
		}
	}
	if (n_sc == 0 && min > thres_fine){		//フレーム間の絶対値差が大きければ一致不明に設定
		n_sc = 1;
	}
	*val = abs(vx)+abs(vy);
	return n_sc;
}

//---------------------------------------------------------------------
//		簡易探索法動き検索関数
//      同じ値の場合は中心に近い方を選択する
//---------------------------------------------------------------------
int tree_search(unsigned char* current_pix,	//現フレームの輝度。8ビット。
				unsigned char* bef_pix,		//前フレームの輝度。8ビット。
				int lx,						//画像の横幅
				int ly,						//画像の縦幅
				int *vx,					//x方向の動きベクトルが代入される。
				int *vy,					//y方向の動きベクトルが代入される。
				int search_block_x,			//検索位置
				int search_block_y,			//検索位置
				int min,					//同位置でのフレーム間の絶対値差。関数内では同位置の比較をしないので、呼び出す前に行う必要あり。
				int pict_struct,			//"1"ならフレーム処理、"2"ならフィールド処理
				int method)					//検索の簡易化（0:探索多回数 1:２分探索 2:検索省略 3:探索多回数外周）
{
	tree++;
	int dx, dy, ddx=0, ddy=0, xs=0, ys;
	int d;
	int x,y;
	int locx, locy;
	int loopmax, inter;
	int nrep, step, dthres;
	int speedup = pict_struct-1;
//検索範囲の上限と下限を設定
	int ylow  = 0 - search_block_y;
	int yhigh = ly- search_block_y-16;
	int xlow  = 0 - search_block_x;
	int xhigh = lx- search_block_x-16;

	if (method == 2) return min;	// 検索省略

	if (method == 0){
		loopmax = 3-speedup;
		inter = 0;					// interは不使用
	}
	else if (method == 3){
		loopmax = 1;
		inter = 0;					// interは不使用
	}
	else{
		loopmax = 5-speedup;		// MAX_SEARCH_EXTENT=32の時（計算省略のため直接定義）
		inter = MAX_SEARCH_EXTENT;
	}
	for(int i=0; i<loopmax; i++){
		if (method == 0){			// ２段階で検索（フィールド処理で比較合計９６回）
			if (i==0){
				locx = MAX_SEARCH_EXTENT - 8;
				locy = MAX_SEARCH_EXTENT - 8;
				nrep = MAX_SEARCH_EXTENT/8*2 - 1;
				step = 8;
				dthres = THRES_STILLDATA << 4;		// 誤差範囲とする適当な値
			}
			else if (i==1){
				locx = ddx - 6;
				locy = ddy - 6;
				nrep = 7;
				step = 2;
				dthres = THRES_STILLDATA << 2;		// 誤差範囲とする適当な値
			}
			else{
				locx = ddx - 1;
				locy = ddy - 1;
				nrep = 3;
				step = 1;
				dthres = 1;			// 誤差範囲とする適当な値
			}
		}
		else if (method == 3){
			locx = ddx - MAX_SEARCH_EXTENT*2;
			locy = ddy - MAX_SEARCH_EXTENT*2;
			nrep = 5;
			step = MAX_SEARCH_EXTENT;
			dthres = THRES_STILLDATA << 4;		// 誤差範囲とする適当な値
		}
		else{						// ２分探索（フィールド処理で比較合計３２回）
			inter = inter / 2;
			locx = ddx - inter;
			locy = ddy - inter;
			nrep = 3;
			step = inter;
			dthres = THRES_STILLDATA << (loopmax - i - 1);		// 誤差範囲とする適当な値
		}
		// 検索開始
		dy = locy;
		for(y=0; y<nrep; y++){
			if ( dy<ylow || dy>yhigh ){			//検索位置が画面外に出ていたら検索をおこなわない。
			}
			else{
				ys = dy * lx;	//検索位置縦軸
				dx = locx;
				for(x=0; x<nrep; x++){
					if( dx<xlow || dx>xhigh ){	//検索位置が画面外に出ていたら検索をおこなわない。
					}
					else if (x == (nrep-1)/2 && y == (nrep-1)/2){	// 中心座標では計算しない。
					}
					else{
						d = dist( current_pix, &bef_pix[ys+dx], lx2, min, block_height );
						if( d <= min ){	//これまでの検索よりフレーム間の絶対値差が小さかったらそれぞれ代入。
							if ((d + dthres <= min) ||
								(abs(dx) + abs(dy) <= abs(ddx) + abs(ddy))){	// 中心に近いか、誤差閾値以上差がある場合セット
									min = d;
									ddx = dx;
									ddy = dy;
							}
						}
					}
					dx += step;
				}
			}
			dy += step;
		}
	}

	if(pict_struct==FIELD_PICTURE){
		for(x=0,dx=ddx-1;x<3;x+=2,dx+=2){
			if( search_block_x+dx<0 || search_block_x+dx+16>lx )	continue;	//検索位置が画面外に出ていたら検索をおこなわない。
			d = dist( current_pix, &bef_pix[ys+dx], lx2, min, block_height );
			if( d < min ){	//これまでの検索よりフレーム間の絶対値差が小さかったらそれぞれ代入。
				min = d;
				ddx = dx;
			}
		}
	}
	

	*vx += ddx;
	*vy += ddy;

	return min;
}
//---------------------------------------------------------------------
//		全探索法動き検索関数
//      同じ値の場合は中心に近い方を選択する
//---------------------------------------------------------------------
int full_search(unsigned char* current_pix,	//現フレームの輝度。8ビット。
				unsigned char* bef_pix,		//前フレームの輝度。8ビット。
				int lx,						//画像の横幅
				int ly,						//画像の縦幅
				int *vx,					//x方向の動きベクトルが代入される。
				int *vy,					//y方向の動きベクトルが代入される。
				int search_block_x,			//検索位置
				int search_block_y,			//検索位置
				int min,					//フレーム間の絶対値差。最初の探索ではINT_MAXが入っている。
				int pict_struct,			//"1"ならフレーム処理、"2"ならフィールド処理
				int search_extent)			//探索範囲。
{
	full++;
	int dx, dy, ddx=0, ddy=0;
	int d;
	int dthres;
//	int search_point;
	unsigned char* p2;

	if(search_extent>MAX_SEARCH_EXTENT)
		search_extent = MAX_SEARCH_EXTENT;

//検索範囲の上限と下限が画像からはみ出していないかチェック
	int ylow  = 0 - ( (search_block_y-search_extent<0) ? search_block_y : search_extent );
	int yhigh = (search_block_y+search_extent+16>ly) ? ly-search_block_y-16 : search_extent;
	int xlow  = 0 - ( (search_block_x-search_extent<0) ? search_block_x : search_extent );
	int xhigh = (search_block_x+search_extent+16>lx) ? lx-search_block_x-16 : search_extent;

	dthres = THRES_STILLDATA;		// 誤差範囲とする適当な値
	for(dy=ylow;dy<=yhigh;dy+=pict_struct)
	{
		p2 = bef_pix + dy*lx + xlow;	//Y軸検索位置。xlowは負の値なので"p2=bef_pix+dy*lx-xlow"とはならない
		for(dx=xlow;dx<=xhigh;dx++)
		{
			d = dist( current_pix, p2, lx2, min, block_height );
			if(d <= min)	//これまでの検索よりフレーム間の絶対値差が小さかったらそれぞれ代入。
			{
				if ((d + dthres <= min) ||
					(abs(dx) + abs(dy) <= abs(ddx) - abs(ddy))){	// 中心に近いか、誤差閾値以上差がある場合セット
					min = d;
					ddx = dx;
					ddy = dy;
				}
			}
			p2++;
		}
	}

	*vx += ddx;
	*vy += ddy;

	return min;
}
//---------------------------------------------------------------------
//		フレーム間絶対値差合計関数
//---------------------------------------------------------------------
//bbMPEGのソースを流用
#include <emmintrin.h>

int dist( unsigned char *p1, unsigned char *p2, int lx, int distlim, int block_height )
{
	if (block_height == 8) {
		__m128i a, b, r;

		a = _mm_load_si128 ((__m128i*)p1 +  0);
		b = _mm_loadu_si128((__m128i*)p2 +  0);
		r = _mm_sad_epu8(a, b);

		a = _mm_load_si128 ((__m128i*)(p1 + lx));
		b = _mm_loadu_si128((__m128i*)(p2 + lx));
		r = _mm_add_epi32(r, _mm_sad_epu8(a, b));

		a = _mm_load_si128 ((__m128i*)(p1 + 2*lx));
		b = _mm_loadu_si128((__m128i*)(p2 + 2*lx));
		r = _mm_add_epi32(r, _mm_sad_epu8(a, b));

		a = _mm_load_si128 ((__m128i*)(p1 + 3*lx));
		b = _mm_loadu_si128((__m128i*)(p2 + 3*lx));
		r = _mm_add_epi32(r, _mm_sad_epu8(a, b));

		a = _mm_load_si128 ((__m128i*)(p1 + 4*lx));
		b = _mm_loadu_si128((__m128i*)(p2 + 4*lx));
		r = _mm_add_epi32(r, _mm_sad_epu8(a, b));

		a = _mm_load_si128 ((__m128i*)(p1 + 5*lx));
		b = _mm_loadu_si128((__m128i*)(p2 + 5*lx));
		r = _mm_add_epi32(r, _mm_sad_epu8(a, b));

		a = _mm_load_si128 ((__m128i*)(p1 + 6*lx));
		b = _mm_loadu_si128((__m128i*)(p2 + 6*lx));
		r = _mm_add_epi32(r, _mm_sad_epu8(a, b));

		a = _mm_load_si128 ((__m128i*)(p1 + 7*lx));
		b = _mm_loadu_si128((__m128i*)(p2 + 7*lx));
		r = _mm_add_epi32(r, _mm_sad_epu8(a, b));
		return _mm_extract_epi16(r, 0) + _mm_extract_epi16(r, 4);;
	}

	int s = 0;
	for(int i=0;i<block_height;i++)
	{
		/*
		s += motion_lookup[p1[0]][p2[0]];
		s += motion_lookup[p1[1]][p2[1]];
		s += motion_lookup[p1[2]][p2[2]];
		s += motion_lookup[p1[3]][p2[3]];
		s += motion_lookup[p1[4]][p2[4]];
		s += motion_lookup[p1[5]][p2[5]];
		s += motion_lookup[p1[6]][p2[6]];
		s += motion_lookup[p1[7]][p2[7]];
		s += motion_lookup[p1[8]][p2[8]];
		s += motion_lookup[p1[9]][p2[9]];
		s += motion_lookup[p1[10]][p2[10]];
		s += motion_lookup[p1[11]][p2[11]];
		s += motion_lookup[p1[12]][p2[12]];
		s += motion_lookup[p1[13]][p2[13]];
		s += motion_lookup[p1[14]][p2[14]];
		s += motion_lookup[p1[15]][p2[15]];*/

		__m128i a = _mm_load_si128((__m128i*)p1);
		__m128i b = _mm_loadu_si128((__m128i*)p2);
		__m128i r = _mm_sad_epu8(a, b);
		s += _mm_extract_epi16(r, 0) + _mm_extract_epi16(r, 4);

		if (s > distlim)	break;

		p1 += lx;
		p2 += lx;
	}
	return s;
}


//---------------------------------------------------------------------
//		フレーム間絶対値差合計関数(SSEバージョン)
//---------------------------------------------------------------------
int dist_SSE( unsigned char *p1, unsigned char *p2, int lx, int distlim, int block_height )
{
	int s = 0;
/*
dist_normalを見ると分かるように、p1とp2の絶対値差を足してき、distlimを超えたらその合計を返すだけ。
block_heightには8か16が代入されており、前者はフィールド処理、後者がフレーム処理用。
block_heightに8が代入されていたらば、lxには画像の横幅が代入されている。
block_heightに16が代入されていたらば、lxには画像の横幅の二倍の値が代入されている。
どなたか、ここを作成していただけたらば、非常に感謝いたします。
*/
	return s;
}


//---------------------------------------------------------------------
//		ブロック内の最大輝度差取得関数
//---------------------------------------------------------------------
int maxmin_block( unsigned char *p, int lx, int block_height )
{
	__m128i rmin, rmax, a, b, z;

	// 各列の最大・最小を求める
	rmin = _mm_load_si128((__m128i*)p);
	rmax = _mm_load_si128((__m128i*)p);
	p += lx;
	for(int i=1; i<block_height; i++){
		a = _mm_load_si128((__m128i*)p);
		rmin = _mm_min_epu8(rmin, a);
		rmax = _mm_max_epu8(rmax, a);
		p += lx;
	}
	// 列間の最大・最小を求める
	// 16データの最大・最小を８データに絞る
	z    = _mm_setzero_si128();
	a    = _mm_unpackhi_epi8(rmin, z);
	b    = _mm_unpacklo_epi8(rmin, z);
	rmin = _mm_min_epi16(a, b);
	a    = _mm_unpackhi_epi8(rmax, z);
	b    = _mm_unpacklo_epi8(rmax, z);
	rmax = _mm_max_epi16(a, b);
	// 8から4
	a    = _mm_unpackhi_epi16(rmin, z);
	b    = _mm_unpacklo_epi16(rmin, z);
	rmin = _mm_min_epi16(a, b);
	a    = _mm_unpackhi_epi16(rmax, z);
	b    = _mm_unpacklo_epi16(rmax, z);
	rmax = _mm_max_epi16(a, b);
	// 4から2
	a    = _mm_unpackhi_epi32(rmin, z);
	b    = _mm_unpacklo_epi32(rmin, z);
	rmin = _mm_min_epi16(a, b);
	a    = _mm_unpackhi_epi32(rmax, z);
	b    = _mm_unpacklo_epi32(rmax, z);
	rmax = _mm_max_epi16(a, b);
	// 2から1
	a    = _mm_unpackhi_epi64(rmin, z);
	b    = _mm_unpacklo_epi64(rmin, z);
	rmin = _mm_min_epi16(a, b);
	a    = _mm_unpackhi_epi64(rmax, z);
	b    = _mm_unpacklo_epi64(rmax, z);
	rmax = _mm_max_epi16(a, b);
	// 結果取り出し
	int val_min = _mm_extract_epi16(rmin, 0);
	int val_max = _mm_extract_epi16(rmax, 0);

	return val_max - val_min;
}

//---------------------------------------------------------------------
//		フレーム内平均値からの絶対値差合計関数
//---------------------------------------------------------------------
int avgdist( int *avg, unsigned char *psrc, int lx, int block_height )
{
	__m128i a, b, r;
	unsigned char *p;
	int sum;
	unsigned char d_avg;

	// ループ２回で結果を取得
	// １回目：平均値を取得
	// ２回目：平均値からの絶対値差合計を取得

	b = _mm_setzero_si128();				// 平均値取得用の比較値
	for(int i=0; i<2; i++){
		p = psrc;							// 取得フレーム開始位置
		r = _mm_setzero_si128();			// 結果初期化
		for(int j=0; j<block_height; j++){
			a = _mm_loadu_si128((__m128i*)p);
			r = _mm_add_epi32(r, _mm_sad_epu8(a, b));
			p += lx;
		}
		sum = _mm_extract_epi16(r, 0) + _mm_extract_epi16(r, 4);

		// １回目の結果は２回目の比較対象値とする（平均値算出＋代入）
		if (i == 0){
			d_avg = (unsigned char) ((sum + (block_height * 16/2)) / (block_height * 16));
			b = _mm_set1_epi8(d_avg);
		}
	}
	*avg = d_avg;
	return sum;
}

