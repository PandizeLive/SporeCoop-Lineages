#include "partner_marker.h"
#include <d3d9.h>
#include <cmath>

namespace partner_marker {
void draw(const SkHost* host, float x, float y, float dx, float dy, float size, unsigned color)
{
    auto* device = static_cast<IDirect3DDevice9*>(host->d3d_device());
    const float length = std::hypot(dx,dy);
    if (!device || !std::isfinite(length) || length < .001f || !std::isfinite(x) || !std::isfinite(y)) return;
    dx /= length; dy /= length;
    IDirect3DStateBlock9* saved = nullptr;
    if (FAILED(device->CreateStateBlock(D3DSBT_ALL,&saved))) return;
    device->SetVertexShader(nullptr); device->SetPixelShader(nullptr);
    device->SetTexture(0,nullptr);
    device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    device->SetRenderState(D3DRS_ZENABLE,FALSE);
    device->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);
    device->SetRenderState(D3DRS_LIGHTING,FALSE);
    device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);
    device->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE);
    device->SetRenderState(D3DRS_STENCILENABLE,FALSE);
    device->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);
    device->SetRenderState(D3DRS_FILLMODE,D3DFILL_SOLID);
    device->SetRenderState(D3DRS_COLORWRITEENABLE,15);
    device->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);
    device->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_DIFFUSE);
    device->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);
    device->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_DIFFUSE);
    device->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE);
    struct Vertex { float x,y,z,rhw; DWORD color; };
    auto paint = [&](float scale, DWORD argb) {
        auto v = [&](float along,float across) -> Vertex {
            return {x+(dx*along-dy*across)*size*scale,y+(dy*along+dx*across)*size*scale,0,1,argb};
        };
        Vertex vertices[] = {v(.65f,0),v(0,.38f),v(0,-.38f),
            v(-.55f,.14f),v(.08f,.14f),v(.08f,-.14f),
            v(-.55f,.14f),v(.08f,-.14f),v(-.55f,-.14f)};
        device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,3,vertices,sizeof(Vertex));
    };
    paint(1.2f,0xFF102018); paint(1.f,color);
    saved->Apply(); saved->Release();
}
}
