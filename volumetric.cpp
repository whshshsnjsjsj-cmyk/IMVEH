// Lampu depan volumetrik: ray-march kerucut cahaya di ruang dunia terhadap depth buffer adegan, resolusi setengah,
// lalu ditambahkan (additive) ke framebuffer. Semua state GL milik game disimpan dan dipulihkan.
#include "volumetric.h"
#include "config.h"
#include "log.h"
#include "vmath.h"
#include <GLES3/gl3.h>
#include <EGL/egl.h>
#include <string.h>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <vector>

namespace {
std::mutex g_mu;
std::vector<Beam> g_build, g_pub;
int64_t g_pubMs = 0;
float g_cacheV[16], g_cacheP[16];
bool g_cacheOk = false;

std::atomic<int> g_stDraw{0}, g_stNoCtx{0}, g_stNoMat{0}, g_stNoDepth{0}, g_stBlitFail{0}, g_stBeamMax{0};
bool g_disabled = false;

// ------------------------------------------------------------------ shader (ditulis sendiri) ------------------------------------------------------------------
const char* kVS = R"GLSL(#version 300 es
precision highp float;
out vec2 vUv;
void main(){
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    vUv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)GLSL";

const char* kMarchFS = R"GLSL(#version 300 es
precision highp float;
precision highp sampler2D;
in vec2 vUv;
uniform sampler2D uDepth;
uniform mat4 uInvVP;
uniform vec3 uCam;
uniform vec2 uDR;           // depth range (near, far) glDepthRange
uniform float uMaxDist, uDensity;
uniform int uCount;
uniform vec4 uPos[4];       // xyz = asal, w = jangkauan
uniform vec4 uDir[4];       // xyz = arah, w = cos(setengah sudut)
out vec4 o;

vec3 worldAt(vec2 uv, float d){
    float z = (d - uDR.x) / max(uDR.y - uDR.x, 1e-6) * 2.0 - 1.0;
    vec4 w = uInvVP * vec4(uv * 2.0 - 1.0, z, 1.0);
    return w.xyz / w.w;
}
float ign(vec2 p){ return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715)))); }

void main(){
    float d = texture(uDepth, vUv).r;
    vec3 target = worldAt(vUv, d);
    vec3 ray = target - uCam;
    float len = length(ray);
    vec3 rd = ray / max(len, 1e-4);
    bool sky = d >= uDR.y - 1e-5;
    float maxLen = sky ? uMaxDist : min(len, uMaxDist);
    const int STEPS = 20;
    float ds = maxLen / float(STEPS);
    float jit = ign(gl_FragCoord.xy);
    vec3 sum = vec3(0.0);
    for (int s = 0; s < STEPS; ++s){
        vec3 p = uCam + rd * ((float(s) + jit) * ds);
        for (int i = 0; i < 4; ++i){
            if (i >= uCount) break;
            vec3 v = p - uPos[i].xyz;
            float along = dot(v, uDir[i].xyz);
            if (along < 0.25 || along > uPos[i].w) continue;
            float r2 = max(dot(v, v) - along * along, 0.0);
            float c = uDir[i].w;
            float radius = along * sqrt(max(1.0 - c * c, 0.0)) / max(c, 0.05);
            float edge = 1.0 - smoothstep(radius * 0.30, radius, sqrt(r2));
            float fall = 1.0 - along / uPos[i].w; fall *= fall;
            float cosT = dot(uDir[i].xyz, -rd);                    // cahaya menuju mata => hamburan maju kuat
            float g = 0.45;
            float phase = (1.0 - g * g) / pow(max(1.0 + g * g - 2.0 * g * cosT, 0.05), 1.5);
            sum += vec3(1.0, 0.94, 0.80) * edge * fall * phase * ds;
        }
    }
    o = vec4(1.0 - exp(-sum * uDensity), 1.0);
}
)GLSL";

const char* kCompFS = R"GLSL(#version 300 es
precision highp float;
precision highp sampler2D;
in vec2 vUv;
uniform sampler2D uVol;
uniform float uGain;
out vec4 o;
void main(){ o = vec4(texture(uVol, vUv).rgb * uGain, 1.0); }
)GLSL";

// ------------------------------------------------------------------ objek GL ------------------------------------------------------------------
struct Gl {
    GLuint march = 0, comp = 0, vao = 0, depthTex = 0, depthFbo = 0, volTex = 0, volFbo = 0, sNearest = 0, sLinear = 0;
    GLint uDepth = -1, uInvVP = -1, uCam = -1, uDR = -1, uMax = -1, uDens = -1, uCount = -1, uPos = -1, uDir = -1, cVol = -1, cGain = -1;
    int W = 0, H = 0, vW = 0, vH = 0;
    GLenum fmt = 0;
    int fmtIdx = 0;
    bool built = false;
} g;

GLuint Compile(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[600]; glGetShaderInfoLog(s, sizeof log, nullptr, log); ImLog("Volumetrik: shader gagal: %s", log); glDeleteShader(s); return 0; }
    return s;
}
GLuint Link(const char* fs) {
    GLuint v = Compile(GL_VERTEX_SHADER, kVS), f = Compile(GL_FRAGMENT_SHADER, fs);
    if (!v || !f) return 0;
    GLuint p = glCreateProgram();
    glAttachShader(p, v); glAttachShader(p, f); glLinkProgram(p);
    glDeleteShader(v); glDeleteShader(f);
    GLint ok = 0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { char log[600]; glGetProgramInfoLog(p, sizeof log, nullptr, log); ImLog("Volumetrik: link gagal: %s", log); glDeleteProgram(p); return 0; }
    return p;
}

bool Build() {
    if (g.built) return true;
    const char* ver = (const char*)glGetString(GL_VERSION);
    const char* rend = (const char*)glGetString(GL_RENDERER);
    ImLog("Volumetrik: thread render GL=%s renderer=%s", ver ? ver : "?", rend ? rend : "?");
    if (!ver || !strstr(ver, "OpenGL ES 3")) { ImLog("Volumetrik: butuh GLES3 - dimatikan"); g_disabled = true; return false; }
    g.march = Link(kMarchFS); g.comp = Link(kCompFS);
    if (!g.march || !g.comp) { g_disabled = true; return false; }
    g.uDepth = glGetUniformLocation(g.march, "uDepth"); g.uInvVP = glGetUniformLocation(g.march, "uInvVP");
    g.uCam = glGetUniformLocation(g.march, "uCam");     g.uDR = glGetUniformLocation(g.march, "uDR");
    g.uMax = glGetUniformLocation(g.march, "uMaxDist"); g.uDens = glGetUniformLocation(g.march, "uDensity");
    g.uCount = glGetUniformLocation(g.march, "uCount"); g.uPos = glGetUniformLocation(g.march, "uPos");
    g.uDir = glGetUniformLocation(g.march, "uDir");
    g.cVol = glGetUniformLocation(g.comp, "uVol");      g.cGain = glGetUniformLocation(g.comp, "uGain");
    glGenVertexArrays(1, &g.vao);
    glGenSamplers(1, &g.sNearest); glGenSamplers(1, &g.sLinear);
    for (GLuint s : {g.sNearest, g.sLinear}) {
        GLint f = (s == g.sNearest) ? GL_NEAREST : GL_LINEAR;
        glSamplerParameteri(s, GL_TEXTURE_MIN_FILTER, f); glSamplerParameteri(s, GL_TEXTURE_MAG_FILTER, f);
        glSamplerParameteri(s, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glSamplerParameteri(s, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    g.built = true;
    return true;
}

void FreeTargets() {
    if (g.depthFbo) glDeleteFramebuffers(1, &g.depthFbo);
    if (g.volFbo) glDeleteFramebuffers(1, &g.volFbo);
    if (g.depthTex) glDeleteTextures(1, &g.depthTex);
    if (g.volTex) glDeleteTextures(1, &g.volTex);
    g.depthFbo = g.volFbo = g.depthTex = g.volTex = 0;
    g.W = g.H = 0;
}

// Buat texture depth (format sama dengan sumber agar glBlitFramebuffer sah) + target volume resolusi rendah.
bool Targets(int W, int H, GLenum fmt) {
    if (g.W == W && g.H == H && g.fmt == fmt && g.depthTex) return true;
    FreeTargets();
    g.vW = std::max(W / std::max(ivm::kVolDownscale, 1), 16);
    g.vH = std::max(H / std::max(ivm::kVolDownscale, 1), 16);
    while (glGetError() != GL_NO_ERROR) {}

    glActiveTexture(GL_TEXTURE0);
    glGenTextures(1, &g.depthTex);
    glBindTexture(GL_TEXTURE_2D, g.depthTex);
    glTexStorage2D(GL_TEXTURE_2D, 1, fmt, W, H);
    glGenFramebuffers(1, &g.depthFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, g.depthFbo);
    GLenum att = (fmt == GL_DEPTH24_STENCIL8) ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT;
    glFramebufferTexture2D(GL_FRAMEBUFFER, att, GL_TEXTURE_2D, g.depthTex, 0);
    GLenum none = GL_NONE;
    glDrawBuffers(1, &none); glReadBuffer(GL_NONE);
    GLenum st1 = glCheckFramebufferStatus(GL_FRAMEBUFFER);

    glGenTextures(1, &g.volTex);
    glBindTexture(GL_TEXTURE_2D, g.volTex);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, g.vW, g.vH);
    glGenFramebuffers(1, &g.volFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, g.volFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g.volTex, 0);
    GLenum st2 = glCheckFramebufferStatus(GL_FRAMEBUFFER);

    GLenum err = glGetError();
    if (st1 != GL_FRAMEBUFFER_COMPLETE || st2 != GL_FRAMEBUFFER_COMPLETE || err != GL_NO_ERROR) {
        ImLog("Volumetrik: alokasi target gagal: fboDepth=0x%x fboVol=0x%x err=0x%x fmt=0x%x", st1, st2, err, fmt);
        FreeTargets();
        return false;
    }
    g.W = W; g.H = H; g.fmt = fmt;
    ImLog("Volumetrik: target depth=%dx%d fmt=0x%x sinar=%dx%d", W, H, fmt, g.vW, g.vH);
    return true;
}

// Simpan / pulihkan state GL milik game.
struct Saved {
    GLint drawFbo, readFbo, vp[4], prog, vao, actTex, tex0, samp0, sRGB, dRGB, sA, dA, eRGB, eA;
    GLboolean cm[4];
    GLboolean blend, depth, cull, scissor, stencil;
    GLfloat dr[2];
    void Save() {
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFbo); glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFbo);
        glGetIntegerv(GL_VIEWPORT, vp); glGetIntegerv(GL_CURRENT_PROGRAM, &prog); glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &actTex);
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex0); glGetIntegerv(GL_SAMPLER_BINDING, &samp0);
        glActiveTexture((GLenum)actTex);
        glGetIntegerv(GL_BLEND_SRC_RGB, &sRGB); glGetIntegerv(GL_BLEND_DST_RGB, &dRGB);
        glGetIntegerv(GL_BLEND_SRC_ALPHA, &sA); glGetIntegerv(GL_BLEND_DST_ALPHA, &dA);
        glGetIntegerv(GL_BLEND_EQUATION_RGB, &eRGB); glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &eA);
        glGetBooleanv(GL_COLOR_WRITEMASK, cm); glGetFloatv(GL_DEPTH_RANGE, dr);
        blend = glIsEnabled(GL_BLEND); depth = glIsEnabled(GL_DEPTH_TEST); cull = glIsEnabled(GL_CULL_FACE);
        scissor = glIsEnabled(GL_SCISSOR_TEST); stencil = glIsEnabled(GL_STENCIL_TEST);
    }
    void Restore() {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, readFbo); glBindFramebuffer(GL_DRAW_FRAMEBUFFER, drawFbo);
        glViewport(vp[0], vp[1], vp[2], vp[3]);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, (GLuint)tex0); glBindSampler(0, (GLuint)samp0);
        glActiveTexture((GLenum)actTex);
        glBindVertexArray((GLuint)vao); glUseProgram((GLuint)prog);
        glBlendFuncSeparate(sRGB, dRGB, sA, dA); glBlendEquationSeparate(eRGB, eA);
        glColorMask(cm[0], cm[1], cm[2], cm[3]);
        if (blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
        if (depth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
        if (cull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
        if (scissor) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
        if (stencil) glEnable(GL_STENCIL_TEST); else glDisable(GL_STENCIL_TEST);
    }
};

// Ambil view+proj yang valid: dari getter langsung, atau cache terakhir (dari RenderScene).
bool FetchMatrices(GetMatFn gv, GetMatFn gp, float* V, float* P, bool* fromCache) {
    *fromCache = false;
    if (gv && gp) {
        const float* pv = gv(); const float* pp = gp();
        if (pv && pp && SafeRead(pv, V, 64) && SafeRead(pp, P, 64) && NormalizeView(V) && NormalizeProj(P)) return true;
    }
    std::lock_guard<std::mutex> lk(g_mu);
    if (!g_cacheOk) return false;
    memcpy(V, g_cacheV, 64); memcpy(P, g_cacheP, 64); *fromCache = true;
    return true;
}

void RenderBeams(const float* V, const float* P, const Beam* beams, int n) {
    float VP[16], inv[16];
    Mul4(P, V, VP);
    if (!Inv4(VP, inv)) return;
    Vec3 cam = CameraFromView(V);

    Saved sv; sv.Save();
    int W = sv.vp[2], H = sv.vp[3];
    bool ok = false;
    do {
        if (W < 32 || H < 32) break;
        while (glGetError() != GL_NO_ERROR) {}

        // --- format depth sumber ---
        GLint db = 0, sb = 0;
        GLenum depthAtt = sv.readFbo ? GL_DEPTH_ATTACHMENT : GL_DEPTH, stencilAtt = sv.readFbo ? GL_STENCIL_ATTACHMENT : GL_STENCIL;
        glGetFramebufferAttachmentParameteriv(GL_READ_FRAMEBUFFER, depthAtt, GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE, &db);
        glGetFramebufferAttachmentParameteriv(GL_READ_FRAMEBUFFER, stencilAtt, GL_FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE, &sb);
        while (glGetError() != GL_NO_ERROR) {}
        if (db <= 0) {
            if (++g_stNoDepth == 1) ImLog("Volumetrik: framebuffer sumber tidak punya depth (fbo=%d) - dilewati", sv.readFbo);
            break;
        }
        GLenum cands[2]; int nc = 0;
        if (db >= 32) cands[nc++] = GL_DEPTH_COMPONENT32F;
        else if (db >= 24) { if (sb > 0) { cands[nc++] = GL_DEPTH24_STENCIL8; cands[nc++] = GL_DEPTH_COMPONENT24; } else { cands[nc++] = GL_DEPTH_COMPONENT24; cands[nc++] = GL_DEPTH24_STENCIL8; } }
        else cands[nc++] = GL_DEPTH_COMPONENT16;
        if (g.fmtIdx >= nc) { g_disabled = true; ImLog("Volumetrik: semua format depth gagal - dimatikan"); break; }
        GLenum fmt = cands[g.fmtIdx];
        if (!Targets(W, H, fmt)) { if (++g.fmtIdx >= nc) { g_disabled = true; ImLog("Volumetrik: target gagal - dimatikan"); } break; }

        // --- salin depth ---
        glBindFramebuffer(GL_READ_FRAMEBUFFER, sv.readFbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g.depthFbo);
        glBlitFramebuffer(sv.vp[0], sv.vp[1], sv.vp[0] + W, sv.vp[1] + H, 0, 0, W, H, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
        GLenum err = glGetError();
        if (err != GL_NO_ERROR) {
            ++g_stBlitFail;
            ImLog("Volumetrik: salin depth gagal: err=0x%x sumberFbo=%d bitDepth=%d bitStencil=%d fmt=0x%x", err, sv.readFbo, db, sb, fmt);
            ++g.fmtIdx;                      // coba format berikutnya di frame depan
            FreeTargets();
            break;
        }

        // --- pass 1: ray-march ke target resolusi rendah ---
        glBindFramebuffer(GL_FRAMEBUFFER, g.volFbo);
        glViewport(0, 0, g.vW, g.vH);
        glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glDisable(GL_SCISSOR_TEST); glDisable(GL_STENCIL_TEST);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glUseProgram(g.march); glBindVertexArray(g.vao);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, g.depthTex); glBindSampler(0, g.sNearest);
        float pos[16] = {0}, dir[16] = {0};
        for (int i = 0; i < n && i < 4; ++i) {
            pos[i * 4 + 0] = beams[i].pos.x; pos[i * 4 + 1] = beams[i].pos.y; pos[i * 4 + 2] = beams[i].pos.z; pos[i * 4 + 3] = ivm::kBeamRange;
            dir[i * 4 + 0] = beams[i].dir.x; dir[i * 4 + 1] = beams[i].dir.y; dir[i * 4 + 2] = beams[i].dir.z; dir[i * 4 + 3] = ivm::kBeamCosHalf;
        }
        glUniform1i(g.uDepth, 0);
        glUniformMatrix4fv(g.uInvVP, 1, GL_FALSE, inv);
        glUniform3f(g.uCam, cam.x, cam.y, cam.z);
        glUniform2f(g.uDR, sv.dr[0], sv.dr[1]);
        glUniform1f(g.uMax, ivm::kVolMaxDist); glUniform1f(g.uDens, ivm::kVolDensity);
        glUniform1i(g.uCount, std::min(n, 4));
        glUniform4fv(g.uPos, 4, pos); glUniform4fv(g.uDir, 4, dir);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        // --- pass 2: tambahkan ke framebuffer game ---
        glBindFramebuffer(GL_READ_FRAMEBUFFER, sv.readFbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, sv.drawFbo);
        glViewport(sv.vp[0], sv.vp[1], sv.vp[2], sv.vp[3]);
        glUseProgram(g.comp);
        glBindTexture(GL_TEXTURE_2D, g.volTex); glBindSampler(0, g.sLinear);
        glUniform1i(g.cVol, 0); glUniform1f(g.cGain, ivm::kVolGain);
        glEnable(GL_BLEND);
        glBlendFuncSeparate(GL_ONE, GL_ONE, GL_ZERO, GL_ONE); glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        err = glGetError();
        if (err != GL_NO_ERROR) { ImLog("Volumetrik: error saat menggambar: 0x%x", err); break; }
        ok = true;
    } while (false);
    sv.Restore();
    if (ok && ++g_stDraw == 1) ImLog("Volumetrik tergambar pertama kali: sinar=%d fmt=0x%x layar=%dx%d", n, g.fmt, W, H);
}
}  // namespace

// ------------------------------------------------------------------ API publik ------------------------------------------------------------------
void VolFrameBegin() {
    std::lock_guard<std::mutex> lk(g_mu);
    g_pub.swap(g_build);
    g_build.clear();
    g_pubMs = NowMs();
}

void VolAddBeam(const Vec3& pos, const Vec3& dir) {
    if (!ivm::kUseVolumetric) return;
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_build.size() < 24) g_build.push_back({pos, dir});
}

void VolCacheMatrices(GetMatFn gv, GetMatFn gp) {
    if (!ivm::kUseVolumetric || !gv || !gp) return;
    const float* pv = gv(); const float* pp = gp();
    float V[16], P[16];
    if (!pv || !pp || !SafeRead(pv, V, 64) || !SafeRead(pp, P, 64) || !NormalizeView(V) || !NormalizeProj(P)) return;
    std::lock_guard<std::mutex> lk(g_mu);
    memcpy(g_cacheV, V, 64); memcpy(g_cacheP, P, 64); g_cacheOk = true;
}

void VolOnPostEffects(GetMatFn gv, GetMatFn gp) {
    if (!ivm::kUseVolumetric || g_disabled) return;
    if (!eglGetCurrentContext()) { if (++g_stNoCtx == 1) ImLog("Volumetrik: tidak ada konteks EGL di thread MobileRender (dilewati)"); return; }

    Beam all[24]; int na = 0;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        if (NowMs() - g_pubMs <= 250) { na = (int)std::min<size_t>(g_pub.size(), 24); for (int i = 0; i < na; ++i) all[i] = g_pub[i]; }
    }
    if (na == 0) return;

    float V[16], P[16]; bool cached = false;
    if (!FetchMatrices(gv, gp, V, P, &cached)) {
        if (++g_stNoMat == 1) ImLog("Volumetrik: matriks kamera tidak valid (getter=%p,%p) - dilewati", (void*)gv, (void*)gp);
        return;
    }
    static bool logged = false;
    if (!logged) { logged = true; ImLog("Volumetrik: matriks kamera dari %s", cached ? "cache RenderScene" : "getter langsung"); }

    // pilih sumber terdekat dari kamera (maks kMaxBeams)
    Vec3 cam = CameraFromView(V);
    struct Sel { float d2; int i; } sel[24]; int ns = 0;
    float maxd2 = ivm::kBeamMaxCamDist * ivm::kBeamMaxCamDist;
    for (int i = 0; i < na; ++i) {
        float dx = all[i].pos.x - cam.x, dy = all[i].pos.y - cam.y, dz = all[i].pos.z - cam.z, d2 = dx * dx + dy * dy + dz * dz;
        if (d2 <= maxd2) sel[ns++] = {d2, i};
    }
    if (ns == 0) return;
    std::sort(sel, sel + ns, [](const Sel& a, const Sel& b) { return a.d2 < b.d2; });
    Beam use[4]; int nu = std::min(ns, std::min(ivm::kMaxBeams, 4));
    for (int i = 0; i < nu; ++i) use[i] = all[sel[i].i];
    if (nu > g_stBeamMax) g_stBeamMax = nu;
    RenderBeams(V, P, use, nu);
}

void VolLogStats() {
    ImLog("Volumetrik: gambar=%d sinarMaks=%d tanpaEGL=%d tanpaMatriks=%d tanpaDepth=%d blitGagal=%d %s",
          g_stDraw.load(), g_stBeamMax.load(), g_stNoCtx.load(), g_stNoMat.load(), g_stNoDepth.load(), g_stBlitFail.load(), g_disabled ? "DIMATIKAN" : "aktif");
}
