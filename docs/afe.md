# Audio Front End Module

## Purpose

The `afe.c` and `afe.h` files define a small wrapper module around the **ESP-SR Audio Front End**, usually shortened to **AFE**.

The AFE module is responsible for connecting raw microphone audio to Espressif's speech-processing pipeline.

In this project, the AFE is used for:

- **VAD** — Voice Activity Detection
- **NS** — Noise Suppression
- **AEC** — Acoustic Echo Cancellation, if a playback reference channel is available

The basic audio flow is:

```text
I2S microphone audio
        ↓
audio_afe_feed()
        ↓
ESP-SR AFE pipeline
        ↓
AEC / NS / VAD
        ↓
audio_afe_fetch()
        ↓
processed audio + VAD state
```

## File Structure

Required component layout:

```text
components/dsp_engine/
├── CMakeLists.txt
├── idf_component.yml
├── afe.c
└── include/
    └── afe.h
```

The `afe.c` file owns the ESP-SR AFE instance.

The `afe.h` file exposes a clean API that other files, such as `main.c`, can use.



## `afe.h`

### Purpose of `afe.h`

The header file `afe.h` declares the public interface of the AFE module.

Other parts of the program should include:

```c
#include "afe.h"
```

and use the functions declared there.

The rest of the program should **not** directly interact with:

```c
esp_afe_sr_data_t
esp_afe_sr_iface_t
afe_config_t
afe_fetch_result_t
```

Those ESP-SR-specific objects should stay hidden inside `afe.c`.


## Main Types

#### `audio_afe_vad_state_t`

```c
typedef enum {
    AUDIO_AFE_VAD_SILENCE = 0,
    AUDIO_AFE_VAD_SPEECH,
    AUDIO_AFE_VAD_UNKNOWN,
} audio_afe_vad_state_t;
```

This enum represents the simplified VAD state returned by the AFE module.

It hides the ESP-SR-specific VAD state type from the rest of the program.

#### States

```c
AUDIO_AFE_VAD_SILENCE
```

Means the AFE currently detects silence or non-speech noise.

```c
AUDIO_AFE_VAD_SPEECH
```

Means the AFE currently detects speech.

```c
AUDIO_AFE_VAD_UNKNOWN
```

Means the returned VAD state was not recognized or could not be mapped cleanly.


#### `audio_afe_result_t`

```c
typedef struct {
    int16_t *data;
    int samples;
    int channels;
    audio_afe_vad_state_t vad_state;
} audio_afe_result_t;
```

This struct is filled by:

```c
audio_afe_fetch(&result);
```

It contains the processed audio frame and the current VAD state.

#### Fields

```c
int16_t *data;
```

Pointer to processed audio returned by ESP-SR AFE.

This memory is owned by ESP-SR. Do **not** call `free()` on it.

```c
int samples;
```

Number of `int16_t` samples in the returned audio frame.

```c
int channels;
```

Number of output channels returned by the AFE.

```c
audio_afe_vad_state_t vad_state;
```

Simplified speech/silence state.


## Public API

### `audio_afe_init`

```c
esp_err_t audio_afe_init(const char *input_format);
```

Initializes the ESP-SR Audio Front End.

This function should be called once before using:

```c
audio_afe_feed()
audio_afe_fetch()
```

Example:

```c
audio_afe_init("M");
```

### Input Format

The `input_format` string tells ESP-SR how many channels are being fed into the AFE and what each channel represents.

Common examples:

```text
"M"
```

One microphone channel.

Use this for a single I2S MEMS microphone.

```text
"MM"
```

Two microphone channels.

Use this if the board has two microphones.

```text
"MR"
```

One microphone channel and one playback reference channel.

Use this for AEC.

```text
"MMR"
```

Two microphone channels and one playback reference channel.


### Meaning of Channel Letters

```text
M = microphone channel
R = reference playback channel
N = unused/null channel
```

For a simple MEMS microphone setup, use:

```c
audio_afe_init("M");
```

For acoustic echo cancellation, use something like:

```c
audio_afe_init("MR");
```

but only if you can provide a proper reference signal.

### Important AEC Requirement

AEC means **Acoustic Echo Cancellation**.

AEC removes your own speaker output from the microphone input.

To do this, the AFE needs two signals:

```text
M = what the microphone hears
R = what the speaker is playing
```

Therefore, AEC does **not** work properly with only a microphone.

For AEC, the feed buffer must contain interleaved microphone and reference samples:

```text
mic_sample_0, ref_sample_0,
mic_sample_1, ref_sample_1,
mic_sample_2, ref_sample_2,
...
```

So for one microphone without playback reference, use:

```c
audio_afe_init("M");
```

not:

```c
audio_afe_init("MR");
```


### `audio_afe_feed`

```c
esp_err_t audio_afe_feed(const int16_t *pcm);
```

Feeds one frame of raw PCM audio into the AFE.

The buffer must contain:

```text
feed_chunksize * feed_channels
```

samples.

The values are obtained using:

```c
int chunksize = audio_afe_get_feed_chunksize();
int channels = audio_afe_get_feed_channels();
```

Example:

```c
int total_samples = audio_afe_get_feed_chunksize() * audio_afe_get_feed_channels();

int16_t *buffer = malloc(total_samples * sizeof(int16_t));

i2s_mic_read(buffer, audio_afe_get_feed_chunksize());

audio_afe_feed(buffer);
```


### Required Audio Format

The AFE expects:

```text
16 kHz sample rate
signed 16-bit PCM
interleaved channels
```

For one microphone channel, the buffer is simply:

```text
mic_0, mic_1, mic_2, mic_3, ...
```

For `"MR"`, the buffer must be:

```text
mic_0, ref_0, mic_1, ref_1, mic_2, ref_2, ...
```

### `audio_afe_fetch`

```c
esp_err_t audio_afe_fetch(audio_afe_result_t *out_result);
```

Fetches one processed frame from the AFE.

This function returns:

- processed audio
- number of samples
- number of channels
- VAD state

Example:

```c
audio_afe_result_t result;

if (audio_afe_fetch(&result) == ESP_OK) {
    if (result.vad_state == AUDIO_AFE_VAD_SPEECH) {
        ESP_LOGI("APP", "Speech detected");
    }
}
```


### `audio_afe_get_feed_chunksize`

```c
int audio_afe_get_feed_chunksize(void);
```

Returns how many audio samples the AFE wants per feed frame.

This value should be used when reading from the I2S microphone.

Example:

```c
int chunksize = audio_afe_get_feed_chunksize();
```

Then read exactly that many samples:

```c
i2s_mic_read(buffer, chunksize);
```


### `audio_afe_get_feed_channels`

```c
int audio_afe_get_feed_channels(void);
```

Returns the number of input channels required by the AFE.

For:

```c
audio_afe_init("M");
```

this usually returns:

```text
1
```

For:

```c
audio_afe_init("MR");
```

this usually returns:

```text
2
```

The total feed buffer size is:

$$
\Large
\text{total samples} = \text{feed chunksize} \cdot \text{feed channels}
$$

In C:

```c
int total_samples = audio_afe_get_feed_chunksize() * audio_afe_get_feed_channels();
```

### `audio_afe_get_fetch_chunksize`

```c
int audio_afe_get_fetch_chunksize(void);
```

Returns the output frame size produced by the AFE.

This can be useful if the processed audio is later sent to another module, recorded, transmitted, or analyzed.


### `audio_afe_get_fetch_channels`

```c
int audio_afe_get_fetch_channels(void);
```

Returns the number of output channels produced by the AFE.

### `audio_afe_destroy`

```c
void audio_afe_destroy(void);
```

Destroys the AFE instance and resets internal state.

Usually this is only needed if the audio pipeline is stopped or reconfigured.

Example:

```c
audio_afe_destroy();
```

## `afe.c`

### Purpose of `afe.c`

The `afe.c` file contains the actual ESP-SR implementation.

It owns the internal ESP-SR variables:

```c
static const esp_afe_sr_iface_t *s_afe_handle = NULL;
static esp_afe_sr_data_t *s_afe_data = NULL;
```

These variables are static, meaning they are private to `afe.c`.

Other files should not access them directly.

### Internal State

The module stores:

```c
static int s_feed_chunksize = 0;
static int s_feed_channels = 0;
static int s_fetch_chunksize = 0;
static int s_fetch_channels = 0;
```

These values are filled during:

```c
audio_afe_init()
```

They are then returned by helper functions such as:

```c
audio_afe_get_feed_chunksize()
audio_afe_get_feed_channels()
```

### VAD State Conversion

Inside `afe.c`, the ESP-SR VAD state is converted to the project-specific enum.

Example:

```c
static audio_afe_vad_state_t convert_vad_state(vad_state_t state)
{
    switch (state) {
    case VAD_SPEECH:
        return AUDIO_AFE_VAD_SPEECH;

    case VAD_SILENCE:
        return AUDIO_AFE_VAD_SILENCE;

    default:
        return AUDIO_AFE_VAD_UNKNOWN;
    }
}
```

This keeps the rest of the project independent from ESP-SR's internal enum names.

## Initialization Flow

The initialization function follows this sequence:

```text
audio_afe_init()
        ↓
esp_srmodel_init("model")
        ↓
afe_config_init(...)
        ↓
enable VAD
enable NS
enable AEC if input_format has R
        ↓
esp_afe_handle_from_config(...)
        ↓
create_from_config(...)
        ↓
store chunksize and channel information
```


### Model Loading

```c
srmodel_list_t *models = esp_srmodel_init("model");
```

This loads the ESP-SR model partition.

The name `"model"` refers to the model partition in the ESP-IDF partition table.

If this fails, check that your project includes the correct model partition.


### AFE Config Creation

```c
afe_config_t *afe_config = afe_config_init(
    input_format,
    models,
    AFE_TYPE_SR,
    AFE_MODE_HIGH_PERF
);
```

This creates the configuration for the AFE pipeline.

The `input_format` controls the expected input channel layout.

For one mic:

```c
audio_afe_init("M");
```

For one mic plus reference:

```c
audio_afe_init("MR");
```


### VAD Configuration

Inside `audio_afe_init()`, VAD is enabled with:

```c
afe_config->vad_init = true;
afe_config->vad_mode = VAD_MODE_1;
afe_config->vad_min_noise_ms = 1000;
afe_config->vad_min_speech_ms = 128;
afe_config->vad_delay_ms = 128;
```

Meaning:

```text
vad_init = true
```

Turns on Voice Activity Detection.

```text
vad_mode = VAD_MODE_1
```

Selects the VAD aggressiveness/mode.

```text
vad_min_noise_ms = 1000
```

Requires around 1000 ms of noise/silence context.

```text
vad_min_speech_ms = 128
```

Minimum duration before speech is considered valid.

```text
vad_delay_ms = 128
```

Adds delay/cache so the beginning of speech is less likely to be cut off.

### NS Configuration

NS means **Noise Suppression**.

Noise suppression tries to reduce steady background noise before speech detection or recognition.

In the code, this may look like:

```c
afe_config->ns_init = true;
```

Depending on the exact ESP-SR version, this field may differ or be controlled through `menuconfig`.

If the line does not compile, check the installed ESP-SR headers:

```bash
grep -R "ns_init" -n managed_components/espressif__esp-sr
```

### AEC Configuration

AEC means **Acoustic Echo Cancellation**.

In the code, AEC is enabled only if the input format contains an `R` channel:

```c
bool has_reference_channel = strchr(input_format, 'R') != NULL;

if (has_reference_channel) {
    afe_config->aec_init = true;
} else {
    afe_config->aec_init = false;
}
```

This prevents accidentally enabling AEC when no reference signal exists.

For a single microphone setup:

```c
audio_afe_init("M");
```

AEC will be disabled.

For a microphone plus playback reference:

```c
audio_afe_init("MR");
```

AEC will be enabled.


## Feed / Fetch Concept

ESP-SR AFE uses a two-step processing model:

```text
feed raw audio into AFE
fetch processed audio/result from AFE
```

The feed side accepts raw audio.

The fetch side returns processed audio and metadata.

### Feed

```c
audio_afe_feed(buffer);
```

Internally calls:

```c
s_afe_handle->feed(s_afe_data, pcm);
```

The buffer must already contain the correct number of `int16_t` samples.

For `"M"`:

```text
buffer = [mic_0, mic_1, mic_2, ...]
```

For `"MR"`:

```text
buffer = [mic_0, ref_0, mic_1, ref_1, ...]
```

### Fetch

```c
audio_afe_fetch(&result);
```

Internally calls:

```c
afe_fetch_result_t *result = s_afe_handle->fetch(s_afe_data);
```

The result includes:

```text
processed audio
VAD state
return status
```

The wrapper copies the useful information into:

```c
audio_afe_result_t
```

## How to Use From `main.c`

Minimal usage:

```c
#include <stdlib.h>
#include "esp_log.h"
#include "i2s_mic.h"
#include "afe.h"

static const char *TAG = "main";

void app_main(void)
{
    i2s_mic_init();

    audio_afe_init("M");

    int chunksize = audio_afe_get_feed_chunksize();
    int channels = audio_afe_get_feed_channels();

    int total_samples = chunksize * channels;

    int16_t *buffer = malloc(total_samples * sizeof(int16_t));

    while (1) {
        i2s_mic_read(buffer, chunksize);

        audio_afe_feed(buffer);

        audio_afe_result_t result;

        if (audio_afe_fetch(&result) == ESP_OK) {
            if (result.vad_state == AUDIO_AFE_VAD_SPEECH) {
                ESP_LOGI(TAG, "Speech detected");
            } else if (result.vad_state == AUDIO_AFE_VAD_SILENCE) {
                ESP_LOGI(TAG, "Silence");
            }
        }
    }
}
```

## How It Connects to `i2s_mic.c`

The microphone module should only handle raw microphone input.

The `i2s_mic.c` file should provide functions such as:

```c
esp_err_t i2s_mic_init(void);
esp_err_t i2s_mic_read(int16_t *buffer, int samples);
```

The AFE module expects `i2s_mic_read()` to fill the buffer with:

```text
signed 16-bit PCM
16 kHz sample rate
mono samples for input_format "M"
```

If the I2S microphone provides 24-bit or 32-bit samples, `i2s_mic.c` should convert them to `int16_t`.

Example conversion:

```c
int16_t pcm_sample = raw_sample >> 16;
```

The exact shift depends on the microphone format and I2S configuration.


## When to Use `"M"` vs `"MR"`

### Use `"M"` when you only have a microphone

```c
audio_afe_init("M");
```

This supports:

```text
VAD
NS
single microphone processing
```

This is the correct starting point for most simple ESP32-S3 MEMS microphone projects.


### Use `"MR"` when you have microphone + speaker reference

```c
audio_afe_init("MR");
```

This supports:

```text
VAD
NS
AEC
```

But it requires the feed buffer to include both:

```text
microphone input
speaker playback reference
```

Without the reference channel, AEC cannot work correctly.


## CMake Requirements

The `audio_hal` component must require both the I2S driver and ESP-SR:

```cmake
idf_component_register(
    SRCS
        "afe.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        esp-sr
)
```

The `main` component should require `audio_hal`:

```cmake
idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES audio_hal
)
```

## Component Manifest

The `audio_hal` component should contain:

```text
components/audio_hal/idf_component.yml
```

with:

```yaml
dependencies:
  espressif/esp-sr: "^2.4.4"
```

Then clean and rebuild:

```bash
rm -rf build managed_components dependencies.lock
idf.py set-target esp32s3
idf.py reconfigure
idf.py build
```

## Common Problems

### `esp-sr` unknown name

Error:

```text
Failed to resolve component 'esp-sr'
```

Possible causes:

- `idf_component.yml` is missing
- file is named incorrectly
- file is in the wrong component folder
- dependency was not downloaded
- `managed_components` is stale

Fix:

```bash
rm -rf build managed_components dependencies.lock
idf.py reconfigure
```

### `ns_init` or `aec_init` does not exist

The installed ESP-SR version may use different config fields.

Check the installed headers:

```bash
grep -R "ns_init" -n managed_components/espressif__esp-sr
grep -R "aec_init" -n managed_components/espressif__esp-sr
grep -R "typedef struct.*afe_config" -n managed_components/espressif__esp-sr
```

Then adjust `afe.c` according to the actual available fields.

### No speech detected

Check:

- microphone wiring
- I2S pin mapping
- sample rate is 16 kHz
- audio is converted to signed 16-bit PCM
- microphone is not saturating/clipping
- `audio_afe_init("M")` is used for one mic
- `i2s_mic_read()` fills exactly the expected frame size


### AEC does not work

AEC needs a reference channel.

This is not enough:

```text
microphone only
```

AEC needs:

```text
microphone input + speaker playback reference
```

Use:

```c
audio_afe_init("MR");
```

only when the feed buffer contains interleaved mic and reference samples.


## Recommended Development Order

1. Get `i2s_mic.c` working by printing raw volume/RMS.
2. Convert microphone samples to signed 16-bit PCM.
3. Initialize AFE with:

```c
audio_afe_init("M");
```

4. Feed microphone audio into AFE.
5. Fetch VAD state.
6. Confirm speech/silence detection.
7. Enable or verify Noise Suppression.
8. Add AEC only after a playback reference channel exists.


## Summary

The `afe.c` file owns the ESP-SR Audio Front End instance.

The `afe.h` file exposes a clean API to the rest of the project.

Use:

```c
audio_afe_init("M");
```

for a single MEMS microphone.

Use:

```c
audio_afe_init("MR");
```

only when using Acoustic Echo Cancellation with a real playback reference channel.

The normal runtime loop is:

```text
read I2S mic frame
        ↓
audio_afe_feed()
        ↓
audio_afe_fetch()
        ↓
check result.vad_state
```

This keeps the project clean:

```text
i2s_mic.c  -> raw microphone input
afe.c      -> ESP-SR VAD / NS / AEC pipeline
main.c     -> application logic
```
