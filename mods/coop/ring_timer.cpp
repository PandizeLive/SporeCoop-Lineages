#include "ring_timer.h"

#include <d3d9.h>
#include <cmath>
#include <algorithm>
#include "fixes.h"

namespace ring_timer {
namespace {
bool g_on = true;
COOP_FIX("ui.revive_ring", "BUG-9141", g_on,
         "cell / creature / adventure / tribe: a downed player's countdown is a draining ring (off: the text countdowns)");

struct Vertex {
    float x, y, z, rhw;
    DWORD color;
};

void paint(IDirect3DDevice9* device, D3DPRIMITIVETYPE type, const std::vector<Pt>& points, uint32_t argb)
{
    const UINT per = type == D3DPT_TRIANGLESTRIP ? 1 : 3;
    if (points.size() < 3) return;
    std::vector<Vertex> v;
    v.reserve(points.size());
    for (const Pt& p : points) v.push_back({p.x - 0.5f, p.y - 0.5f, 0, 1, (DWORD)argb});
    const UINT count = type == D3DPT_TRIANGLESTRIP ? (UINT)v.size() - 2 : (UINT)v.size() / per;
    device->DrawPrimitiveUP(type, count, v.data(), sizeof(Vertex));
}
} // namespace

bool enabled() { return g_on; }

// BUG-9156: a red edge round the screen (a downed player spectating): four bands from the edge, opaque to clear
void vignette(const SkHost* host, int sw, int sh, float strength)
{
    auto* device = static_cast<IDirect3DDevice9*>(host->d3d_device());
    if (!device || sw <= 0 || sh <= 0 || !(strength > 0)) return;
    IDirect3DStateBlock9* saved = nullptr;
    if (FAILED(device->CreateStateBlock(D3DSBT_ALL, &saved))) return;
    device->SetVertexShader(nullptr);
    device->SetPixelShader(nullptr);
    device->SetTexture(0, nullptr);
    device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    device->SetRenderState(D3DRS_ZENABLE, FALSE);
    device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    device->SetRenderState(D3DRS_LIGHTING, FALSE);
    device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    device->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
    device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    device->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    device->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    const float w = (float)sw, h = (float)sh, band = std::min(w, h) * 0.18f;
    const DWORD edge = (DWORD)(std::clamp(strength, 0.f, 1.f) * 150.f) << 24 | 0x00B00000, clear = 0x00B00000;
    auto quad = [&](float x0, float y0, DWORD c0, float x1, float y1, DWORD c1, float x2, float y2, DWORD c2, float x3, float y3, DWORD c3) {
        Vertex v[4] = {{x0, y0, 0, 1, c0}, {x1, y1, 0, 1, c1}, {x2, y2, 0, 1, c2}, {x3, y3, 0, 1, c3}};
        device->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2, v, sizeof(Vertex));
    };
    quad(0, 0, edge, w, 0, edge, w - band, band, clear, band, band, clear);         // top
    quad(0, h, edge, band, h - band, clear, w - band, h - band, clear, w, h, edge); // bottom
    quad(0, 0, edge, band, band, clear, band, h - band, clear, 0, h, edge);         // left
    quad(w, 0, edge, w, h, edge, w - band, h - band, clear, w - band, band, clear); // right
    saved->Apply();
    saved->Release();
}

// BUG-9156: a filled arrow (left or right) with a dark edge, centred at (x, y)
void arrow(const SkHost* host, float x, float y, float size, bool left, uint32_t argb)
{
    auto* device = static_cast<IDirect3DDevice9*>(host->d3d_device());
    if (!device || !(size > 1)) return;
    IDirect3DStateBlock9* saved = nullptr;
    if (FAILED(device->CreateStateBlock(D3DSBT_ALL, &saved))) return;
    device->SetVertexShader(nullptr);
    device->SetPixelShader(nullptr);
    device->SetTexture(0, nullptr);
    device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    device->SetRenderState(D3DRS_ZENABLE, FALSE);
    device->SetRenderState(D3DRS_LIGHTING, FALSE);
    device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    device->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    const float d = left ? -1.f : 1.f;
    auto tri = [&](float s, DWORD c) {
        Vertex v[3] = {{x + d * s, y, 0, 1, c}, {x - d * s * 0.7f, y - s, 0, 1, c}, {x - d * s * 0.7f, y + s, 0, 1, c}};
        device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, v, sizeof(Vertex));
    };
    tri(size + 2.f, 0xC0000000);
    tri(size, (DWORD)argb);
    saved->Apply();
    saved->Release();
}

void draw(const SkHost* host, float x, float y, float radius, float left, float revive, double now)
{
    auto* device = static_cast<IDirect3DDevice9*>(host->d3d_device());
    if (!device || !std::isfinite(x) || !std::isfinite(y) || !(radius > 2)) return;
    IDirect3DStateBlock9* saved = nullptr;
    if (FAILED(device->CreateStateBlock(D3DSBT_ALL, &saved))) return;
    device->SetVertexShader(nullptr);
    device->SetPixelShader(nullptr);
    device->SetTexture(0, nullptr);
    device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    device->SetRenderState(D3DRS_ZENABLE, FALSE);
    device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    device->SetRenderState(D3DRS_LIGHTING, FALSE);
    device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE); // the disc is see-through
    device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    device->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
    device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    device->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    device->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
    device->SetRenderState(D3DRS_COLORWRITEENABLE, 15);
    device->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);

    const float r_out = radius, r_in = radius * 0.68f;
    left = std::clamp(left, 0.f, 1.f);
    paint(device, D3DPT_TRIANGLELIST, pie(x, y, r_out + 3.f, 1.f), kDisc);                  // the dark disc
    paint(device, D3DPT_TRIANGLESTRIP, arc_strip(x, y, r_in, r_out, 0.f, 1.f), kTrack);       // the empty track
    const bool blink_on = std::fmod(now, 0.8) < 0.5;
    paint(device, D3DPT_TRIANGLESTRIP, arc_strip(x, y, r_in, r_out, left_start(left), left), time_color(left, blink_on));
    if (revive > 0.01f)
        paint(device, D3DPT_TRIANGLELIST, pie(x, y, r_in - 2.f, revive), kRevive); // a revive under way
    // the "+" (a downed one waiting for help)
    const float arm = r_in * 0.55f, half = std::max(1.5f, r_in * 0.13f);
    const std::vector<Pt> cross{{x - half, y - arm}, {x + half, y - arm}, {x - half, y + arm},
                                {x + half, y - arm}, {x + half, y + arm}, {x - half, y + arm},
                                {x - arm, y - half}, {x + arm, y - half}, {x - arm, y + half},
                                {x + arm, y - half}, {x + arm, y + half}, {x - arm, y + half}};
    paint(device, D3DPT_TRIANGLELIST, cross, kCross);
    saved->Apply();
    saved->Release();
}
} // namespace ring_timer
