#include <SDL_audio.h>
#ifdef N64_WIN
#include <windows.h>
#else
#include <pthread.h>
#include <metrics.h>

#endif
#include "audio.h"

#define AUDIO_SAMPLE_RATE 48000
#define SYSTEM_SAMPLE_FORMAT AUDIO_F32SYS
#define SYSTEM_SAMPLE_SIZE 4
#define BYTES_PER_HALF_SECOND ((AUDIO_SAMPLE_RATE / 2) * SYSTEM_SAMPLE_SIZE)

static SDL_AudioStream* audio_stream = NULL;
#ifdef N64_WIN
static HANDLE audio_stream_mutex;
#else
static pthread_mutex_t audio_stream_mutex;
#endif
SDL_AudioSpec audio_spec;
SDL_AudioSpec request;
SDL_AudioDeviceID audio_dev;

INLINE void acquire_audiostream_mutex() {
#ifdef N64_WIN
    WaitForSingleObject(audio_stream_mutex, INFINITE);
#else
    pthread_mutex_lock(&audio_stream_mutex);
#endif
}

INLINE void release_audiostream_mutex() {
#ifdef N64_WIN
    ReleaseMutex(audio_stream_mutex);
#else
    pthread_mutex_unlock(&audio_stream_mutex);
#endif
}

void audio_callback(void* userdata, Uint8* stream, int length) {
    int gotten = 0;
    acquire_audiostream_mutex();
    int available = SDL_AudioStreamAvailable(audio_stream);
    set_metric(METRIC_AUDIOSTREAM_AVAILABLE, available);
    if (available > 0) {
        gotten = SDL_AudioStreamGet(audio_stream, stream, length);
    }
    release_audiostream_mutex();

    int gotten_samples = gotten / sizeof(float);
    float* out = (float*)stream;
    out += gotten_samples;

    /*
    if (gotten_samples < length / sizeof(float)) {
        logwarn("AudioStream buffer underflow! You may hear crackling! We're %d bytes short.", length - gotten);
    }
     */

    for (int i = gotten_samples; i < length / sizeof(float); i++) {
        float sample = 0;
        *out++ = sample;
    }
}

void audio_init() {
    adjust_audio_sample_rate(AUDIO_SAMPLE_RATE);
    memset(&request, 0, sizeof(request));

    request.freq = AUDIO_SAMPLE_RATE;
    request.format = SYSTEM_SAMPLE_FORMAT;
    request.channels = 2;
    request.samples = 1024;
    request.callback = audio_callback;
    request.userdata = NULL;

    audio_dev = SDL_OpenAudioDevice(NULL, 0, &request, &audio_spec, 0);

    audio_dev = SDL_OpenAudioDevice(NULL, 0, &request, &audio_spec, 0);
    unimplemented(request.format != audio_spec.format, "Request != got");

    if (audio_dev == 0) {
        logfatal("Failed to initialize SDL audio: %s", SDL_GetError());
    }

    SDL_PauseAudioDevice(audio_dev, false);

#ifdef N64_WIN
    audio_stream_mutex = CreateMutex(NULL, FALSE, NULL);
    if (audio_stream_mutex == NULL) {
#else
    if (pthread_mutex_init(&audio_stream_mutex, NULL) != 0) {
#endif
        logfatal("Unable to initialize mutex");
    }
}

void adjust_audio_sample_rate(int sample_rate) {
    logwarn("Adjusting audio sample rate, locking mutex!");
    acquire_audiostream_mutex();
    if (audio_stream != NULL) {
        SDL_FreeAudioStream(audio_stream);
    }

    audio_stream = SDL_NewAudioStream(AUDIO_S16SYS, 2, sample_rate, SYSTEM_SAMPLE_FORMAT, 2, AUDIO_SAMPLE_RATE);
    release_audiostream_mutex();
}

void audio_push_sample(shalf left, shalf right) {
    shalf samples[2] = {
            left,
            right
    };

    int available_bytes = SDL_AudioStreamAvailable(audio_stream);
    if (available_bytes < BYTES_PER_HALF_SECOND) {
        SDL_AudioStreamPut(audio_stream, samples, 2 * sizeof(shalf));
    } else {
        logwarn("Not pushing sample, there are already %d bytes available.", available_bytes);
    }
}
