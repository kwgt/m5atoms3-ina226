/*
 * Date time initialize for Arduino environment.
 *
 *  Copyright (C) 2024 Hiroshi Kuwagata <kgt9221@gmail.com>.
 */
#ifndef __DATETIME_CTL_H__
#define __DATETIME_CTL_H__

#ifdef __cplusplus
extern "C" {
#endif /* defined(__cplusplus) */

/**
 * 時刻情報の初期化
 *
 * @retrun
 *   処理に成功した場合は0を、失敗した場合は0以外の値を返す。
 *
 * @remark
 *  本関数を呼び出すと以下の処理を行う。
 *
 *   1. SDカードからWiFiアクセスポイント情報の読み出し
 *   2. WiFiアクセスポイントへの接続
 *   3. NTPを用いた時刻調整
 *
 *  1.について、 SDカードのルーテディレクトリ上の"ap_info.txt"ファイルからアク
 *  セスポイント情報を読み出す。このファイルには、テキストで一行目に接続対象の
 *  アクセスポイントのSSID、二行目にそのパスワードを記述しておく必要がある。
 *
 *  3.についてはNTPサーバはntp.nict.jpでハードコーディングされているので注意す
 *  ること。また、タイムゾーンはJST+9に固定で設定される。
 *
 *  本関数の処理が成功すると、getLocalTime()関数を用いて時刻情報の取得が可能と
 *  なる
 */
int datetime_initialize();

#ifdef __cplusplus
}
#endif /* defined(__cplusplus) */
#endif /* !defined(__DATETIME_CTL_H__) */
