#pragma once
// Penyaring sentuhan untuk hook AND_TouchEvent(type, id, x, y).
//
// ATURAN UTAMA: jangan pernah merusak input bawaan game.
//  - Hanya sentuhan yang DIAWALI tepat di tombol mod yang ditelan (DOWN, MOVE, UP-nya).
//  - Semua sentuhan lain (gas, rem, setir, kamera, dst.) diteruskan utuh ke game.
//
// Masalah v0.2: kode "type" (DOWN/UP) ditebak (1 dan 3) dan status "ditangkap" bisa tersangkut.
// Akibatnya event UP/MOVE jari lain ikut ditelan -> tombol game terasa macet / kamera bergeser.
//
// v0.3: kode DOWN/UP dipelajari otomatis dari aliran event, tanpa tebakan:
//   * event pertama sebuah jari yang sedang tidak aktif = DOWN
//   * jari aktif yang menerima DOWN lagi  => event terakhirnya tadi adalah UP
// Status tangkapan juga selalu dibersihkan saat jari yang sama menyentuh lagi.
#include "log.h"

class TouchFilter {
public:
    static constexpr int kIds = 16, kTypes = 8;

    TouchFilter() { for (int i = 0; i < kIds; ++i) { m_active[i] = false; m_last[i] = -1; m_cap[i] = -1; } }

    int downType() const { return m_downType; }
    int upType() const { return m_upType; }

    // Mengembalikan true bila event harus DITELAN (jangan diteruskan ke game).
    //   hit(x, y)  -> indeks tombol yang tertekan, atau -1
    //   press(b)   -> dipanggil sekali saat tombol b mulai ditekan
    //   release(b) -> dipanggil saat jari yang menekan b terangkat
    template <class HitFn, class PressFn, class ReleaseFn>
    bool OnEvent(int type, int id, int x, int y, bool uiVisible, HitFn hit, PressFn press, ReleaseFn release) {
        if (id < 0 || id >= kIds || type < 0 || type >= kTypes) return false;   // tak dikenal: teruskan

        bool isDown;
        if (!m_active[id]) {
            ++m_downCnt[type]; learn();
            if (type != m_downType) return false;      // event liar (bukan awal sentuhan): teruskan apa adanya
            isDown = true;
        } else if (type == m_downType) {
            // DOWN baru padahal jari ini masih dianggap aktif: UP sebelumnya tidak kita kenali
            if (m_last[id] >= 0 && m_last[id] != m_downType) { ++m_upCnt[m_last[id]]; learn(); }
            isDown = true;
        } else {
            isDown = false;
        }

        if (isDown) {
            if (m_cap[id] >= 0) release(m_cap[id]);    // tangkapan lama yang tersangkut -> bersihkan
            m_cap[id] = -1; m_active[id] = true; m_last[id] = type;
            if (uiVisible) {
                int b = hit(x, y);
                if (b >= 0) { m_cap[id] = b; press(b); return true; }
            }
            return false;
        }

        // MOVE atau UP
        m_last[id] = type;
        bool isUp = (m_upType >= 0 && type == m_upType);
        if (isUp) m_active[id] = false;
        int cb = m_cap[id];
        if (cb < 0) return false;                      // bukan jari kita: selalu diteruskan
        if (isUp) { m_cap[id] = -1; release(cb); }
        return true;
    }

private:
    void learn() {
        int d = -1, u = -1;
        for (int t = 0; t < kTypes; ++t) if (m_downCnt[t] > (d < 0 ? 0 : m_downCnt[d])) d = t;
        for (int t = 0; t < kTypes; ++t) if (t != d && m_upCnt[t] > (u < 0 ? 0 : m_upCnt[u])) u = t;
        if (d != m_downType || u != m_upType) {
            m_downType = d; m_upType = u;
            ImLog("Touch: kode DOWN=%d UP=%d (dipelajari otomatis)", d, u);
        }
    }

    bool m_active[kIds];
    int  m_last[kIds];
    int  m_cap[kIds];
    int  m_downCnt[kTypes] = {};
    int  m_upCnt[kTypes] = {};
    int  m_downType = -1, m_upType = -1;
};
