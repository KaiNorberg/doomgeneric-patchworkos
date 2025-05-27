#include "m_argv.h"
#include "doomkeys.h"
#include "doomgeneric.h"
#include "i_video.h"

#include <errno.h>
#include <threads.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/proc.h>
#include <sys/fb.h>
#include <libpatchwork/patchwork.h>

// Note: I might have over optimized rendering a little.

static bool init;
static clock_t startTime;

static display_t* disp;
static window_t* win;
static rect_t screenRect;

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
    //[KBD_SPACE] = ' ',
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

#define KBD_QUEUE_SIZE 16

static event_kbd_t kbdQueue[KBD_QUEUE_SIZE];
static uint64_t kbdQueueWriteIndex = 0;
static uint64_t kbdQueueReadIndex = 0;

static void key_queue_push(const event_kbd_t* event)
{
    kbdQueue[kbdQueueWriteIndex] = *event;
    kbdQueueWriteIndex = (kbdQueueWriteIndex + 1) % KBD_QUEUE_SIZE;
}

static event_kbd_t key_queue_pop(void)
{
    event_kbd_t event = kbdQueue[kbdQueueReadIndex];
    kbdQueueReadIndex = (kbdQueueReadIndex + 1) % KBD_QUEUE_SIZE;
    return event;
}

static bool key_queue_avail(void)
{
    return (kbdQueueReadIndex != kbdQueueWriteIndex);
}

static inline void* memset32_inline(void* s, uint32_t c, size_t n)
{
    uint32_t* p = s;

    while (((uintptr_t)p & 3) && n) 
    {
        *p++ = c;
        n--;
    }

    while (n >= 8) 
    {
        p[0] = c; 
        p[1] = c; 
        p[2] = c; 
        p[3] = c;
        p[4] = c; 
        p[5] = c; 
        p[6] = c; 
        p[7] = c;
        p += 8;
        n -= 8;
    }
    
    while (n >= 1) 
    {
        *p++ = c;
        n--;
    }
    
    return s;
}

static uint64_t dstYStart[SCREENHEIGHT];
static uint64_t dstYEnd[SCREENHEIGHT];
static uint64_t dstXStart[SCREENWIDTH];
static uint64_t dstXEnd[SCREENWIDTH];

static void precalculate_coords(void)
{
    int64_t width = RECT_WIDTH(&screenRect);
    int64_t height = RECT_HEIGHT(&screenRect);

    uint64_t upscaleNumerator;
    uint64_t upscaleDenominator;
    if (width / SCREENWIDTH > height / SCREENHEIGHT)
    {
        upscaleNumerator = height;
        upscaleDenominator = SCREENHEIGHT;
    }
    else
    {
        upscaleNumerator = width;
        upscaleDenominator = SCREENWIDTH;
    }

    const uint64_t scaledWidth = (SCREENWIDTH * upscaleNumerator) / upscaleDenominator;
    const uint64_t scaledHeight = (SCREENHEIGHT * upscaleNumerator) / upscaleDenominator;
    const uint64_t topLeftX = (width - scaledWidth) / 2;
    const uint64_t topLeftY = (height - scaledHeight) / 2;
    
    for (uint64_t srcY = 0; srcY < SCREENHEIGHT; srcY++) 
    {
        dstYStart[srcY] = topLeftY + (srcY * upscaleNumerator) / upscaleDenominator;
        dstYEnd[srcY] = topLeftY + ((srcY + 1) * upscaleNumerator) / upscaleDenominator;
    }
    
    for (uint64_t srcX = 0; srcX < SCREENWIDTH; srcX++) 
    {
        dstXStart[srcX] = topLeftX + (srcX * upscaleNumerator) / upscaleDenominator;
        dstXEnd[srcX] = topLeftX + ((srcX + 1) * upscaleNumerator) / upscaleDenominator;
    }
}

static uint64_t procedure(window_t* win, element_t* elem, const event_t* event)
{
    switch (event->type)
    {
    case LEVENT_REDRAW:
    {
        drawable_t draw;
        element_draw_begin(elem, &draw);

        const uint64_t scaledStartX = dstXStart[0];
        const uint64_t scaledWidth = dstXEnd[SCREENWIDTH-1] - scaledStartX;
    
        for (uint64_t srcY = 0; srcY < SCREENHEIGHT; srcY++)
        {
            const uint64_t srcRowOffset = srcY * SCREENWIDTH;
            
            uint64_t relativeStartX = 0;
            uint32_t currentPixel = *((uint32_t*)&colors[I_VideoBuffer[srcRowOffset]]);

            uint32_t* firstRow = &draw.buffer[dstYStart[srcY] * draw.stride + scaledStartX];

            for (uint64_t srcX = 1; srcX < SCREENWIDTH; srcX++)
            {
                const uint32_t nextPixel = *((uint32_t*)&colors[I_VideoBuffer[srcX + srcRowOffset]]);
                
                if (nextPixel != currentPixel) 
                {
                    uint64_t relativeEndX = dstXStart[srcX] - scaledStartX; 
                    
                    memset32_inline(&firstRow[relativeStartX], currentPixel,
                        relativeEndX - relativeStartX);
                    
                    relativeStartX = relativeEndX;
                    currentPixel = nextPixel;
                }
            }
            memset32_inline(&firstRow[relativeStartX], currentPixel,
                scaledWidth - relativeStartX);
    
            for (uint64_t y = dstYStart[srcY] + 1; y < dstYEnd[srcY]; y++) 
            {
                uint32_t* dstRowTarget = &draw.buffer[y * draw.stride + scaledStartX];
                
                memcpy(dstRowTarget, firstRow, scaledWidth * sizeof(uint32_t));
            }
        }

        draw_invalidate(&draw, NULL);

        element_draw_end(elem, &draw);
    }
    break;
    case EVENT_KBD:
    {
        key_queue_push(&event->kbd);
    }
    break;
    }

    return 0;
}

void DG_Init()
{
    init = TRUE;
    startTime = uptime();

    disp = display_new();

    display_screen_rect(disp, &screenRect, 0);
    win = window_new(disp, "Doom", &screenRect, SURFACE_FULLSCREEN, WINDOW_NONE, procedure, NULL);
    if (win == NULL)
    {
        fprintf(stderr, "doom: failed to create window (%s)\n", strerror(errno));
        abort();
    }

    precalculate_coords();
}

static void deinit(void)
{
    window_free(win);
    display_free(disp);
}

void DG_DrawFrame()
{   
    levent_redraw_t event;
    event.id = element_id_get(window_client_element_get(win));
    event.propagate = false;
    display_emit(disp, window_id_get(win), LEVENT_REDRAW, &event, sizeof(event));
}

void DG_SleepMs(uint32_t ms)
{
    struct timespec timespec = {.tv_nsec = ms * (CLOCKS_PER_SEC / 1000)};
    thrd_sleep(&timespec, NULL);
}

uint32_t DG_GetTicksMs()
{
    return (uptime() - startTime) / (CLOCKS_PER_SEC / 1000);
}

int DG_GetKey(int* pressed, unsigned char* doomKey)
{
    if (key_queue_avail())
    {
        event_kbd_t event = key_queue_pop();

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
    init = FALSE;

    doomgeneric_Create(argc, argv);

    while (display_connected(disp))
    {
        event_t event;
        while (display_next_event(disp, &event, 0))
        {
            display_dispatch(disp, &event);
        }
        doomgeneric_Tick();
    }

    if (init)
    {
        deinit();
    }

    printf("doom: exit\n");
    return 0;
}