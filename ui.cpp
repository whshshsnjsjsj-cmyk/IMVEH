#include "ui.h"
#include <GLES3/gl3.h>
#include <android/log.h>
#include <algorithm>
#include <atomic>
#include <string.h>
#include <time.h>

#define TAG "ImprovedVehicleMobile"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)

static const char* kVS = R"GLSL(#version 300 es
precision highp float;
uniform vec2 uRes;
uniform vec4 uRect;   // cx, cy, half, scale
out vec2 vLocal;
void main(){
    vec2 c = vec2(float(gl_VertexID & 1) * 2.0 - 1.0, float(gl_VertexID >> 1) * 2.0 - 1.0);
    float ext = uRect.z * uRect.w * 1.9;           // ruang ekstra untuk glow
    vLocal = c * ext;
    vec2 px = uRect.xy + vLocal;
    gl_Position = vec4(px.x / uRes.x * 2.0 - 1.0, 1.0 - px.y / uRes.y * 2.0, 0.0, 1.0);
}
)GLSL";

static const char* kFS = R"GLSL(#version 300 es
precision highp float;
in vec2 vLocal;
uniform vec4 uRect;
uniform vec3 uAccent;
uniform float uActive, uPulse, uPress;
uniform int uIcon;
out vec4 o;

float sdRound(vec2 p, float b, float r){ vec2 q = abs(p) - vec2(b - r); return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r; }
float seg(vec2 p, vec2 a, vec2 b){ vec2 pa = p - a, ba = b - a; float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0); return length(pa - ba * h); }
vec4 over(vec4 t, vec4 b){ float a = t.a + b.a * (1.0 - t.a); vec3 c = (t.rgb * t.a + b.rgb * b.a * (1.0 - t.a)) / max(a, 1e-4); return vec4(c, a); }

float iconDist(vec2 q){
    float t = 0.085;
    if (uIcon == 0 || uIcon == 1){                // panah kiri / kanan
        if (uIcon == 1) q.x = -q.x;
        float d = seg(q, vec2(0.16, 0.36), vec2(-0.24, 0.0));
        d = min(d, seg(q, vec2(-0.24, 0.0), vec2(0.16, -0.36)));
        d = min(d, seg(q, vec2(-0.24, 0.0), vec2(0.42, 0.0)));
        return d - t;
    }
    if (uIcon == 2){                               // hazard: segitiga + tanda seru
        vec2 a = vec2(0.0, 0.44), b = vec2(-0.44, -0.30), c = vec2(0.44, -0.30);
        float d = min(seg(q, a, b), min(seg(q, b, c), seg(q, c, a))) - 0.055;
        d = min(d, seg(q, vec2(0.0, 0.14), vec2(0.0, -0.02)) - 0.05);
        d = min(d, length(q - vec2(0.0, -0.15)) - 0.06);
        return d;
    }
    float d = length(q - vec2(-0.16, 0.0)) - 0.24; // lampu depan + berkas cahaya
    d = min(d, seg(q, vec2(0.18, 0.20), vec2(0.46, 0.27)) - 0.04);
    d = min(d, seg(q, vec2(0.18, 0.00), vec2(0.48, 0.00)) - 0.04);
    d = min(d, seg(q, vec2(0.18, -0.20), vec2(0.46, -0.27)) - 0.04);
    return d;
}

void main(){
    float R = uRect.z * uRect.w;
    vec2 p = vLocal;
    float d = sdRound(p, R, R * 0.40);
    float aa = 1.25;
    float glowAmt = uActive * (0.35 + 0.65 * uPulse);

    vec4 col = vec4(uAccent, glowAmt * 0.60 * exp(-max(d, 0.0) / (R * 0.30)) * step(0.0, d));

    vec3 base = mix(vec3(0.055, 0.065, 0.085), uAccent * 0.40, uActive * 0.85);
    base += 0.07 * clamp(-p.y / R, -1.0, 1.0);      // gradasi halus atas -> bawah
    base *= 1.0 - 0.18 * uPress;
    float bodyA = (1.0 - smoothstep(-aa, aa, d)) * mix(0.66, 0.90, uActive);
    col = over(vec4(base, bodyA), col);

    float ring = 1.0 - smoothstep(0.0, aa * 1.2, abs(d + 1.2));
    vec3 ringCol = mix(vec3(1.0), uAccent, uActive);
    col = over(vec4(ringCol, ring * mix(0.20, 0.85, uActive)), col);

    vec2 q = vec2(p.x, -p.y) / (R * 0.92);
    float di = iconDist(q) * R * 0.92;
    float iconA = 1.0 - smoothstep(-aa, aa, di);
    vec3 iconCol = mix(vec3(0.84, 0.87, 0.92), mix(uAccent, vec3(1.0), 0.55), uActive * (0.45 + 0.55 * uPulse));
    col = over(vec4(iconCol, iconA), col);

    o = col;
}
)GLSL";

static GLuint g_prog = 0, g_vao = 0;
static bool g_failed = false;
static GLint uRes, uRect, uAccent, uActive, uPulse, uPress, uIcon;
static float g_act[BTN_COUNT], g_prs[BTN_COUNT], g_pulse = 0.f;
static std::atomic<bool> g_down[BTN_COUNT];
static double g_last = 0.0;

struct Layout { float cx[BTN_COUNT]; float cy, half; };

static Layout makeLayout(int W, int H) {
    Layout L;
    float S = std::min(W, H) * 0.105f;
    float gap = S * 0.26f, extra = S * 0.45f;
    float x[BTN_COUNT] = {0.f, S + gap, 2 * (S + gap), 3 * (S + gap) + extra};
    float total = x[BTN_LIGHTS] + S;
    float start = (W - total) * 0.5f;
    L.half = S * 0.5f;
    for (int i = 0; i < BTN_COUNT; ++i) L.cx[i] = start + x[i] + L.half;
    L.cy = H - L.half - H * 0.045f;
    return L;
}

static GLuint compile(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[512]; glGetShaderInfoLog(s, sizeof log, nullptr, log); LOGI("UI shader error: %s", log); glDeleteShader(s); return 0; }
    return s;
}

static bool ensureInit() {
    if (g_prog) return true;
    if (g_failed) return false;
    const char* ver = (const char*)glGetString(GL_VERSION);
    if (!ver || !strstr(ver, "OpenGL ES 3")) { LOGI("UI butuh GLES3, versi: %s", ver ? ver : "?"); g_failed = true; return false; }
    GLuint vs = compile(GL_VERTEX_SHADER, kVS), fs = compile(GL_FRAGMENT_SHADER, kFS);
    if (!vs || !fs) { g_failed = true; return false; }
    g_prog = glCreateProgram();
    glAttachShader(g_prog, vs); glAttachShader(g_prog, fs);
    glLinkProgram(g_prog);
    glDeleteShader(vs); glDeleteShader(fs);
    GLint ok = 0; glGetProgramiv(g_prog, GL_LINK_STATUS, &ok);
    if (!ok) { char log[512]; glGetProgramInfoLog(g_prog, sizeof log, nullptr, log); LOGI("UI link error: %s", log); g_prog = 0; g_failed = true; return false; }
    uRes = glGetUniformLocation(g_prog, "uRes");     uRect = glGetUniformLocation(g_prog, "uRect");
    uAccent = glGetUniformLocation(g_prog, "uAccent"); uActive = glGetUniformLocation(g_prog, "uActive");
    uPulse = glGetUniformLocation(g_prog, "uPulse"); uPress = glGetUniformLocation(g_prog, "uPress");
    uIcon = glGetUniformLocation(g_prog, "uIcon");
    glGenVertexArrays(1, &g_vao);   // VAO sendiri tanpa atribut, agar tidak terganggu state engine
    LOGI("UI siap (GL %s)", ver);
    return true;
}

static double nowSec() { timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

void UiSetPressed(int id, bool pressed) { if (id >= 0 && id < BTN_COUNT) g_down[id] = pressed; }

int UiHitTest(int x, int y, int W, int H) {
    if (W <= 0 || H <= 0) return -1;
    Layout L = makeLayout(W, H);
    float r = L.half * 1.2f;                     // area sentuh sedikit lebih besar dari tampilan
    for (int i = 0; i < BTN_COUNT; ++i) {
        float dx = x - L.cx[i], dy = y - L.cy;
        if (dx * dx + dy * dy <= r * r) return i;
    }
    return -1;
}

void UiDraw(int W, int H, const bool active[BTN_COUNT], bool blinkOn) {
    if (W <= 0 || H <= 0 || !ensureInit()) return;

    double t = nowSec(), dt = g_last > 0 ? std::min(t - g_last, 0.1) : 0.016; g_last = t;
    float k = (float)std::min(1.0, dt * 14.0);
    g_pulse += ((blinkOn ? 1.f : 0.f) - g_pulse) * std::min(1.f, (float)dt * 18.f);
    for (int i = 0; i < BTN_COUNT; ++i) {
        g_act[i] += ((active[i] ? 1.f : 0.f) - g_act[i]) * k;
        g_prs[i] += ((g_down[i] ? 1.f : 0.f) - g_prs[i]) * k;
    }

    // ---- simpan state GL milik game ----
    GLint prog, vao, vp[4], sRGB, dRGB, sA, dA, eRGB, eA;
    GLboolean cm[4], dm;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prog);   glGetIntegerv(0x85B5 /*VERTEX_ARRAY_BINDING*/, &vao);
    glGetIntegerv(GL_VIEWPORT, vp);
    glGetIntegerv(0x80C9, &sRGB); glGetIntegerv(0x80C8, &dRGB); glGetIntegerv(0x80CB, &sA); glGetIntegerv(0x80CA, &dA);
    glGetIntegerv(0x8009, &eRGB); glGetIntegerv(0x883D, &eA);
    glGetBooleanv(GL_COLOR_WRITEMASK, cm); glGetBooleanv(GL_DEPTH_WRITEMASK, &dm);
    GLboolean eBlend = glIsEnabled(GL_BLEND), eDepth = glIsEnabled(GL_DEPTH_TEST), eCull = glIsEnabled(GL_CULL_FACE),
              eScis = glIsEnabled(GL_SCISSOR_TEST), eSten = glIsEnabled(GL_STENCIL_TEST);

    glViewport(0, 0, W, H);
    glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glDisable(GL_SCISSOR_TEST); glDisable(GL_STENCIL_TEST);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    glUseProgram(g_prog); glBindVertexArray(g_vao);
    glUniform2f(uRes, (float)W, (float)H);

    static const float acc[BTN_COUNT][3] = {{1.0f, 0.62f, 0.05f}, {1.0f, 0.30f, 0.12f}, {1.0f, 0.62f, 0.05f}, {0.45f, 0.78f, 1.0f}};
    static const int   icon[BTN_COUNT]   = {0, 2, 1, 3};
    Layout L = makeLayout(W, H);
    for (int i = 0; i < BTN_COUNT; ++i) {
        float scale = 1.0f - 0.07f * g_prs[i];
        glUniform4f(uRect, L.cx[i], L.cy, L.half, scale);
        glUniform3f(uAccent, acc[i][0], acc[i][1], acc[i][2]);
        glUniform1f(uActive, g_act[i]);
        glUniform1f(uPulse, i == BTN_LIGHTS ? 1.0f : g_pulse);   // tombol lampu menyala tetap, sein berkedip
        glUniform1f(uPress, g_prs[i]);
        glUniform1i(uIcon, icon[i]);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }

    // ---- pulihkan state GL ----
    glBindVertexArray(vao); glUseProgram(prog);
    glViewport(vp[0], vp[1], vp[2], vp[3]);
    glBlendFuncSeparate(sRGB, dRGB, sA, dA); glBlendEquationSeparate(eRGB, eA);
    glColorMask(cm[0], cm[1], cm[2], cm[3]);
    (void)dm;
    if (eBlend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (eDepth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (eCull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    if (eScis) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
    if (eSten) glEnable(GL_STENCIL_TEST); else glDisable(GL_STENCIL_TEST);
}
