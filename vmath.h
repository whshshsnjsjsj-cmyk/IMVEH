#pragma once
// Matematika matriks 4x4 (kolom-mayor, gaya OpenGL) untuk lampu depan volumetrik.
#include <math.h>
#include <string.h>
#include "common.h"

static inline void Mul4(const float* a, const float* b, float* r) {   // r = a * b
    float t[16];
    for (int c = 0; c < 4; ++c)
        for (int row = 0; row < 4; ++row) {
            float s = 0.f;
            for (int k = 0; k < 4; ++k) s += a[k * 4 + row] * b[c * 4 + k];
            t[c * 4 + row] = s;
        }
    memcpy(r, t, sizeof t);
}

static inline bool Inv4(const float* m, float* inv) {
    float t[16];
    t[0]  =  m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15] + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
    t[4]  = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15] - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
    t[8]  =  m[4]*m[9]*m[15]  - m[4]*m[11]*m[13] - m[8]*m[5]*m[15] + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
    t[12] = -m[4]*m[9]*m[14]  + m[4]*m[10]*m[13] + m[8]*m[5]*m[14] - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];
    t[1]  = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15] - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
    t[5]  =  m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15] + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
    t[9]  = -m[0]*m[9]*m[15]  + m[0]*m[11]*m[13] + m[8]*m[1]*m[15] - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
    t[13] =  m[0]*m[9]*m[14]  - m[0]*m[10]*m[13] - m[8]*m[1]*m[14] + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];
    t[2]  =  m[1]*m[6]*m[15]  - m[1]*m[7]*m[14]  - m[5]*m[2]*m[15] + m[5]*m[3]*m[14] + m[13]*m[2]*m[7]  - m[13]*m[3]*m[6];
    t[6]  = -m[0]*m[6]*m[15]  + m[0]*m[7]*m[14]  + m[4]*m[2]*m[15] - m[4]*m[3]*m[14] - m[12]*m[2]*m[7]  + m[12]*m[3]*m[6];
    t[10] =  m[0]*m[5]*m[15]  - m[0]*m[7]*m[13]  - m[4]*m[1]*m[15] + m[4]*m[3]*m[13] + m[12]*m[1]*m[7]  - m[12]*m[3]*m[5];
    t[14] = -m[0]*m[5]*m[14]  + m[0]*m[6]*m[13]  + m[4]*m[1]*m[14] - m[4]*m[2]*m[13] - m[12]*m[1]*m[6]  + m[12]*m[2]*m[5];
    t[3]  = -m[1]*m[6]*m[11]  + m[1]*m[7]*m[10]  + m[5]*m[2]*m[11] - m[5]*m[3]*m[10] - m[9]*m[2]*m[7]   + m[9]*m[3]*m[6];
    t[7]  =  m[0]*m[6]*m[11]  - m[0]*m[7]*m[10]  - m[4]*m[2]*m[11] + m[4]*m[3]*m[10] + m[8]*m[2]*m[7]   - m[8]*m[3]*m[6];
    t[11] = -m[0]*m[5]*m[11]  + m[0]*m[7]*m[9]   + m[4]*m[1]*m[11] - m[4]*m[3]*m[9]  - m[8]*m[1]*m[7]   + m[8]*m[3]*m[5];
    t[15] =  m[0]*m[5]*m[10]  - m[0]*m[6]*m[9]   - m[4]*m[1]*m[10] + m[4]*m[2]*m[9]  + m[8]*m[1]*m[6]   - m[8]*m[2]*m[5];
    float det = m[0] * t[0] + m[1] * t[4] + m[2] * t[8] + m[3] * t[12];
    if (!isfinite(det) || fabsf(det) < 1e-20f) return false;
    float id = 1.f / det;
    for (int i = 0; i < 16; ++i) inv[i] = t[i] * id;
    return true;
}

static inline void Transpose4(const float* a, float* r) { float t[16]; for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) t[i * 4 + j] = a[j * 4 + i]; memcpy(r, t, sizeof t); }
static inline bool AllFinite16(const float* m) { for (int i = 0; i < 16; ++i) if (!isfinite(m[i])) return false; return true; }

// View: baris terakhir (0,0,0,1), kolom 0 berpanjang ~1.
static inline bool ValidView(const float* v) {
    if (!AllFinite16(v)) return false;
    if (fabsf(v[3]) > 1e-3f || fabsf(v[7]) > 1e-3f || fabsf(v[11]) > 1e-3f || fabsf(v[15] - 1.f) > 1e-3f) return false;
    float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    return l > 0.9f && l < 1.1f;
}
// Proyeksi perspektif: |m[11]| = 1 dan m[15] = 0 (tanda bergantung handedness engine), m[0] dan m[5] bukan nol.
static inline bool ValidProj(const float* p) {
    if (!AllFinite16(p)) return false;
    return fabsf(fabsf(p[11]) - 1.f) < 1e-3f && fabsf(p[15]) < 1e-3f && fabsf(p[0]) > 1e-4f && fabsf(p[5]) > 1e-4f;
}
// Terima matriks kolom-mayor; bila tidak valid, coba transposenya (baris-mayor). Return false bila keduanya gagal.
static inline bool NormalizeView(float* v) { if (ValidView(v)) return true; float t[16]; Transpose4(v, t); if (ValidView(t)) { memcpy(v, t, sizeof t); return true; } return false; }
static inline bool NormalizeProj(float* p) { if (ValidProj(p)) return true; float t[16]; Transpose4(p, t); if (ValidProj(t)) { memcpy(p, t, sizeof t); return true; } return false; }

// Posisi kamera dunia dari view matrix rigid: cam = -R^T * t
static inline Vec3 CameraFromView(const float* v) {
    float tx = v[12], ty = v[13], tz = v[14];
    return { -(v[0] * tx + v[1] * ty + v[2] * tz), -(v[4] * tx + v[5] * ty + v[6] * tz), -(v[8] * tx + v[9] * ty + v[10] * tz) };
}
