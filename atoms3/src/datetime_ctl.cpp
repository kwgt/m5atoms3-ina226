/*
 * Date time initialize for Arduino environment.
 *
 *  Copyright (C) 2024 Hiroshi Kuwagata <kgt9221@gmail.com>.
 */
#include <Arduino.h>
#include <WiFi.h>
#include <SdFat.h>
#include <time.h>

#include "datetime_ctl.h"

//! 内部用のデフォルトエラーコード
#define DEFAULT_ERROR       (__LINE__)

//! アクセスポイントへの接続タイムアウト(500msの待ちに対する回数で指定)
#define AP_TIMEOUT          (40)

//! 時刻設定時のタイムオフセット
#define TIME_OFFSET         (32400)    // JST+9にを指定

//! 使用するNTPサーバ
#define NTP_SERVER1         ("ntp.nict.jp")
#define NTP_SERVER2         ("ntp.jst.mfeed.ad.jp")

#undef DEBUG

#ifdef DEBUG
#define debug_printf(...)   Serial.printf(__VA_ARGS__)
#define debug_println(...)  Serial.println(__VA_ARGS__)
#define debug_print(...)    Serial.print(__VA_ARGS__)
#else /* defined(DEBUG) */
#define debug_printf(...)
#define debug_println(...)
#define debug_print(...)
#endif /* defined(DEBUG) */

/*
 * 内部関数定義
 */

/**
 * WiFiアクセスポイント情報の読み込み
 *
 * @param[out] ssid  読み込んだSSIDの書き込み先
 * @param[out] pass  読み込んだパスワードの書き込み先
 *
 * @return
 *  読み込みに成功した場合は0を返す。失敗した場合は0以外の値を返す。
 *
 * @remark
 *  本関数はSDカードのルートディレクトリ上のap_info.txtをオープンし、記述され
 *  ているアクセスポイントの設定情報を読み出す（設定情報はテキストで記述され、
 *  一行目にSSID名、二行目にパスワードを記述すること）。
 *  読み込みに成功した場合、引数ssidで渡された領域にNULターミネートされたSSID
 *  文字列が、passにNULLターミネートされたパスワード文字列が格納される。
 */
static int
read_ap_info(char ssid[33], char pass[65])
{
  int ret;
  bool err;
  char tmp[80];
  int i;
  int n;
  SdFile f;

  /*
   * initialize
   */
  ret = 0;

  /*
   * argument check
   */
  do {
    if (ssid == NULL) {
      ret = DEFAULT_ERROR;
      break;
    }

    if (ssid == NULL) {
      ret = DEFAULT_ERROR;
      break;
    }
  } while(false);

  /*
   * open file
   */
  if (!ret) {
    err = f.open("/ap_info.txt");
    if (!err) ret = DEFAULT_ERROR;
  }

  /*
   * read SSID
   */
  if (!ret) {
    do {
      n = f.fgets(tmp, 80);
      if (n < 0) {
        // 読み込んだ文字数として負の数が返されたら読み込みエラー
        ret = DEFAULT_ERROR;
        break;
      }

      if (n > 33) {
        // WPA2でSSIDの最大長は32文字として規定されるので、改行文字を
        // 含めて33文字を超える場合は記述間違いとしてエラー
        ret = DEFAULT_ERROR;
        break;
      }

      Serial.printf("%d\n", n);

      for (i = 0; i < n && tmp[i] != '\n'; i++) ssid[i] = tmp[i];
      ssid[i] = '\0';
    } while (false);
  }

  /*
   * read password
   */
  if (!ret) {
    do {
      n = f.fgets(tmp, 80);
      if (n < 0) {
        // 読み込んだ文字数として負の数が返されたら読み込みエラー
        ret = DEFAULT_ERROR;
        break;
      }

      if (n > 65) {
        // WPA2でパスワードの最大長は64文字として規定されるので、改行
        // 文字を含めて65文字を超える場合は記述間違いとしてエラー
        ret = DEFAULT_ERROR;
        break;
      }

      for (i = 0; i < n && tmp[i] != '\n'; i++) pass[i] = tmp[i];
      pass[i] = '\0';
    } while (false);
  }

  /*
   * post process
   */
  if (f.isOpen()) f.close();
  
  return ret;
}

/**
 * WiFiアクセスポイントへの接続
 *
 * @param[in] ssid  接続先のSSID
 * @param[in] pass  接続先のパスワード
 *
 * @return
 *  読み込みに成功した場合は0を返す。失敗した場合は0以外の値を返す。
 */
static int
connect_to_wifi_ap(char* ssid, char* pass)
{
  int ret;
  int i;

  /*
   * initialize
   */
  ret = 0;

  /*
   * argument check
   */
  do {
    if (ssid == NULL) {
      ret = DEFAULT_ERROR;
      break;
    }

    if (pass == NULL) {
      ret = DEFAULT_ERROR;
      break;
    }
  } while (false);

  /*
   * try connect
   */
  if (!ret) {
    delay(1000);
    debug_printf("Connection to \"%s\" ", ssid);
    WiFi.begin(ssid, pass);

    for (i = 0; ; i++) {
      debug_print(".");

      if (WiFi.status() == WL_CONNECTED) break;
      if (i >= AP_TIMEOUT) break;

      delay(500);
    }

    if (WiFi.status() != WL_CONNECTED) {
      ret = DEFAULT_ERROR;
      debug_println(" TIMEDOUT");
    }
#ifdef DEBUG
    else {
      debug_println(" CONNECTED");
    }
#endif /* defined(DEBUG) */
  }

  /*
   * post process
   */
  if (ret) {
    if (WiFi.status() == WL_CONNECTED) WiFi.disconnect();
  }

  return ret;
}


/*
 * 公開関数の定義
 */

int
datetime_initialize()
{
  int ret;
  int err;
  char ssid[33];
  char pass[65];
  int i;
  struct tm tm;

#ifdef DEBUG
  char str[80];
#endif /* defined(DEBUG) */

  /*
   * initialize
   */
  ret   = 0;

  /*
   * do process
   */
  do {
    /*
     * read AP information
     */
    err = read_ap_info(ssid, pass);
    if (err) {
      ret = DEFAULT_ERROR;
      break;
    }

    /*
     * connecting to WiFI AP
     */
    err = connect_to_wifi_ap(ssid, pass);
    if (err) {
      ret = DEFAULT_ERROR;
      break;
    }

    /*
     * configuration for system time
     */
    configTime(TIME_OFFSET, 0, NTP_SERVER1, NTP_SERVER2);

    /*
     * test load
     */
    if (!getLocalTime(&tm)) {
      ret = DEFAULT_ERROR;
      debug_println("date time configuration failed.");
      break;
    }

#ifdef DEBUG
    strftime(str, 80, "%Y/%m/%d %H:%M:%S", &tm);
    debug_printf("datetime initialized, %s\n", str);
#endif /* defined(DEBUG) */

  } while (false);

  /*
   * post process
   */
  if (WiFi.status() == WL_CONNECTED) WiFi.disconnect();

  return ret;
}
