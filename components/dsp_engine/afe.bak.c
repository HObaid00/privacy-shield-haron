#include "afe.h"

/**
 * AFE - Audio Front End
 */

#include <stdlib.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"

#include "esp_afe_config.h"
#include "esp_afe_sr_iface.h"
#include "esp_mn_models.h"
#include "model_path.h"

static const char *TAG = "audio_afe";

static const esp_afe_sr_iface_t *s_afe_handle = NULL;
static esp_afe_sr_data_t *s_afe_data = NULL;

static int s_feed_chunksize = 0;
static int s_feed_channels = 0;
static int s_fetch_chunksize = 0;
static int s_fetch_channels = 0;

static audio_afe_vad_state_t convert_vad_state(vad_state_t state) {
  switch (state) {
  case VAD_SPEECH:
    return AUDIO_AFE_VAD_SPEECH;

  case VAD_SILENCE:
    return AUDIO_AFE_VAD_SILENCE;

  default:
    return AUDIO_AFE_VAD_UNKNOWN;
  }
}

esp_err_t audio_afe_init(const char *input_format) {
  if (s_afe_data != NULL) {
    ESP_LOGW(TAG, "AFE already initialized");
    return ESP_OK;
  }

  if (input_format == NULL) {
    ESP_LOGE(TAG, "input_format is NULL");
    return ESP_ERR_INVALID_ARG;
  }

  /*
   * Loads ESP-SR model partition.
   *
   * Your partition table must contain a model partition,
   * and sdkconfig/menuconfig must include the selected AFE/VAD/NS models.
   */
  srmodel_list_t *models = esp_srmodel_init("model");
  if (models == NULL) {
    ESP_LOGE(TAG, "Failed to initialize SR models. Check model partition.");
    return ESP_FAIL;
  }

  /*
   * Examples:
   *   "M"   : one mic
   *   "MR"  : one mic + reference channel for AEC
   *   "MMR" : two mics + reference channel for AEC
   *
   * AFE_TYPE_SR is the speech-recognition front-end mode.
   */
  afe_config_t *afe_config =
      afe_config_init(input_format, models, AFE_TYPE_SR, AFE_MODE_HIGH_PERF);

  if (afe_config == NULL) {
    ESP_LOGE(TAG, "Failed to create AFE config");
    return ESP_FAIL;
  }

  /*
   * VAD: Voice Activity Detection.
   */
  afe_config->vad_init = true;
  afe_config->vad_mode = VAD_MODE_1;
  afe_config->vad_min_noise_ms = 1000;
  afe_config->vad_min_speech_ms = 128;
  afe_config->vad_delay_ms = 128;

  /*
   * NS: Noise Suppression.
   *
   * In current ESP-SR AFE, NS/NSNet availability also depends on
   * selected models/options in menuconfig.
   *
   * Depending on the exact ESP-SR version, this field may be:
   *   afe_config->ns_init
   * or model/menuconfig-controlled.
   *
   * If this line fails to compile, comment it out and enable NS in:
   *   idf.py menuconfig -> ESP Speech Recognition
   */
#ifdef CONFIG_SR_NSN_MODEL_QUANT
  afe_config->ns_init = true;
#else
  /*
   * Leave this as-is if your ESP-SR version does not expose ns_init.
   * You can still enable NS model selection from menuconfig.
   */
  afe_config->ns_init = true;
#endif

  /*
   * AEC: Acoustic Echo Cancellation.
   *
   * AEC only makes sense if input_format contains an R reference channel,
   * for example "MR" or "MMR".
   *
   * The R channel should be the audio you are sending to the speaker,
   * time-aligned as well as possible with the mic input.
   */
  bool has_reference_channel = strchr(input_format, 'R') != NULL;

  if (has_reference_channel) {
    afe_config->aec_init = true;
  } else {
    afe_config->aec_init = false;
    ESP_LOGW(TAG,
             "AEC disabled because input_format has no R reference channel");
  }

  s_afe_handle = esp_afe_handle_from_config(afe_config);
  if (s_afe_handle == NULL) {
    ESP_LOGE(TAG, "Failed to get AFE handle");
    afe_config_free(afe_config);
    return ESP_FAIL;
  }

  s_afe_data = s_afe_handle->create_from_config(afe_config);
  afe_config_free(afe_config);

  if (s_afe_data == NULL) {
    ESP_LOGE(TAG, "Failed to create AFE data");
    s_afe_handle = NULL;
    return ESP_FAIL;
  }

  s_feed_chunksize = s_afe_handle->get_feed_chunksize(s_afe_data);
  s_feed_channels = s_afe_handle->get_feed_channel_num(s_afe_data);
  s_fetch_chunksize = s_afe_handle->get_fetch_chunksize(s_afe_data);
  s_fetch_channels = s_afe_handle->get_fetch_channel_num(s_afe_data);

  ESP_LOGI(TAG, "AFE initialized");
  ESP_LOGI(TAG, "input_format=%s", input_format);
  ESP_LOGI(TAG, "feed_chunksize=%d, feed_channels=%d", s_feed_chunksize,
           s_feed_channels);
  ESP_LOGI(TAG, "fetch_chunksize=%d, fetch_channels=%d", s_fetch_chunksize,
           s_fetch_channels);
  ESP_LOGI(TAG, "VAD enabled");
  ESP_LOGI(TAG, "AEC %s", has_reference_channel ? "enabled" : "disabled");

  return ESP_OK;
}

esp_err_t audio_afe_feed(const int16_t *pcm) {
  if (s_afe_handle == NULL || s_afe_data == NULL) {
    return ESP_ERR_INVALID_STATE;
  }

  if (pcm == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  /*
   * pcm must contain:
   *
   *   s_feed_chunksize * s_feed_channels
   *
   * int16_t samples.
   */
  int ret = s_afe_handle->feed(s_afe_data, pcm);

  if (ret < 0) {
    ESP_LOGE(TAG, "AFE feed failed: %d", ret);
    return ESP_FAIL;
  }

  return ESP_OK;
}

esp_err_t audio_afe_fetch(audio_afe_result_t *out_result) {
  if (s_afe_handle == NULL || s_afe_data == NULL) {
    return ESP_ERR_INVALID_STATE;
  }

  if (out_result == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  afe_fetch_result_t *result = s_afe_handle->fetch(s_afe_data);

  if (result == NULL) {
    ESP_LOGE(TAG, "AFE fetch returned NULL");
    return ESP_FAIL;
  }

  if (result->ret_value == ESP_FAIL) {
    ESP_LOGE(TAG, "AFE fetch failed");
    return ESP_FAIL;
  }

  out_result->data = result->data;
  out_result->samples = result->data_size / sizeof(int16_t);
  out_result->channels = s_fetch_channels;
  out_result->vad_state = convert_vad_state(result->vad_state);

  return ESP_OK;
}

int audio_afe_get_feed_chunksize(void) { return s_feed_chunksize; }

int audio_afe_get_feed_channels(void) { return s_feed_channels; }

int audio_afe_get_fetch_chunksize(void) { return s_fetch_chunksize; }

int audio_afe_get_fetch_channels(void) { return s_fetch_channels; }

void audio_afe_destroy(void) {
  if (s_afe_handle != NULL && s_afe_data != NULL) {
    s_afe_handle->destroy(s_afe_data);
  }

  s_afe_data = NULL;
  s_afe_handle = NULL;

  s_feed_chunksize = 0;
  s_feed_channels = 0;
  s_fetch_chunksize = 0;
  s_fetch_channels = 0;

  ESP_LOGI(TAG, "AFE destroyed");
}
