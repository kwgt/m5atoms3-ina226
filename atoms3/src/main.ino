#include <SdFat.h>
#include <M5Unified.h>

#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>
#include <freertos/semphr.h>

#include <math.h>

#include "writer.hpp"
#include "datetime_ctl.h"

//! INA226の設定アドレス
#define ADDR            (0x4a)

//! I2Cのクロックの設定周波数
#define FREQ            (250000)

//! Configurationレジスタ AVGフィールドの設定値
#define AVG             (0)

//! Configurationレジスタ VbusCTフィールドの設定値
#define VBUSCT          (3)

//! Configurationレジスタ VshCTフィールドの設定値
#define VSHCT           (3)

//! Configurationレジスタ MODEフィールドの設定値
#define MODE            (7)

//! Configurationレジスタの設定値
#define CONFIG          ((AVG << 8)|(VBUSCT << 6)|(VSHCT << 3)|(MODE << 0))

//! CORRECTレジスタの設定値
#define CORRECT         (2028)

//! SDアクセス用SPIの速度設定
#define SPI_SPEED       SD_SCK_MHZ(10)

//! SdFatのコンフィギュレーション設定
#define SD_CONFIG       SdSpiConfig(0, SHARED_SPI, SPI_SPEED)

//! SPI制御のSCKに割り当てるGPIOの番号
#define  SCK            (7)

//! SPI制御のMISOに割り当てるGPIOの番号
#define  MISO           (8)

//! SPI制御のMOSIに割り当てるGPIOの番号
#define  MOSI           (6)

//! SPI制御のSSに割り当てるGPIOの番号
#define  SS             (-1)

//! レジスタ値から電圧(V)に変換するための係数
#define VOLTAGE_RATIO   (0.00125)

//! レジスタ値から電流(mA)に変換するための係数
#define CURRENT_RATIO   (0.1)

//! 計測値格納用の構造体
typedef struct {
  //! 電圧値(V)
  float voltage;

  //! 電流値(A)
  float current;
} measure_value_t;

//! 電力測定モジュールから読み出した値を格納する型
typedef struct {
  //! 最後に読み出した値
  measure_value_t latest;

  //! 最小値
  measure_value_t minimum;

  //! 最大値
  measure_value_t maximum;
} value_set_t;

//! SDカードインタフェースオブジェクト
static SdFat SD;

//! データを格納する領域(取得用)
static value_set_t capture = {{NAN, NAN}, {NAN, NAN}, {NAN, NAN}};

//! データを格納する領域(表示用)
static value_set_t display = {{NAN, NAN}, {NAN, NAN}, {NAN, NAN}};

//! 画面表示用スプライト(フレームバッファとして使用)
static M5Canvas canvas(&M5.Lcd);

//! 計測用の周期起床ハンドラ
static TimerHandle_t timer = NULL;

//! 排他制御用のミューテックス
static SemaphoreHandle_t mutex = NULL;

//! 書き込み中を表すフラグ
static bool enableStorage = true;

//! 書き込み中を表すフラグ
static bool enableDatetime = true;

//! 書き込み中を表すフラグ
static bool writingFlag = false;

/**
 * INA226へのレジスタ書き込み
 *
 * @param [in] reg  レジスタ番号
 * @param [in] val  書き込む値(16ビット値)
 *
 * @return
 *  書き込みに成功した場合はtrueを返す。失敗した場合はfalseを返す。
 */
static bool
write_register(uint8_t reg, uint16_t val)
{
  uint8_t tmp[2];

  tmp[0] = (val >> 8) & 0xff;
  tmp[1] = (val >> 0) & 0xff;

  return M5.Ex_I2C.writeRegister(ADDR, reg, tmp, 2, FREQ);
}

/**
 * INA226からのレジスタ読みだし
 *
 * @param [in] reg  レジスタ番号
 * @param [in] val  読み出した値(16ビット値)の書き込み先
 *
 * @return
 *  読み出しに成功した場合はtrueを返す。失敗した場合はfalseを返す。
 */
static bool
read_register(uint8_t reg, uint16_t* dst)
{
  bool ret;
  uint8_t tmp[2];

  ret = M5.Ex_I2C.readRegister(ADDR, reg, tmp, 2, FREQ);
  if (ret) {
    *dst = ((tmp[0] << 8) & 0xff00)|
           ((tmp[1] << 0) & 0x00ff);
  }

  return ret;
}

/**
 * 計測用コールバック関数
 *
 * @param [in] handle  タイマーハンドラ(未使用)
 */
static void
measure_callback(TimerHandle_t handle)
{
  struct {
    uint32_t ts;
    int16_t vol;
    int16_t cur;
  } data;

  float vol;
  float cur;

  measure_value_t* latest  = &capture.latest;
  measure_value_t* minimum = &capture.minimum;
  measure_value_t* maximum = &capture.maximum;

  /*
   * センサーからの値の読み出し
   */

  // 電圧の読み取り
  read_register(2, (uint16_t*)&data.vol);
  vol = data.vol * VOLTAGE_RATIO;

  // 電流の読み取り
  read_register(4, (uint16_t*)&data.cur);
  cur = data.cur * CURRENT_RATIO;

  /*
   * 値の評価
   */
  if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
    /*
     * データの書き込み
     */
    if (writingFlag) {
      data.ts = millis();
      writer_push(&data, sizeof(data));
    }

    /*
     * 電圧値の評価(無効値は除外)
     */
    if (vol < 40.0) {
      // 最新値を更新
      latest->voltage = vol;

      // 最小を更新
      if (isnan(minimum->voltage) || vol < minimum->voltage) {
        minimum->voltage = vol;
      }

      // 最大を更新
      if (isnan(maximum->voltage) || vol > maximum->voltage) {
        maximum->voltage = vol;
      }
    }

    /*
     * 電流値の評価(無効値は除外)
     */
    if (cur < 20000.0) {
      // 最新値を更新
      latest->current = cur;

      // 最小を更新
      if (isnan(minimum->current) || cur < minimum->current) {
        minimum->current = cur;
      }

      // 最大を更新
      if (isnan(maximum->current) || cur > maximum->current) {
        maximum->current = cur;
      }
    }

    xSemaphoreGive(mutex);
  }
}

/**
 * ボタン状態の評価
 */
static void
eval_button()
{
  if (M5.BtnA.wasHold()) {
    if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
      if (!writingFlag) {
        start_writer_task();

      } else {
        writer_finish();
      }

      writingFlag = !writingFlag;
      xSemaphoreGive(mutex);
    }

  } else if (M5.BtnA.wasDoubleClicked()) {
    // ダブルクリックの場合 (最小・最大値のクリア)

    if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
      capture.minimum = {NAN, NAN};
      capture.maximum = {NAN, NAN};

      xSemaphoreGive(mutex);
    }
  }
}

/**
 * センサーから読み取った値を表示用データとして取り込む処理
 */
static void
load_value()
{
  /*
   * センサーから読み取った値を表示用領域にコピー
   * ※ 表示中に、タイマーが起動され値が書き換わるのを防ぐことを目的としている
   */
  if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
    display = capture;

    xSemaphoreGive(mutex);
  }
}

/**
 * 表示更新処理
 */
static void
display_update()
{
  float val;
  float min;
  float max;
  char buf[30];
  int x;
  int y;

  y = 4;

  /*
   * フレームバッファのクリア
   */
  canvas.fillScreen(TFT_BLACK);

  /*
   * 電圧の表示
   */
  // 計測値 (値)
  if (!isnan(display.latest.voltage)) {
    sprintf(buf, "%6.3f", display.latest.voltage);

    canvas.setFont(&fonts::lgfxJapanGothic_24);
    canvas.setTextColor(TFT_WHITE);
    canvas.setCursor(18, y);
    canvas.print(buf);
  }

  // 計測値 (単位)
  canvas.setFont(&fonts::lgfxJapanGothic_16);
  canvas.setTextColor(TFT_WHITE);
  canvas.setCursor(18 + 74, y + 8);
  canvas.print("V");

  y += 24;

  // 最小・最大値 (値)
  if (!(isnan(display.minimum.voltage) || isnan(display.maximum.voltage))) {
    sprintf(buf, "%6.3f", display.minimum.voltage);

    canvas.setFont(&fonts::lgfxJapanGothic_16);
    canvas.setTextColor(TFT_CYAN);
    canvas.setCursor(28, y);
    canvas.print(buf);

    canvas.setFont(&fonts::lgfxJapanGothic_8);
    canvas.setTextColor(TFT_CYAN);
    canvas.setCursor(28 + 52, y + 6);
    canvas.print("(最小)");

    sprintf(buf, "%6.3f", display.maximum.voltage);

    canvas.setFont(&fonts::lgfxJapanGothic_16);
    canvas.setTextColor(TFT_PINK);
    canvas.setCursor(28, y + 16);
    canvas.print(buf);

    canvas.setFont(&fonts::lgfxJapanGothic_8);
    canvas.setTextColor(TFT_PINK);
    canvas.setCursor(28 + 52, y + 22);
    canvas.print("(最大)");
  }

  y += 36;

  /*
   * 電流の表示
   */
  // 計測値 (値)
  if (!isnan(display.latest.current)) {
    sprintf(buf, "%6.1f", display.latest.current);

    canvas.setFont(&fonts::lgfxJapanGothic_24);
    canvas.setTextColor(TFT_WHITE);
    canvas.setCursor(18, y);
    canvas.print(buf);
  }

  // 計測値 (単位)
  canvas.setFont(&fonts::lgfxJapanGothic_16);
  canvas.setTextColor(TFT_WHITE);
  canvas.setCursor(18 + 74, y + 8);
  canvas.print("mA");

  y += 24;

  // 最小・最大値 (値)
  if (!(isnan(display.minimum.current) || isnan(display.maximum.current))) {
    sprintf(buf, "%6.1f", display.minimum.current);

    canvas.setFont(&fonts::lgfxJapanGothic_16);
    canvas.setTextColor(TFT_CYAN);
    canvas.setCursor(28, y);
    canvas.print(buf);

    canvas.setFont(&fonts::lgfxJapanGothic_8);
    canvas.setTextColor(TFT_CYAN);
    canvas.setCursor(28 + 52, y + 6);
    canvas.print("(最小)");

    sprintf(buf, "%6.1f", display.maximum.current);

    canvas.setFont(&fonts::lgfxJapanGothic_16);
    canvas.setTextColor(TFT_PINK);
    canvas.setCursor(28, y + 16);
    canvas.print(buf);

    canvas.setFont(&fonts::lgfxJapanGothic_8);
    canvas.setTextColor(TFT_PINK);
    canvas.setCursor(28 + 52, y + 22);
    canvas.print("(最大)");
  }

  /*
   * 記録状態のマーキング
   */
  if (enableStorage) {
    if (writingFlag) {
      canvas.fillCircle(118, 10, 5, TFT_RED);
    } else {
      canvas.fillCircle(118, 10, 5, (enableDatetime)? TFT_BLUE: TFT_CYAN);
    }

  } else {
    canvas.fillCircle(118, 10, 5, TFT_BLUE);
    canvas.drawCircle(118, 10, 5, TFT_RED);
    canvas.drawLine(115, 7, 121, 13, TFT_RED);
  }

  /*
   * 液晶画面への反映
   */
  M5.Lcd.startWrite();
  canvas.pushSprite(0, 0);
  M5.Lcd.endWrite();
}

/**
 * ライタータスクの起動関数
 */
static void
start_writer_task()
{
  // あまり良い方法ではないが、管理が面倒だったので書き込みタスクに渡すパス文
  // 字列を保持し続けるためにstaticローカルにしています。
  // ※状態遷移的に、writer_finish()が呼び出されるまでこの関数が呼び出されるこ
  //   とはないので内容は変更されないはず…
  
  static char path[64];

  bool err;
  time_t t;
  struct tm* tm;

  int fno = 1;

  if (enableDatetime) {
    t  = time(NULL);
    tm = localtime(&t);

    sprintf(path,
            "/powerlog-%04d%02d%02d-%02d%02d%02d.dat",
            1900 + tm->tm_year,
            tm->tm_mon + 1,
            tm->tm_mday,
            tm->tm_hour,
            tm->tm_min,
            tm->tm_sec);

  } else {
    do {
      sprintf(path, "/powerlog-%03d.dat", fno++);
    } while (SD.exists(path));
  }

  writer_start(path);
}

/**
 * SdFatの時刻情報取得用のコールバック関数
 *
 * @param [out] dst1  日付情報の書き込み先
 * @param [out] dst2  時刻情報(秒以上)の書き込み先
 * @param [out] dst3  時刻情報(秒未満)の書き込み先
 *
 * @remark
 *  FATエントリのタイムスタンプについて、Linuxと Windowsでタイムゾーンの扱いが
 *  異なるのでWindowsで正しい時間が表示されるように調整を行っている。
 */
static void
fs_timestamp_callback(uint16_t* dst1, uint16_t* dst2, uint8_t* dst3)
{
  time_t t;
  struct tm* tm;

  if (enableDatetime) {
    t  = time(NULL);
    tm = localtime(&t);   // for Windows
    // tm = gmtime(&t);    // for Linux

    *dst1 = FS_DATE(1900 + tm->tm_year, tm->tm_mon + 1, tm->tm_mday);
    *dst2 = FS_TIME(tm->tm_hour, tm->tm_min, tm->tm_sec);
    *dst3 = 0;
  }
}

/**
 * セットアップ処理
 */
void
setup()
{
  /*
   * ボードの初期化
   */
  M5.begin();

  /*
   * I2Cの初期化
   */
  M5.Ex_I2C.begin();

  /*
   * SPIの初期化
   */
  SPI.begin(SCK, MISO, MOSI, SS);

  /*
   * 液晶の設定
   */
  M5.Lcd.setBrightness(32);
  M5.Lcd.setRotation(2);

  /*
   * INA226の初期化
   */
  // set configuration register
  write_register(0, CONFIG);

  // set correct register
  write_register(5, CORRECT);

  /*
   * SDカードの初期化
   */
  if (!SD.begin(SD_CONFIG)) enableStorage = false;

  /*
   * 時刻の設定
   */
  if (enableStorage) {
    if (datetime_initialize()) {
      enableDatetime = false;
    } else {
      FsDateTime::setCallback(fs_timestamp_callback);
    }
  }

  /*
   * フレームバッファの初期化
   */
  canvas.setColorDepth(16);
  canvas.createSprite(M5.Lcd.width(), M5.Lcd.height());

  /*
   * その他資源の確保
   */
  mutex = xSemaphoreCreateMutex();

  /*
   * 計測用周期起床ハンドラの起動
   */
  timer = xTimerCreate("measure timer",
                       pdMS_TO_TICKS(1),
                       pdTRUE,
                       NULL,
                       measure_callback);

  xTimerStart(timer, 0);
}

/**
 * ループ処理
 */
void
loop()
{
  unsigned long t0 = millis();

  /*
   * ボタン状態の更新。
   */
  M5.update();

  /*
   * ボタン状態の評価
   */
  eval_button();

  /*
   * 数値の取得
   */
  load_value();

  /*
   * 表示の更新
   */
  display_update();

  /*
   * 次のループまでの待ち
   */
  delay(100 - (millis() - t0));
}
