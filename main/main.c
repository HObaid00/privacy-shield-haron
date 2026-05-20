#include "afe.h"
#include "audio_hal.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "global_config.h"
#include "mesh_core.h"
#include <stdio.h>

static const char *TAG = "MAIN_APP";

QueueHandle_t audio_ai_queue;

/* -------------------------------------------------------------------------- */
/*  Hello task — broadcast our presence every 5 seconds                       */
/* -------------------------------------------------------------------------- */

static void hello_task(void *arg) {
  TickType_t last_wake = xTaskGetTickCount();
  while (1) {
    mesh_send_hello();
    vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(10000));
  }
}

/* -------------------------------------------------------------------------- */
/*  Prune task — clean up timed-out neighbors every 2 seconds                 */
/* -------------------------------------------------------------------------- */

static void prune_task(void *arg) {
  while (1) {
    mesh_discovery_prune();
    int count = mesh_discovery_count();
    if (count > 0) {
      ESP_LOGI(TAG, "%d neighbor(s) online", count);
    }
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}

/* -------------------------------------------------------------------------- */
/*  Packet received callback — handle incoming mesh packets                   */
/* -------------------------------------------------------------------------- */

static void on_mesh_packet(const uint8_t *src_mac, const void *data,
                           size_t len) {
  const mesh_header_t *hdr = (const mesh_header_t *)data;

  switch (hdr->type) {
  case MESH_PKT_HELLO:
    ESP_LOGI(TAG, "HELLO from node %u (" MACSTR ")", hdr->src_id,
             MAC2STR(src_mac));
    break;

  case MESH_PKT_STATUS:
    if (len >= sizeof(mesh_status_pkt_t)) {
      const mesh_status_pkt_t *status = (const mesh_status_pkt_t *)data;
      ESP_LOGI(TAG, "STATUS from node %u: masking=%s vol=%u batt=%u%%",
               status->header.src_id, status->masking_active ? "ON" : "OFF",
               status->volume, status->battery_pct);
    }
    break;

  case MESH_PKT_COMMAND:
    if (len >= sizeof(mesh_command_pkt_t)) {
      const mesh_command_pkt_t *cmd = (const mesh_command_pkt_t *)data;
      ESP_LOGI(TAG, "COMMAND from node %u: cmd=%u val=%u", cmd->header.src_id,
               cmd->command, cmd->value);
      /* Future: act on mute/unmute/volume commands here */
    }
    break;

  default:
    ESP_LOGD(TAG, "Unknown packet type 0x%02X from node %u", hdr->type,
             hdr->src_id);
    break;
  }
}
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "global_config.h"
#include "audio_hal.h"
#include "afe.h"

static const char *TAG = "MAIN_APP";

// Shared Queue handling raw audio chunks between Core 1 and Core 0
extern QueueHandle_t audio_ai_queue;

/* -------------------------------------------------------------------------- */
/* AFE Processing Task — Pulls from queue, processes VAD                     */
/* -------------------------------------------------------------------------- */
static void afe_processing_task(void *arg) {
    // Allocate space matching our expected I2S Mic frame chunk size (512 samples)
    int16_t mic_frame[512]; 
    audio_afe_result_t afe_res;
    
    // Track VAD state change to avoid spamming the log console
    audio_afe_vad_state_t last_vad_state = AUDIO_AFE_VAD_SILENCE;

    ESP_LOGI(TAG, "AFE processing task started on Core %d", xPortGetCoreID());

    while (1) {
        // Wait indefinitely until a raw mic chunk arrives from Core 1
        if (xQueueReceive(audio_ai_queue, &mic_frame, portMAX_DELAY) == pdTRUE) {
            
            // 1. Feed the 16-bit PCM block into the ESP Front End Engine
            esp_err_t err = audio_afe_feed(mic_frame);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to feed audio to AFE");
                continue;
            }

            // 2. Fetch the calculated result from the AFE processing block
            err = audio_afe_fetch(&afe_res);
            if (err == ESP_OK) {
                
                // Only print when the speech state actually flips
                if (afe_res.vad_state != last_vad_state) {
                    if (afe_res.vad_state == AUDIO_AFE_VAD_SPEECH) {
                        ESP_LOGI(TAG, "🎙️ [VAD] Speech Detected!");
                    } else if (afe_res.vad_state == AUDIO_AFE_VAD_SILENCE) {
                        ESP_LOGI(TAG, "🤫 [VAD] Silence...");
                    } else {
                        ESP_LOGW(TAG, "❓ [VAD] Unknown VAD state.");
                    }
                    last_vad_state = afe_res.vad_state;
                }
            }
        }
    }
}

/* -------------------------------------------------------------------------- */
/* Initialization Pipeline                                                   */
/* -------------------------------------------------------------------------- */
void privacy_shield_init(void) {
    ESP_LOGI(TAG, "======================================");
    ESP_LOGI(TAG, "   Initializing Audio VAD Pipeline    ");
    ESP_LOGI(TAG, "======================================");

    // Create the FreeRTOS Queue to hold 10 chunks of 512-sample int16 arrays
    audio_ai_queue = xQueueCreate(10, 512 * sizeof(int16_t));
    if (audio_ai_queue == NULL) {
        ESP_LOGE(TAG, "Critical: Failed to create audio queue!");
        return;
    }

    // Initialize the ESP-SR Audio Front End in Single Microphone mode ("M")
    esp_err_t afe_err = audio_afe_init("M");
    if (afe_err != ESP_OK) {
        ESP_LOGE(TAG, "Critical: AFE Initialization failed!");
        return;
    }

    // Double check internal ESP-SR expectations against our hardware sample layouts
    int expected_chunk = audio_afe_get_feed_chunksize();
    ESP_LOGI(TAG, "AFE Engine expects chunk size of: %d samples", expected_chunk);

    // Initialize physical I2S Hardware 
    audio_hal_mic_init();

    // Spawn the hardware reading loop on Core 1 (Pin down to decouple ISR/DMA overhead)
    xTaskCreatePinnedToCore(audio_hal_mic_read_task, "Mic_Read_Task", 4096, NULL, 5, NULL, 1);

    // Spawn the AFE Heavy DSP Processing Loop on Core 0
    xTaskCreatePinnedToCore(afe_processing_task, "AFE_Proc_Task", 8192, NULL, 5, NULL, 0);

    ESP_LOGI(TAG, "System Pipeline Up and Operational.");
}

/* -------------------------------------------------------------------------- */
/* Entry point                                                               */
/* -------------------------------------------------------------------------- */
void app_main(void) { 
    privacy_shield_init(); 
}