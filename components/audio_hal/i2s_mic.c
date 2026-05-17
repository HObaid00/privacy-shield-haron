#include "i2s_mic.h"
#include "driver/i2s_std.h"
#include "esp_err.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
// #include "i2s_std.h"
// #include <cstd.io>

#define I2S_DIN GPIO_NUM_4  // Microphone data in = SD
#define I2S_BCLK GPIO_NUM_5 // SCK is the new BCLK
#define I2S_WS GPIO_NUM_6   // WS (Word Select) is the new name for LRCLK

#define SAMPLE_BUFFER_SIZE 256

// INMP441-style I2S mics usually send 24-bit audio inside a 32-bit word.
// Shifting by 8 makes the values easier to work with.
#define SAMPLE_SHIFT 8

// Change this after testing.
// Larger value = less sensitive volume bar.
#define VOLUME_SCALE 1000

// Global handle for the RX channel
i2s_chan_handle_t rx_chan;

static int32_t sample_buffer[SAMPLE_BUFFER_SIZE];
static size_t bytes_read;
static int print_counter;

void mic_init(void) {
  printf("Initializing I2S microphone...\n");

  // Allocate a new RX channel
  i2s_chan_config_t chan_cfg =
      I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
  ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &rx_chan));

  // Configure the channel to standard I2S mode (Philips format)
  i2s_std_config_t std_cfg = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT,
                                                      I2S_SLOT_MODE_MONO),
      .gpio_cfg =
          {
              .mclk = I2S_GPIO_UNUSED,
              .bclk = I2S_BCLK,
              .ws = I2S_WS,
              .dout = I2S_GPIO_UNUSED,
              .din = I2S_DIN,
              .invert_flags =
                  {
                      .mclk_inv = false,
                      .bclk_inv = false,
                      .ws_inv = false,
                  },
          },
  };

  printf("Microphone initialized successfully!\n");
  printf("Wiring expected:\n");
  printf("  GPIO 5 -> Mic SCK / BCLK\n");
  printf("  GPIO 6 -> Mic WS / LRCLK\n");
  printf("  GPIO 4 -> Mic SD / DIN\n");
  printf("  3V3    -> Mic VDD\n");
  printf("  GND    -> Mic GND\n");
  printf("  GND    -> Mic L/R for LEFT channel\n\n");

  ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_chan, &std_cfg));

  // 3. Enable the channel so it starts receiving data
  ESP_ERROR_CHECK(i2s_channel_enable(rx_chan));

  bytes_read = 0;
  print_counter = 0;
}

static int32_t remove_dc_and_get_volume(int32_t *buffer, int samples_read) {
  if (samples_read <= 0) {
    return 0;
  }

  // First pass: calculate mean / DC offset
  int64_t sum = 0;

  for (int i = 0; i < samples_read; i++) {
    int32_t sample = buffer[i] >> SAMPLE_SHIFT;
    sum += sample;
  }

  int32_t mean = (int32_t)(sum / samples_read);

  // Second pass: calculate average absolute signal around the mean
  int64_t total_abs = 0;

  for (int i = 0; i < samples_read; i++) {
    int32_t sample = buffer[i] >> SAMPLE_SHIFT;
    sample -= mean;

    if (sample < 0) {
      sample = -sample;
    }

    total_abs += sample;
  }

  return (int32_t)(total_abs / samples_read);
}

void read_microphone() {
  esp_err_t err =
      i2s_channel_read(rx_chan, sample_buffer, sizeof(sample_buffer),
                       &bytes_read, portMAX_DELAY);

  if (err == ESP_OK && bytes_read > 0) {
    int samples_read = bytes_read / sizeof(int32_t);

    int32_t average_volume =
        remove_dc_and_get_volume(sample_buffer, samples_read);

    print_microphone_volume(average_volume);
  } else {
    printf("I2S read failed: %s\n", esp_err_to_name(err));
  }
}

static void print_microphone_volume(int32_t volume) {
  print_counter++;

  // Slowing down printing process, 1 write per 10 reads
  if (print_counter >= 10) {
    int bars = volume / VOLUME_SCALE;

    if (bars > 150) {
      bars = 150;
    }

    printf("Vol [%" PRId32 "] | ", volume);

    for (int i = 0; i < bars; i++) {
      printf("#");
    }

    printf("\n");

    print_counter = 0;
  }
}
