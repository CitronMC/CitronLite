#include "../../backend/backend_impl.h"
#include <algorithm>

namespace citron {
namespace {

bool EndsWith(const std::string& s, const char* tail) {
    const size_t n = std::char_traits<char>::length(tail);
    return s.size() >= n && s.compare(s.size() - n, n, tail) == 0;
}

// Whether a hotbar item is what the module throws. Servers that wrap the game's items in their own
// namespace keep the vanilla name at the end.
bool Wanted(const std::string& module, const bedrock::Item& it, bool eggs) {
    if (it.name.empty() || it.count <= 0) return false;
    if (module == "keybindpearl") return EndsWith(it.name, "ender_pearl");
    if (module == "keybindsnowball") return EndsWith(it.name, "snowball") || (eggs && EndsWith(it.name, "egg"));
    if (module == "keybindpot") return EndsWith(it.name, "splash_potion") && (it.aux == 21 || it.aux == 22);
    return false;
}

constexpr const char* kThrowers[] = {"keybindpearl", "keybindsnowball", "keybindpot"};

}  // namespace

void Backend::Impl::FinishThrow(bool goBack) {
    if (throwing.useDown) gamebind::Send(binds.use, false);
    if (goBack && throwing.back >= 0 && throwing.back != throwing.slot && local.valid) {
        const int key = binds.hotbar[throwing.back];
        gamebind::Send(key, true);
        gamebind::Send(key, false);
    }
    throwing = ItemThrow{};
}

// On the module's key: the item's hotbar slot, one use, and back to the slot you had.
void Backend::Impl::ThrowTick(const Modules& m) {
    const auto now = clock_t_::now();
    if (throwing.stage) {
        const ModuleSnapshot* s = Mod(m, throwing.module.c_str());
        if (!s || !s->on || !InGame() || now >= throwing.abortAt) {
            FinishThrow(InGame());
            return;
        }
        if (now < throwing.step) return;
        int slot = -1;
        switch (throwing.stage) {
        case 1:   // the item's slot
            gamebind::Send(binds.hotbar[throwing.slot], true);
            gamebind::Send(binds.hotbar[throwing.slot], false);
            throwing.stage = 2;
            throwing.step = now + std::chrono::milliseconds(10);
            return;
        case 2:   // wait for the game to have it in hand
            if (!rd.SelectedSlot(local.actor.ptr, slot) || slot != throwing.slot) return;
            throwing.stage = 3;
            throwing.step = now + std::chrono::milliseconds(20);
            return;
        case 3:
            if (gamebind::Send(binds.use, true)) throwing.useDown = true;
            throwing.stage = 4;
            throwing.step = now + std::chrono::milliseconds(35);
            return;
        case 4:
            gamebind::Send(binds.use, false);
            throwing.useDown = false;
            throwing.stage = 5;
            throwing.step = now + std::chrono::milliseconds(55);
            return;
        default:
            FinishThrow(true);
            return;
        }
    }
    for (const char* id : kThrowers) {
        const ModuleSnapshot* s = Mod(m, id);
        const uint32_t edges = s && s->bind ? PhysKeyEdges(s->bind) : 0;
        const auto known = throwEdges.find(id);
        const bool pressed = known != throwEdges.end() && known->second != edges;
        throwEdges[id] = edges;
        if (!pressed || !s->on || !InGame() || !local.valid) continue;
        int current = -1, found = -1;
        if (!rd.SelectedSlot(local.actor.ptr, current)) continue;
        for (int i = 0; i < 9; ++i) {
            const int at = (current + i) % 9;   // the slot in hand first
            bedrock::Item it;
            if (rd.HotbarItem(local.actor.ptr, at, it) && Wanted(id, it, Check(*s, "eggs", false))) {
                found = at;
                break;
            }
        }
        if (found < 0) continue;
        throwing = ItemThrow{};
        throwing.module = id;
        throwing.slot = found;
        throwing.back = current;
        throwing.stage = found == current ? 3 : 1;
        throwing.step = now;
        throwing.abortAt = now + std::chrono::milliseconds(800);
        return;
    }
}

}  // namespace citron
