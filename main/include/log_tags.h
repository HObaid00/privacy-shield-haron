#pragma once

/* =========================================================================
 *  Log Tags — Single source of truth for all component TAG strings
 *
 *  Every .c file that logs should use these instead of ad-hoc strings.
 *  This lets log_levels_init() reference tags reliably and keeps
 *  the Logging_Guide.md always accurate.
 *
 *  Usage:
 *    #include "log_tags.h"
 *    static const char *TAG = LOG_TAG_MESH_CORE;
 * ========================================================================= */

/* ---- Mesh ---- */
#define LOG_TAG_MESH_CORE   "mesh_core"
#define LOG_TAG_DISCOVERY   "discovery"

/* ---- Audio ---- */
#define LOG_TAG_AUDIO_MIC   "AUDIO_HAL_MIC"
#define LOG_TAG_AUDIO_AMP   "audio_amp"       /* future: MAX98357A driver */

/* ---- DSP Engine ---- */
#define LOG_TAG_VAD         "dsp_vad"         /* future: voice activity detection */
#define LOG_TAG_NOISE_GEN   "dsp_noise"       /* future: pink/brown noise gen */
#define LOG_TAG_AEC         "dsp_aec"         /* future: acoustic echo cancellation */

/* ---- Web Dashboard ---- */
#define LOG_TAG_WEB         "web_dashboard"   /* future: Hub HTTP server + API */

/* ---- Main ---- */
#define LOG_TAG_MAIN        "main"
