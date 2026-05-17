#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/i2s_std.h"
#include "esp_err.h"

#include "i2s_mic.h"

void app_main(void) {
  mic_init();

  while (1) {
    read_microphone();
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}
