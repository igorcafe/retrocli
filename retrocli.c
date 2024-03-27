#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <string.h>
#include <errno.h>
#include "libretro.h"
#include <alsa/asoundlib.h>
#include <signal.h>

#define LOG_LEVEL RETRO_LOG_INFO

// TODO make it a function
#define fatal(msg, ...)                      \
    {                                        \
        fprintf(stderr, "FATAL: ");          \
        fprintf(stderr, msg, ##__VA_ARGS__); \
        fprintf(stderr, "\n");               \
        shutdown(1);                         \
    }

// loads a symbol named N from handle H inside struct B
#define load_sym(H, B, N)                                       \
    {                                                           \
        (*(void **)&B.N) = dlsym(H, #N);                        \
        if (!B.N)                                               \
        {                                                       \
            fatal("failed to load symbol '#N': %s", dlerror()); \
        }                                                       \
    }

struct g_app
{
    // used to play sound
    snd_pcm_t *pcm;

    // indicates the state of each button in the retropad
    unsigned joypad[RETRO_DEVICE_ID_JOYPAD_L3 + 1];

    // libretro functions
    void (*retro_set_environment)(retro_environment_t);
    void (*retro_set_video_refresh)(retro_video_refresh_t);
    void (*retro_set_audio_sample)(retro_audio_sample_t);
    void (*retro_set_audio_sample_batch)(retro_audio_sample_batch_t);
    void (*retro_set_input_poll)(retro_input_poll_t);
    void (*retro_set_input_state)(retro_input_state_t);
    void (*retro_init)(void);
    void (*retro_deinit)(void);
    unsigned (*retro_api_version)(void);
    void (*retro_get_system_info)(struct retro_system_info *);
    void (*retro_get_system_av_info)(struct retro_system_av_info *);
    void (*retro_set_controller_port_device)(unsigned, unsigned);
    void (*retro_reset)(void);
    void (*retro_run)(void);
    size_t (*retro_serialize_size)(void);
    bool (*retro_serialize)(void *, size_t);
    bool (*retro_unserialize)(const void *, size_t);
    bool (*retro_load_game)(const struct retro_game_info *);
    void (*retro_unload_game)(void);
} g;

void shutdown(int status)
{
    if (g.pcm)
        snd_pcm_close(g.pcm);

    if (g.retro_unload_game)
        g.retro_unload_game();

    if (g.retro_deinit)
        g.retro_deinit();

    endwin();

    fprintf(stderr, "exited\n");

    exit(status);
}

void cb_core_log(enum retro_log_level level, const char *fmt, ...)
{
    if (level < LOG_LEVEL)
        return;

    char buf[1024] = {0};
    char *levels[] = {"DEB", "INF", "WRN", "ERR"};
    va_list va;
    va_start(va, fmt);
    vsnprintf(buf, sizeof(buf), fmt, va);
    va_end(va);

    fprintf(stderr, "%s: %s", levels[level], buf);
}

// TODO
bool cb_environment(unsigned cmd, void *data)
{
    switch (cmd)
    {
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        struct retro_log_callback *cb = data;
        cb->log = cb_core_log;
        return true;

    // disallow setting pixel format
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        return false;

    case RETRO_ENVIRONMENT_GET_CAN_DUPE:
        *(bool *)data = true;
        return true;

    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        *(char **)data = ".";

    default:
        // fprintf(stderr, "unhandled environment: #%u\n", cmd);
        return false;
    }
}

struct color
{
    short bits;
    short id;
};

struct color colors[] = {
    {0b000, COLOR_BLACK},
    {0b100, COLOR_RED},
    {0b010, COLOR_GREEN},
    {0b001, COLOR_BLUE},

    {0b110, COLOR_YELLOW},
    {0b101, COLOR_MAGENTA},
    {0b011, COLOR_CYAN},

    {0b111, COLOR_WHITE},
    {-1, -1},
};

// TODO: remove from this function calls that doesn´t have to happen on every refresh
//
// Currently only supports 0RGB155 pixel format, where each pixel contains 16 bit of information:
// ARRRRRGGGGGBBBBB
// A (alpha):  0 = transparent, 1 = opaque
// R (red): 5 bit unsigned number, representing the intensity of red (0 = no red, 31 = max red)
// G (green): same idea as R
// B (blue): same idea as R
void cb_video_refresh(const void *data, unsigned width, unsigned height, size_t pitch)
{
    if (!data)
        return;

    uint16_t *pixels = (uint16_t *)data;

    start_color();

    // init terminal colors from COLOR_BLACK to COLOR_WHITE
    for (int i = 0; i <= 7; i++)
    {
        init_pair(i, i, i);
    }

    // used for skipping pixels. 1 = no skip, 2 = half the size
    int step = 1;

    for (int i = 0; i < width * height; i += step)
    {
        unsigned w = i % width / step;
        unsigned h = i / width / step;

        uint16_t a = (pixels[i] >> 14) & 1;
        // alpha = 0, draw transparent block
        if (!a)
        {
            mvprintw(h, w, " ");
            continue;
        }

        // just taking the bits from 0RGB1555 as explained before
        uint16_t red = (pixels[i] >> 9) & 0x1F;
        uint16_t green = (pixels[i] >> 6) & 0x1F;
        uint16_t blue = pixels[i] & 0x1F;

        // divide by 16 so values in range [0, 15] become 0 and values in range [16, 31] become 1
        short bits = (red / 16 % 2) << 2;
        bits |= (green / 16 % 2) << 1;
        bits |= blue / 16 % 2;

        struct color clr = {-1, -1};
        for (int i = 0; colors[i].bits >= 0; i++)
        {
            if (colors[i].bits == bits)
            {
                clr = colors[i];
                break;
            }
        }

        // color not identified, draw transparent block
        if (clr.id < 0)
        {
            mvprintw(h, w, " ");
            continue;
        }

        // if (clr.id == COLOR_GREEN)
        //     fprintf(stderr, "green!\n");
        // if (clr.id == COLOR_MAGENTA)
        //     fprintf(stderr, "magenta!\n");

        // draw pixel
        attron(COLOR_PAIR(clr.id));
        mvprintw(h, w, " ");
        attroff(COLOR_PAIR(clr.id));
    }

    refresh();
}

size_t cb_audio_sample_batch(const int16_t *data, size_t frames)
{
    if (!g.pcm)
        return 0;

    int n = snd_pcm_writei(g.pcm, data, frames);
    if (n < 0)
    {
        snd_pcm_recover(g.pcm, n, 0);
        return 0;
    }

    return n;
}

void cb_audio_sample(int16_t left, int16_t right)
{
    int16_t buf[2] = {left, right};
    cb_audio_sample_batch(buf, 1);
}

// input poll captures and stores input state
void cb_input_poll(void)
{
    int ch = getch();
    if (ch == ERR)
        return;

    if (ch == 'j')
        g.joypad[RETRO_DEVICE_ID_JOYPAD_LEFT] = 1;
    else
        g.joypad[RETRO_DEVICE_ID_JOYPAD_LEFT] = 0;

    if (ch == 'l')
        g.joypad[RETRO_DEVICE_ID_JOYPAD_RIGHT] = 1;
    else
        g.joypad[RETRO_DEVICE_ID_JOYPAD_RIGHT] = 0;

    if (ch == 'i')
        g.joypad[RETRO_DEVICE_ID_JOYPAD_UP] = 1;
    else
        g.joypad[RETRO_DEVICE_ID_JOYPAD_UP] = 0;

    if (ch == 'k')
        g.joypad[RETRO_DEVICE_ID_JOYPAD_DOWN] = 1;
    else
        g.joypad[RETRO_DEVICE_ID_JOYPAD_DOWN] = 0;

    if (ch == 'z')
        g.joypad[RETRO_DEVICE_ID_JOYPAD_B] = 1;
    else
        g.joypad[RETRO_DEVICE_ID_JOYPAD_B] = 0;

    if (ch == 'x')
        g.joypad[RETRO_DEVICE_ID_JOYPAD_A] = 1;
    else
        g.joypad[RETRO_DEVICE_ID_JOYPAD_A] = 0;

    if (ch == ' ')
        g.joypad[RETRO_DEVICE_ID_JOYPAD_START] = 1;
    else
        g.joypad[RETRO_DEVICE_ID_JOYPAD_START] = 0;
}

// core calls this function to get the state of a specific button for example.
// port 0 is the console port for player 1.
// index is basically only used for analogs, where 0 is left analog and 1 is right analog.
// since it only supports keyboard it is always treated as 0.
// device is the emulated device and can be a mouse, keyboard, retropad, analog, light gun, ...
int16_t cb_input_state(unsigned port, unsigned device, unsigned index, unsigned id)
{
    if (port || index || device != RETRO_DEVICE_JOYPAD)
        return 0;

    return g.joypad[id];
}

void signal_handler(int i)
{
    fatal("interruped: shutting down");
}

int main(int argc, char *argv[])
{
    signal(SIGINT, signal_handler);
    signal(SIGKILL, signal_handler);

    if (argc != 3)
        fatal("usage: %s <corepath> <rompath> (only %d args given)", argv[0], argc);

    char *core_path = argv[1];
    char *rom_path = argv[2];

    // try to dynamically link with core
    void *handle = dlopen(core_path, RTLD_LAZY);
    if (!handle)
        fatal("failed to load core %s: %s", core_path, dlerror());

    load_sym(handle, g, retro_set_environment);
    load_sym(handle, g, retro_set_video_refresh);
    load_sym(handle, g, retro_set_audio_sample);
    load_sym(handle, g, retro_set_audio_sample_batch);
    load_sym(handle, g, retro_set_input_poll);
    load_sym(handle, g, retro_set_input_state);
    load_sym(handle, g, retro_init);
    load_sym(handle, g, retro_deinit);
    load_sym(handle, g, retro_api_version);
    load_sym(handle, g, retro_get_system_info);
    load_sym(handle, g, retro_get_system_av_info);
    load_sym(handle, g, retro_set_controller_port_device);
    load_sym(handle, g, retro_reset);
    load_sym(handle, g, retro_run);
    load_sym(handle, g, retro_serialize_size);
    load_sym(handle, g, retro_serialize);
    load_sym(handle, g, retro_unserialize);
    load_sym(handle, g, retro_load_game);
    load_sym(handle, g, retro_unload_game);

    // register callbacks
    g.retro_set_environment(cb_environment);
    g.retro_set_video_refresh(cb_video_refresh);
    g.retro_set_audio_sample(cb_audio_sample);
    g.retro_set_audio_sample_batch(cb_audio_sample_batch);
    g.retro_set_input_poll(cb_input_poll);
    g.retro_set_input_state(cb_input_state);

    // load core
    g.retro_init();
    fprintf(stderr, "core loaded\n");

    // gather game info
    struct retro_game_info game = {rom_path, 0};

    FILE *file = fopen(rom_path, "rb");
    if (!file)
        fatal("failed to open rom: %s", strerror(errno));

    fseek(file, 0, SEEK_END);
    game.size = ftell(file);
    rewind(file);

    struct retro_system_info system = {0};
    g.retro_get_system_info(&system);

    if (!system.need_fullpath)
    {
        game.data = malloc(game.size);
        if (!game.data)
            fatal("failed to allocate %ld bytes: %s", game.size, strerror(errno));

        if (!fread(game.data, game.size, 1, file))
            fatal("failed to read game rom: %s", strerror(errno));
    }

    if (!g.retro_load_game(&game))
        fatal("core failed to load game");

    // init sound
    struct retro_system_av_info av = {0};
    g.retro_get_system_av_info(&av);

    int err = snd_pcm_open(&g.pcm, "default", SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0)
        fatal("failed to open playback device: %s", snd_strerror(err));

    err = snd_pcm_set_params(g.pcm, SND_PCM_FORMAT_S16, SND_PCM_ACCESS_RW_INTERLEAVED, 2, av.timing.sample_rate, 1, 64 * 1000);
    if (err < 0)
        fatal("failed to configure playback device: %s", snd_strerror(err));

    // init window
    initscr();
    cbreak();
    keypad(stdscr, TRUE);
    timeout(5);

    // main loop
    for (;;)
        g.retro_run();

    shutdown(0);
}