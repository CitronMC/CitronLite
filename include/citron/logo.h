// The mark in the header: a white mask from logo_data.h, tinted with the accent when drawn.
#pragma once
#include "imgui.h"
struct ID3D11Device;
struct ID3D11DeviceContext;

namespace citron {
bool        LoadLogo(ID3D11Device* dev, ID3D11DeviceContext* ctx);
bool        LogoReady();
ImTextureID LogoTex();
float       LogoAspect();   // width / height
} // namespace citron
