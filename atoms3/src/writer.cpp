/*
 * DC power monitor for INA226 with M5 AtomS3
 *
 *  Copyright (C) 2024 Hiroshi Kuwagata <kgt9221@gmail.com>.
 */
#include <stdint.h>

#include <SdFat.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

#include "esp32-hal.h"
#include "writer.hpp"

//! バッファのサイズ
#define BUFF_SIZE       (16384)

//! デフォルトのエラーコード
#define DEFAULT_ERROR   (__LINE__)

//! タスク終了通知イベント
#define TASK_COMPLETE   (0x000000001)

//! LED点灯用のウェイト時間 (ミリ秒で指定)
#define EMIT_DURATION   (500)

//! 処理状態
static int state = 0;

//! 排他制御用のミューテックス
static SemaphoreHandle_t mutex = NULL;

//! 書き込みタスクとの連絡用キュー
static QueueHandle_t queue = NULL;

//! 書き込みタスクのハンドラ
static TaskHandle_t task = NULL;

//! イベント通知用のイベントグループ
static EventGroupHandle_t events = NULL;

//! バッファ（表面）
static uint8_t buff_plane1[BUFF_SIZE];

//! バッファ（裏面）
static uint8_t buff_plane2[BUFF_SIZE];

//! 現在のデータ追加用のバッファの面
static uint8_t* cur_buff = buff_plane1;

//! 現在のバッファ使用量
static size_t used = 0;

//! 書き込み情報
struct Command {
  enum {
    OP_FLUSH,
    OP_EXIT
  } op;

  uint8_t* data;
  size_t size;
};

/*
 * 内部関数の定義
 */
static void
writer_task_func(void* arg)
{
  char *path = (char*)arg;
  bool error;
  Command cmd;
  SdFile file;

  error = !file.open(path, O_WRONLY | O_CREAT | O_TRUNC);

  /*
   * コマンド受信ループ
   */
  while (true) {
    if (xQueueReceive(queue, &cmd, portMAX_DELAY) == pdPASS) {
      //disableCore0WDT();

      if (!error) {
        if (cmd.size > 0) {
          if (file.write((const uint8_t*)cmd.data, cmd.size) == cmd.size) {
            error = !file.sync();
          } else {
            error = true;
          }
        }
      }
 
      delay(1);

      //enableCore0WDT();

      if (cmd.op == Command::OP_EXIT) break;
    }
  }

  file.close();

  xEventGroupSetBits(events, TASK_COMPLETE);
  vTaskDelete(NULL);
}

static int
push_byte(uint8_t b, bool* dst)
{
  int ret;
  Command cmd;

  /*
   * initialize
   */
  ret   = 0;

  /*
   * push data
   */
  cur_buff[used++] = b;

  if (used == BUFF_SIZE) {
    cmd.op   = Command::OP_FLUSH;
    cmd.data = cur_buff;
    cmd.size = BUFF_SIZE;

    if (xQueueSend(queue, &cmd, portMAX_DELAY) != pdPASS) {
      ESP_LOGD("writer_push", "Queue fauled.");
      ret = DEFAULT_ERROR;
    }

    cur_buff = (cur_buff == buff_plane1)? buff_plane2: buff_plane1;
    used     = 0;

    // 書き込みのエッジのみマーク
    if (*dst == false) *dst = true;
  }

  /*
   * post process
   */
  // nothing

  return ret;
}

/*
 * 公開関数の定義
 */

int
writer_start(const char* path)
{
  int ret;
  BaseType_t err;

  /*
   * initialize
   */
  ret = 0;

  /*
   * argument check
   */
  if (path == NULL) ret = DEFAULT_ERROR;

  /*
   * state check
   */
  if (state != 0) ret = DEFAULT_ERROR;

  /*
   * start task
   */
  if (!ret) {
    queue = xQueueCreate(3, sizeof(Command));
    if (queue == NULL) ret = DEFAULT_ERROR;
  }

  if (!ret) {
    mutex = xSemaphoreCreateMutex();
    if (mutex == NULL) ret = DEFAULT_ERROR;
  }

  if (!ret) {
    events = xEventGroupCreate();
    if (events == NULL) ret = DEFAULT_ERROR;
  }

  if (!ret) {
    // 無線系を使っていないのでPRO_CPUが余ってるはず…
    err = xTaskCreateUniversal(writer_task_func,
                               "Writer task",
                               8192,
                               (void*)path,
                               1,
                               &task,
                               PRO_CPU_NUM);
    if (err != pdPASS) ret = DEFAULT_ERROR;
  }

  /*
   * transition state
   */
  if (!ret) state = 1;

  /*
   * post process
   */
  if (ret) {
    if (queue != NULL) vQueueDelete(queue);
    if (mutex != NULL) vSemaphoreDelete(mutex);
    if (events != NULL) vEventGroupDelete(events);
    if (task != NULL) vTaskDelete(task);

    queue  = NULL;
    mutex  = NULL;
    events = NULL;
    task   = NULL;
  }

  return 0;
}

int
writer_push(const char* s, bool* dst)
{
  int ret;
  int err;
  int lock;
  bool wrote;
  int i;

  /*
   * initialize
   */
  ret   = 0;
  lock  = 0;
  wrote = false;

  /*
   * mutex lock
   */
  if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
    lock = !0;
  } else {
    ret = DEFAULT_ERROR;
  }

  /*
   * state check
   */
  if (!ret) {
    if (state != 1) ret = DEFAULT_ERROR;
  }

  /*
   * push data
   */
  if (!ret) {
    for (i = 0; s[i] != '\0'; i++) {
      err = push_byte((uint8_t)s[i], &wrote);
      if (err) {
        ret = DEFAULT_ERROR;
        break;
      }
    }
  }

  /*
   * put return parameter
   */
  if (!ret) {
    if (dst != NULL) *dst = wrote;
  }

  /*
   * post process
   */
  if (lock) xSemaphoreGive(mutex);

  return ret;
}

int
writer_push(const void* src, size_t size, bool* dst)
{
  int ret;
  int err;
  int lock;
  uint8_t* p;
  bool wrote;
  int i;

  /*
   * initialize
   */
  ret   = 0;
  lock  = 0;
  wrote = false;

  /*
   * mutex lock
   */
  if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
    lock = !0;
  } else {
    ret = DEFAULT_ERROR;
  }

  /*
   * state check
   */
  if (!ret) {
    if (state != 1) ret = DEFAULT_ERROR;
  }

  /*
   * push data
   */
  if (!ret) {
    for (i = 0, p = (uint8_t*)src; i < size; i++, p++) {
      err = push_byte(*p, &wrote);
      if (err) {
        ret = DEFAULT_ERROR;
        break;
      }
    }
  }

  /*
   * put return parameter
   */
  if (!ret) {
    if (dst != NULL) *dst = wrote;
  }

  /*
   * post process
   */
  if (lock) xSemaphoreGive(mutex);

  return ret;
}

int
writer_push(uint8_t b, bool* dst)
{
  int ret;
  int err;
  int lock;
  bool wrote;

  /*
   * initialize
   */
  ret   = 0;
  lock  = 0;
  wrote = false;

  /*
   * mutex lock
   */
  if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
    lock = !0;
  } else {
    ret = DEFAULT_ERROR;
  }

  /*
   * state check
   */
  if (!ret) {
    if (state != 1) ret = DEFAULT_ERROR;
  }

  /*
   * push data
   */
  if (!ret) {
    err = push_byte(b, &wrote);
    if (err) ret = DEFAULT_ERROR;
  }

  /*
   * put return parameter
   */
  if (!ret) {
    if (dst != NULL) *dst = wrote;
  }

  /*
   * post process
   */
  if (lock) xSemaphoreGive(mutex);

  return ret;
}

int
writer_finish()
{
  int ret;
  int lock;
  Command cmd;

  /*
   * initialize
   */
  ret  = 0;
  lock = 0;

  /*
   * mutex lock
   */
  if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
    lock = !0;
  } else {
    ret = DEFAULT_ERROR;
  }

  /*
   * state check
   */
  if (ret) {
    if (state != 1) ret = DEFAULT_ERROR;
  }

  /*
   * queue command
   */
  if (!ret) {
    cmd.op   = Command::OP_EXIT;
    cmd.data = cur_buff;
    cmd.size = used;

    if (xQueueSend(queue, &cmd, portMAX_DELAY) != pdPASS) {
      ESP_LOGD("writer_finish", "Queue fauled.");
      ret = DEFAULT_ERROR;
    }
  }

  /*
   * wait for task complete
   */
  if (!ret) {
    xEventGroupWaitBits(events, TASK_COMPLETE, pdTRUE, pdTRUE, portMAX_DELAY);
  }

  /*
   * state transition
   */
  if (!ret) state = 2;

  /*
   * post process
   */
  if (lock) xSemaphoreGive(mutex);

  if (!ret) {
    if (queue != NULL) vQueueDelete(queue);
    if (mutex != NULL) vSemaphoreDelete(mutex);
    if (events != NULL) vEventGroupDelete(events);

    queue    = NULL;
    mutex    = NULL;
    events   = NULL;
    task     = NULL;

    cur_buff = buff_plane1;
    used     = 0;
    state    = 0;
  }

  return ret;
}
