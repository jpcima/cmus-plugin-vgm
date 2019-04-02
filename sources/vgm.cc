//          Copyright Jean Pierre Cimalando 2019.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include "vgm.h"
#include "mapped_file.h"
extern "C" {
#include <comment.h>
#include <xmalloc.h>
#include <debug.h>
}
#include <player/vgmplayer.hpp>
#include <player/s98player.hpp>
#include <player/droplayer.hpp>
#include <zlib.h>
#include <memory>
#include <cmath>
#include <cstdio>
#include <cstring>

static_assert(sizeof(WAVE_32BS) == 2 * sizeof(INT32) &&
              alignof(WAVE_32BS) == alignof(INT32),
              "The type WAVE_32BS is not structured as expected.");

static constexpr int samplerate = 44100;
static constexpr UINT32 maxrender = 4096;
static const double fadefactor = std::exp(-1.0 / samplerate);
static UINT32 maxloops = 1;

struct vgm_private {
    enum class State { stopped, started, atend };
    State state = State::stopped;
    double volume = 1;
    mapped_file map;
    std::unique_ptr<FileLoader> loader;
    std::unique_ptr<PlayerBase> player;
};

//------------------------------------------------------------------------------
static int vgz_open(input_plugin_data *ip_data, const UINT8 *data, size_t size);
static int vgm_open_after_map(input_plugin_data *ip_data);
static UINT8 vgm_play_callback(PlayerBase *player, void *user_param, UINT8 evt_type, void *evt_param);

//------------------------------------------------------------------------------
static int vgm_open(input_plugin_data *ip_data)
{
    d_print("vgm_open(%p): %s\n", ip_data, ip_data->filename);

    int ret = 0;
    std::unique_ptr<vgm_private> priv(new vgm_private);

    ip_data->priv = priv.get();

    mapped_file &map = priv->map;
    if (!map.open(ip_data->fd))
        ret = -IP_ERROR_ERRNO;
    else {
        const UINT8 *data = (const UINT8 *)map.data();
        size_t size = map.size();

        if (size >= 2 && data[0] == 0x1f && data[1] == 0x8b)
            ret = vgz_open(ip_data, data, size);
        else
            ret = vgm_open_after_map(ip_data);
    }

    if (ret == 0)
        priv.release();
    else
        ip_data->priv = nullptr;

    return ret;
}

static int vgz_open(input_plugin_data *ip_data, const UINT8 *data, size_t size)
{
    vgm_private *priv = (vgm_private *)ip_data->priv;

    struct File_Deleter { void operator()(FILE *x) { fclose(x); } };
    std::unique_ptr<FILE, File_Deleter> fh(tmpfile());
    if (!fh)
        return -IP_ERROR_ERRNO;

    int fd = dup(ip_data->fd);
    if (fd == -1)
        return -IP_ERROR_ERRNO;

    struct Gz_Deleter { void operator()(gzFile x) { gzclose(x); } };
    std::unique_ptr<gzFile_s, Gz_Deleter> gz(gzdopen(fd, "r"));
    if (!gz) {
        close(fd);
        return -IP_ERROR_FILE_FORMAT;
    }

    int gzcount;
    UINT8 buffer[8192];
    while ((gzcount = gzread(gz.get(), buffer, sizeof(buffer))) > 0) {
        if (fwrite(buffer, 1, gzcount, fh.get()) != gzcount)
            return -IP_ERROR_ERRNO;
    }
    if (gzcount == -1)
        return -IP_ERROR_FILE_FORMAT;

    if (fflush(fh.get()) != 0)
        return -IP_ERROR_ERRNO;
    rewind(fh.get());

    mapped_file &map = priv->map;
    if (!map.open(fileno(fh.get())))
        return -IP_ERROR_ERRNO;

    return vgm_open_after_map(ip_data);
}

static int vgm_open_after_map(input_plugin_data *ip_data)
{
    vgm_private *priv = (vgm_private *)ip_data->priv;

    mapped_file &map = priv->map;
    const UINT8 *data = (const UINT8 *)map.data();
    size_t size = map.size();

    FileLoader *loader = new FileLoader;
    priv->loader.reset(loader);
    loader->SetPreloadBytes(0x100);
    if (loader->LoadData(size, data) != 0)
        return -IP_ERROR_FILE_FORMAT;

    if (VGMPlayer::IsMyFile(*loader) == 0)
        priv->player.reset(new VGMPlayer);
    else if (S98Player::IsMyFile(*loader) == 0)
        priv->player.reset(new S98Player);
    else if (DROPlayer::IsMyFile(*loader) == 0)
        priv->player.reset(new DROPlayer);
    else
        return -IP_ERROR_FILE_FORMAT;

    PlayerBase &player = *priv->player;
    if (player.LoadFile(*loader) != 0)
        return -IP_ERROR_FILE_FORMAT;
    player.SetCallback(&vgm_play_callback, priv);
    player.SetSampleRate(samplerate);
    player.Start();
    priv->state = vgm_private::State::started;
    priv->volume = 1;

    ip_data->sf = sf_bits(32) | sf_rate(samplerate) | sf_channels(2) | sf_signed(1);
    ip_data->sf |= sf_host_endian();
    channel_map_init_stereo(ip_data->channel_map);
    return 0;
}

static UINT8 vgm_play_callback(PlayerBase *player, void *user_param, UINT8 evt_type, void *evt_param)
{
    vgm_private *priv = (vgm_private *)user_param;

    switch (evt_type) {
    case PLREVT_LOOP: {
        UINT32 curloop = *(UINT32 *)evt_param;
        if (curloop >= maxloops)
            priv->state = vgm_private::State::atend;
        break;
    }
    case PLREVT_END:
        priv->state = vgm_private::State::atend;
        break;
    }
    return 0;
}

static int vgm_close(input_plugin_data *ip_data)
{
    d_print("vgm_close(%p)\n", ip_data);

    vgm_private *priv = (vgm_private *)ip_data->priv;
    PlayerBase *player = priv->player.get();

    if (player) {
        player->Stop();
        player->UnloadFile();
    }

    delete priv;
    ip_data->priv = nullptr;
    return 0;
}

static int vgm_read(input_plugin_data *ip_data, char *buffer, int count)
{
    d_print("vgm_read(%p, %d)\n", ip_data, count);

    vgm_private *priv = (vgm_private *)ip_data->priv;
    PlayerBase &player = *priv->player;

    bool atend = priv->state == vgm_private::State::atend;
    if (atend && player.GetLoopTicks() == 0)
        return 0; // if not a looped song, just stop right here

    int want = count / sizeof(WAVE_32BS);
    if (want > maxrender) want = maxrender;  // workaround for libvgm internal limit

    std::fill(buffer, buffer + want * sizeof(WAVE_32BS), 0);
    int got = player.Render(want, (WAVE_32BS *)buffer);

    for (int i = 0; i < 2 * got; ++i) {
        INT32 *dst = (INT32 *)(buffer + i * sizeof(int32_t));

        INT32 smpl = *dst;

        constexpr unsigned smplbits = 24;
        constexpr INT32 smplmax = (1 << (smplbits - 1)) - 1;
        constexpr INT32 smplmin = -smplmax;

        // clipping
        if (smpl > smplmax)
            smpl = smplmax;
        else if (smpl < smplmin)
            smpl = smplmin;
        // 32 bit conversion
        smpl *= 1 << (32 - smplbits);

        *dst = smpl;
    }

    if (atend) { // if a looped song, smoothly turn down the volume
        double vol = priv->volume;
        for (int i = 0; i < got; ++i) {
            WAVE_32BS *dst = &((WAVE_32BS *)buffer)[i];
            vol *= fadefactor;
            dst->L = (INT32)std::lround(vol * dst->L);
            dst->R = (INT32)std::lround(vol * dst->R);
            if (vol < 1e-4) { got = i; break; }
        }
        priv->volume = vol;
    }

    return got * sizeof(WAVE_32BS);
}

static int vgm_seek(input_plugin_data *ip_data, double offset)
{
    d_print("vgm_seek(%p)\n", ip_data);

    //TODO: optimize this

    vgm_private *priv = (vgm_private *)ip_data->priv;
    PlayerBase &player = *priv->player;

    priv->state = vgm_private::State::started;
    priv->volume = 1;
    player.Reset();

    static WAVE_32BS skipbuf[maxrender];
    UINT32 skip = std::lround(offset * samplerate);

    while (skip > 0) {
        UINT32 count = skip;
        if (count > maxrender) count = maxrender;
        player.Render(count, skipbuf);
        skip -= count;
    }

    return 0;
}

static int vgm_read_comments(input_plugin_data *ip_data, struct keyval **comments)
{
    d_print("vgm_read_comments(%p)\n", ip_data);

    vgm_private *priv = (vgm_private *)ip_data->priv;
    PlayerBase &player = *priv->player;
    GROWING_KEYVALS(c);

    const char *title = player.GetSongTitle();
    if (title && title[0])
        comments_add_const(&c, "title", title);

    keyvals_terminate(&c);
    *comments = c.keyvals;

    return 0;
}

static int vgm_duration(input_plugin_data *ip_data)
{
    d_print("vgm_duration(%p)\n", ip_data);

    vgm_private *priv = (vgm_private *)ip_data->priv;
    PlayerBase &player = *priv->player;

    return player.Tick2Second(player.GetTotalPlayTicks(maxloops));
}

static long vgm_bitrate(input_plugin_data *ip_data)
{
    d_print("vgm_bitrate(%p)\n", ip_data);

    return -IP_ERROR_FUNCTION_NOT_SUPPORTED;
}

static char *vgm_codec(input_plugin_data *ip_data)
{
    d_print("vgm_codec(%p)\n", ip_data);

    return nullptr;
}

static char *vgm_codec_profile(input_plugin_data *ip_data)
{
    d_print("vgm_codec_profile(%p)\n", ip_data);

    return nullptr;
}

//------------------------------------------------------------------------------
static int vgm_set_maxloops(const char *val)
{
    unsigned num;
    unsigned size;
    if (sscanf(val, "%u%n", &num, &size) != 1 || strlen(val) != size) {
        errno = EINVAL;
        return -IP_ERROR_ERRNO;
    }
    maxloops = num;
    return 0;
}

static int vgm_get_maxloops(char **val)
{
    char str[32];
    sprintf(str, "%u", maxloops);
    *val = xstrdup(str);
    return 0;
}

//------------------------------------------------------------------------------
const struct input_plugin_ops ip_ops = {
    .open = &vgm_open,
    .close = &vgm_close,
    .read = &vgm_read,
    .seek = &vgm_seek,
    .read_comments = &vgm_read_comments,
    .duration = &vgm_duration,
    .bitrate = &vgm_bitrate,
    .bitrate_current = &vgm_bitrate,
    .codec = &vgm_codec,
    .codec_profile = &vgm_codec_profile
};

const int ip_priority = 50;
const char * const ip_extensions[] = { "vgm", "vgz", "s98", "dro", nullptr };
const char * const ip_mime_types[] = { nullptr };
const struct input_plugin_opt ip_options[] = { {"max_loops", &vgm_set_maxloops, &vgm_get_maxloops}, { nullptr } };
const unsigned ip_abi_version = IP_ABI_VERSION;
