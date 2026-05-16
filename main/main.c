#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "driver/i2s_std.h"

#include "i2s_mic.h"

#define SAMPLE_BUFFER_SIZE 256

// INMP441-style I2S mics usually send 24-bit audio inside a 32-bit word.
// Shifting by 8 makes the values easier to work with.
#define SAMPLE_SHIFT 8

// Change this after testing.
// Larger value = less sensitive volume bar.
#define VOLUME_SCALE 2000

static int32_t remove_dc_and_get_volume(int32_t *buffer, int samples_read)
{
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

void app_main(void)
{
    printf("Initializing I2S microphone...\n");

    mic_init();

    printf("Microphone initialized successfully!\n");
    printf("Wiring expected:\n");
    printf("  GPIO 5 -> Mic SCK / BCLK\n");
    printf("  GPIO 6 -> Mic WS / LRCLK\n");
    printf("  GPIO 4 -> Mic SD / DOUT\n");
    printf("  3V3    -> Mic VDD\n");
    printf("  GND    -> Mic GND\n");
    printf("  GND    -> Mic L/R for LEFT channel\n\n");

    int32_t sample_buffer[SAMPLE_BUFFER_SIZE];
    size_t bytes_read = 0;
    int print_counter = 0;

    while (1) {
        esp_err_t err = i2s_channel_read(
            rx_chan,
            sample_buffer,
            sizeof(sample_buffer),
            &bytes_read,
            portMAX_DELAY
        );

        if (err == ESP_OK && bytes_read > 0) {
            int samples_read = bytes_read / sizeof(int32_t);

            int32_t average_volume = remove_dc_and_get_volume(
                sample_buffer,
                samples_read
            );

            // Slow down terminal output
            print_counter++;

            if (print_counter >= 10) {
                int bars = average_volume / VOLUME_SCALE;

                if (bars > 50) {
                    bars = 50;
                }

                printf("Vol [%" PRId32 "] | ", average_volume);

                for (int i = 0; i < bars; i++) {
                    printf("#");
                }

                printf("\n");

                print_counter = 0;
            }
        } else {
            printf("I2S read failed: %s\n", esp_err_to_name(err));
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }
}