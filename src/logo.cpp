#include "citron/logo.h"
#include "citron/logo_data.h"
#include <d3d11.h>
#include <cstdint>

namespace citron {
namespace { ID3D11ShaderResourceView* g_srv = nullptr; }

bool LoadLogo(ID3D11Device* dev, ID3D11DeviceContext* ctx) {
    // Several times the size it is drawn at, with mips, so it stays clean at any window scale.
    D3D11_TEXTURE2D_DESC d = {};
    d.Width = kLogoW; d.Height = kLogoH; d.MipLevels = 0; d.ArraySize = 1;
    d.Format = DXGI_FORMAT_R8G8B8A8_UNORM; d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    d.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
    ID3D11Texture2D* tex = nullptr;
    if (FAILED(dev->CreateTexture2D(&d, nullptr, &tex)) || !tex) return false;
    ctx->UpdateSubresource(tex, 0, nullptr, kLogoRGBA, kLogoW * 4, 0);
    HRESULT hr = dev->CreateShaderResourceView(tex, nullptr, &g_srv);
    if (SUCCEEDED(hr)) ctx->GenerateMips(g_srv);
    tex->Release();
    return SUCCEEDED(hr);
}
bool        LogoReady() { return g_srv != nullptr; }
ImTextureID LogoTex()   { return (ImTextureID)(uintptr_t)g_srv; }
float       LogoAspect(){ return (float)kLogoW / (float)kLogoH; }
} // namespace citron
