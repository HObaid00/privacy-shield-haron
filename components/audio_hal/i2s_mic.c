#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "global_config.h"
#include "sdkconfig.h"
#include "esp_timer.h"

static const char *TAG = "AUDIO_HAL_MIC";
static i2s_chan_handle_t rx_handle;

extern QueueHandle_t audio_ai_queue;

void audio_hal_mic_init(void) {
    if (true) {
        // Boost USB baud rate for raw sample streaming
        uart_set_baudrate(UART_NUM_0, 2000000);
    }
    
    ESP_LOGI(TAG, "Initializing I2S microphone hardware...");
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &rx_handle));

    // Configure I2S for 16kHz, 32-bit, Mono
    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(16000), 
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = -1,             
            .bclk = PIN_I2S_MIC_BCLK,
            .ws   = PIN_I2S_MIC_LRCLK,
            .dout = -1,             
            .din  = PIN_I2S_MIC_DIN,   
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };

    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT; 
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_handle));
}

void audio_hal_mic_read_task(void *pvParameters) {
    int32_t raw_samples[512];
    int16_t ai_buffer[512];
    int32_t dc_offset = 0;
    bool is_calibrated = false;
    long long calibration_sum = 0;
    int calibration_samples_read = 0;
    
    ESP_LOGI(TAG, "Stay completely quiet for 1 second. Calibrating...");

    while(1) {
        size_t bytes_read = 0;
        esp_err_t err = i2s_channel_read(rx_handle, raw_samples, sizeof(raw_samples), &bytes_read, portMAX_DELAY);
        
        if (err == ESP_OK && bytes_read > 0) {
            int samples_read = bytes_read / 4; // 4 bytes per 32-bit sample

            // 1. DC offset calibration phase
            if (!is_calibrated) {
                for (int i = 0; i < samples_read; i++) {
                    calibration_sum += (raw_samples[i] >> 16);
                    calibration_samples_read++;
                }
                
                if (calibration_samples_read >= 16000) {
                    dc_offset = calibration_sum / calibration_samples_read;
                    is_calibrated = true;
                    ESP_LOGI(TAG, "Calibration complete! DC Offset: %ld. Starting DSP pipeline...", dc_offset);
                }
            } 
            // 2. Audio processing and queue streaming phase
            else {
                for (int i = 0; i < samples_read; i++) {
                    // Apply offset correction and downshift 32-bit to 16-bit
                    int32_t corrected = (raw_samples[i] >> 16) - dc_offset;
                    
                    // Clip to prevent int16_t overflow safely
                    if (corrected > 32767) corrected = 32767;
                    if (corrected < -32768) corrected = -32768;
                    
                    ai_buffer[i] = (int16_t)corrected; 
                }

                // Send the captured raw audio frame to the processing queue
                if (audio_ai_queue != NULL) {
                    // Use a small timeout or portMAX_DELAY to avoid dropping frames if AFE is busy
                    xQueueSend(audio_ai_queue, &ai_buffer, pdMS_TO_TICKS(10));
                }
            }
        }
    }
}