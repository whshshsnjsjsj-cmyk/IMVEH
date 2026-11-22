// Improved Vehicle Mobile - plugin AML untuk GTA SA Android arm64.
// Fitur v0.1: sein kiri/kanan/hazard (lampu sein berkedip), tombol lampu depan on/off,
// tombol modern bergaya rounded + glow. Semua hook memakai NAMA SIMBOL (tanpa offset hardcode).
#include <mod/amlmod.h>
#include <android/log.h>
#include <EGL/egl.h>
#include <atomic>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include "ui.h"

MYMODCFG(net.local.improvedvehiclemobile, Improved Vehicle Mobile, 0.1.0, YourName)

#define TAG "ImprovedVehicleMobile"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)

// ====================== PERLU DIVERIFIKASI DI GAME ANDA ======================
// 1) Kode event sentuh pada AND_TouchEvent(type, id, x, y). Nilai asli dicatat di logcat
//    (20 event pertama) supaya bisa dicocokkan.
static const int kTouchDown = 1, kTouchUp = 3;
// 2) Offset pointer CMatrix di dalam objek CVehicle (CPlaceable::m_matrix) pada build arm64.
//    Pembacaan memakai safeRead, jadi nilai salah tidak membuat crash, hanya lampu sein 3D mati.
static const size_t kMatrixPtrOffset = 0x18;
// 3) Perkiraan ukuran mobil untuk posisi lampu sein (meter). Belum membaca bounding box model.
static const float kHalfWidth = 0.90f, kHalfLength = 2.20f, kLampHeight = 0.30f;
// =============================================================================

struct Vec3 { float x, y, z; };

static void* g_gta = nullptr;
static void* (*FindPlayerVehicle)(int, bool) = nullptr;
static int   (*OS_ScreenGetWidth)() = nullptr;
static int   (*OS_ScreenGetHeight)() = nullptr;
static void  (*AddLight)(unsigned char, Vec3, Vec3, float, float, float, float, unsigned char, bool, void*) = nullptr;

static void (*PreRender_o)(void*);
static void (*MobileRender_o)();
static void (*Touch_o)(int, int, int, int);
static void (*HeadBeam_o)(void*, int, void*, unsigned char);
static unsigned char (*HeadEffect_o)(void*, int, void*, unsigned char, unsigned char);
static void (*HeadFloorTwin_o)(void*, void*);
static void (*HeadFloorSingle_o)(void*, void*, unsigned char);

enum { SIG_OFF = 0, SIG_LEFT, SIG_RIGHT, SIG_HAZARD };
static std::atomic<int>     g_mode{SIG_OFF};
static std::atomic<bool>    g_lightsOff{false};
static std::atomic<bool>    g_visible{false};
static std::atomic<int64_t> g_lastPlayerVehMs{0};
static std::atomic<bool>    g_cornerOk{true};

static int64_t nowMs() { timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000; }
static bool blinkOn() { return (nowMs() % 700) < 350; }

// Baca memori dengan aman (tidak crash bila pointer salah).
static int g_pipe[2] = {-1, -1};
static bool safeRead(const void* src, void* dst, size_t n) {
    if (g_pipe[0] < 0 && pipe2(g_pipe, O_NONBLOCK) != 0) return false;
    ssize_t w = write(g_pipe[1], src, n);
    if (w != (ssize_t)n) { if (w > 0) { char tmp[128]; read(g_pipe[0], tmp, (size_t)w); } return false; }
    return read(g_pipe[0], dst, n) == (ssize_t)n;
}

static bool readMatrix(void* veh, Vec3& right, Vec3& up, Vec3& at, Vec3& pos) {
    uintptr_t mp = 0;
    if (!safeRead((uint8_t*)veh + kMatrixPtrOffset, &mp, sizeof mp) || mp < 0x10000 || (mp & 3)) return false;
    float f[15];
    if (!safeRead((void*)mp, f, sizeof f)) return false;
    for (float v : f) if (!isfinite(v)) return false;
    right = {f[0], f[1], f[2]}; up = {f[4], f[5], f[6]}; at = {f[8], f[9], f[10]}; pos = {f[12], f[13], f[14]};
    float lr = sqrtf(right.x * right.x + right.y * right.y + right.z * right.z);
    float lu = sqrtf(up.x * up.x + up.y * up.y + up.z * up.z);
    return lr > 0.9f && lr < 1.1f && lu > 0.9f && lu < 1.1f;
}

static bool isPlayerVehicle(void* self) { return FindPlayerVehicle && self && self == FindPlayerVehicle(-1, false); }

// ---------------------------- hook kendaraan ----------------------------
static void PreRender_h(void* self) {
    PreRender_o(self);
    if (!isPlayerVehicle(self)) return;
    g_lastPlayerVehMs = nowMs();

    int mode = g_mode;
    if (mode == SIG_OFF || !blinkOn() || !AddLight || !g_cornerOk) return;
    Vec3 r, u, a, p;
    if (!readMatrix(self, r, u, a, p)) {
        g_cornerOk = false;
        LOGI("Matrix kendaraan tidak valid (kMatrixPtrOffset=0x%zx salah?) - sein 3D dimatikan", kMatrixPtrOffset);
        return;
    }
    for (int side = -1; side <= 1; side += 2) {
        bool left = side < 0;
        if (!(mode == SIG_HAZARD || (mode == SIG_LEFT && left) || (mode == SIG_RIGHT && !left))) continue;
        for (int end = -1; end <= 1; end += 2) {
            float x = side * kHalfWidth, y = end * kHalfLength, z = kLampHeight;
            Vec3 w = {p.x + r.x * x + u.x * y + a.x * z, p.y + r.y * x + u.y * y + a.y * z, p.z + r.z * x + u.z * y + a.z * z};
            AddLight(0 /*point light*/, w, Vec3{0, 0, 0}, 2.5f, 1.0f, 0.45f, 0.0f, 0, false, nullptr);
        }
    }
}

static void HeadBeam_h(void* s, int i, void* m, unsigned char b) { if (g_lightsOff && isPlayerVehicle(s)) return; HeadBeam_o(s, i, m, b); }
static unsigned char HeadEffect_h(void* s, int i, void* m, unsigned char a, unsigned char b) { if (g_lightsOff && isPlayerVehicle(s)) return 0; return HeadEffect_o(s, i, m, a, b); }
static void HeadFloorTwin_h(void* s, void* m) { if (g_lightsOff && isPlayerVehicle(s)) return; HeadFloorTwin_o(s, m); }
static void HeadFloorSingle_h(void* s, void* m, unsigned char b) { if (g_lightsOff && isPlayerVehicle(s)) return; HeadFloorSingle_o(s, m, b); }

// ---------------------------- render tombol (thread render) ----------------------------
static void MobileRender_h() {
    MobileRender_o();
    if (!eglGetCurrentContext()) return;
    bool vis = nowMs() - g_lastPlayerVehMs < 500;
    g_visible = vis;
    if (!vis || !OS_ScreenGetWidth || !OS_ScreenGetHeight) return;
    int m = g_mode;
    bool act[BTN_COUNT] = {m == SIG_LEFT, m == SIG_HAZARD, m == SIG_RIGHT, !g_lightsOff};
    UiDraw(OS_ScreenGetWidth(), OS_ScreenGetHeight(), act, blinkOn());
}

// ---------------------------- sentuhan (thread UI) ----------------------------
static void onButton(int b) {
    int m = g_mode;
    switch (b) {
        case BTN_LEFT:   g_mode = (m == SIG_LEFT) ? SIG_OFF : SIG_LEFT; break;
        case BTN_RIGHT:  g_mode = (m == SIG_RIGHT) ? SIG_OFF : SIG_RIGHT; break;
        case BTN_HAZARD: g_mode = (m == SIG_HAZARD) ? SIG_OFF : SIG_HAZARD; break;
        case BTN_LIGHTS: g_lightsOff = !g_lightsOff; break;
    }
}

static void Touch_h(int type, int id, int x, int y) {
    static int captured[16]; static bool init = false; static int logged = 0;
    if (!init) { for (int& c : captured) c = -1; init = true; }
    if (logged < 20) { ++logged; LOGI("Touch type=%d id=%d x=%d y=%d", type, id, x, y); }

    if (g_visible && OS_ScreenGetWidth && id >= 0 && id < 16) {
        if (type == kTouchDown) {
            int b = UiHitTest(x, y, OS_ScreenGetWidth(), OS_ScreenGetHeight());
            if (b >= 0) { captured[id] = b; UiSetPressed(b, true); onButton(b); return; }
        } else if (captured[id] >= 0) {
            if (type == kTouchUp) { UiSetPressed(captured[id], false); captured[id] = -1; }
            return;   // sentuhan yang diawali di tombol tidak diteruskan ke game
        }
    }
    Touch_o(type, id, x, y);
}

// ---------------------------- pemasangan hook ----------------------------
template <class F> static bool hookSym(const char* sym, F ours, F& orig) {
    void* addr = aml->GetSym(g_gta, sym);
    if (!addr) { LOGI("Hook %s: simbol TIDAK ditemukan", sym); return false; }
    aml->Hook(addr, reinterpret_cast<void*>(ours), reinterpret_cast<void**>(&orig));
    LOGI("Hook %s: ok", sym);
    return true;
}

extern "C" void OnModLoad() {
    g_gta = aml->GetLibHandle("libGTASA.so");
    if (!g_gta) { LOGI("libGTASA.so tidak ditemukan"); return; }

    FindPlayerVehicle  = (void* (*)(int, bool))aml->GetSym(g_gta, "_Z17FindPlayerVehicleib");
    OS_ScreenGetWidth  = (int (*)())aml->GetSym(g_gta, "_Z17OS_ScreenGetWidthv");
    OS_ScreenGetHeight = (int (*)())aml->GetSym(g_gta, "_Z18OS_ScreenGetHeightv");
    AddLight = (void (*)(unsigned char, Vec3, Vec3, float, float, float, float, unsigned char, bool, void*))
               aml->GetSym(g_gta, "_ZN12CPointLights8AddLightEh7CVectorS0_ffffhbP7CEntity");
    LOGI("FindPlayerVehicle=%p ScreenW=%p ScreenH=%p AddLight=%p", FindPlayerVehicle, OS_ScreenGetWidth, OS_ScreenGetHeight, AddLight);

    hookSym("_ZN8CVehicle9PreRenderEv", PreRender_h, PreRender_o);
    hookSym("_ZN12CPostEffects12MobileRenderEv", MobileRender_h, MobileRender_o);
    hookSym("_Z14AND_TouchEventiiii", Touch_h, Touch_o);
    hookSym("_ZN8CVehicle15DoHeadLightBeamEiR7CMatrixh", HeadBeam_h, HeadBeam_o);
    hookSym("_ZN8CVehicle17DoHeadLightEffectEiR7CMatrixhh", HeadEffect_h, HeadEffect_o);
    hookSym("_ZN8CVehicle25DoHeadLightReflectionTwinER7CMatrix", HeadFloorTwin_h, HeadFloorTwin_o);
    hookSym("_ZN8CVehicle27DoHeadLightReflectionSingleER7CMatrixh", HeadFloorSingle_h, HeadFloorSingle_o);
    LOGI("Improved Vehicle Mobile 0.1.0 dimuat");
}
