/* s3eSound (PCM channels), s3eAudio (compressed music) and s3eVideo.
 *
 * Everything is mixed here into one 44.1 kHz stereo stream that the
 * platform plays from its own thread (sf_plat_audio_start). How the game
 * drives it (docs/RE_NOTES.md):
 *
 *   s3eSoundSetInt(DEFAULT_FREQ, 22050) once; effects are mono s16 WAV
 *   data at that rate, played with ChannelPlay(ch, pcm, bytes / 2,
 *   loop ? 0 : 1, 0) after ChannelSetInt(ch, VOLUME, 0..256).
 *   Music is an MP3: s3eAudioPlayFromBuffer, falling back to
 *   s3eAudioPlay(file), with an S3E_AUDIO_STOP callback for the end of
 *   the track. Every volume is 0..256.
 *
 * Video reports failure so any intro movie is skipped rather than waited on.
 */
#include "s3e/s3e_internal.h"
#include "platform/platform.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#define MINIMP3_IMPLEMENTATION
#include "third_party/minimp3.h"

#define OUT_RATE 44100
#define NCHAN    16
#define MAX_VOL  256

/* s3eSoundProperty / s3eSoundChannelProperty / s3eAudioProperty */
enum { SND_VOLUME = 0, SND_OUTPUT_FREQ = 1, SND_DEFAULT_FREQ = 2 };
enum { CH_PITCH = 0, CH_RATE = 1, CH_VOLUME = 3, CH_PAUSED = 5 };
enum { AUD_VOLUME = 0 };
enum { AUDIO_STOP_CB = 2 };

typedef struct {
    const int16_t *pcm;
    uint32_t len, loop_from;
    uint64_t pos;          /* 32.32 fixed point, in samples */
    uint64_t step;
    int32_t repeat;        /* plays left; 0 = forever */
    int32_t volume, paused, playing;
} sf_channel;

typedef struct {
    uint8_t *owned;        /* file data we loaded (NULL for game buffers) */
    const uint8_t *data;
    uint32_t size, start, pos;
    int32_t repeat, playing, paused, volume;
    mp3dec_t dec;
    int16_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
    int frames, channels, hz;
    uint64_t fpos;         /* 32.32 into pcm, in frames */
} sf_music;

static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
static sf_channel s_ch[NCHAN];
static sf_music s_music;
static int32_t s_master = MAX_VOL, s_default_freq = 22050;
static int s_started;
static volatile int s_music_ended;
static struct { s3eCallback fn; void *ud; } s_audio_cb[3];

static uint64_t step_for(int32_t hz)
{
    return ((uint64_t)(hz > 0 ? hz : 1) << 32) / OUT_RATE;
}

/* --- music decoding ------------------------------------------------------ */

static uint32_t skip_id3(const uint8_t *d, uint32_t n)
{
    if (n >= 10 && !memcmp(d, "ID3", 3))
        return 10 + ((uint32_t)(d[6] & 0x7f) << 21 | (uint32_t)(d[7] & 0x7f) << 14 |
                     (uint32_t)(d[8] & 0x7f) << 7 | (d[9] & 0x7f));
    return 0;
}

/* Decode the next frame into m->pcm; 0 at the end of the data. */
static int music_decode(sf_music *m)
{
    mp3dec_frame_info_t info;
    while (m->pos < m->size) {
        int n = mp3dec_decode_frame(&m->dec, m->data + m->pos, (int)(m->size - m->pos), m->pcm, &info);
        if (!info.frame_bytes)
            break;
        m->pos += (uint32_t)info.frame_bytes;
        if (n > 0) {
            m->frames = n;
            m->channels = info.channels;
            m->hz = info.hz;
            m->fpos = 0;
            return 1;
        }
    }
    return 0;
}

static void music_free(sf_music *m)
{
    free(m->owned);
    memset(m, 0, sizeof(*m));
    m->volume = MAX_VOL;
}

/* Called with the lock held. */
static void music_mix(int32_t *acc, int frames)
{
    sf_music *m = &s_music;
    int i;

    if (!m->playing || m->paused)
        return;
    for (i = 0; i < frames; i++) {
        uint32_t idx = (uint32_t)(m->fpos >> 32);
        int32_t l, r;
        while (idx >= (uint32_t)m->frames) {
            if (!music_decode(m)) {
                if (m->repeat == 1) {
                    m->playing = 0;
                    s_music_ended = 1;
                    return;
                }
                if (m->repeat > 1)
                    m->repeat--;
                m->pos = m->start;
                mp3dec_init(&m->dec);
                if (!music_decode(m)) {
                    m->playing = 0;
                    return;
                }
            }
            idx = (uint32_t)(m->fpos >> 32);
        }
        if (m->channels == 2) {
            l = m->pcm[idx * 2];
            r = m->pcm[idx * 2 + 1];
        } else {
            l = r = m->pcm[idx];
        }
        acc[i * 2] += l * m->volume / MAX_VOL;
        acc[i * 2 + 1] += r * m->volume / MAX_VOL;
        m->fpos += step_for(m->hz);
    }
}

/* --- PCM channels -------------------------------------------------------- */

static void channel_mix(sf_channel *c, int32_t *acc, int frames)
{
    int32_t vol = c->volume * s_master / MAX_VOL;
    int i;

    for (i = 0; i < frames; i++) {
        uint32_t idx = (uint32_t)(c->pos >> 32);
        int32_t a, b, frac, v;
        if (idx >= c->len) {
            if (c->repeat == 1) {
                c->playing = 0;
                return;
            }
            if (c->repeat > 1)
                c->repeat--;
            c->pos = (uint64_t)(c->loop_from < c->len ? c->loop_from : 0) << 32;
            idx = (uint32_t)(c->pos >> 32);
        }
        a = c->pcm[idx];
        b = idx + 1 < c->len ? c->pcm[idx + 1] : a;
        frac = (int32_t)((c->pos >> 16) & 0xffff);
        v = (a + ((b - a) * frac >> 16)) * vol / MAX_VOL;
        acc[i * 2] += v;
        acc[i * 2 + 1] += v;
        c->pos += c->step;
    }
}

static void mix(int16_t *out, int frames)
{
    int32_t acc[1024 * 2];
    int i, done = 0;

    while (done < frames) {
        int n = frames - done > 1024 ? 1024 : frames - done;
        memset(acc, 0, sizeof(int32_t) * (size_t)n * 2);
        pthread_mutex_lock(&s_lock);
        for (i = 0; i < NCHAN; i++)
            if (s_ch[i].playing && !s_ch[i].paused)
                channel_mix(&s_ch[i], acc, n);
        music_mix(acc, n);
        pthread_mutex_unlock(&s_lock);
        for (i = 0; i < n * 2; i++) {
            int32_t v = acc[i];
            out[done * 2 + i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
        }
        done += n;
    }
}

static void ensure_started(void)
{
    if (!s_started) {
        s_started = 1;
        s_music.volume = MAX_VOL;
        sf_plat_audio_start(OUT_RATE, mix);
        LOGI("audio: mixer started (%d Hz stereo, %d channels)", OUT_RATE, NCHAN);
    }
}

void sf_sound_poll(void)
{
    if (s_music_ended) {
        int32_t data[4] = { 0 };
        s_music_ended = 0;
        if (s_audio_cb[AUDIO_STOP_CB].fn)
            s_audio_cb[AUDIO_STOP_CB].fn(data, s_audio_cb[AUDIO_STOP_CB].ud);
    }
}

/* --- s3eSound ------------------------------------------------------------ */

static int32_t sf_s3eSoundGetFreeChannel(void)
{
    int i;
    ensure_started();
    pthread_mutex_lock(&s_lock);
    for (i = 0; i < NCHAN && s_ch[i].playing; i++) {}
    pthread_mutex_unlock(&s_lock);
    return i < NCHAN ? i : -1;
}

static int32_t sf_s3eSoundChannelPlay(int32_t ch, const int16_t *pcm, uint32_t samples,
                                      int32_t repeat, int32_t loop_from)
{
    sf_channel *c;
    if (ch < 0 || ch >= NCHAN || !pcm || !samples)
        return S3E_RESULT_ERROR;
    ensure_started();
    pthread_mutex_lock(&s_lock);
    c = &s_ch[ch];
    c->pcm = pcm;
    c->len = samples;
    c->loop_from = loop_from > 0 ? (uint32_t)loop_from : 0;
    c->repeat = repeat;
    c->pos = 0;
    if (!c->step)
        c->step = step_for(s_default_freq);
    if (!c->volume)
        c->volume = MAX_VOL;
    c->paused = 0;
    c->playing = 1;
    pthread_mutex_unlock(&s_lock);
    return S3E_RESULT_SUCCESS;
}

static int32_t sf_s3eSoundChannelStop(int32_t ch)
{
    if (ch < 0 || ch >= NCHAN)
        return S3E_RESULT_ERROR;
    pthread_mutex_lock(&s_lock);
    s_ch[ch].playing = 0;
    pthread_mutex_unlock(&s_lock);
    return S3E_RESULT_SUCCESS;
}

static int32_t sf_s3eSoundChannelSetInt(int32_t ch, uint32_t prop, int32_t v)
{
    if (ch < 0 || ch >= NCHAN)
        return S3E_RESULT_ERROR;
    pthread_mutex_lock(&s_lock);
    switch (prop) {
    case CH_RATE:   s_ch[ch].step = step_for(v); break;
    case CH_PITCH:  s_ch[ch].step = (uint64_t)(v > 0 ? v : 1) << 16; break;   /* 16.16 */
    case CH_VOLUME: s_ch[ch].volume = v < 0 ? 0 : v > MAX_VOL ? MAX_VOL : v; break;
    case CH_PAUSED: s_ch[ch].paused = v != 0; break;
    default:
        LOGV("s3eSoundChannelSetInt(%d, %u, %d) ignored", (int)ch, (unsigned)prop, (int)v);
        break;
    }
    pthread_mutex_unlock(&s_lock);
    return S3E_RESULT_SUCCESS;
}

static int32_t sf_s3eSoundSetInt(uint32_t prop, int32_t v)
{
    pthread_mutex_lock(&s_lock);
    if (prop == SND_VOLUME)
        s_master = v < 0 ? 0 : v > MAX_VOL ? MAX_VOL : v;
    else if (prop == SND_DEFAULT_FREQ && v > 0) {
        int i;
        s_default_freq = v;
        for (i = 0; i < NCHAN; i++)
            s_ch[i].step = step_for(v);
    }
    pthread_mutex_unlock(&s_lock);
    LOGI("s3eSoundSetInt(%u, %d)", (unsigned)prop, (int)v);
    return S3E_RESULT_SUCCESS;
}

static int32_t sf_s3eSoundGetError(void) { return 0; }

static void set_all_channels(int what)
{
    int i;
    pthread_mutex_lock(&s_lock);
    for (i = 0; i < NCHAN; i++) {
        if (what == 0)
            s_ch[i].playing = 0;
        else
            s_ch[i].paused = what == 1;
    }
    pthread_mutex_unlock(&s_lock);
}
static int32_t sf_s3eSoundStopAllChannels(void) { set_all_channels(0); return S3E_RESULT_SUCCESS; }
static int32_t sf_s3eSoundPauseAllChannels(void) { set_all_channels(1); return S3E_RESULT_SUCCESS; }
static int32_t sf_s3eSoundResumeAllChannels(void) { set_all_channels(2); return S3E_RESULT_SUCCESS; }

/* --- s3eAudio ------------------------------------------------------------ */

static int32_t music_start(uint8_t *owned, const uint8_t *data, uint32_t size, uint32_t repeat)
{
    sf_music *m = &s_music;
    int32_t vol;

    ensure_started();
    pthread_mutex_lock(&s_lock);
    vol = m->volume;
    music_free(m);
    m->volume = vol;
    m->owned = owned;
    m->data = data;
    m->size = size;
    m->start = m->pos = skip_id3(data, size) < size ? skip_id3(data, size) : 0;
    m->repeat = (int32_t)repeat;
    mp3dec_init(&m->dec);
    m->playing = music_decode(m);
    pthread_mutex_unlock(&s_lock);
    if (!m->playing) {
        LOGW("s3eAudio: no MP3 frames in %u bytes", (unsigned)size);
        return S3E_RESULT_ERROR;
    }
    return S3E_RESULT_SUCCESS;
}

static int32_t sf_s3eAudioPlay(const char *file, uint32_t repeat)
{
    uint32_t size = 0;
    uint8_t *data = file ? sf_file_load(file, &size) : NULL;
    LOGI("s3eAudioPlay(%s, %u)%s", file ? file : "", (unsigned)repeat, data ? "" : " -> not found");
    if (!data)
        return S3E_RESULT_ERROR;
    return music_start(data, data, size, repeat);
}

static int32_t sf_s3eAudioPlayFromBuffer(void *buf, uint32_t len, uint32_t repeat)
{
    if (!buf || !len)
        return S3E_RESULT_ERROR;
    LOGI("s3eAudioPlayFromBuffer(%u bytes, %u)", (unsigned)len, (unsigned)repeat);
    return music_start(NULL, buf, len, repeat);
}

static int32_t sf_s3eAudioStop(void)
{
    pthread_mutex_lock(&s_lock);
    s_music.playing = 0;
    pthread_mutex_unlock(&s_lock);
    return S3E_RESULT_SUCCESS;
}

static int32_t sf_s3eAudioPause(void) { s_music.paused = 1; return S3E_RESULT_SUCCESS; }
static int32_t sf_s3eAudioResume(void) { s_music.paused = 0; return S3E_RESULT_SUCCESS; }

static int32_t sf_s3eAudioSetInt(uint32_t prop, int32_t v)
{
    if (prop == AUD_VOLUME)
        s_music.volume = v < 0 ? 0 : v > MAX_VOL ? MAX_VOL : v;
    else
        LOGI("s3eAudioSetInt(%u, %d) ignored", (unsigned)prop, (int)v);
    return S3E_RESULT_SUCCESS;
}

static int32_t sf_s3eAudioRegister(uint32_t id, s3eCallback fn, void *ud)
{
    if (id >= 3)
        return S3E_RESULT_ERROR;
    s_audio_cb[id].fn = fn;
    s_audio_cb[id].ud = ud;
    return S3E_RESULT_SUCCESS;
}

/* --- s3eVideo ------------------------------------------------------------ */

static int32_t sf_s3eVideoPlay(const char *file, uint32_t repeat, int32_t x, int32_t y,
                               int32_t w, int32_t h)
{
    LOGI("s3eVideoPlay(%s) -> unsupported, skipping", file ? file : "");
    return S3E_RESULT_ERROR;
}
static int32_t sf_s3eVideoIsPlaying(void) { return 0; }
static int32_t sf_s3eVideoStop(void) { return S3E_RESULT_SUCCESS; }
static int32_t sf_s3eVideoRegister(uint32_t id, s3eCallback fn, void *ud) { return S3E_RESULT_SUCCESS; }
static int32_t sf_s3eVideoUnRegister(uint32_t id, s3eCallback fn) { return S3E_RESULT_SUCCESS; }

const sf_export sf_exports_sound[] = {
    { "s3eSoundGetFreeChannel", (void *)sf_s3eSoundGetFreeChannel },
    { "s3eSoundChannelPlay", (void *)sf_s3eSoundChannelPlay },
    { "s3eSoundChannelStop", (void *)sf_s3eSoundChannelStop },
    { "s3eSoundChannelSetInt", (void *)sf_s3eSoundChannelSetInt },
    { "s3eSoundSetInt", (void *)sf_s3eSoundSetInt },
    { "s3eSoundGetError", (void *)sf_s3eSoundGetError },
    { "s3eSoundStopAllChannels", (void *)sf_s3eSoundStopAllChannels },
    { "s3eSoundPauseAllChannels", (void *)sf_s3eSoundPauseAllChannels },
    { "s3eSoundResumeAllChannels", (void *)sf_s3eSoundResumeAllChannels },
    { "s3eAudioPlay", (void *)sf_s3eAudioPlay },
    { "s3eAudioPlayFromBuffer", (void *)sf_s3eAudioPlayFromBuffer },
    { "s3eAudioStop", (void *)sf_s3eAudioStop },
    { "s3eAudioPause", (void *)sf_s3eAudioPause },
    { "s3eAudioResume", (void *)sf_s3eAudioResume },
    { "s3eAudioSetInt", (void *)sf_s3eAudioSetInt },
    { "s3eAudioRegister", (void *)sf_s3eAudioRegister },
    { "s3eVideoPlay", (void *)sf_s3eVideoPlay },
    { "s3eVideoIsPlaying", (void *)sf_s3eVideoIsPlaying },
    { "s3eVideoStop", (void *)sf_s3eVideoStop },
    { "s3eVideoRegister", (void *)sf_s3eVideoRegister },
    { "s3eVideoUnRegister", (void *)sf_s3eVideoUnRegister },
    { NULL, NULL }
};
