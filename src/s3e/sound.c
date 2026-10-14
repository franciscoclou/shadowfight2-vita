/* s3eSound (PCM channels), s3eAudio (compressed music) and s3eVideo.
 *
 * Milestone 1 is silent: every call succeeds without producing output, so
 * the game's audio code runs its normal paths. Video reports failure so
 * any intro movie is skipped rather than waited on.
 */
#include "s3e/s3e_internal.h"

#define NCHAN 16
static int s_next_chan;

static int32_t sf_s3eSoundGetFreeChannel(void)
{
    int c = s_next_chan;
    s_next_chan = (s_next_chan + 1) % NCHAN;
    return c;
}

static int32_t sf_s3eSoundChannelPlay(int32_t ch, const int16_t *pcm, uint32_t samples,
                                      int32_t repeat, int32_t loop_from)
{
    return S3E_RESULT_SUCCESS;
}

static int32_t sf_s3eSoundChannelStop(int32_t ch) { return S3E_RESULT_SUCCESS; }
static int32_t sf_s3eSoundChannelSetInt(int32_t ch, uint32_t prop, int32_t v) { return S3E_RESULT_SUCCESS; }
static int32_t sf_s3eSoundSetInt(uint32_t prop, int32_t v) { return S3E_RESULT_SUCCESS; }
static int32_t sf_s3eSoundGetError(void) { return 0; }
static int32_t sf_s3eSoundStopAllChannels(void) { return S3E_RESULT_SUCCESS; }
static int32_t sf_s3eSoundPauseAllChannels(void) { return S3E_RESULT_SUCCESS; }
static int32_t sf_s3eSoundResumeAllChannels(void) { return S3E_RESULT_SUCCESS; }

static int32_t sf_s3eAudioPlay(const char *file, uint32_t repeat)
{
    LOGI("s3eAudioPlay(%s, %u) [silent]", file ? file : "", (unsigned)repeat);
    return S3E_RESULT_SUCCESS;
}
static int32_t sf_s3eAudioPlayFromBuffer(void *buf, uint32_t len, uint32_t repeat) { return S3E_RESULT_SUCCESS; }
static int32_t sf_s3eAudioStop(void) { return S3E_RESULT_SUCCESS; }
static int32_t sf_s3eAudioPause(void) { return S3E_RESULT_SUCCESS; }
static int32_t sf_s3eAudioResume(void) { return S3E_RESULT_SUCCESS; }
static int32_t sf_s3eAudioSetInt(uint32_t prop, int32_t v) { return S3E_RESULT_SUCCESS; }
static int32_t sf_s3eAudioRegister(uint32_t id, s3eCallback fn, void *ud) { return S3E_RESULT_SUCCESS; }

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
