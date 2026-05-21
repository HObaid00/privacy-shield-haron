#include "afe.h"

/* AFE - Audio Front End */

#include "esp_afe_config.h"
#include "esp_err.h"
#include "model_path.h"

static const char *TAG = "AFE";

/**
 * Audio Front End initailization process.
 * Takes a char @arg which represents the models
 *   "M"   : Microphone channel
 *   "N"   : Unused or unknown channel
 *   "R"   : Playback reference channel for AEC
 *   "MR"  : one mic + reference channel for AEC
 *   "MMR" : two mics + reference channel for AEC
 *
 */
esp_err_t afe_init(const char *arg) {

  // Baseline config
  srmodel_list_t *model = esp_srmodel_init("model");
  afe_config_t *afe_config =
      afe_config_init(arg, model, AFE_TYPE_SR, AFE_MODE_HIGH_PERF);
}
