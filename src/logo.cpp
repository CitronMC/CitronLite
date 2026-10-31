#include "citron/logo.h"
#include "citron/icons_data.h"
#include "citron/logo_data.h"
#include <d3d11.h>
#include <cstdint>
#include <vector>

namespace citron {
namespace {

ID3D11ShaderResourceView* g_letters = nullptr;
ID3D11ShaderResourceView* g_leaves = nullptr;
ID3D11ShaderResourceView* g_icons = nullptr;

// White with the mask for alpha. Several times the size it is drawn at, with mips, so it stays clean
// at any window scale.
ID3D11ShaderResourceView* MaskTexture(ID3D11Device* dev, ID3D11DeviceContext* ctx, const unsigned char* alpha, int w, int h) {
    std::vector<uint32_t> px((size_t)w * h);
    for (size_t i = 0; i < px.size(); ++i) px[i] = 0x00FFFFFFu | (uint32_t)alpha[i] << 24;
    D3D11_TEXTURE2D_DESC d = {};
    d.Width = (UINT)w; d.Height = (UINT)h; d.MipLevels = 0; d.ArraySize = 1;
    d.Format = DXGI_FORMAT_R8G8B8A8_UNORM; d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    d.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
    ID3D11Texture2D* tex = nullptr;
    if (FAILED(dev->CreateTexture2D(&d, nullptr, &tex)) || !tex) return nullptr;
    ctx->UpdateSubresource(tex, 0, nullptr, px.data(), (UINT)w * 4, 0);
    ID3D11ShaderResourceView* srv = nullptr;
    if (SUCCEEDED(dev->CreateShaderResourceView(tex, nullptr, &srv))) ctx->GenerateMips(srv);
    tex->Release();
    return srv;
}

}  // namespace

bool LoadLogo(ID3D11Device* dev, ID3D11DeviceContext* ctx) {
    g_letters = MaskTexture(dev, ctx, kLogoAlpha, kLogoW, kLogoH);
    g_leaves = MaskTexture(dev, ctx, kLogoAlpha + (size_t)kLogoW * kLogoH, kLogoW, kLogoH);
    g_icons = MaskTexture(dev, ctx, kIconAtlasAlpha, kIconAtlasW, kIconAtlasH);
    return g_letters && g_leaves && g_icons;
}
ImTextureID LogoLetters() { return (ImTextureID)(uintptr_t)g_letters; }
ImTextureID LogoLeaves()  { return (ImTextureID)(uintptr_t)g_leaves; }
float       LogoAspect()  { return (float)kLogoW / (float)kLogoH; }
ImTextureID IconAtlas()   { return (ImTextureID)(uintptr_t)g_icons; }
} // namespace citron
