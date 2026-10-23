#include "pch.h"
#include "emu48.h"
#include "io.h"
#include "i28f160.h"

#include "FreeRTOS.h"
#include "task.h"

#include "sys_llapi.h"
#include "SystemUI.h"

#include "emu48_kb_39g.h"
#include "keyboard_gii39.h"

CRITICAL_SECTION csGDILock;					// critical section for hWindowDC
CRITICAL_SECTION csLcdLock;					// critical section for display update
CRITICAL_SECTION csKeyLock;					// critical section for key scan
CRITICAL_SECTION csIOLock;					// critical section for I/O access
CRITICAL_SECTION csT1Lock;					// critical section for timer1 access
CRITICAL_SECTION csT2Lock;					// critical section for timer2 access
CRITICAL_SECTION csTxdLock;					// critical section for transmit byte
CRITICAL_SECTION csRecvLock;				// critical section for receive byte
CRITICAL_SECTION csSlowLock;				// critical section for speed slow down

HANDLE           hEventShutdn;				// event handle to stop cpu thread
HANDLE           hThread;

BYTE   cCurrentRomType = 0;					// Model -> hardware
UINT   nCurrentClass = 0;					// Class -> derivate
BOOL   bRomWriteable = TRUE;				// flag if ROM writeable

LPBYTE pbyRom = NULL;
DWORD  dwRomSize = 0;

LPBYTE pbyPort2 = NULL;
BOOL   bPort2Writeable = FALSE;
BOOL   bPort2IsShared = FALSE;
DWORD  dwPort2Size = 0;						// size of mapped port2
DWORD  dwPort2Mask = 0;

LARGE_INTEGER    lFreq;						// high performance counter frequency
LARGE_INTEGER    lAppStart;					// high performance counter value at Appl. start

static uint8_t *full_screen_buf = NULL;

static TaskHandle_t hEmuWorkerTask = NULL;
static TaskHandle_t hEmuTimerTask  = NULL;
static TaskHandle_t hEmuDispTask   = NULL;

bool rom_is_packed(void);
int load_rom(const char *romfile);
void unload_rom(void);
int load_port_file(const char *path, void *buf, size_t size);
int save_port_file(const char *path, const void *buf, size_t size);
void *get_emu_rom_addr(void);
size_t get_rom_size(void);

void __attribute__((target("arm"))) emu_thread(void *_)
{
    int ret = WorkerThread(NULL);
    printf("WorkerThread Exit:%d\n", ret);
    hEmuWorkerTask = NULL;
    vTaskDelete(NULL);
}

typedef struct emu48_emu_timer_t
{
    struct emu48_emu_timer_t *next;
    bool oneshot;
    uint32_t period_ms;
    uint32_t count;
    LPTIMECALLBACK cb;
    uint32_t userData;
} emu48_emu_timer_t;

static emu48_emu_timer_t *emu_timers = NULL;

uint32_t emu_add_timer(uint32_t period_ms, LPTIMECALLBACK cb, uint32_t userData, bool oneshot)
{
    emu48_emu_timer_t *t;
    emu48_emu_timer_t *t_prev;

    vPortEnterCritical();
    if (emu_timers) {
        t = emu_timers;
        do {
            t_prev = t;
            t = t->next;
        } while (t != NULL);
        t = pvPortMalloc(sizeof(emu48_emu_timer_t));
    } else {
        t = emu_timers = pvPortMalloc(sizeof(emu48_emu_timer_t));
        t_prev = NULL;
    }

    assert(t != NULL);

    memset(t, 0, sizeof(emu48_emu_timer_t));

    t->period_ms = period_ms;
    t->cb = cb;
    t->userData = userData;
    t->oneshot = oneshot;
    t->count = 0;
    t->next = NULL;

    if (t_prev) {
        t_prev->next = t;
    }
    vPortExitCritical();

    return (uint32_t)t;
}

uint32_t emu_del_timer(emu48_emu_timer_t *t_del)
{
    emu48_emu_timer_t *t_prev;

    if (emu_timers == NULL || t_del == NULL) {
        return 0;
    }

    vPortEnterCritical();

    if (t_del == emu_timers) {
        emu_timers = t_del->next;
        vPortFree(t_del);
    } else {
        t_prev = emu_timers;
        while ((t_prev->next != NULL) && (t_prev->next != t_del)) {
            t_prev = t_prev->next;
        }
        if (t_prev->next == t_del) {
            t_prev->next = t_del->next;
            vPortFree(t_del);
        }
    }

    vPortExitCritical();
    return 0;
}

static void emu_clear_all_timers(void)
{
    vPortEnterCritical();
    while (emu_timers) {
        emu48_emu_timer_t *next = emu_timers->next;
        vPortFree(emu_timers);
        emu_timers = next;
    }
    vPortExitCritical();
}

void __attribute__((target("arm"))) emu_timer_thread(void *_)
{
    emu48_emu_timer_t *t;
    for (;;) {
        if (emu_timers) {
            t = emu_timers;
            while (t != NULL) {
                emu48_emu_timer_t *next_t = t->next;
                if ((t->period_ms != 0) && (t->count < t->period_ms)) {
                    t->count += 10;
                    if (t->count >= t->period_ms) {
                        if (t->oneshot) {
                            t->cb((UINT)t, 0, t->userData, 0, 0);
                            emu_del_timer(t);
                            break;
                        } else {
                            t->count = 0;
                            t->cb((UINT)t, 0, t->userData, 0, 0);
                        }
                    }
                }
                t = next_t;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static bool need_Refresh = false;

void emu48_update_display(void)
{
    need_Refresh = true;
}

void __attribute__((target("arm"))) emu48_disp_update_task(void *_)
{
    if (full_screen_buf) {
        memset(full_screen_buf, 255, 256 * 128);
    }
    for (;;) {
        if (need_Refresh && pbyLcd && full_screen_buf) {
            for (uint16_t y = 0; y < SCREENHEIGHT; y++) {
                uint16_t sy = y * 2;
                if (sy >= 127) break;
                for (uint16_t x = 0; x < 131; x++) {
                    uint16_t sx = (x * 250) / 131;
                    uint8_t pix = pbyLcd[144 * y + x];
                    if (sx < 255) {
                        full_screen_buf[256 * sy + sx] = pix;
                        full_screen_buf[256 * (sy + 1) + sx] = pix;
                        full_screen_buf[256 * sy + (sx + 1)] = pix;
                        full_screen_buf[256 * (sy + 1) + (sx + 1)] = pix;
                    }
                }
            }
            ll_disp_put_area(full_screen_buf, 0, 0, 255, 126);
            need_Refresh = false;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

extern void *emu_rom_addr;

static BYTE pbyNULL[16];
static BYTE pbyCODE_packed[64];
static BYTE pbyCODE_unpacked[sizeof(pbyCODE_packed) * 2];

LPBYTE FASTPTR(DWORD d)
{
    LPBYTE lpbyPage;

    d &= 0xFFFFF;							// handle address overflows

    if ((Chipset.IOCfig) && ((d & 0xFFFC0) == Chipset.IOBase))
        return Chipset.IORam + d - Chipset.IOBase;

    if ((lpbyPage = RMap[d >> 12]) != NULL)	// page valid
    {
        lpbyPage += d & 0xFFF;				// full address

        if ((uint32_t)lpbyPage >= 0x70000000) {
            uint32_t acc_addr = ((uint32_t)lpbyPage - 0x70000000) / 2 + (uint32_t)emu_rom_addr;

            memcpy(pbyCODE_packed, (void *)acc_addr, sizeof(pbyCODE_packed));

            for (int i = 0, j = 0; i < (int)sizeof(pbyCODE_packed); i++) {
                pbyCODE_unpacked[j++] = pbyCODE_packed[i] & 0xF;
                pbyCODE_unpacked[j++] = pbyCODE_packed[i] >> 4;
            }
            return &pbyCODE_unpacked[(uint32_t)lpbyPage & 1];
        } else {
            return lpbyPage;
        }
    } else {
        lpbyPage = pbyNULL;					// memory allocation
        Npeek(lpbyPage, d, 13);				// fill with data (LA(8) = longest opcode)
    }
    return lpbyPage;
}

void emu48_main(int select)
{
    int ret = load_rom("/rom.39g");
    if (ret) {
        printf("Failed to open ROM (/rom.39g or /rom.g39): %d\n", ret);
        return;
    }

    pbyRom = (LPBYTE)0x70000000;
    dwRomSize = get_rom_size() * 2;

    cCurrentRomType = 'E';
    nCurrentClass = 39;

    pbyLcd = pvPortMalloc(144 * SCREENHEIGHT);
    full_screen_buf = pvPortMalloc(256 * 128);
    if (!pbyLcd || !full_screen_buf) {
        if (pbyLcd) {
            vPortFree(pbyLcd);
            pbyLcd = NULL;
        }
        if (full_screen_buf) {
            vPortFree(full_screen_buf);
            full_screen_buf = NULL;
        }
        unload_rom();
        return;
    }
    memset(pbyLcd, 255, 144 * SCREENHEIGHT);

    QueryPerformanceFrequency(&lFreq);
    QueryPerformanceCounter(&lAppStart);

    ZeroMemory(&Chipset, sizeof(Chipset));
    ZeroMemory(&RMap, sizeof(RMap));			// delete MMU mappings
    ZeroMemory(&WMap, sizeof(WMap));

    Chipset.type = cCurrentRomType;

    if (Chipset.type == 'E')				// HP39/40G
    {
        Chipset.Port0Size = 128;
        Chipset.Port1Size = 0;
        Chipset.Port2Size = 128;

        Chipset.cards_status = 0xF;

        bPort2Writeable = TRUE;				// port2 is writeable
    }
    Chipset.IORam[LPE] = RST;

    SetSpeed(bRealSpeed);
    SetLcdMode(bGrayscale);

    // allocate port memory and restore saved state if present
    if (Chipset.Port0Size) {
        Chipset.Port0 = HeapAlloc(hHeap, HEAP_ZERO_MEMORY, Chipset.Port0Size * 2048);
        _ASSERT(Chipset.Port0 != NULL);
        load_port_file("/port0.39g", Chipset.Port0, Chipset.Port0Size * 2048);
    }
    if (Chipset.Port1Size) {
        Chipset.Port1 = HeapAlloc(hHeap, HEAP_ZERO_MEMORY, Chipset.Port1Size * 2048);
        _ASSERT(Chipset.Port1 != NULL);
    }
    if (Chipset.Port2Size) {
        Chipset.Port2 = HeapAlloc(hHeap, HEAP_ZERO_MEMORY, Chipset.Port2Size * 2048);
        _ASSERT(Chipset.Port2 != NULL);
        load_port_file("/port2.39g", Chipset.Port2, Chipset.Port2Size * 2048);
    }

    RomSwitch(0);							// boot ROM view and map memory

    CpuReset();
    Chipset.Shutdn = FALSE;					// automatic restart

    nState     = SM_RUN;					// init state must be <> nNextState
    nNextState = SM_INVALID;				// go into invalid state

    xTaskCreate(emu_thread, "emu48", 4096, NULL, configMAX_PRIORITIES - 3, &hEmuWorkerTask);
    xTaskCreate(emu_timer_thread, "emu48_tmr", 1000, NULL, configMAX_PRIORITIES - 3, &hEmuTimerTask);
    xTaskCreate(emu48_disp_update_task, "emu48_disp", 1000, NULL, configMAX_PRIORITIES - 3, &hEmuDispTask);

    ll_cpu_slowdown_enable(false);

    printf("emu_thread Created\n");

    vTaskDelay(pdMS_TO_TICKS(500));
    SwitchToState(SM_RUN);

    int on_press = 0;
    uint32_t last_key = 0;
    uint32_t last_press = 0;

    for (;;) {
        uint32_t keys, key, kpress;

        do {
            keys = ll_vm_check_key();
            key = keys & 0xFFFF;
            kpress = keys >> 16;

            vTaskDelay(pdMS_TO_TICKS(30));

            if (on_press > 0) {
                on_press--;
                if (on_press == 1) {
                    KeyboardEvent(false, KEY_39G_ON.out, KEY_39G_ON.in);
                }
            }
        } while ((last_key == key) && (last_press == kpress));

        // Exit Emu48 when ON + F3 (or F3 while ON hold timer active) is pressed
        if (kpress && key == KEY_F3 && on_press > 0) {
            KeyboardEvent(false, KEY_39G_ON.out, KEY_39G_ON.in);
            break;
        }

        #define KEYEVENT(name)  KeyboardEvent(kpress, name.out, name.in)
        #define KEY_CASE(w, s)  case w: KEYEVENT(s); break

        switch (key) {
            KEY_CASE(KEY_F1, KEY_39G_F1);
            KEY_CASE(KEY_F2, KEY_39G_F2);
            KEY_CASE(KEY_F3, KEY_39G_F3);
            KEY_CASE(KEY_F4, KEY_39G_F4);
            KEY_CASE(KEY_F5, KEY_39G_F5);
            KEY_CASE(KEY_F6, KEY_39G_F6);

            KEY_CASE(KEY_SYMB , KEY_39G_SYMB );
            KEY_CASE(KEY_PLOT , KEY_39G_PLOT );
            KEY_CASE(KEY_NUM  , KEY_39G_NUM  );
            KEY_CASE(KEY_HOME , KEY_39G_HOME );
            KEY_CASE(KEY_APPS , KEY_39G_APPS );
            KEY_CASE(KEY_VIEWS, KEY_39G_VIEWS);

            KEY_CASE(KEY_VARS      , KEY_39G_VARS  );
            KEY_CASE(KEY_MATH      , KEY_39G_MATH  );
            KEY_CASE(KEY_ABC       , KEY_39G_ABC   );
            KEY_CASE(KEY_XTPHIN    , KEY_39G_XTPHIN);
            KEY_CASE(KEY_BACKSPACE , KEY_39G_CLEAR );

            KEY_CASE(KEY_SIN    , KEY_39G_SIN   );
            KEY_CASE(KEY_COS    , KEY_39G_COS   );
            KEY_CASE(KEY_TAN    , KEY_39G_TAN   );
            KEY_CASE(KEY_LN     , KEY_39G_LN    );
            KEY_CASE(KEY_LOG    , KEY_39G_LOG   );

            KEY_CASE(KEY_X2              , KEY_39G_X2             );
            KEY_CASE(KEY_XY              , KEY_39G_XY             );
            KEY_CASE(KEY_LEFTBRACKET     , KEY_39G_LEFTBRACKET    );
            KEY_CASE(KEY_RIGHTBRACKET    , KEY_39G_RIGHTBRACKET   );
            KEY_CASE(KEY_DIVISION        , KEY_39G_DIVISION       );

            KEY_CASE(KEY_COMMA         , KEY_39G_COMMA         );
            KEY_CASE(KEY_7             , KEY_39G_7             );
            KEY_CASE(KEY_8             , KEY_39G_8             );
            KEY_CASE(KEY_9             , KEY_39G_9             );
            KEY_CASE(KEY_MULTIPLICATION, KEY_39G_MULTIPLICATION);

            KEY_CASE(KEY_ALPHA         , KEY_39G_ALPHA         );
            KEY_CASE(KEY_4             , KEY_39G_4             );
            KEY_CASE(KEY_5             , KEY_39G_5             );
            KEY_CASE(KEY_6             , KEY_39G_6             );
            KEY_CASE(KEY_SUBTRACTION   , KEY_39G_SUBTRACTION   );

            KEY_CASE(KEY_SHIFT        , KEY_39G_SHIFT        );
            KEY_CASE(KEY_1            , KEY_39G_1            );
            KEY_CASE(KEY_2            , KEY_39G_2            );
            KEY_CASE(KEY_3            , KEY_39G_3            );
            KEY_CASE(KEY_PLUS         , KEY_39G_PLUS         );

            KEY_CASE(KEY_0            , KEY_39G_0            );
            KEY_CASE(KEY_DOT          , KEY_39G_DOT          );
            KEY_CASE(KEY_NEGATIVE     , KEY_39G_NEGATIVE     );
            KEY_CASE(KEY_ENTER        , KEY_39G_ENTER        );

            KEY_CASE(KEY_RIGHT    , KEY_39G_RIGHT   );
            KEY_CASE(KEY_DOWN     , KEY_39G_DOWN    );
            KEY_CASE(KEY_LEFT     , KEY_39G_LEFT    );
            KEY_CASE(KEY_UP       , KEY_39G_UP      );

            case KEY_ON:
                KeyboardEvent(true, KEY_39G_ON.out, KEY_39G_ON.in);
                on_press = 15;
                break;

            default:
                break;
        }

        last_key = key;
        last_press = kpress;
    }

    // Stop emulation and clean up resources
    SwitchToState(SM_INVALID);
    vTaskDelay(pdMS_TO_TICKS(100));

    if (hEmuDispTask) {
        vTaskDelete(hEmuDispTask);
        hEmuDispTask = NULL;
    }
    if (hEmuTimerTask) {
        vTaskDelete(hEmuTimerTask);
        hEmuTimerTask = NULL;
    }
    if (hEmuWorkerTask) {
        vTaskDelete(hEmuWorkerTask);
        hEmuWorkerTask = NULL;
    }
    emu_clear_all_timers();

    if (Chipset.Port0 && Chipset.Port0Size) {
        save_port_file("/port0.39g", Chipset.Port0, Chipset.Port0Size * 2048);
        HeapFree(hHeap, 0, Chipset.Port0);
        Chipset.Port0 = NULL;
    }
    if (Chipset.Port1 && Chipset.Port1Size) {
        HeapFree(hHeap, 0, Chipset.Port1);
        Chipset.Port1 = NULL;
    }
    if (Chipset.Port2 && Chipset.Port2Size) {
        save_port_file("/port2.39g", Chipset.Port2, Chipset.Port2Size * 2048);
        HeapFree(hHeap, 0, Chipset.Port2);
        Chipset.Port2 = NULL;
    }
    if (pbyLcd) {
        vPortFree(pbyLcd);
        pbyLcd = NULL;
    }
    if (full_screen_buf) {
        vPortFree(full_screen_buf);
        full_screen_buf = NULL;
    }
    unload_rom();
}

void __attribute__((target("arm"))) emu48_task(void *_)
{
    SystemUISuspend();
    emu48_main(0);
    SystemUIResume();
    vTaskDelete(NULL);
}

void StartEmu48(void)
{
    xTaskCreate(emu48_task, "Emu48", 2048, NULL, configMAX_PRIORITIES - 3, NULL);
}

void EnterCriticalSection(CRITICAL_SECTION *obj)
{
    vPortEnterCritical();
}

void LeaveCriticalSection(CRITICAL_SECTION *obj)
{
    vPortExitCritical();
}

void SetEvent(HANDLE h)
{
}

BOOL ResetEvent(HANDLE h)
{
    return true;
}

DWORD WaitForSingleObject(HANDLE hHandle, int dwMilliseconds)
{
    return WAIT_OBJECT_0;
}

BOOL QueryPerformanceCounter(PLARGE_INTEGER l)
{
    l->QuadPart = ll_get_time_us();
    return TRUE;
}

MMRESULT timeSetEvent(UINT uDelay, UINT uResolution, LPTIMECALLBACK fptc, DWORD_PTR dwUser, UINT fuEvent) 
{
    return (MMRESULT)emu_add_timer(uDelay, fptc, (uint32_t)dwUser, fuEvent == TIME_ONESHOT);
}

MMRESULT timeKillEvent(UINT uTimerID)
{
    emu_del_timer((emu48_emu_timer_t *)uTimerID);
    return 0;
}

VOID DisableDebugger(VOID)
{
}

VOID GetLocalTime(struct SYSTEMTIME *lpSystemTime) {
    if (lpSystemTime) {
        uint32_t sec = ll_rtc_get_sec();
        lpSystemTime->wYear = 2026;
        lpSystemTime->wMonth = 1;
        lpSystemTime->wDay = 1;
        lpSystemTime->wHour = (sec / 3600) % 24;
        lpSystemTime->wMinute = (sec / 60) % 60;
        lpSystemTime->wSecond = sec % 60;
        lpSystemTime->wMilliseconds = (ll_get_time_ms() % 1000);
    }
}

MMRESULT timeGetDevCaps(struct TIMECAPS *ptc, UINT cbtc) {
    if (ptc) {
        ptc->wPeriodMin = 1; // ms
        ptc->wPeriodMax = 1000000; // ms -> 1000s
    }
    return 0; // No error
}

MMRESULT timeBeginPeriod(UINT uPeriod) {
    return 0; // No error
}

MMRESULT timeEndPeriod(UINT uPeriod) {
    return 0; // No error
}

VOID UpdateWindowStatus(VOID)
{
}

DWORD timeGetTime(void)
{
    time_t t = ll_get_time_ms();
    return (DWORD)t;
}

BOOL QueryPerformanceFrequency(PLARGE_INTEGER l) {
    l->QuadPart = 1000000;
    return TRUE;
}

BOOL CheckBreakpoint(DWORD dwAddr, DWORD dwRange, UINT nType)
{
    return FALSE;
}

VOID NotifyDebugger(INT nType)				// update registers
{
    return;
}

VOID UpdatePatches(BOOL bPatch)
{
}

VOID UpdateDbgCycleCounter(VOID)
{
    return;
}

void OutputDebugString(char *s)
{
    printf("%s\n", s);
}
