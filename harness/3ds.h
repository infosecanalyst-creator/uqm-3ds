/* Desktop stand-in for the parts of libctru the UQM 3DS port uses.
 * Lets the patched tree (built with -D__3DS__) run headless on Linux:
 * framebuffers are plain memory dumped to PNG on request, buttons and touch
 * come from a timed script (UQM3DS_SCRIPT), threads are pthreads.
 * Only used by harness/Makefile; the real build uses devkitPro's libctru. */
#ifndef FAKE_3DS_H
#define FAKE_3DS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
typedef int32_t s32; typedef int64_t s64;
typedef s32 Result; typedef u32 Handle;
typedef struct Thread_tag *Thread;
typedef void (*ThreadFunc)(void *);

#define R_SUCCEEDED(r) ((r) >= 0)
#define R_FAILED(r) ((r) < 0)
#define U64_MAX UINT64_MAX
#define CUR_THREAD_HANDLE 0xFFFF8000
#define SYSCLOCK_ARM11 268111856ULL

#define BIT(n) (1U << (n))
enum {
	KEY_A = BIT(0), KEY_B = BIT(1), KEY_SELECT = BIT(2), KEY_START = BIT(3),
	KEY_DRIGHT = BIT(4), KEY_DLEFT = BIT(5), KEY_DUP = BIT(6), KEY_DDOWN = BIT(7),
	KEY_R = BIT(8), KEY_L = BIT(9), KEY_X = BIT(10), KEY_Y = BIT(11),
	KEY_ZL = BIT(14), KEY_ZR = BIT(15), KEY_TOUCH = BIT(20),
	KEY_CSTICK_RIGHT = BIT(24), KEY_CSTICK_LEFT = BIT(25),
	KEY_CSTICK_UP = BIT(26), KEY_CSTICK_DOWN = BIT(27),
};

typedef struct { u16 px, py; } touchPosition;
typedef struct { int16_t dx, dy; } circlePosition;

typedef enum { GFX_TOP = 0, GFX_BOTTOM = 1 } gfxScreen_t;
typedef enum { GFX_LEFT = 0, GFX_RIGHT = 1 } gfx3dSide_t;
typedef enum { GSP_RGBA8_OES = 0 } GSPGPU_FramebufferFormat;
enum { GSPLCD_SCREEN_TOP = 1, GSPLCD_SCREEN_BOTTOM = 2 };

void gfxInit (GSPGPU_FramebufferFormat top, GSPGPU_FramebufferFormat bottom, bool vram);
void gfxExit (void);
void gfxSet3D (bool on);
float osGet3DSliderState (void);
u8 *gfxGetFramebuffer (gfxScreen_t screen, gfx3dSide_t side, u16 *w, u16 *h);
void gfxScreenSwapBuffers (gfxScreen_t screen, bool stereo);
void gfxSwapBuffers (void);
Result GSPGPU_FlushDataCache (const void *p, u32 size);
void gspWaitForVBlank (void);
Result gspLcdInit (void);
void gspLcdExit (void);
Result GSPLCD_PowerOffBacklight (u32 screen);
Result GSPLCD_PowerOnBacklight (u32 screen);

void osSetSpeedupEnable (bool on);
u64 osGetTime (void);
u64 svcGetSystemTick (void);
bool aptMainLoop (void);
Result APT_CheckNew3DS (bool *out);
Result APT_SetAppCpuTimeLimit (u32 percent);

void hidScanInput (void);
u32 hidKeysDown (void);
u32 hidKeysHeld (void);
u32 hidKeysUp (void);
void hidCircleRead (circlePosition *p);
void hidTouchRead (touchPosition *p);

Thread threadCreate (ThreadFunc fn, void *arg, size_t stack, int prio, int core, bool detached);
Result threadJoin (Thread t, u64 timeout);
void threadFree (Thread t);
Handle threadGetHandle (Thread t);
Result svcGetThreadId (u32 *id, Handle h);
Result svcGetThreadPriority (s32 *prio, Handle h);
void svcSleepThread (s64 ns);

typedef enum { SWKBD_TYPE_NORMAL = 0, SWKBD_TYPE_QWERTY, SWKBD_TYPE_NUMPAD, SWKBD_TYPE_WESTERN } SwkbdType;
typedef enum { SWKBD_BUTTON_NONE = -1, SWKBD_BUTTON_LEFT = 0, SWKBD_BUTTON_MIDDLE, SWKBD_BUTTON_RIGHT,
	SWKBD_BUTTON_CONFIRM = SWKBD_BUTTON_RIGHT } SwkbdButton;
enum { SWKBD_NOTEMPTY_NOTBLANK = 3 };
typedef struct { int max; char initial[128]; } SwkbdState;
void swkbdInit (SwkbdState *s, SwkbdType t, int buttons, int maxlen);
void swkbdSetInitialText (SwkbdState *s, const char *t);
void swkbdSetHintText (SwkbdState *s, const char *t);
void swkbdSetValidation (SwkbdState *s, int v, u32 f, int d);
SwkbdButton swkbdInputText (SwkbdState *s, char *out, size_t size);

#endif
