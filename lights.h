#pragma once
#include "common.h"

typedef void (*AddLightFn)(unsigned char, Vec3, Vec3, float, float, float, float, unsigned char, bool, void*);
void VehInit(AddLightFn addLight);

// ---- siklus frame / kendaraan (thread utama) ----
void VehBeginFrame();                                  // dipanggil sekali per frame
void VehOnPreRender(void* veh, bool isPlayer, int playerDir); // playerDir: 0 mati, 1 kiri, -1 kanan, 2 hazard
void VehOnBeam(void* veh, const void* cmatrix);        // dari DoHeadLightBeam
int  VehSnapshotBeams(Beam* out, int maxCount, const Vec3& cam);   // thread render

// ---- material emissive ----
void VehSetCurrentClump(void* clump);                  // dari SetEditableMaterials
void MatClassify(void* material);                      // setelah RpMaterialStreamRead
void MatCopyTag(const void* from, void* to);          // setelah RpMaterialClone
void MatForget(void* material);                        // sebelum RpMaterialDestroy
bool VehEmissiveFor(const void* material, float rgba[4]);
