// Improved Vehicle Mobile - plugin AML untuk GTA SA Android arm64.
// Fitur: sein kiri/kanan/hazard (pemain + NPC), tombol lampu depan on/off, tombol modern (glow).
// Semua hook memakai NAMA SIMBOL (tanpa offset hardcode untuk fungsi).
//
// v0.3: perbaikan sentuhan (tombol gas/kamera macet), sein NPC, pembacaan matrix lebih tahan banting,
//       dan ringkasan diagnosa di IMVEH.log.
#include "logger.h"
#include "config.h"
#include "amlmod.h"
#include <android/log.h>
#include <atomic>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "common.h"
#include "config.h"
#include "touch_filter.h"
#include "ui.h"
#include "log.h"
#include "symscan.h"
#include "coronasig.h"
#include "volumetric.h"
#include "emissive.h"

MYMODCFG(net.local.improvedvehiclemobile, Improved Vehicle Mobile, 0.4.0, YourName)

#define LOGI(...) ImLog(__VA_ARGS__)

typedef void (*AddLightFn_t)(unsigned char, Vec3, Vec3, float, float, float, float, unsigned char, bool, void*);

static void* g_gta = nullptr;
static void* (*FindPlayerVehicle)(int, bool) = nullptr;
static int   (*OS_ScreenGetWidth)() = nullptr;
static int   (*OS_ScreenGetHeight)() = nullptr;
static AddLightFn_t AddLight = nullptr;

// CCoronas::RegisterCorona - efek lampu yang benar-benar tampil. Alamat dicari saat OnModLoad (lihat resolveCorona).
typedef void (*RegisterCoronaFn_t)(uint32_t id, void* attachTo, uint8_t r, uint8_t g, uint8_t b, uint8_t a, const Vec3* pos,
                                   float radius, float farClip, uint8_t type, uint8_t flare, bool reflect, bool los, int unused,
                                   float angle, bool longDist, float nearClip, uint8_t fadeState, float fadeSpeed,
                                   bool onlyBelow, bool reflDelay);
static RegisterCoronaFn_t RegisterCorona = nullptr;
static bool g_coronaOk = false;
static int  g_stCoronas = 0;

static void (*PreRender_o)(void*);
static void (*PreRenderAuto_o)(void*);
static void (*PreRenderBike_o)(void*);
static void (*Render2d_o)();
static void (*Touch_o)(int, int, int, int);
static void (*HeadBeam_o)(void*, int, void*, unsigned char);
static unsigned char (*HeadEffect_o)(void*, int, void*, unsigned char, unsigned char);
static void (*HeadFloorTwin_o)(void*, void*);
static void (*HeadFloorSingle_o)(void*, void*, unsigned char);
static void  (*ConstructRenderList_o)();
static void  (*RenderScene_o)(bool);
static void  (*MobileRender_o)();
static void* (*ReadMaterial_o)(void*);
static void* (*CloneMaterial_o)(void*);
static int   (*DestroyMaterial_o)(void*);
static void  (*EditMaterials_o)(void*);
static void  (*MatProps_o)(const void*, unsigned);
static GetMatFn GetView = nullptr, GetProj = nullptr;

enum { SIG_OFF = 0, SIG_LEFT, SIG_RIGHT, SIG_HAZARD };
static std::atomic<int>     g_mode{SIG_OFF};
static std::atomic<bool>    g_lightsOff{false};
static std::atomic<bool>    g_visible{false};
static std::atomic<int64_t> g_lastPlayerVehMs{0};
static std::atomic<bool>    g_cornerOk{true};

// diagnosa (thread utama)
static int g_stPlayerFrames = 0, g_stNpcSeen = 0, g_stLights = 0, g_stMatrixFail = 0, g_stNpcBlink = 0;
static int g_matFailStreak = 0;
static Vec3 g_playerPos = {0, 0, 0};
static int  g_npcBudget = ivm::kNpcMaxLights;

static bool blinkOn(int64_t now, uint32_t phase = 0) { return ((now + phase) % 700) < 350; }

static bool finite3(const Vec3& v) { return isfinite(v.x) && isfinite(v.y) && isfinite(v.z); }
static float len3(const Vec3& v) { return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z); }

// Orientasi kendaraan. Nama sumbu mengikuti CMatrix SA: right = kanan, fwd = depan ("top"), up = atas ("at").
// 1) Pakai CMatrix lewat pointer m_matrix. 2) Bila m_matrix == null, pakai CSimpleTransform (posisi + heading).
static bool readOrientation(void* veh, Vec3& right, Vec3& fwd, Vec3& up, Vec3& pos) {
    uint8_t* base = (uint8_t*)veh;
    uintptr_t mp = 0;
    if (!SafeRead(base + ivm::kMatrixPtrOffset, &mp, sizeof mp)) return false;
    if (mp != 0) {
        if (mp < 0x10000 || (mp & 7)) return false;
        float f[15];
        if (!SafeRead((void*)mp, f, sizeof f)) return false;
        right = {f[0], f[1], f[2]}; fwd = {f[4], f[5], f[6]}; up = {f[8], f[9], f[10]}; pos = {f[12], f[13], f[14]};
        if (!finite3(right) || !finite3(fwd) || !finite3(up) || !finite3(pos)) return false;   // padding f[3],f[7],f[11] sengaja tidak dicek
        float lr = len3(right), lf = len3(fwd);
        return lr > 0.9f && lr < 1.1f && lf > 0.9f && lf < 1.1f;
    }
    float t[4];   // x, y, z, heading
    if (!SafeRead(base + ivm::kPlacementOffset, t, sizeof t)) return false;
    for (float v : t) if (!isfinite(v)) return false;
    if (fabsf(t[3]) > 20.f || fabsf(t[0]) > 20000.f || fabsf(t[1]) > 20000.f || fabsf(t[2]) > 20000.f) return false;
    right = {cosf(t[3]), sinf(t[3]), 0.f}; fwd = {-sinf(t[3]), cosf(t[3]), 0.f}; up = {0.f, 0.f, 1.f}; pos = {t[0], t[1], t[2]};
    return true;
}

static bool isPlayerVehicle(void* self) { return FindPlayerVehicle && self && self == FindPlayerVehicle(-1, false); }

// ---------------------------- sein NPC (heuristik) ----------------------------
// NPC tidak punya "tombol sein"; kita tebak dari laju putar (yaw) dan kecepatan, lalu berkedip
// selama berbelok + kNpcHoldMs sesudahnya.
struct NpcState { void* veh; int64_t lastMs; Vec3 lastPos; float lastYaw; float yawRate; int64_t holdUntil; int dir; };
static NpcState g_npc[48];

// return: 0 mati, -1 kiri, +1 kanan
static int npcDirection(void* veh, const Vec3& pos, const Vec3& fwd, int64_t now) {
    NpcState* s = nullptr; NpcState* oldest = &g_npc[0];
    for (NpcState& e : g_npc) { if (e.veh == veh) { s = &e; break; } if (e.lastMs < oldest->lastMs) oldest = &e; }
    float yaw = atan2f(fwd.y, fwd.x);
    if (!s || now - s->lastMs > 2000) {
        if (!s) s = oldest;
        *s = NpcState{}; s->veh = veh; s->lastMs = now; s->lastPos = pos; s->lastYaw = yaw;
        return 0;
    }
    float dt = (now - s->lastMs) * 0.001f;
    if (dt >= 0.004f) {                                           // abaikan pemanggilan ganda dalam 1 frame
        float dy = yaw - s->lastYaw;
        while (dy > 3.14159265f) dy -= 6.2831853f;
        while (dy < -3.14159265f) dy += 6.2831853f;
        float dx = pos.x - s->lastPos.x, dyy = pos.y - s->lastPos.y;
        float speed = sqrtf(dx * dx + dyy * dyy) / dt;
        if (dt > 0.25f) s->yawRate = 0.f; else s->yawRate += (dy / dt - s->yawRate) * 0.35f;   // haluskan
        s->lastMs = now; s->lastPos = pos; s->lastYaw = yaw;
        float thr = (now < s->holdUntil) ? ivm::kNpcYawOff : ivm::kNpcYawOn;   // histeresis
        if (speed > ivm::kNpcMinSpeed && fabsf(s->yawRate) > thr) { s->dir = s->yawRate > 0.f ? -1 : 1; s->holdUntil = now + ivm::kNpcHoldMs; }   // yaw naik = belok kiri
    }
    return now < s->holdUntil ? s->dir : 0;
}

// ---------------------------- lampu sein 3D ----------------------------
// Satu cahaya titik di tiap sudut sisi yang menyala (depan + belakang). mode: SIG_LEFT / SIG_RIGHT / SIG_HAZARD.
static void addSeinLights(void* veh, const Vec3& r, const Vec3& f, const Vec3& u, const Vec3& p, int mode, int* budget) {
    // ID corona unik per kendaraan + sudut, supaya engine memperbarui corona yang sama tiap frame (bukan menumpuk).
    uint32_t base = ivm::kCoronaIdBase + ((uint32_t)(((uintptr_t)veh >> 4) & 0x3FF) << 3);
    for (int side = -1; side <= 1; side += 2) {
        bool left = (side < 0) != ivm::kSwapSides;
        if (!(mode == SIG_HAZARD || (mode == SIG_LEFT && left) || (mode == SIG_RIGHT && !left))) continue;
        for (int end = -1; end <= 1; end += 2) {
            if (budget && *budget <= 0) return;
            float x = side * ivm::kHalfWidth, y = end * ivm::kHalfLength, z = ivm::kLampHeight;
            Vec3 w = {p.x + r.x * x + f.x * y + u.x * z, p.y + r.y * x + f.y * y + u.y * z, p.z + r.z * x + f.z * y + u.z * z};
            if (g_coronaOk && ivm::kUseCoronas) {
                uint32_t id = base + (uint32_t)((side > 0) * 2 + (end > 0));
                RegisterCorona(id, veh, ivm::kCoronaColor[0], ivm::kCoronaColor[1], ivm::kCoronaColor[2], 255, &w,
                               ivm::kCoronaSize, ivm::kCoronaFarClip, (uint8_t)ivm::kCoronaType, (uint8_t)ivm::kCoronaFlare,
                               false /*reflect*/, true /*los*/, 0, 0.f, false, ivm::kCoronaNearClip, 0, ivm::kCoronaFadeSpeed, false, false);
                ++g_stCoronas;
            }
            if (AddLight) AddLight(0 /*point light*/, w, Vec3{0, 0, 0}, ivm::kSeinRadius, ivm::kSeinColor[0], ivm::kSeinColor[1], ivm::kSeinColor[2], 0, false, nullptr);
            ++g_stLights;
            if (budget) --*budget;
        }
    }
}

// ---------------------------- hook kendaraan ----------------------------
static int  g_sigOut = EM_SIG_NONE;      // status sein kendaraan yang sedang diproses (untuk emissive)
static bool g_blinkOut = false;

static void OnVehiclePreRenderImpl(void* self) {
    if (!self) return;
    int64_t now = NowMs();
    bool player = isPlayerVehicle(self);
    if (player) {
        static bool seen = false;
        if (!seen) { seen = true; LOGI("Kendaraan pemain terdeteksi (PreRender berjalan)"); }
        g_lastPlayerVehMs = now; ++g_stPlayerFrames;
    } else if (!ivm::kNpcSignals) {
        return;
    }
    if (!g_cornerOk) return;
    int mode = player ? g_mode.load() : SIG_OFF;

    Vec3 r, f, u, p;
    bool ok = readOrientation(self, r, f, u, p);
    if (player) {
        if (ok) g_playerPos = p;                      // acuan jarak untuk NPC
        if (mode == SIG_OFF) return;
    }
    if (!ok) {
        ++g_stMatrixFail;
        ++g_matFailStreak;
        if (g_matFailStreak == 1) LOGI("Peringatan: orientasi kendaraan gagal dibaca (kMatrixPtrOffset=0x%zx)", ivm::kMatrixPtrOffset);
        if (g_matFailStreak >= 600) {                 // 600 kegagalan beruntun tanpa satu pun berhasil
            g_cornerOk = false;
            LOGI("Matrix kendaraan tidak valid (kMatrixPtrOffset=0x%zx salah?) - sein 3D dimatikan", ivm::kMatrixPtrOffset);
        }
        return;
    }
    g_matFailStreak = 0;
    if (player) {
        static bool logged = false;
        if (!logged) { logged = true; LOGI("Orientasi pemain ok: pos=(%.1f %.1f %.1f) fwd=(%.2f %.2f %.2f)", p.x, p.y, p.z, f.x, f.y, f.z); }
    }

    if (player) {
        if (blinkOn(now)) addSeinLights(self, r, f, u, p, mode, nullptr);
        return;
    }

    // ---- NPC ----
    ++g_stNpcSeen;
    int dir = npcDirection(self, p, f, now);
    if (dir == 0) return;
    bool nb = blinkOn(now, (uint32_t)(((uintptr_t)self >> 4) * 37u));       // tiap mobil beda fase kedip
    g_sigOut = dir < 0 ? EM_SIG_LEFT : EM_SIG_RIGHT; g_blinkOut = nb;
    if (!nb) return;
    if (now - g_lastPlayerVehMs < 500) {                                    // jarak ke pemain (bila ada acuan)
        float dx = p.x - g_playerPos.x, dy = p.y - g_playerPos.y, dz = p.z - g_playerPos.z;
        if (dx * dx + dy * dy + dz * dz > ivm::kNpcLightRange * ivm::kNpcLightRange) return;
    }
    ++g_stNpcBlink;
    addSeinLights(self, r, f, u, p, dir < 0 ? SIG_LEFT : SIG_RIGHT, &g_npcBudget);
}

// Pembungkus: jalankan logika sein, lalu perbarui status emissive (sein + lampu depan) untuk kendaraan ini.
static void OnVehiclePreRender(void* self) {
    g_sigOut = EM_SIG_NONE; g_blinkOut = false;
    if (self && isPlayerVehicle(self)) {
        int m = g_mode.load();
        g_sigOut = m == SIG_LEFT ? EM_SIG_LEFT : m == SIG_RIGHT ? EM_SIG_RIGHT : m == SIG_HAZARD ? EM_SIG_HAZARD : EM_SIG_NONE;
        g_blinkOut = blinkOn(NowMs());
    }
    OnVehiclePreRenderImpl(self);
    if (self) EmisVehicleState(self, g_sigOut, g_blinkOut);
}

static void PreRender_h(void* self)     { PreRender_o(self);     OnVehiclePreRender(self); }
static void PreRenderAuto_h(void* self) { PreRenderAuto_o(self); OnVehiclePreRender(self); }
static void PreRenderBike_h(void* self) { PreRenderBike_o(self); OnVehiclePreRender(self); }

// Catat sumber sinar dari matriks yang diberikan game ke DoHeadLightBeam. Bila matriks itu = matriks kendaraan (posisi ~ pusat mobil),
// asal sinar digeser ke posisi lampu depan; bila sudah berupa matriks lampu, dipakai apa adanya.
static void noteBeam(void* veh, int idx, const void* cm) {
    EmisNoteHeadlight(veh);
    float f[15];
    if (!cm || !SafeRead(cm, f, sizeof f)) return;
    for (float v : f) if (!isfinite(v)) return;
    Vec3 r = {f[0], f[1], f[2]}, fw = {f[4], f[5], f[6]}, up = {f[8], f[9], f[10]}, pos = {f[12], f[13], f[14]};
    float lf = len3(fw);
    if (lf < 0.9f || lf > 1.1f) return;
    fw = {fw.x / lf, fw.y / lf, fw.z / lf};
    Vec3 vr, vf, vu, vp;
    if (readOrientation(veh, vr, vf, vu, vp)) {
        float dx = pos.x - vp.x, dy = pos.y - vp.y, dz = pos.z - vp.z;
        if (dx * dx + dy * dy + dz * dz < 0.25f) {                       // matriks kendaraan -> geser ke lampu
            float side = (idx == 0 ? -1.f : 1.f) * ivm::kHeadHalfWidth;
            pos = {pos.x + r.x * side + fw.x * ivm::kHeadForward + up.x * ivm::kHeadHeight,
                   pos.y + r.y * side + fw.y * ivm::kHeadForward + up.y * ivm::kHeadHeight,
                   pos.z + r.z * side + fw.z * ivm::kHeadForward + up.z * ivm::kHeadHeight};
        }
    }
    VolAddBeam(pos, fw);
}

static void HeadBeam_h(void* s, int i, void* m, unsigned char b) {
    if (g_lightsOff && isPlayerVehicle(s)) return;
    HeadBeam_o(s, i, m, b);
    noteBeam(s, i, m);
}

// ---- hook frame / render / material (v0.4) ----
static void ConstructRenderList_h() { ConstructRenderList_o(); VolFrameBegin(); EmisFrame(); }
static void RenderScene_h(bool a)   { RenderScene_o(a); VolCacheMatrices(GetView, GetProj); }
static void MobileRender_h()        { MobileRender_o(); VolOnPostEffects(GetView, GetProj); }
static void* ReadMaterial_h(void* stream) { void* m = ReadMaterial_o(stream); if (m) EmisMaterialRead(m); return m; }
static void* CloneMaterial_h(void* src)   { void* m = CloneMaterial_o(src); if (m) EmisMaterialClone(src, m); return m; }
static int   DestroyMaterial_h(void* m)   { EmisMaterialDestroy(m); return DestroyMaterial_o(m); }
static void  EditMaterials_h(void* clump) { EmisSetCurrentClump(clump); EditMaterials_o(clump); }
static void  MatProps_h(const void* m, unsigned f) { MatProps_o(m, f); EmisAfterMaterialProps(m); }
static unsigned char HeadEffect_h(void* s, int i, void* m, unsigned char a, unsigned char b) { if (g_lightsOff && isPlayerVehicle(s)) return 0; return HeadEffect_o(s, i, m, a, b); }
static void HeadFloorTwin_h(void* s, void* m) { if (g_lightsOff && isPlayerVehicle(s)) return; HeadFloorTwin_o(s, m); }
static void HeadFloorSingle_h(void* s, void* m, unsigned char b) { if (g_lightsOff && isPlayerVehicle(s)) return; HeadFloorSingle_o(s, m, b); }

// ---------------------------- render tombol (thread utama, di Render2dStuff) ----------------------------
static bool g_uiOk = false;
static void Render2d_h() {
    Render2d_o();
    int64_t now = NowMs();
    bool vis = now - g_lastPlayerVehMs < 500;
    g_visible = UiShown();
    g_npcBudget = ivm::kNpcMaxLights;                 // frame berikutnya mulai dengan jatah penuh
    static int logged = 0;
    if (logged < 3) { ++logged; LOGI("Render2dStuff berjalan: uiOk=%d vis=%d", (int)g_uiOk, (int)vis); }

    // ringkasan diagnosa tiap 10 detik (maks. 12 baris) -> IMVEH.log
    static int64_t nextStat = 0; static int statLines = 0;
    if (statLines < 12 && now >= nextStat) {
        if (nextStat != 0) {
            ++statLines;
            LOGI("Ringkas: frameMobilPemain=%d npcDilihat=%d npcBerkedip=%d cahayaDitambah=%d gagalBacaMatrix=%d mode=%d AddLight=%s corona=%d(%s)",
                 g_stPlayerFrames, g_stNpcSeen, g_stNpcBlink, g_stLights, g_stMatrixFail, g_mode.load(), AddLight ? "ada" : "TIDAK ADA",
                 g_stCoronas, g_coronaOk ? "aktif" : "TIDAK AKTIF");
            VolLogStats();
            EmisLogStats();
        }
        nextStat = now + 10000;
    }

    if (!g_uiOk || !OS_ScreenGetWidth || !OS_ScreenGetHeight) return;
    int m = g_mode;
    bool act[BTN_COUNT] = {m == SIG_LEFT, m == SIG_HAZARD, m == SIG_RIGHT, !g_lightsOff};
    UiDraw(OS_ScreenGetWidth(), OS_ScreenGetHeight(), act, vis);
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

static TouchFilter g_touch;

static void Touch_h(int type, int id, int x, int y) {
    static int logged = 0;
    if (logged < 20) { ++logged; LOGI("Touch type=%d id=%d x=%d y=%d", type, id, x, y); }
    bool swallow = false;
    if (OS_ScreenGetWidth && OS_ScreenGetHeight) {
        swallow = g_touch.OnEvent(type, id, x, y, g_visible.load(),
            [](int px, int py) { return UiHitTest(px, py, OS_ScreenGetWidth(), OS_ScreenGetHeight()); },
            [](int b) { UiSetPressed(b, true); onButton(b); },
            [](int b) { UiSetPressed(b, false); });
    }
    if (!swallow) Touch_o(type, id, x, y);
}

// ---------------------------- pemasangan hook ----------------------------
template <class F> static bool hookSym(const char* sym, F ours, F& orig) {
    void* addr = reinterpret_cast<void*>(aml->GetSym(g_gta, sym));
    if (!addr) { LOGI("Hook %s: simbol TIDAK ditemukan", sym); return false; }
    aml->Hook(addr, reinterpret_cast<void*>(ours), reinterpret_cast<void**>(&orig));
    LOGI("Hook %s: ok", sym);
    return true;
}

// Cari CCoronas::RegisterCorona. Coba nama yang diharapkan; bila tidak ada, pindai tabel simbol libGTASA.so
// dan hanya pakai kandidat yang signature-nya cocok (lihat coronasig.h). Semua kandidat dicatat di IMVEH.log.
static void resolveCorona() {
    static const char* kExpected = "_ZN8CCoronas14RegisterCoronaEjP7CEntityhhhhRK7CVectorffhhbbifbfhfbb";
    static char cand[12][256];
    int n = FindSymbolsByPrefix("libGTASA.so", "_ZN8CCoronas", cand, 12);
    LOGI("Simbol CCoronas ditemukan: %d", n);
    for (int i = 0; i < n; ++i) LOGI("  CCoronas[%d]: %s", i, cand[i]);

    void* addr = reinterpret_cast<void*>(aml->GetSym(g_gta, kExpected));
    const char* used = addr ? kExpected : nullptr;
    for (int i = 0; i < n && !addr; ++i) {
        if (!CoronaSigMatches(cand[i])) continue;
        addr = reinterpret_cast<void*>(aml->GetSym(g_gta, cand[i]));
        if (addr) used = cand[i];
    }
    if (addr) {
        RegisterCorona = (RegisterCoronaFn_t)addr;
        g_coronaOk = true;
        LOGI("RegisterCorona: ok (%s) -> %p", used, addr);
    } else {
        LOGI("RegisterCorona: TIDAK ditemukan / signature tidak cocok. Sein hanya cahaya titik (hampir tak terlihat). "
             "Kirim baris 'CCoronas[...]' di atas agar signature bisa disesuaikan.");
    }
}

extern "C" __attribute__((visibility("default"))) void OnModLoad() {
    g_gta = aml->GetLibHandle("libGTASA.so");
    if (!g_gta) { LOGI("libGTASA.so tidak ditemukan"); return; }

    FindPlayerVehicle  = (void* (*)(int, bool))aml->GetSym(g_gta, "_Z17FindPlayerVehicleib");
    OS_ScreenGetWidth  = (int (*)())aml->GetSym(g_gta, "_Z17OS_ScreenGetWidthv");
    OS_ScreenGetHeight = (int (*)())aml->GetSym(g_gta, "_Z18OS_ScreenGetHeightv");
    AddLight = (AddLightFn_t)aml->GetSym(g_gta, "_ZN12CPointLights8AddLightEh7CVectorS0_ffffhbP7CEntity");
    resolveCorona();
    GetView = (GetMatFn)aml->GetSym(g_gta, "_Z20GetCurrentViewMatrixv");
    GetProj = (GetMatFn)aml->GetSym(g_gta, "_Z26GetCurrentProjectionMatrixv");
    LOGI("GetCurrentViewMatrix=%p GetCurrentProjectionMatrix=%p", (void*)GetView, (void*)GetProj);
    EmisInit((EmuMaterialfvFn)aml->GetSym(g_gta, "_Z16emu_glMaterialfvjjPKf"));
    LOGI("FindPlayerVehicle=%p ScreenW=%p ScreenH=%p AddLight=%p", (void*)FindPlayerVehicle, (void*)OS_ScreenGetWidth, (void*)OS_ScreenGetHeight, (void*)AddLight);

    // Sein 3D butuh PreRender. Bila CVehicle::PreRender tidak ada, coba subkelas (mobil & motor).
    if (!hookSym("_ZN8CVehicle9PreRenderEv", PreRender_h, PreRender_o)) {
        hookSym("_ZN11CAutomobile9PreRenderEv", PreRenderAuto_h, PreRenderAuto_o);
        hookSym("_ZN5CBike9PreRenderEv", PreRenderBike_h, PreRenderBike_o);
    }
    hookSym("_Z13Render2dStuffv", Render2d_h, Render2d_o);
    {
        UiGfx gfx{};
        gfx.nearScreenZ     = (float (*)())aml->GetSym(g_gta, "_Z20RwIm2DGetNearScreenZv");
        gfx.rsSet           = (int (*)(int, void*))aml->GetSym(g_gta, "_Z16RwRenderStateSet13RwRenderStatePv");
        gfx.rsGet           = (int (*)(int, void*))aml->GetSym(g_gta, "_Z16RwRenderStateGet13RwRenderStatePv");
        gfx.renderPrimitive = (int (*)(int, void*, int))aml->GetSym(g_gta, "_Z21RwIm2DRenderPrimitive15RwPrimitiveTypeP14RwOpenGLVertexi");
        g_uiOk = UiInit(gfx);
    }
    hookSym("_Z14AND_TouchEventiiii", Touch_h, Touch_o);
    hookSym("_ZN8CVehicle15DoHeadLightBeamEiR7CMatrixh", HeadBeam_h, HeadBeam_o);
    hookSym("_ZN9CRenderer19ConstructRenderListEv", ConstructRenderList_h, ConstructRenderList_o);
    hookSym("_Z11RenderSceneb", RenderScene_h, RenderScene_o);
    hookSym("_ZN12CPostEffects12MobileRenderEv", MobileRender_h, MobileRender_o);
    hookSym("_Z20RpMaterialStreamReadP8RwStream", ReadMaterial_h, ReadMaterial_o);
    hookSym("_Z15RpMaterialCloneP10RpMaterial", CloneMaterial_h, CloneMaterial_o);
    hookSym("_Z17RpMaterialDestroyP10RpMaterial", DestroyMaterial_h, DestroyMaterial_o);
    hookSym("_ZN17CVehicleModelInfo20SetEditableMaterialsEP7RpClump", EditMaterials_h, EditMaterials_o);
    hookSym("_Z36_rwOpenGLLightsSetMaterialPropertiesPK10RpMaterialj", MatProps_h, MatProps_o);
    hookSym("_ZN8CVehicle17DoHeadLightEffectEiR7CMatrixhh", HeadEffect_h, HeadEffect_o);
    hookSym("_ZN8CVehicle25DoHeadLightReflectionTwinER7CMatrix", HeadFloorTwin_h, HeadFloorTwin_o);
    hookSym("_ZN8CVehicle27DoHeadLightReflectionSingleER7CMatrixh", HeadFloorSingle_h, HeadFloorSingle_o);
    LOGI("Improved Vehicle Mobile 0.4.0 dimuat");
}
