﻿// -----------------------------------------------------------------------------------------
// ffmpegOut by rigaya
// -----------------------------------------------------------------------------------------
// The MIT License
//
// Copyright (c) 2012-2017 rigaya
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
// --------------------------------------------------------------------------------------------

#include <Windows.h>
#pragma comment(lib, "user32.lib") //WaitforInputIdle
#include <stdlib.h>
#include <stdio.h>
#include <float.h>
#include <Process.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib") 
#include <limits.h>
#include <shlwapi.h>
#pragma comment(lib, "shlwapi.lib")
#include <vector>

#include "output.h"

#include "auo.h"
#include "auo_frm.h"
#include "auo_pipe.h"
#include "auo_error.h"
#include "auo_conf.h"
#include "auo_util.h"
#include "auo_convert.h"
#include "auo_system.h"
#include "auo_version.h"

#include "auo_encode.h"
#include "auo_video.h"
#include "auo_audio_parallel.h"

#include "cpu_info.h"

typedef struct video_output_thread_t {
    CONVERT_CF_DATA *pixel_data;
    FILE *f_out;
    BOOL abort;
    HANDLE thread;
    HANDLE he_out_start;
    HANDLE he_out_fin;
    int repeat;
} video_output_thread_t;

static const char * specify_input_csp(int output_csp) {
    return specify_csp[output_csp];
}

int get_aviutl_color_format(int use_highbit, int output_csp) {
    //Aviutlからの入力に使用するフォーマット
    switch (output_csp) {
        case OUT_CSP_P010:
        case OUT_CSP_YUV444:
        case OUT_CSP_YUV444_16:
            return CF_YC48;
        case OUT_CSP_RGB:
            return CF_RGB;
        case OUT_CSP_NV12:
        case OUT_CSP_NV16:
        case OUT_CSP_YUY2:
        default:
            return (use_highbit) ? CF_YC48 : CF_YUY2;
    }
}

static int calc_input_frame_size(int width, int height, int color_format) {
    width = (color_format == CF_RGB) ? (width+3) & ~3 : (width+1) & ~1;
    return width * height * COLORFORMATS[color_format].size;
}

//auo_pipe.cppのread_from_pipeの特別版
static int ReadLogEnc(PIPE_SET *pipes, int total_drop, int current_frames) {
    DWORD pipe_read = 0;
    if (!PeekNamedPipe(pipes->stdErr.h_read, NULL, 0, NULL, &pipe_read, NULL))
        return -1;
    if (pipe_read) {
        ReadFile(pipes->stdErr.h_read, pipes->read_buf + pipes->buf_len, sizeof(pipes->read_buf) - pipes->buf_len - 1, &pipe_read, NULL);
        pipes->buf_len += pipe_read;
        write_log_enc_mes(pipes->read_buf, &pipes->buf_len, total_drop, current_frames);
    } else {
        log_process_events();
    }
    return pipe_read;
}

//cmdexのうち、guiから発行されるオプションとの衝突をチェックして、読み取られなかったコマンドを追加する
static void append_cmdex(char *cmd, size_t nSize, const char *cmdex) {
    const size_t cmd_len = strlen(cmd);

    sprintf_s(cmd + cmd_len, nSize - cmd_len, " %s", cmdex);

    //改行のチェックのみ行う
    replace_cmd_CRLF_to_Space(cmd + cmd_len + 1, nSize - cmd_len - 1);
}

static void build_full_cmd(char *cmd, size_t nSize, const CONF_GUIEX *conf, const OUTPUT_INFO *oip, const PRM_ENC *pe, const SYSTEM_DATA *sys_dat, const char *input) {
    CONF_GUIEX prm;
    memcpy(&prm, conf, sizeof(CONF_GUIEX));
    //共通置換を実行
    cmd_replace(prm.vid.cmdex, sizeof(prm.vid.cmdex), pe, sys_dat, conf, oip);
    cmd_replace(prm.vid.incmd, sizeof(prm.vid.incmd), pe, sys_dat, conf, oip);
    //コマンドライン作成
    strcpy_s(cmd, nSize, "-f rawvideo");
    //解像度情報追加(-s)
    if (strcmp(input, PIPE_FN) == NULL)
        sprintf_s(cmd + strlen(cmd), nSize - strlen(cmd), " -s %dx%d", oip->w, oip->h);
    //rawの形式情報追加
    sprintf_s(cmd + strlen(cmd), nSize - strlen(cmd), " -pix_fmt %s", specify_input_csp(prm.enc.output_csp));
    //fps
    int gcd = get_gcd(oip->rate, oip->scale);
    sprintf_s(cmd + strlen(cmd), nSize - strlen(cmd), " -r %d/%d", oip->rate / gcd, oip->scale / gcd);
    //入力追加オプション
    if (strlen(prm.vid.incmd) > 0) sprintf_s(cmd + strlen(cmd), nSize - strlen(cmd), " %s", prm.vid.incmd);
    //入力ファイル
    sprintf_s(cmd + strlen(cmd), nSize - strlen(cmd), " -i \"%s\"", input);
    //音声入力
    if ((oip->flag & OUTPUT_INFO_FLAG_AUDIO) && conf->enc.audio_input) {
        if (!(conf->enc.use_auto_npass && pe->current_x264_pass == 1)) {
            char tmp[MAX_PATH_LEN];
            get_aud_filename(tmp, _countof(tmp), pe, 0);
            sprintf_s(cmd + strlen(cmd), nSize - strlen(cmd), " -i \"%s\"", tmp);
        }
    }
    //コマンドライン追加
    append_cmdex(cmd, nSize, prm.vid.cmdex);
    /////////  vframesを指定すると音声の最後の数秒が切れる場合があるようなので、vfrmaesは指定しない /////////
    //1pass目でafsでない、-vframesがなければ-vframesを指定
    //if ((!prm.vid.afs) && strstr(cmd, "-vframes") == NULL)
    //	sprintf_s(cmd + strlen(cmd), nSize - strlen(cmd), " -vframes %d", oip->n - pe->drop_count);
    //自動2pass
    if (conf->enc.use_auto_npass)
        sprintf_s(cmd + strlen(cmd), nSize - strlen(cmd), " -pass %d", pe->current_x264_pass);
    //出力ファイル
    sprintf_s(cmd + strlen(cmd), nSize - strlen(cmd), " -y \"%s\"", pe->temp_filename);
}

static void set_pixel_data(CONVERT_CF_DATA *pixel_data, const CONF_GUIEX *conf, int w, int h) {
    const int byte_per_pixel = (conf->enc.use_highbit_depth) ? sizeof(short) : sizeof(BYTE);
    ZeroMemory(pixel_data, sizeof(CONVERT_CF_DATA));
    switch (conf->enc.output_csp) {
        case OUT_CSP_NV16: //nv16 (YUV422)
            pixel_data->count = 2;
            pixel_data->size[0] = w * h * byte_per_pixel;
            pixel_data->size[1] = pixel_data->size[0];
            break;
        case OUT_CSP_YUY2: //yuy2 (YUV422)
            pixel_data->count = 1;
            pixel_data->size[0] = w * h * byte_per_pixel * 2;
            break;
        case OUT_CSP_YUV444: //i444 (YUV444 planar)
        case OUT_CSP_YUV444_16:
            pixel_data->count = 3;
            pixel_data->size[0] = w * h * byte_per_pixel;
            pixel_data->size[1] = pixel_data->size[0];
            pixel_data->size[2] = pixel_data->size[0];
            break;
        case OUT_CSP_RGB: //RGB packed
            pixel_data->count = 1;
            pixel_data->size[0] = w * h * 3 * sizeof(BYTE); //8bit only
            break;
        case OUT_CSP_NV12: //nv12 (YUV420)
        case OUT_CSP_P010:
        default:
            pixel_data->count = 2;
            pixel_data->size[0] = w * h * byte_per_pixel;
            pixel_data->size[1] = pixel_data->size[0] / 2;
            break;
    }
    //サイズの総和計算
    for (int i = 0; i < pixel_data->count; i++)
        pixel_data->total_size += pixel_data->size[i];
}

static inline void check_enc_priority(HANDLE h_aviutl, HANDLE h_x264, DWORD priority) {
    if (priority == AVIUTLSYNC_PRIORITY_CLASS)
        priority = GetPriorityClass(h_aviutl);
    SetPriorityClass(h_x264, priority);
}

//並列処理時に音声データを取得する
static AUO_RESULT aud_parallel_task(const OUTPUT_INFO *oip, PRM_ENC *pe) {
    AUO_RESULT ret = AUO_RESULT_SUCCESS;
    AUD_PARALLEL_ENC *aud_p = &pe->aud_parallel; //長いんで省略したいだけ
    if (aud_p->th_aud) {
        //---   排他ブロック 開始  ---> 音声スレッドが止まっていなければならない
        if_valid_wait_for_single_object(aud_p->he_vid_start, INFINITE);
        if (aud_p->he_vid_start && aud_p->get_length) {
            DWORD required_buf_size = aud_p->get_length * (DWORD)oip->audio_size;
            if (aud_p->buf_max_size < required_buf_size) {
                //メモリ不足なら再確保
                if (aud_p->buffer) free(aud_p->buffer);
                aud_p->buf_max_size = required_buf_size;
                if (NULL == (aud_p->buffer = malloc(aud_p->buf_max_size)))
                    aud_p->buf_max_size = 0; //ここのmallocエラーは次の分岐でAUO_RESULT_ERRORに設定
            }
            void *data_ptr = NULL;
            if (NULL == aud_p->buffer || 
                NULL == (data_ptr = oip->func_get_audio(aud_p->start, aud_p->get_length, &aud_p->get_length))) {
                ret = AUO_RESULT_ERROR; //mallocエラーかget_audioのエラー
            } else {
                //自前のバッファにコピーしてdata_ptrが破棄されても良いようにする
                memcpy(aud_p->buffer, data_ptr, aud_p->get_length * oip->audio_size);
            }
            //すでにTRUEなら変更しないようにする
            aud_p->abort |= oip->func_is_abort();
        }
        flush_audio_log();
        if_valid_set_event(aud_p->he_aud_start);
        //---   排他ブロック 終了  ---> 音声スレッドを開始
    }
    return ret;
}

//音声処理をどんどん回して終了させる
static AUO_RESULT finish_aud_parallel_task(const OUTPUT_INFO *oip, PRM_ENC *pe, AUO_RESULT vid_ret) {
    //エラーが発生していたら音声出力ループをとめる (すでにTRUEなら変更しないようにする)
    pe->aud_parallel.abort |= (vid_ret != AUO_RESULT_SUCCESS);
    if (pe->aud_parallel.th_aud) {
        for (int wait_for_audio_count = 0; pe->aud_parallel.he_vid_start; wait_for_audio_count++) {
            vid_ret |= aud_parallel_task(oip, pe);
            if (wait_for_audio_count == 5)
                write_log_auo_line(LOG_INFO, "音声処理の終了を待機しています...");
        }
    }
    return vid_ret;
}

//並列処理スレッドの終了を待ち、終了コードを回収する
static AUO_RESULT exit_audio_parallel_control(const OUTPUT_INFO *oip, PRM_ENC *pe, AUO_RESULT vid_ret) {
    vid_ret |= finish_aud_parallel_task(oip, pe, vid_ret); //wav出力を完了させる
    release_audio_parallel_events(pe);
    if (pe->aud_parallel.buffer) free(pe->aud_parallel.buffer);
    if (pe->aud_parallel.th_aud) {
        //音声エンコードを完了させる
        //2passエンコードとかだと音声エンコーダの終了を待機する必要あり
        int wait_for_audio_count = 0;
        while (WaitForSingleObject(pe->aud_parallel.th_aud, LOG_UPDATE_INTERVAL) == WAIT_TIMEOUT) {
            if (wait_for_audio_count == 10)
                set_window_title("音声処理の終了を待機しています...", PROGRESSBAR_MARQUEE);
            pe->aud_parallel.abort |= oip->func_is_abort();
            log_process_events();
            wait_for_audio_count++;
        }
        flush_audio_log();
        if (wait_for_audio_count > 10)
            set_window_title(AUO_FULL_NAME, PROGRESSBAR_DISABLED);

        DWORD exit_code = 0;
        //GetExitCodeThreadの返り値がNULLならエラー
        vid_ret |= (NULL == GetExitCodeThread(pe->aud_parallel.th_aud, &exit_code)) ? AUO_RESULT_ERROR : exit_code;
        CloseHandle(pe->aud_parallel.th_aud);
    }
    //初期化 (重要!!!)
    ZeroMemory(&pe->aud_parallel, sizeof(pe->aud_parallel));
    return vid_ret;
}

static unsigned __stdcall video_output_thread_func(void *prm) {
    video_output_thread_t *thread_data = reinterpret_cast<video_output_thread_t *>(prm);
    CONVERT_CF_DATA *pixel_data = thread_data->pixel_data;
    WaitForSingleObject(thread_data->he_out_start, INFINITE);
    while (false == thread_data->abort) {
        //映像データをパイプに
        for (int i = 0; i < 1 + thread_data->repeat; i++)
            for (int j = 0; j < pixel_data->count; j++)
                _fwrite_nolock((void *)pixel_data->data[j], 1, pixel_data->size[j], thread_data->f_out);

        thread_data->repeat = 0;
        SetEvent(thread_data->he_out_fin);
        WaitForSingleObject(thread_data->he_out_start, INFINITE);
    }
    return 0;
}

static int video_output_create_thread(video_output_thread_t *thread_data, CONVERT_CF_DATA *pixel_data, FILE *pipe_stdin) {
    AUO_RESULT ret = AUO_RESULT_SUCCESS;
    thread_data->abort = false;
    thread_data->pixel_data = pixel_data;
    thread_data->f_out = pipe_stdin;
    if (   NULL == (thread_data->he_out_start = (HANDLE)CreateEvent(NULL, false, false, NULL))
        || NULL == (thread_data->he_out_fin   = (HANDLE)CreateEvent(NULL, false, true,  NULL))
        || NULL == (thread_data->thread       = (HANDLE)_beginthreadex(NULL, 0, video_output_thread_func, thread_data, 0, NULL))) {
        ret = AUO_RESULT_ERROR;
    }
    return ret;
}

static void video_output_close_thread(video_output_thread_t *thread_data, AUO_RESULT ret) {
    if (thread_data->thread) {
        if (!ret)
            while (WAIT_TIMEOUT == WaitForSingleObject(thread_data->he_out_fin, LOG_UPDATE_INTERVAL))
                log_process_events();
        thread_data->abort = true;
        SetEvent(thread_data->he_out_start);
        WaitForSingleObject(thread_data->thread, INFINITE);
        CloseHandle(thread_data->thread);
        CloseHandle(thread_data->he_out_start);
        CloseHandle(thread_data->he_out_fin);
    }
    memset(thread_data, 0, sizeof(thread_data[0]));
}

static AUO_RESULT ffmpeg_out(CONF_GUIEX *conf, const OUTPUT_INFO *oip, PRM_ENC *pe, const SYSTEM_DATA *sys_dat) {
    AUO_RESULT ret = AUO_RESULT_SUCCESS;
    PIPE_SET pipes = { 0 };
    PROCESS_INFORMATION pi_enc = { 0 };

    char enc_cmd[MAX_CMD_LEN]  = { 0 };
    char enc_args[MAX_CMD_LEN] = { 0 };
    char enc_dir[MAX_PATH_LEN] = { 0 };
    char *enc_path = sys_dat->exstg->s_local.ffmpeg_path;
    
    video_output_thread_t thread_data = { 0 };
    thread_data.repeat = pe->delay_cut_additional_vframe;
    CONVERT_CF_DATA pixel_data;
    set_pixel_data(&pixel_data, conf, oip->w, oip->h);

    int rp_ret;

    //x264優先度関連の初期化
    DWORD set_priority = (pe->h_p_aviutl || conf->vid.priority != AVIUTLSYNC_PRIORITY_CLASS) ? priority_table[conf->vid.priority].value : NORMAL_PRIORITY_CLASS;

    //プロセス用情報準備
    if (!PathFileExists(enc_path)) {
        ret |= AUO_RESULT_ERROR; error_no_exe_file("ffmpeg/avconv", enc_path);
        return ret;
    }
    PathGetDirectory(enc_dir, _countof(enc_dir), enc_path);

    const int color_format = get_aviutl_color_format(conf->enc.use_highbit_depth, conf->enc.output_csp);
    const DWORD aviutl_fourcc = COLORFORMATS[color_format].FOURCC;

    //YUY2/YC48->NV12/YUV444, RGBコピー用関数
    const func_convert_frame convert_frame = get_convert_func(oip->w, conf->enc.use_highbit_depth, conf->enc.interlaced, conf->enc.output_csp);
    if (convert_frame == NULL) {
        ret |= AUO_RESULT_ERROR; error_select_convert_func(oip->w, oip->h, conf->enc.use_highbit_depth, conf->enc.interlaced, conf->enc.output_csp);
        return ret;
    }
    //映像バッファ用メモリ確保
    if (!malloc_pixel_data(&pixel_data, oip->w, oip->h, conf->enc.output_csp, conf->enc.use_highbit_depth)) {
        ret |= AUO_RESULT_ERROR; error_malloc_pixel_data();
        return ret;
    }

    //パイプの設定
    pipes.stdIn.mode = AUO_PIPE_ENABLE;
    pipes.stdErr.mode = AUO_PIPE_ENABLE;
    pipes.stdIn.bufferSize = pixel_data.total_size * 2;

    //コマンドライン生成
    build_full_cmd(enc_cmd, _countof(enc_cmd), conf, oip, pe, sys_dat, PIPE_FN);
    write_log_auo_line(LOG_INFO, "ffmpeg/avconv options...");
    write_args(enc_cmd);
    sprintf_s(enc_args, _countof(enc_args), "\"%s\" %s", enc_path, enc_cmd);
    
    if ((rp_ret = RunProcess(enc_args, enc_dir, &pi_enc, &pipes, (set_priority == AVIUTLSYNC_PRIORITY_CLASS) ? GetPriorityClass(pe->h_p_aviutl) : set_priority, TRUE, FALSE)) != RP_SUCCESS) {
        ret |= AUO_RESULT_ERROR; error_run_process("ffmpeg/avconv", rp_ret);
    } else if (video_output_create_thread(&thread_data, &pixel_data, pipes.f_stdin)) {
        ret |= AUO_RESULT_ERROR; error_video_output_thread_start();
    } else {
        //全て正常
        int i;
        void *frame = NULL;
        BOOL enc_pause = FALSE, copy_frame = FALSE;

        //Aviutlの時間を取得
        PROCESS_TIME time_aviutl;
        GetProcessTime(pe->h_p_aviutl, &time_aviutl);

        //x264が待機に入るまでこちらも待機
        while (WaitForInputIdle(pi_enc.hProcess, LOG_UPDATE_INTERVAL) == WAIT_TIMEOUT)
            log_process_events();

        //ログウィンドウ側から制御を可能に
        DWORD tm_vid_enc_start = timeGetTime();
        enable_x264_control(&set_priority, &enc_pause, FALSE, FALSE, tm_vid_enc_start, oip->n);

        //------------メインループ------------
        for (i = 0, pe->drop_count = 0; i < oip->n; i++) {
            //中断を確認
            ret |= (oip->func_is_abort()) ? AUO_RESULT_ABORT : AUO_RESULT_SUCCESS;

            //x264が実行中なら、メッセージを取得・ログウィンドウに表示
            if (ReadLogEnc(&pipes, pe->drop_count, i) < 0) {
                //勝手に死んだ...
                ret |= AUO_RESULT_ERROR; error_x264_dead();
                break;
            }

            if (!(i & 7)) {
                //Aviutlの進捗表示を更新
                oip->func_rest_time_disp(i + oip->n * (pe->current_x264_pass - 1), oip->n * pe->total_x264_pass);

                //x264優先度
                check_enc_priority(pe->h_p_aviutl, pi_enc.hProcess, set_priority);

                //音声同時処理
                ret |= aud_parallel_task(oip, pe);
            }

            //一時停止
            while (enc_pause & !ret) {
                Sleep(LOG_UPDATE_INTERVAL);
                ret |= (oip->func_is_abort()) ? AUO_RESULT_ABORT : AUO_RESULT_SUCCESS;
                log_process_events();
            }

            //標準入力への書き込み完了をチェック
            while (WAIT_TIMEOUT == WaitForSingleObject(thread_data.he_out_fin, LOG_UPDATE_INTERVAL)) {
                ret |= (oip->func_is_abort()) ? AUO_RESULT_ABORT : AUO_RESULT_SUCCESS;
                log_process_events();
            }

            //中断・エラー等をチェック
            if (AUO_RESULT_SUCCESS != ret)
                break;

            //Aviutl(afs)からフレームをもらう
            if ((frame = oip->func_get_video_ex(i, aviutl_fourcc)) == NULL) {
                ret |= AUO_RESULT_ERROR; error_afs_get_frame();
                break;
            }

            //コピーフレームフラグ処理
            copy_frame = (i && (oip->func_get_flag(i) & OUTPUT_INFO_FRAME_FLAG_COPYFRAME));

            //コピーフレームの場合は、映像バッファの中身を更新せず、そのままパイプに流す
            if (!copy_frame)
                convert_frame(frame, &pixel_data, oip->w, oip->h);  /// YUY2/YC48->NV12/YUV444変換, RGBコピー
            //標準入力への書き込みを開始
            SetEvent(thread_data.he_out_start);

            // 「表示 -> セーブ中もプレビュー表示」がチェックされていると
            // func_update_preview() の呼び出しによって func_get_video_ex() の
            // 取得したバッファが書き換えられてしまうので、呼び出し位置を移動 (拡張AVI出力 plus より)
            oip->func_update_preview();
        }
        //------------メインループここまで--------------

        //書き込みスレッドを終了
        video_output_close_thread(&thread_data, ret);

        //ログウィンドウからのx264制御を無効化
        disable_x264_control();

        //パイプを閉じる
        CloseStdIn(&pipes);

        if (!ret) oip->func_rest_time_disp(oip->n * pe->current_x264_pass, oip->n * pe->total_x264_pass);

        //音声の同時処理を終了させる
        ret |= finish_aud_parallel_task(oip, pe, ret);
        //音声との同時処理が終了
        release_audio_parallel_events(pe);

        //エンコーダ終了待機
        while (WaitForSingleObject(pi_enc.hProcess, LOG_UPDATE_INTERVAL) == WAIT_TIMEOUT)
            ReadLogEnc(&pipes, pe->drop_count, i);

        DWORD tm_vid_enc_fin = timeGetTime();

        //最後にメッセージを取得
        while (ReadLogEnc(&pipes, pe->drop_count, i) > 0);
        
        write_log_auo_line_fmt(LOG_INFO, "CPU使用率: Aviutl: %.2f%% / ffmpeg: %.2f%%", GetProcessAvgCPUUsage(pe->h_p_aviutl, &time_aviutl), GetProcessAvgCPUUsage(pi_enc.hProcess));
        write_log_auo_enc_time("動画エンコード時間", tm_vid_enc_fin - tm_vid_enc_start);
    }

    //解放処理
    if (pipes.stdErr.mode)
        CloseHandle(pipes.stdErr.h_read);
    CloseHandle(pi_enc.hProcess);
    CloseHandle(pi_enc.hThread);

    free_pixel_data(&pixel_data);

    ret |= exit_audio_parallel_control(oip, pe, ret);

    return ret;
}

static void set_window_title_ffmpegout(const PRM_ENC *pe) {
    char mes[256];
    strcpy_s(mes, _countof(mes), "ffmpeg/avconv エンコード");
    if (pe->total_x264_pass > 1)
        sprintf_s(mes + strlen(mes), _countof(mes) - strlen(mes), "   %d / %d pass", pe->current_x264_pass, pe->total_x264_pass);
    if (pe->aud_parallel.th_aud)
        strcat_s(mes, _countof(mes), " + 音声エンコード");
    set_window_title(mes, PROGRESSBAR_CONTINUOUS);
}

static AUO_RESULT video_output_inside(CONF_GUIEX *conf, const OUTPUT_INFO *oip, PRM_ENC *pe, const SYSTEM_DATA *sys_dat) {
    AUO_RESULT ret = AUO_RESULT_SUCCESS;
    //動画エンコードの必要がなければ終了
    if (pe->video_out_type == VIDEO_OUTPUT_DISABLED)
        return ret;

    for (; !ret && pe->current_x264_pass <= pe->total_x264_pass; pe->current_x264_pass++) {
        if (pe->current_x264_pass > 1)
            open_log_window(oip->savefile, sys_dat, pe->current_x264_pass, pe->total_x264_pass);
        set_window_title_ffmpegout(pe);
        ret |= ffmpeg_out(conf, oip, pe, sys_dat);
        set_window_title(AUO_FULL_NAME, PROGRESSBAR_DISABLED);
    }
    return ret;
}

AUO_RESULT video_output(CONF_GUIEX *conf, const OUTPUT_INFO *oip, PRM_ENC *pe, const SYSTEM_DATA *sys_dat) {
    return exit_audio_parallel_control(oip, pe, video_output_inside(conf, oip, pe, sys_dat));
}
