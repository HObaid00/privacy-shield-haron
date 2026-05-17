#pragma once

#include "driver/i2s_std.h"
#include <stdint.h>

extern i2s_chan_handle_t rx_chan;

// static int32_t sample_buffer;
// static size_t bytes_read;
// static int print_counter;

void mic_init(void);

static int32_t remove_dc_and_get_volume(int32_t *buffer, int samples_read);

void read_microphone();

static void print_microphone_volume(int32_t volume);
