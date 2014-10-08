﻿//  -----------------------------------------------------------------------------------------
//   ffmpeg / avconv 出力 by rigaya
//  -----------------------------------------------------------------------------------------
//   ソースコードについて
//   ・無保証です。
//   ・本ソースコードを使用したことによるいかなる損害・トラブルについてrigayaは責任を負いません。
//   以上に了解して頂ける場合、本ソースコードの使用、複製、改変、再頒布を行って頂いて構いません。
//  -----------------------------------------------------------------------------------------

#ifndef _AUO_VERSION_H_
#define _AUO_VERSION_H_

#define AUO_VERSION          0,0,4,0
#define AUO_VERSION_STR      "0.04"
#define AUO_NAME_WITHOUT_EXT "ffmpegOut"
#define AUO_NAME             "ffmpegOut.auo"
#define AUO_NAME_W          L"ffmpegOut.auo"
#define AUO_FULL_NAME        "ffmpeg / avconv 出力 "
#define AUO_VERSION_NAME     "ffmpeg / avconv 出力 "AUO_VERSION_STR
#define AUO_VERSION_INFO     "ffmpeg / avconv 出力 "AUO_VERSION_STR" by rigaya"
#define AUO_EXT_FILTER       "All Support Formats (*.*)\0*\0"

#ifdef DEBUG
#define VER_DEBUG   VS_FF_DEBUG
#define VER_PRIVATE VS_FF_PRIVATEBUILD
#else
#define VER_DEBUG   0
#define VER_PRIVATE 0
#endif

#define VER_STR_COMMENTS         AUO_FULL_NAME
#define VER_STR_COMPANYNAME      ""
#define VER_STR_FILEDESCRIPTION  AUO_FULL_NAME
#define VER_FILEVERSION          AUO_VERSION
#define VER_STR_FILEVERSION      AUO_VERSION_STR
#define VER_STR_INTERNALNAME     AUO_FULL_NAME
#define VER_STR_ORIGINALFILENAME AUO_NAME
#define VER_STR_LEGALCOPYRIGHT   AUO_FULL_NAME" by rigaya"
#define VER_STR_PRODUCTNAME      AUO_NAME_WITHOUT_EXT
#define VER_PRODUCTVERSION       VER_FILEVERSION
#define VER_STR_PRODUCTVERSION   VER_STR_FILEVERSION

#endif //_AUO_VERSION_H_