// The wordmark's two masks and the icon atlas: white coverage, tinted when drawn.
#pragma once
#include "imgui.h"
struct ID3D11Device;
struct ID3D11DeviceContext;

namespace citron {
bool        LoadLogo(ID3D11Device* dev, ID3D11DeviceContext* ctx);
ImTextureID LogoLetters();
ImTextureID LogoLeaves();
float       LogoAspect();   // width / height
ImTextureID IconAtlas();
} // namespace citron
