#include "m_argv.h"
#include "doomkeys.h"
#include "doomgeneric.h"

#include <threads.h>
#include <stdio.h>
#include <string.h>
#include <sys/proc.h>
#include <sys/fb.h>
#include <libdwm/dwm.h>

static fb_info_t info;
static uint32_t* screenbuffer;

static bool init;

static clock_t startTime;
static display_t* disp;

static fd_t kbd;

const int keyToDoomkey[256] = {
    [KBD_NONE] = 0,

    [KBD_A] = 'A',
    [KBD_B] = 'B',
    [KBD_C] = 'C',
    [KBD_D] = 'D',
    [KBD_E] = 'E',
    [KBD_F] = 'F',
    [KBD_G] = 'G',
    [KBD_H] = 'H',
    [KBD_I] = 'I',
    [KBD_J] = 'J',
    [KBD_K] = 'K',
    [KBD_L] = 'L',
    [KBD_M] = 'M',
    [KBD_N] = 'N',
    [KBD_O] = 'O',
    [KBD_P] = 'P',
    [KBD_Q] = 'Q',
    [KBD_R] = 'R',
    [KBD_S] = 'S',
    [KBD_T] = 'T',
    [KBD_U] = 'U',
    [KBD_V] = 'V',
    [KBD_W] = 'W',
    [KBD_X] = 'X',
    [KBD_Y] = 'Y',
    [KBD_Z] = 'Z',

    [KBD_1] = '1',
    [KBD_2] = '2',
    [KBD_3] = '3',
    [KBD_4] = '4',
    [KBD_5] = '5',
    [KBD_6] = '6',
    [KBD_7] = '7',
    [KBD_8] = '8',
    [KBD_9] = '9',
    [KBD_0] = '0',

    [KBD_F1] = KEY_F1,
    [KBD_F2] = KEY_F2,
    [KBD_F3] = KEY_F3,
    [KBD_F4] = KEY_F4,
    [KBD_F5] = KEY_F5,
    [KBD_F6] = KEY_F6,
    [KBD_F7] = KEY_F7,
    [KBD_F8] = KEY_F8,
    [KBD_F9] = KEY_F9,
    [KBD_F10] = KEY_F10,
    [KBD_F11] = KEY_F11,
    [KBD_F12] = KEY_F12,

    [KBD_ENTER] = KEY_ENTER,
    [KBD_RETURN] = KEY_ENTER,
    [KBD_ESC] = KEY_ESCAPE,
    [KBD_BACKSPACE] = KEY_BACKSPACE,
    [KBD_TAB] = KEY_TAB,
    [KBD_SPACE] = ' ',
    [KBD_MINUS] = KEY_MINUS,
    [KBD_EQUAL] = KEY_EQUALS,
    [KBD_PAUSE] = KEY_PAUSE,

    [KBD_UP] = KEY_UPARROW,
    [KBD_DOWN] = KEY_DOWNARROW,
    [KBD_LEFT] = KEY_LEFTARROW,
    [KBD_RIGHT] = KEY_RIGHTARROW,

    [KBD_LEFT_SHIFT] = KEY_RSHIFT,
    [KBD_RIGHT_SHIFT] = KEY_RSHIFT,
    [KBD_LEFT_CTRL] = KEY_FIRE,
    [KBD_RIGHT_CTRL] = KEY_FIRE,
    [KBD_LEFT_ALT] = KEY_LALT,
    [KBD_RIGHT_ALT] = KEY_RALT,

    [KBD_CAPS_LOCK] = KEY_CAPSLOCK,
    [KBD_NUM_LOCK] = KEY_NUMLOCK,
    [KBD_SCROLL_LOCK] = KEY_SCRLCK,
    [KBD_SYSRQ] = KEY_PRTSCR,
    [KBD_HOME] = KEY_HOME,
    [KBD_END] = KEY_END,
    [KBD_PAGE_UP] = KEY_PGUP,
    [KBD_PAGE_DOWN] = KEY_PGDN,
    [KBD_INSERT] = KEY_INS,
    [KBD_SPACE] = KEY_USE,
    [KBD_DELETE] = KEY_DEL,

    [KBD_KP_0] = KEYP_0,
    [KBD_KP_1] = KEYP_1,
    [KBD_KP_2] = KEYP_2,
    [KBD_KP_3] = KEYP_3,
    [KBD_KP_4] = KEYP_4,
    [KBD_KP_5] = KEYP_5,
    [KBD_KP_6] = KEYP_6,
    [KBD_KP_7] = KEYP_7,
    [KBD_KP_8] = KEYP_8,
    [KBD_KP_9] = KEYP_9,
    [KBD_KP_SLASH] = KEYP_DIVIDE,
    [KBD_KP_PLUS] = KEYP_PLUS,
    [KBD_KP_MINUS] = KEYP_MINUS,
    [KBD_KP_ASTERISK] = KEYP_MULTIPLY,
    [KBD_KP_PERIOD] = KEYP_PERIOD,
    [KBD_KP_EQUAL] = KEYP_EQUALS,
    [KBD_KP_ENTER] = KEYP_ENTER,

    [KBD_LEFT_BRACE] = '[',
    [KBD_RIGHT_BRACE] = ']',
    [KBD_SEMICOLON] = ';',
    [KBD_APOSTROPHE] = '\'',
    [KBD_GRAVE] = '`',
    [KBD_COMMA] = ',',
    [KBD_PERIOD] = '.',
    [KBD_SLASH] = '/',
    [KBD_BACKSLASH] = '\\',

    [KBD_ERR_OVF] = KEY_ESCAPE,
    [KBD_POST_FAIL] = KEY_ESCAPE,
    [KBD_ERR_UNDEFINED] = KEY_ESCAPE
};

void DG_Init()
{
    init = true;
    startTime = uptime();

    disp = display_new();
    display_screen_acquire(disp, 0);

    fd_t fb = open("sys:/fb0");
    if (fb == ERR)
    {
        exit(EXIT_FAILURE);
    }
    if (ioctl(fb, IOCTL_FB_INFO, &info, sizeof(fb_info_t)) == ERR)
    {
        exit(EXIT_FAILURE);
    }

    switch (info.format)
    {
    case FB_ARGB32:
    {
        screenbuffer = mmap(fb, NULL, info.stride * info.height * sizeof(uint32_t), PROT_READ | PROT_WRITE);
        if (screenbuffer == NULL)
        {
            exit(EXIT_FAILURE);
        }
        memset(screenbuffer, 0, info.stride * info.height * sizeof(uint32_t));
    }
    break;
    default:
    {
        printf("invalid framebuffer format\n");
        exit(EXIT_FAILURE);
    }
    }

    close(fb);

    kbd = open("sys:/kbd/ps2");
}

static void deinit(void)
{
    munmap(screenbuffer, info.stride * info.height * sizeof(uint32_t));

    display_screen_release(disp, 0);
    display_free(disp);
}

void DG_DrawFrame()
{
    int topLeftX = (info.width / 2 - DOOMGENERIC_RESX / 2);
    int topLeftY = (info.height / 2 - DOOMGENERIC_RESY / 2);

    for (uint64_t y = 0; y < DOOMGENERIC_RESY; y++)
    {
        memcpy(&screenbuffer[(topLeftY + y) * info.stride + topLeftX], &DG_ScreenBuffer[y * DOOMGENERIC_RESX], DOOMGENERIC_RESX * sizeof(uint32_t));
    }

}

void DG_SleepMs(uint32_t ms)
{
    struct timespec timespec = {.tv_nsec = ms * 1000000};
    thrd_sleep(&timespec, NULL);
}

uint32_t DG_GetTicksMs()
{
    return (uptime() - startTime) / (CLOCKS_PER_SEC / 1000);
}

int DG_GetKey(int* pressed, unsigned char* doomKey)
{
    if (poll1(kbd, POLL_READ, 0) & POLL_READ)
    {
        kbd_event_t event;
        read(kbd, &event, sizeof(kbd_event_t));

        *pressed = event.type == KBD_PRESS;
        *doomKey = keyToDoomkey[event.code];

        return 1;
    }

    return 0;
}

void DG_SetWindowTitle(const char * title)
{
}

int main(int argc, char **argv)
{
    init = false;

    doomgeneric_Create(argc, argv);

    while (1)
    {
        doomgeneric_Tick();
    }

    if (init)
    {
        deinit();
    }

    return 0;
}
