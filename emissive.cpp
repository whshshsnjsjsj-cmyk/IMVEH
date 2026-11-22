#include "emissive.h"
#include "config.h"
#include "log.h"
#include <atomic>
#include <mutex>
#include <string.h>
#include <unordered_map>

namespace {
struct VState {
    void* veh = nullptr; void* clump = nullptr;
    int64_t lastMs = 0, lastBeamMs = 0;
    uint8_t mask = 0;            // bit slot yang menyala (0=depan-kiri 1=depan-kanan 2=belakang-kiri 3=belakang-kanan)
    float col[4][3] = {};
};
constexpr int kMaxStates = 96;
VState g_st[kMaxStates];
std::mutex g_mu;
std::unordered_map<const void*, uint8_t> g_tags;
std::atomic<void*> g_curClump{nullptr};
EmuMaterialfvFn g_emu = nullptr;
std::atomic<bool> g_dirty{false};
std::atomic<int> g_stTagged{0}, g_stApplied{0}, g_stMiss{0}, g_stClumpBad{0}, g_stDraws{0};

VState* Find(void* veh, bool create) {
    VState* oldest = &g_st[0];
    for (VState& s : g_st) {
        if (s.veh == veh) return &s;
        if (!s.veh) { oldest = &s; break; }
        if (s.lastMs < oldest->lastMs) oldest = &s;
    }
    if (!create) return nullptr;
    *oldest = VState{}; oldest->veh = veh;
    return oldest;
}
}  // namespace

void EmisInit(EmuMaterialfvFn fn) {
    g_emu = fn;
    ImLog("Emissive: emu_glMaterialfv=%p %s", (void*)fn, fn ? "ok" : "TIDAK ADA - emissive dimatikan");
}

void EmisNoteHeadlight(void* veh) {
    if (!ivm::kUseEmissive) return;
    std::lock_guard<std::mutex> lk(g_mu);
    VState* s = Find(veh, true);
    s->lastBeamMs = NowMs(); s->lastMs = s->lastBeamMs;
}

void EmisSetCurrentClump(void* clump) { g_curClump.store(clump); }

void EmisVehicleState(void* veh, int sig, bool blink) {
    if (!ivm::kUseEmissive || !g_emu || !veh) return;
    int64_t now = NowMs();

    // RpClump milik kendaraan (CEntity::m_pRwObject); RpClump->object.type harus 2 (rpCLUMP).
    void* clump = nullptr; uint8_t type = 0;
    bool clumpOk = SafeRead((uint8_t*)veh + ivm::kClumpPtrOffset, &clump, sizeof clump) && clump && ((uintptr_t)clump & 7) == 0 &&
                   SafeRead(clump, &type, 1) && type == 2;
    if (!clumpOk) { if (++g_stClumpBad == 1) ImLog("Emissive: pointer clump kendaraan tidak valid (kClumpPtrOffset=0x%zx salah?)", ivm::kClumpPtrOffset); return; }

    std::lock_guard<std::mutex> lk(g_mu);
    VState* s = Find(veh, true);
    s->lastMs = now; s->clump = clump; s->mask = 0;

    auto set = [&](int slot, const float* c) { s->mask |= (uint8_t)(1u << slot); s->col[slot][0] = c[0]; s->col[slot][1] = c[1]; s->col[slot][2] = c[2]; };
    if (now - s->lastBeamMs < 150) { set(0, ivm::kEmisHead); set(1, ivm::kEmisHead); }   // lampu depan menyala

    if (sig != EM_SIG_NONE && blink) {
        bool left = sig == EM_SIG_LEFT || sig == EM_SIG_HAZARD, right = sig == EM_SIG_RIGHT || sig == EM_SIG_HAZARD;
        if (ivm::kSwapSides) { bool t = left; left = right; right = t; }
        if (left)  { set(0, ivm::kEmisAmber); set(2, ivm::kEmisAmber); }
        if (right) { set(1, ivm::kEmisAmber); set(3, ivm::kEmisAmber); }
    }
}

void EmisFrame() {
    int64_t now = NowMs();
    std::lock_guard<std::mutex> lk(g_mu);
    for (VState& s : g_st) if (s.veh && now - s.lastMs > 1500) s = VState{};
}

// ---------------- material ----------------
void EmisMaterialRead(void* mat) {
    if (!ivm::kUseEmissive || !mat) return;
    uint8_t c[4];
    if (!SafeRead((uint8_t*)mat + ivm::kRpMaterialColorOffset, c, 4)) return;
    for (int slot = 0; slot < 4; ++slot) {
        if (c[0] == ivm::kLampColors[slot][0] && c[1] == ivm::kLampColors[slot][1] && c[2] == ivm::kLampColors[slot][2]) {
            std::lock_guard<std::mutex> lk(g_mu);
            g_tags[mat] = (uint8_t)slot;
            ++g_stTagged;
            return;
        }
    }
}

void EmisMaterialClone(const void* src, void* dst) {
    if (!ivm::kUseEmissive || !src || !dst) return;
    std::lock_guard<std::mutex> lk(g_mu);
    auto it = g_tags.find(src);
    if (it != g_tags.end()) g_tags[dst] = it->second;
}

void EmisMaterialDestroy(void* mat) {
    if (!mat) return;
    int16_t ref = 2;
    if (!SafeRead((uint8_t*)mat + ivm::kRpMaterialRefOffset, &ref, sizeof ref)) return;
    if (ref > 1) return;                       // masih dipakai pihak lain, tag dipertahankan
    std::lock_guard<std::mutex> lk(g_mu);
    g_tags.erase(mat);
}

void EmisAfterMaterialProps(const void* mat) {
    if (!ivm::kUseEmissive || !g_emu) return;
    float rgba[4] = {0, 0, 0, 1};
    bool apply = false;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        auto it = g_tags.find(mat);
        if (it != g_tags.end()) {
            ++g_stDraws;
            void* clump = g_curClump.load();
            VState* s = nullptr;
            if (clump) for (VState& e : g_st) if (e.veh && e.clump == clump) { s = &e; break; }
            if (!s) ++g_stMiss;
            else if (s->mask & (1u << it->second)) { memcpy(rgba, s->col[it->second], sizeof(float) * 3); apply = true; }
        }
    }
    if (apply) { g_emu(0x0408 /*GL_FRONT_AND_BACK*/, 0x1600 /*GL_EMISSION*/, rgba); g_dirty = true; ++g_stApplied; }
    else if (g_dirty.exchange(false)) { g_emu(0x0408, 0x1600, rgba); }      // pulihkan ke 0 agar tidak bocor ke material lain
}

void EmisLogStats() {
    size_t tags; { std::lock_guard<std::mutex> lk(g_mu); tags = g_tags.size(); }
    ImLog("Emissive: materialDitandai=%d tagAktif=%zu gambarBerTag=%d diterapkan=%d clumpTakCocok=%d clumpTakValid=%d",
          g_stTagged.load(), tags, g_stDraws.load(), g_stApplied.load(), g_stMiss.load(), g_stClumpBad.load());
}
