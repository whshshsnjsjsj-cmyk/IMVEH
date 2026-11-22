#pragma once
#include "common.h"

typedef const float* (*GetMatFn)();   // GetCurrentViewMatrix / GetCurrentProjectionMatrix (mengembalikan pointer ke 16 float)

// ---- thread utama ----
void VolFrameBegin();                                   // sekali per frame (CRenderer::ConstructRenderList)
void VolAddBeam(const Vec3& pos, const Vec3& dir);      // dari DoHeadLightBeam: asal + arah kerucut (dir ternormalisasi)
void VolCacheMatrices(GetMatFn view, GetMatFn proj);    // dari RenderScene: simpan kamera 3D terakhir yang valid

// ---- thread render (di CPostEffects::MobileRender, setelah aslinya) ----
void VolOnPostEffects(GetMatFn view, GetMatFn proj);

void VolLogStats();
