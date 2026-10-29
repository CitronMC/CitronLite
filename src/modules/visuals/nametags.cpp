#include "../../backend/backend_impl.h"

namespace citron {

// The nametag pass draws every actor's tag but skips your own with one je. In third person that
// skip is taken out; in first person it stays, or your tag would hang over your view when you look
// up. Which view it is comes from the game's own getPerspective: its value read is hooked to hand
// over the option it reads.
void Backend::Impl::NametagTick(const Modules& m) {
    const uintptr_t skip = SiteAt(game::Site::OwnNametag), persp = SiteAt(game::Site::Perspective);
    const bool on = On(m, "tpnametags");
    if (on && persp && !perspHook.installed) engine::InstallCaptureRaxHook(proc, perspHook, persp + 0x43, 8);
    if (!on && perspHook.installed) {
        engine::RemoveHook(proc, perspHook);
        perspHook = engine::CaveHook{};
    }
    int view = 0;
    uintptr_t option = 0;
    if (perspHook.installed && engine::Read(proc, perspHook.slot, option) && rd.Plausible(option))
        engine::Read(proc, option + 0x18, view);
    const bool want = on && skip && local.valid && view >= 1 && view <= 2;
    const uint8_t* original = game::LayoutFor(build).ownNametagSkip;
    static const uint8_t kNothing[2] = {0x66, 0x90};
    if (want && !ownNametag.on) WritePatch(ownNametag, skip, kNothing, 2, original);
    else if (!want && ownNametag.on) ClearPatch(ownNametag);
}

// A hit starts a short wait before your next item use. The call that starts it is jumped over, so
// a pearl, a block or food goes right after the hit.
void Backend::Impl::NoUseDelayTick(const Modules& m) {
    const uintptr_t at = SiteAt(game::Site::UseDelay);
    if (!at) return;
    static const uint8_t kCall[2] = {0xFF, 0x15}, kSkip[2] = {0xEB, 0x04};
    const bool want = On(m, "nousedelay");
    if (want && !useDelay.on) WritePatch(useDelay, at + 20, kSkip, 2, kCall);
    else if (!want && useDelay.on) ClearPatch(useDelay);
}

}  // namespace citron
