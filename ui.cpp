#include "ui.h"
#include "log.h"
#include <algorithm>
#include <atomic>
#include <math.h>
#include <stdint.h>
#include <time.h>

#define LOGI(...) ImLog(__VA_ARGS__)

// ========================= ASUMSI YANG PERLU DIVERIFIKASI =========================
// Tata letak vertex Im2D (RwIm2DVertex) dianggap standar RenderWare: 28 byte.
// Jika warna merah/biru tertukar, ubah kColorAbgr menjadi true.
static constexpr bool kColorAbgr = false;
// ==================================================================================

namespace {

struct Vtx { float x, y, z, rhw; uint32_t col; float u, v; };
static_assert(sizeof(Vtx) == 28, "layout vertex Im2D harus 28 byte");

// Enum RenderWare standar.
enum { rsTEXRASTER = 1, rsZTEST = 6, rsSHADE = 7, rsZWRITE = 8, rsSRCBLEND = 10, rsDESTBLEND = 11, rsVALPHA = 12 };
enum { blSRCALPHA = 5, blINVSRCALPHA = 6, shadeGOURAUD = 2, primTRILIST = 3 };
const int kRsCount = 7;
const int kRsIds[kRsCount] = {rsTEXRASTER, rsZTEST, rsSHADE, rsZWRITE, rsSRCBLEND, rsDESTBLEND, rsVALPHA};

struct C { float r, g, b, a; };

UiGfx g_gfx{};
bool  g_ready = false;
float g_z = 0.f, g_alphaMul = 1.f;
const int kBuf = 576;                    // kelipatan 3
Vtx   g_buf[kBuf];
int   g_n = 0;

inline float clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }
inline C mix(C a, C b, float t) { return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t}; }
inline C withA(C c, float a) { c.a = a; return c; }
inline float easeOutBack(float t) { const float c1 = 1.70158f, c3 = c1 + 1.f; t -= 1.f; return 1.f + c3 * t * t * t + c1 * t * t; }

uint32_t pack(C c) {
    auto q = [](float v) { int i = (int)(v * 255.f + .5f); return (uint32_t)(i < 0 ? 0 : (i > 255 ? 255 : i)); };
    uint32_t r = q(c.r), g = q(c.g), b = q(c.b), a = q(c.a * g_alphaMul);
    return kColorAbgr ? ((a << 24) | (b << 16) | (g << 8) | r) : ((a << 24) | (r << 16) | (g << 8) | b);
}

void flush() { if (g_n >= 3) g_gfx.renderPrimitive(primTRILIST, g_buf, g_n - g_n % 3); g_n = 0; }
Vtx  V(float x, float y, C c) { return Vtx{x, y, g_z, 1.f, pack(c), 0.f, 0.f}; }
void tri(Vtx a, Vtx b, Vtx c) { if (g_n + 3 > kBuf) flush(); g_buf[g_n++] = a; g_buf[g_n++] = b; g_buf[g_n++] = c; }
void quad(Vtx a, Vtx b, Vtx c, Vtx d) { tri(a, b, c); tri(a, c, d); }

// ---------- bentuk dasar ----------
const int kSeg = 6, kPer = 4 * (kSeg + 1);
struct Pt { float x, y; };

void perim(float cx, float cy, float hw, float hh, float r, float grow, Pt* o) {
    hw += grow; hh += grow; r += grow;
    r = std::max(0.f, std::min(r, std::min(hw, hh)));
    const float ccx[4] = {cx + hw - r, cx - hw + r, cx - hw + r, cx + hw - r};
    const float ccy[4] = {cy + hh - r, cy + hh - r, cy - hh + r, cy - hh + r};
    int n = 0;
    for (int k = 0; k < 4; ++k)
        for (int s = 0; s <= kSeg; ++s) {
            float a = (k * 90.f + s * (90.f / kSeg)) * 0.0174532925f;
            o[n++] = {ccx[k] + cosf(a) * r, ccy[k] + sinf(a) * r};
        }
}

void fillRound(float cx, float cy, float hw, float hh, float r, C top, C bot) {
    Pt p[kPer]; perim(cx, cy, hw, hh, r, 0.f, p);
    float y0 = cy - hh, h = std::max(1.f, 2.f * hh);
    Vtx c = V(cx, cy, mix(top, bot, .5f));
    for (int i = 0; i < kPer; ++i) {
        int j = (i + 1) % kPer;
        tri(c, V(p[i].x, p[i].y, mix(top, bot, clamp01((p[i].y - y0) / h))), V(p[j].x, p[j].y, mix(top, bot, clamp01((p[j].y - y0) / h))));
    }
}

void ringRound(float cx, float cy, float hw, float hh, float r, float g0, float g1, C c0, C c1) {
    Pt a[kPer], b[kPer]; perim(cx, cy, hw, hh, r, g0, a); perim(cx, cy, hw, hh, r, g1, b);
    for (int i = 0; i < kPer; ++i) {
        int j = (i + 1) % kPer;
        quad(V(a[i].x, a[i].y, c0), V(b[i].x, b[i].y, c1), V(b[j].x, b[j].y, c1), V(a[j].x, a[j].y, c0));
    }
}

void disc(float cx, float cy, float r, C c) {
    const int N = 12;
    for (int i = 0; i < N; ++i) {
        float a0 = i * 6.2831853f / N, a1 = (i + 1) * 6.2831853f / N;
        tri(V(cx, cy, c), V(cx + cosf(a0) * r, cy + sinf(a0) * r, c), V(cx + cosf(a1) * r, cy + sinf(a1) * r, c));
    }
}

// Garis dengan tepi halus (feather) + ujung membulat.
void line(float x0, float y0, float x1, float y1, float w, C c) {
    float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
    if (len < 1e-3f) return;
    float nx = -dy / len, ny = dx / len, h = w * .5f, f = 1.5f; C t = withA(c, 0.f);
    quad(V(x0 + nx * h, y0 + ny * h, c), V(x1 + nx * h, y1 + ny * h, c), V(x1 - nx * h, y1 - ny * h, c), V(x0 - nx * h, y0 - ny * h, c));
    quad(V(x0 + nx * h, y0 + ny * h, c), V(x1 + nx * h, y1 + ny * h, c), V(x1 + nx * (h + f), y1 + ny * (h + f), t), V(x0 + nx * (h + f), y0 + ny * (h + f), t));
    quad(V(x0 - nx * h, y0 - ny * h, c), V(x1 - nx * h, y1 - ny * h, c), V(x1 - nx * (h + f), y1 - ny * (h + f), t), V(x0 - nx * (h + f), y0 - ny * (h + f), t));
    disc(x0, y0, h, c); disc(x1, y1, h, c);
}

// ---------- animasi ----------
struct Spring {
    float x = 1.f, v = 0.f;
    void step(float target, float dt) { const float k = 380.f, c = 22.f; v += (k * (target - x) - c * v) * dt; x += v * dt; }
};

float g_act[BTN_COUNT], g_beam = 1.f, g_pulse = 0.f, g_appear = 0.f, g_rip[BTN_COUNT] = {-1, -1, -1, -1};
Spring g_scale[BTN_COUNT];
std::atomic<bool> g_down[BTN_COUNT], g_ripReq[BTN_COUNT];
std::atomic<bool> g_shown{false};
double g_last = 0.0;
bool g_firstDraw = true;

const C kAcc[BTN_COUNT] = {{1.f, .62f, .05f, 1.f}, {1.f, .30f, .12f, 1.f}, {1.f, .62f, .05f, 1.f}, {.45f, .78f, 1.f, 1.f}};
const C kDim = {.62f, .66f, .72f, 1.f};

double nowSec() { timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

struct Layout { float cx[BTN_COUNT]; float cy, half; };
Layout makeLayout(int W, int H) {
    Layout L;
    float S = std::min(W, H) * 0.105f, gap = S * 0.26f, extra = S * 0.45f;
    float x[BTN_COUNT] = {0.f, S + gap, 2 * (S + gap), 3 * (S + gap) + extra};
    float total = x[BTN_LIGHTS] + S, start = (W - total) * 0.5f;
    L.half = S * 0.5f;
    for (int i = 0; i < BTN_COUNT; ++i) L.cx[i] = start + x[i] + L.half;
    L.cy = H - L.half - H * 0.045f;
    return L;
}

// ---------- ikon ----------
void chevron(float x, float y, float sz, float dir, float w, C c) {
    float tipX = x + dir * sz * .45f, backX = x - dir * sz * .45f;
    line(backX, y - sz, tipX, y, w, c);
    line(backX, y + sz, tipX, y, w, c);
}

void iconArrows(int id, float cx, float cy, float hw, float act, float t) {
    float dir = id == BTN_LEFT ? -1.f : 1.f;
    float phase = fmodf((float)t / 0.7f, 1.f);
    C bright = mix(kAcc[id], C{1, 1, 1, 1}, .35f);
    for (int k = 0; k < 3; ++k) {                   // k=0 paling belakang, k=2 paling depan
        float pk = (k + .5f) / 3.f, bump = clamp01(1.f - fabsf(phase - pk) * 3.f);
        float amt = act * (.35f + .65f * bump);
        chevron(cx + dir * (k - 1) * hw * .36f, cy, hw * .38f, dir, hw * .11f, mix(kDim, bright, amt));
    }
}

void iconHazard(float cx, float cy, float hw, float act, float pulse) {
    float R = hw * .50f * (1.f + .06f * act * pulse);
    C c = mix(kDim, mix(kAcc[BTN_HAZARD], C{1, 1, 1, 1}, .30f), act * (.40f + .60f * pulse));
    float w = hw * .10f;
    Pt t = {cx, cy - R}, l = {cx - R * .87f, cy + R * .5f}, r = {cx + R * .87f, cy + R * .5f};
    line(t.x, t.y, l.x, l.y, w, c); line(l.x, l.y, r.x, r.y, w, c); line(r.x, r.y, t.x, t.y, w, c);
    line(cx, cy - R * .25f, cx, cy + R * .08f, w, c);
    disc(cx, cy + R * .30f, hw * .06f, c);
}

void iconLights(float cx, float cy, float hw, float act, float beam) {
    C c = mix(kDim, mix(kAcc[BTN_LIGHTS], C{1, 1, 1, 1}, .35f), act);
    disc(cx - hw * .20f, cy, hw * .24f, c);
    float bx = cx + hw * .12f, len = hw * (.14f + .26f * beam), w = hw * .09f;
    for (int i = -1; i <= 1; ++i) line(bx, cy + i * hw * .22f, bx + len, cy + i * hw * (.22f + .12f * beam), w, c);
    if (act < .98f) line(cx - hw * .42f, cy - hw * .36f, cx + hw * .42f, cy + hw * .36f, hw * .09f, C{1.f, .42f, .36f, 1.f - act});
}

// ---------- satu tombol ----------
void drawButton(int id, float cx, float cy, float half, float sc, float act, float pulse, float t, float rip) {
    C acc = kAcc[id];
    float hw = half * sc, r = hw * .46f;
    float breathe = act * (.45f + .55f * pulse) * (id == BTN_LIGHTS ? .6f : 1.f);
    float press = clamp01((1.f - sc) / .10f);

    // bayangan lembut
    ringRound(cx, cy + half * .10f, hw, hw, r, 0.f, half * .30f, C{0, 0, 0, .30f}, C{0, 0, 0, 0});
    // cahaya (glow) berdenyut
    if (breathe > .01f)
        for (int j = 0; j < 4; ++j) {
            float a0 = breathe * .42f * powf(1.f - j / 4.f, 2.f), a1 = breathe * .42f * powf(1.f - (j + 1) / 4.f, 2.f);
            ringRound(cx, cy, hw, hw, r, j * half * .16f, (j + 1) * half * .16f, withA(acc, a0), withA(acc, a1));
        }
    // badan kaca gelap, diwarnai aksen saat aktif
    C top = mix(C{.14f, .16f, .21f, .80f}, C{acc.r * .62f, acc.g * .62f, acc.b * .62f, .92f}, act * .85f);
    C bot = mix(C{.04f, .05f, .07f, .88f}, C{acc.r * .28f, acc.g * .28f, acc.b * .28f, .92f}, act * .85f);
    float dk = 1.f - .22f * press;
    top = {top.r * dk, top.g * dk, top.b * dk, top.a}; bot = {bot.r * dk, bot.g * dk, bot.b * dk, bot.a};
    fillRound(cx, cy, hw, hw, r, top, bot);
    // kilau kaca di bagian atas
    fillRound(cx, cy - hw * .42f, hw * .80f, hw * .46f, r * .8f, C{1, 1, 1, .13f}, C{1, 1, 1, 0});
    // bingkai tipis
    C rim = mix(C{1, 1, 1, .22f}, withA(mix(acc, C{1, 1, 1, 1}, .25f), .95f), act);
    ringRound(cx, cy, hw, hw, r, -1.8f, 0.f, rim, rim);
    ringRound(cx, cy, hw, hw, r, 0.f, 1.5f, rim, withA(rim, 0.f));
    // ikon
    if (id == BTN_LEFT || id == BTN_RIGHT) iconArrows(id, cx, cy, hw, act, t);
    else if (id == BTN_HAZARD) iconHazard(cx, cy, hw, act, pulse);
    else iconLights(cx, cy, hw, act, g_beam);
    // riak saat disentuh
    if (rip >= 0.f) {
        float e = 1.f - (1.f - rip) * (1.f - rip), rr = half * (.55f + 1.05f * e);
        ringRound(cx, cy, rr, rr, rr, -2.f, 2.f, withA(acc, (1.f - rip) * .55f), withA(acc, (1.f - rip) * .55f));
    }
}

}  // namespace

bool UiInit(const UiGfx& gfx) {
    g_gfx = gfx;
    g_ready = gfx.renderPrimitive != nullptr;
    LOGI("UiInit: renderPrimitive=%p rsSet=%p rsGet=%p nearZ=%p -> %s", (void*)gfx.renderPrimitive, (void*)gfx.rsSet, (void*)gfx.rsGet,
         (void*)gfx.nearScreenZ, g_ready ? "siap" : "TIDAK bisa menggambar");
    return g_ready;
}

bool UiShown() { return g_shown.load(); }

void UiSetPressed(int id, bool pressed) {
    if (id < 0 || id >= BTN_COUNT) return;
    g_down[id] = pressed;
    if (pressed) g_ripReq[id] = true;
}

int UiHitTest(int x, int y, int W, int H) {
    if (W <= 0 || H <= 0) return -1;
    Layout L = makeLayout(W, H);
    float r = L.half * 1.2f;                      // area sentuh sedikit lebih besar dari tampilan
    for (int i = 0; i < BTN_COUNT; ++i) {
        float dx = x - L.cx[i], dy = y - L.cy;
        if (dx * dx + dy * dy <= r * r) return i;
    }
    return -1;
}

void UiDraw(int W, int H, const bool active[BTN_COUNT], bool visible) {
    if (!g_ready || W <= 0 || H <= 0) return;
    double now = nowSec();
    float dt = g_last > 0 ? (float)std::min(now - g_last, 0.03) : 0.016f;
    g_last = now;

    // muncul / hilang (bertahap, tiap tombol sedikit tertunda)
    g_appear = clamp01(g_appear + (visible ? dt / 0.55f : -dt / 0.30f));
    g_shown = g_appear > 0.6f;
    if (g_appear <= 0.001f) return;

    bool blink = fmodf((float)now, 0.7f) < 0.35f;                  // selaras dengan blinkOn di main (700 ms)
    g_pulse += ((blink ? 1.f : 0.f) - g_pulse) * std::min(1.f, dt * 16.f);
    float kAct = std::min(1.f, dt * 12.f);
    g_beam += ((active[BTN_LIGHTS] ? 1.f : 0.f) - g_beam) * std::min(1.f, dt * 8.f);
    for (int i = 0; i < BTN_COUNT; ++i) {
        g_act[i] += ((active[i] ? 1.f : 0.f) - g_act[i]) * kAct;
        g_scale[i].step(g_down[i] ? .90f : 1.f, dt);
        if (g_ripReq[i].exchange(false)) g_rip[i] = 0.f;
        if (g_rip[i] >= 0.f) { g_rip[i] += dt / 0.55f; if (g_rip[i] > 1.f) g_rip[i] = -1.f; }
    }

    // ---- simpan & atur render state RenderWare ----
    void* saved[kRsCount]; for (int i = 0; i < kRsCount; ++i) saved[i] = nullptr;
    auto set = [](int s, intptr_t v) { if (g_gfx.rsSet) g_gfx.rsSet(s, (void*)v); };
    if (g_gfx.rsGet) for (int i = 0; i < kRsCount; ++i) g_gfx.rsGet(kRsIds[i], &saved[i]);
    set(rsTEXRASTER, 0); set(rsZTEST, 0); set(rsZWRITE, 0); set(rsSHADE, shadeGOURAUD);
    set(rsVALPHA, 1); set(rsSRCBLEND, blSRCALPHA); set(rsDESTBLEND, blINVSRCALPHA);
    g_z = g_gfx.nearScreenZ ? g_gfx.nearScreenZ() : 0.f;

    Layout L = makeLayout(W, H);
    float tsec = (float)now;
    for (int i = 0; i < BTN_COUNT; ++i) {
        float raw = clamp01(g_appear * 1.36f - i * .12f);
        g_alphaMul = clamp01(raw * 1.5f);
        float slide = (1.f - easeOutBack(raw)) * L.half * 2.4f;          // meluncur naik dari bawah
        drawButton(i, L.cx[i], L.cy + slide, L.half, std::max(.5f, g_scale[i].x), g_act[i],
                   i == BTN_LIGHTS ? 1.f : g_pulse, tsec, g_rip[i]);
    }
    g_alphaMul = 1.f;
    flush();

    // ---- pulihkan state ----
    if (g_gfx.rsSet && g_gfx.rsGet) for (int i = 0; i < kRsCount; ++i) g_gfx.rsSet(kRsIds[i], saved[i]);
    if (g_firstDraw) { g_firstDraw = false; LOGI("Tombol digambar pertama kali: layar %dx%d, z=%.3f", W, H, g_z); }
}
