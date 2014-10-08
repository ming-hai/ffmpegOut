﻿//  -----------------------------------------------------------------------------------------
//   ffmpeg / avconv 出力 by rigaya
//  -----------------------------------------------------------------------------------------
//   ソースコードについて
//   ・無保証です。
//   ・本ソースコードを使用したことによるいかなる損害・トラブルについてrigayaは責任を負いません。
//   以上に了解して頂ける場合、本ソースコードの使用、複製、改変、再頒布を行って頂いて構いません。
//  -----------------------------------------------------------------------------------------

#ifndef _AUO_CONF_H_
#define _AUO_CONF_H_

#include <Windows.h>
#include "auo.h"
#include "auo_options.h"

const int CONF_INITIALIZED = 1;

enum {
	TMP_DIR_OUTPUT = 0,
	TMP_DIR_SYSTEM = 1,
	TMP_DIR_CUSTOM = 2,
};

enum {
	RUN_BAT_NONE   = 0x00,
	RUN_BAT_AFTER  = 0x01,
	RUN_BAT_BEFORE = 0x02,
};

static const char *const CONF_NAME    = "ffmpegOut ConfigFile";
const int CONF_NAME_BLOCK_LEN         = 32;
const int CONF_BLOCK_MAX              = 32;
const int CONF_BLOCK_COUNT            = 5; //最大 CONF_BLOCK_MAXまで
const int CONF_HEAD_SIZE              = (3 + CONF_BLOCK_MAX) * sizeof(int) + CONF_BLOCK_MAX * sizeof(size_t) + CONF_NAME_BLOCK_LEN;

enum {
	CONF_ERROR_NONE = 0,
	CONF_ERROR_FILE_OPEN,
	CONF_ERROR_BLOCK_SIZE,
	CONF_ERROR_INVALID_FILENAME,
};

const int CMDEX_MAX_LEN = 2048;    //追加コマンドラインの最大長

enum {
	AMPLIMIT_FILE_SIZE = 0x01, //自動マルチパス時、ファイルサイズのチェックを行う
	AMPLIMIT_BITRATE   = 0x02, //自動マルチパス時、ビットレートのチェックを行う
};

typedef struct {
	BOOL    use_highbit_depth;
	int     output_csp;
	int     pass;
	BOOL    use_auto_npass;
	int		auto_npass;
	int     bitrate;
	BOOL    interlaced;
	BOOL    audio_input;
} CONF_ffmpegout;

typedef struct {
	BOOL   afs;                      //自動フィールドシフトの使用
	BOOL   afs_bitrate_correction;   //afs & 2pass時、ドロップ数に応じてビットレートを補正
	BOOL   auo_tcfile_out;           //auo側でタイムコードを出力する
	BOOL   check_keyframe;           //キーフレームチェックを行う
	int    priority;                 //x264のCPU優先度(インデックス)
	//char   stats[MAX_PATH_LEN];      //x264用ステータスファイルの場所
	//char   tcfile_in[MAX_PATH_LEN];  //x264 tcfile-in用タイムコードファイルの場所
	//char   cqmfile[MAX_PATH_LEN];    //x264 cqmfileの場所
	char   cmdex[CMDEX_MAX_LEN];       //追加コマンドライン
	char   outext[MAX_APPENDIX_LEN];   //出力拡張子
	//int    __yc48_colormatrix_conv;  //YC48の色変換 (使用されていません)
	//DWORD  amp_check;                //自動マルチパス時のチェックの種類(AMPLIMIT_FILE_SIZE/AMPLIMIT_BITRATE)
	//double amp_limit_file_size;      //自動マルチパス時のファイルサイズ制限(MB)
	//double amp_limit_bitrate;        //自動マルチパス時のビットレート制限(kbps)
} CONF_VIDEO; //動画用設定(x264以外)

typedef struct {
	int  encoder;             //使用する音声エンコーダ
	int  enc_mode;            //使用する音声エンコーダの設定
	int  bitrate;             //ビットレート指定モード
	BOOL use_2pass;           //音声2passエンコードを行う
	BOOL use_wav;             //パイプを使用せず、wavを出力してエンコードを行う
	BOOL faw_check;           //FAWCheckを行う
	int  priority;            //音声エンコーダのCPU優先度(インデックス)
	BOOL minimized;           //音声エンコーダを最小化で実行
	int  aud_temp_dir;        //音声専用一時フォルダ
	int  audio_encode_timing; //音声を先にエンコード
} CONF_AUDIO; //音声用設定

typedef struct {
	BOOL disable_mp4ext;  //mp4出力時、外部muxerを使用する
	BOOL disable_mkvext;  //mkv出力時、外部muxerを使用する
	int  mp4_mode;        //mp4 外部muxer用追加コマンドの設定
	int  mkv_mode;        //mkv 外部muxer用追加コマンドの設定
	BOOL minimized;       //muxを最小化で実行
	int  priority;        //mux優先度(インデックス)
	int  mp4_temp_dir;    //mp4box用一時ディレクトリ
	BOOL apple_mode;      //Apple用モード(mp4系専用)
	BOOL disable_mpgext;  //mpg出力時、外部muxerを使用する
	int  mpg_mode;        //mpg 外部muxer用追加コマンドの設定
} CONF_MUX; //muxer用設定

typedef struct {
	BOOL disable_guicmd;         //GUIによるコマンドライン生成を停止(CLIモード)
	int  temp_dir;               //一時ディレクトリ
	BOOL out_audio_only;         //音声のみ出力
	char notes[128];             //メモ
	DWORD run_bat;               //バッチファイルを実行するかどうか
	DWORD dont_wait_bat_fin;     //バッチファイルの処理終了待機をするかどうか
	char batfile_after[MAX_PATH_LEN];   //エンコ後バッチファイルのパス
	char batfile_before[MAX_PATH_LEN];  //エンコ前バッチファイルのパス
} CONF_OTHER;

typedef struct {
	char        conf_name[CONF_NAME_BLOCK_LEN];  //保存時に使用
	int         size_all;                        //保存時: CONF_GUIEXの全サイズ / 設定中、エンコ中: CONF_INITIALIZED
	int         head_size;                       //ヘッダ部分の全サイズ
	int         block_count;                     //ヘッダ部を除いた設定のブロック数
	int         block_size[CONF_BLOCK_MAX];      //各ブロックのサイズ
	size_t      block_head_p[CONF_BLOCK_MAX];    //各ブロックのポインタ位置
	CONF_ffmpegout enc;                          //ffmpegについての設定
	CONF_VIDEO  vid;                             //その他動画についての設定
	CONF_AUDIO  aud;                             //音声についての設定
	CONF_MUX    mux;                             //muxについての設定
	CONF_OTHER  oth;                             //その他の設定
} CONF_GUIEX;

class guiEx_config {
private:
	static const size_t conf_block_pointer[CONF_BLOCK_COUNT];
	static const int conf_block_data[CONF_BLOCK_COUNT];
public:
	guiEx_config();
	static void write_conf_header(CONF_GUIEX *conf);
	static int  adjust_conf_size(CONF_GUIEX *conf_buf, void *old_data, int old_size);
	int  load_auo_conf(CONF_GUIEX *conf, const char *stg_file);       //設定をstgファイルから読み込み
	int  save_auo_conf(const CONF_GUIEX *conf, const char *stg_file); //設定をstgファイルとして保存
};

//定義はffmpegOut.cpp
void init_CONF_GUIEX(CONF_GUIEX *conf, BOOL use_highbit); //初期化し、x264設定のデフォルトを設定 

#endif //_AUO_CONF_H_