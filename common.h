#pragma once
#include <stdint.h>
#include <stddef.h>

struct Vec3 { float x, y, z; };
struct Beam { Vec3 pos, dir; };   // posisi + arah (ternormalisasi) sebuah lampu depan

int64_t NowMs();
// Baca memori dengan aman (EFAULT -> false, tidak crash). Aman dipanggil dari thread mana pun.
bool SafeRead(const void* src, void* dst, size_t n);
