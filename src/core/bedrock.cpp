#include "citron/bedrock.h"
#include "citron/log.h"
#include "citron/option_record.h"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace citron::bedrock {
namespace {

// The entt registry, as the game's inlined try_get walks it.
constexpr uintptr_t kSparseBegin = 0x48, kSparseEnd = 0x50, kNodes = 0x68, kNodesEnd = 0x70;
constexpr uintptr_t kNodeStride = 0x20;
constexpr uintptr_t kStorageSparse = 0x08, kStorageSparseEnd = 0x10, kStoragePacked = 0x20, kStorageComponents = 0x50;
constexpr uint32_t kEntityIndex = 0x3FFFFu;   // the low bits of an id; all set is a removed entry
constexpr size_t kComponentPage = 128;        // components to a page of the storage
// WeakEntityRef { EntityRegistry*, control block, entity id }; entt lives at EntityRegistry + 0x30.
constexpr uintptr_t kEnttInRegistry = 0x30;

bool LooksLikeMotion(const float (&f)[9]) {
    for (float x : f)
        if (!std::isfinite(x)) return false;
    if (std::fabs(f[0]) > 3.2e7f || std::fabs(f[2]) > 3.2e7f || std::fabs(f[1]) > 4096.f) return false;
    const float dx = f[0] - f[3], dy = f[1] - f[4], dz = f[2] - f[5];
    if (dx * dx + dy * dy + dz * dz > 64.f * 64.f) return false;
    for (int i = 6; i < 9; ++i)
        if (std::fabs(f[i]) > 12.f) return false;
    return true;
}

}  // namespace

void Reader::Attach(const engine::Proc& proc, uintptr_t gameCoreSlot, const game::Layout& layout) {
    Detach();
    if (!proc.h || !proc.base || !gameCoreSlot) return;
    proc_ = &proc;
    lay_ = layout;
    base_ = (uintptr_t)proc.base;
    end_ = base_ + proc.size;
    slot_ = gameCoreSlot;
}

void Reader::Detach() {
    proc_ = nullptr;
    base_ = end_ = slot_ = ci_ = 0;
    nextWalk_ = 0;
    childKnown_ = false;
    itemsOff_ = itemsFor_ = 0;
    for (Known& k : cache_) k = Known{};
    nextCache_ = 0;
}

uintptr_t Reader::WalkFromCore(uintptr_t core) {
    uintptr_t mcg = 0, map = 0, root = 0;
    if (!Rd(core + lay_.coreMinecraftGame, mcg) || !Plausible(mcg) || !Rd(mcg + lay_.gameClientMap, map) ||
        !Plausible(map) || !Rd(map + 0x8, root) || !Plausible(root))
        return 0;
    for (uintptr_t o = 0x18; o <= 0x48; o += 8) {
        uintptr_t cand = 0, back = 0;
        if (Rd(root + o, cand) && Plausible(cand) && Rd(cand + lay_.clientMinecraftGame, back) && back == mcg)
            return cand;
    }
    return 0;
}

uintptr_t Reader::FindClientInstance() {
    uintptr_t gv = 0;
    if (!slot_ || !Rd(slot_, gv) || !Plausible(gv)) return 0;
    if (const uintptr_t ci = WalkFromCore(gv)) return ci;
    // Since 1.26.50 an object sits between the game core and the MinecraftGame: each child is
    // tried and the one that closes is remembered.
    auto viaChild = [&](uintptr_t root) -> uintptr_t {
        if (!Plausible(root)) return 0;
        if (childKnown_) {
            uintptr_t child = 0;
            if (Rd(root + childOff_, child) && Plausible(child))
                if (const uintptr_t ci = WalkFromCore(child)) return ci;
            childKnown_ = false;
        }
        for (uintptr_t o = 0; o <= 0x1000; o += 8) {
            uintptr_t child = 0;
            if (!Rd(root + o, child) || !Plausible(child)) continue;
            if (const uintptr_t ci = WalkFromCore(child)) {
                childOff_ = o;
                childKnown_ = true;
                return ci;
            }
        }
        return 0;
    };
    uintptr_t core = 0;
    if (Rd(gv + 0x8, core) && Plausible(core)) {
        if (const uintptr_t ci = WalkFromCore(core)) return ci;
        if (const uintptr_t ci = viaChild(core)) return ci;
    }
    return viaChild(gv);
}

bool Reader::StillClientInstance(uintptr_t ci) {
    uintptr_t vt = 0, mcg = 0;
    return Plausible(ci) && Rd(ci, vt) && InImage(vt) && Rd(ci + lay_.clientMinecraftGame, mcg) && Plausible(mcg);
}

bool Reader::ClientInstance(uintptr_t& ci) {
    if (!Attached()) return false;
    if (ci_ && StillClientInstance(ci_)) {
        ci = ci_;
        return true;
    }
    ci_ = 0;
    const uint64_t now = GetTickCount64();
    if (now < nextWalk_) return false;
    ci_ = FindClientInstance();
    if (!ci_) {
        nextWalk_ = now + 500;   // a failed walk reads thousands of words
        return false;
    }
    ci = ci_;
    Log("[bedrock] ClientInstance 0x%llX", (unsigned long long)ci_);
    return true;
}

bool Reader::Timer(uintptr_t& at, float& ticksPerSecond, float& alpha) {
    uintptr_t ci = 0, minecraft = 0, timer = 0;
    float tps = 0.f, a = 0.f;
    if (!ClientInstance(ci) || !Rd(ci + lay_.clientMinecraft, minecraft) || !Plausible(minecraft) ||
        !Rd(minecraft + lay_.minecraftTimer, timer) || !Plausible(timer) || !Rd(timer + game::kTimerTicksPerSecond, tps) ||
        !Rd(timer + game::kTimerAlpha, a) || !std::isfinite(tps) || tps < 0.1f || tps > 2000.f)
        return false;
    at = timer;
    ticksPerSecond = tps;
    alpha = std::isfinite(a) ? std::clamp(a, 0.f, 1.f) : 1.f;
    return true;
}

uintptr_t Reader::ResolveStorage(uintptr_t registry, uint32_t hash) {
    for (const Known& k : cache_)
        if (k.registry == registry && k.hash == hash && k.storage) return k.storage;
    uint64_t buckets = 0, bucketsEnd = 0, nodes = 0, nodesEnd = 0;
    if (!Plausible(registry) || !Rd(registry + kSparseBegin, buckets) || !Rd(registry + kSparseEnd, bucketsEnd) ||
        !Rd(registry + kNodes, nodes) || !Rd(registry + kNodesEnd, nodesEnd) || !Plausible(buckets) ||
        bucketsEnd <= buckets || !Plausible(nodes) || nodesEnd < nodes)
        return 0;
    const uint64_t count = (bucketsEnd - buckets) / 8;
    if (!count || count > (1u << 24) || (count & (count - 1))) return 0;
    uint64_t cur = 0;
    if (!Rd(buckets + (hash & (count - 1)) * 8, cur)) return 0;
    for (int guard = 0; guard < 2048; ++guard) {
        if (cur == ~0ull) return 0;
        const uintptr_t node = nodes + cur * kNodeStride;
        if (cur > (1u << 24) || node >= nodesEnd) return 0;
        struct Raw { uint64_t next; uint32_t key, pad; uintptr_t storage; } raw;
        if (!Rd(node, raw)) return 0;
        if (raw.key == hash) {
            if (!Plausible(raw.storage)) return 0;
            cache_[nextCache_] = Known{registry, hash, raw.storage};
            nextCache_ = (nextCache_ + 1) % 16;
            return raw.storage;
        }
        cur = raw.next;
    }
    return 0;
}

bool Reader::PackedPosition(uintptr_t storage, uint32_t entity, uint32_t& packed) {
    const uint32_t index = entity & 0x3FFFFu;
    uintptr_t pages = 0, pagesEnd = 0, page = 0;
    if (!Plausible(storage) || !Rd(storage + kStorageSparse, pages) || !Rd(storage + kStorageSparseEnd, pagesEnd) ||
        !Plausible(pages) || pagesEnd < pages)
        return false;
    if ((uint64_t)(index >> 11) >= (pagesEnd - pages) / 8) return false;
    if (!Rd(pages + (uintptr_t)(index >> 11) * 8, page) || !Plausible(page) || !Rd(page + (uintptr_t)(index & 0x7FF) * 4, packed))
        return false;
    return ((entity & 0xFFFC0000u) ^ packed) < 0x3FFFFu;
}

uintptr_t Reader::ComponentIn(uintptr_t storage, uint32_t entity, uint32_t size) {
    uint32_t packed = 0;
    uintptr_t pages = 0, page = 0;
    if (!PackedPosition(storage, entity, packed) || !Rd(storage + kStorageComponents, pages) || !Plausible(pages) ||
        !Rd(pages + (uintptr_t)((packed & 0x3FF80u) >> 4), page) || !Plausible(page))
        return 0;
    return page + (uintptr_t)(packed & 0x7Fu) * size;
}

bool Reader::LocalPlayer(Actor& out) {
    uintptr_t ci = 0;
    if (!ClientInstance(ci)) return false;
    struct Ref { uintptr_t registry, control; uint32_t entity; } ref{};
    if (!Rd(ci + lay_.clientLocalPlayer, ref) || !Plausible(ref.registry) || !Plausible(ref.control) ||
        (ref.entity & 0x3FFFFu) == 0x3FFFFu)
        return false;
    const uintptr_t registry = ref.registry + kEnttInRegistry;
    uintptr_t owner = 0, actor = 0, vt = 0;
    if (!(owner = ComponentIn(ResolveStorage(registry, game::kActorOwnerComponent), ref.entity, 8)) || !Rd(owner, actor) ||
        !Plausible(actor) || !Rd(actor, vt) || !InImage(vt))
        return false;
    struct Head { uintptr_t registry; uint32_t entity; } head{};
    if (!Rd(actor + lay_.actorRegistry, head) || head.registry != registry || head.entity != ref.entity) return false;
    out.ptr = actor;
    out.registry = registry;
    out.entity = ref.entity;
    return true;
}

// The owner components lie in pages in the same order as the entity ids in `packed`.
bool Reader::Actors(const Actor& me, std::vector<ActorRef>& out) {
    out.clear();
    const uintptr_t storage = ResolveStorage(me.registry, game::kActorOwnerComponent);
    uintptr_t packed[2] = {}, pages = 0;
    if (!storage || !Rd(storage + kStoragePacked, packed) || !Rd(storage + kStorageComponents, pages) ||
        !Plausible(packed[0]) || !Plausible(pages) || packed[1] <= packed[0])
        return false;
    const size_t count = (size_t)(packed[1] - packed[0]) / sizeof(uint32_t);
    if (count > 1u << 16) return false;
    entities_.resize(count);
    if (!Bytes(packed[0], entities_.data(), count * sizeof(uint32_t))) return false;
    const size_t pageCount = (count - 1) / kComponentPage + 1;
    std::vector<uintptr_t> pageList(pageCount);
    if (!Bytes(pages, pageList.data(), pageCount * sizeof(uintptr_t))) return false;
    uintptr_t page[kComponentPage];
    for (size_t p = 0; p < pageCount; ++p) {
        const size_t first = p * kComponentPage, n = std::min(count - first, kComponentPage);
        if (!Plausible(pageList[p]) || !Bytes(pageList[p], page, n * sizeof(uintptr_t))) continue;
        for (size_t i = 0; i < n; ++i)
            if ((entities_[first + i] & kEntityIndex) != kEntityIndex && Plausible(page[i]))
                out.push_back({page[i], entities_[first + i]});
    }
    return !out.empty();
}

bool Reader::ReadMotion(uintptr_t actor, Motion& out) {
    uintptr_t sv = 0;
    float f[9];
    if (!Plausible(actor) || !Rd(actor + lay_.actorStateVector, sv) || !Plausible(sv) || !Rd(sv, f) || !LooksLikeMotion(f))
        return false;
    std::memcpy(out.pos, f, sizeof out.pos);
    std::memcpy(out.last, f + 3, sizeof out.last);
    std::memcpy(out.vel, f + 6, sizeof out.vel);
    out.state = sv;
    return true;
}

bool Reader::ReadBox(uintptr_t actor, float (&mn)[3], float (&mx)[3], uintptr_t* component) {
    uintptr_t ab = 0;
    float f[6];
    if (!Plausible(actor) || !Rd(actor + lay_.actorAabb, ab) || !Plausible(ab) || !Rd(ab, f)) return false;
    for (int k = 0; k < 3; ++k) {
        if (!std::isfinite(f[k]) || !std::isfinite(f[k + 3]) || f[k + 3] < f[k] || f[k + 3] - f[k] > 8.f ||
            std::fabs(f[k]) > 3.2e7f)
            return false;
        mn[k] = f[k];
        mx[k] = f[k + 3];
    }
    if (component) *component = ab;
    return true;
}

bool Reader::ReadRotation(uintptr_t actor, float& pitch, float& yaw) {
    uintptr_t rot = 0;
    float f[2];
    if (!Plausible(actor) || !Rd(actor + lay_.actorRotation, rot) || !Plausible(rot) || !Rd(rot, f) ||
        !std::isfinite(f[0]) || !std::isfinite(f[1]))
        return false;
    pitch = f[0];
    yaw = f[1];
    return true;
}

bool Reader::OnGround(const Actor& a, bool& on) {
    // A tag component: its storage exists once anything has stood. No storage is "do not know".
    const uintptr_t storage = ResolveStorage(a.registry, game::kOnGroundFlagComponent);
    if (!storage) return false;
    uint32_t packed = 0;
    on = PackedPosition(storage, a.entity, packed);
    return true;
}

// ActorDataFlagComponent: the ActorFlags bitset (24 bytes).
bool Reader::Flag(const Actor& a, int bit, bool& on) {
    const uintptr_t comp = ComponentIn(ResolveStorage(a.registry, game::kActorDataFlagComponent), a.entity, 24);
    uint8_t byte = 0;
    if (!comp || bit < 0 || bit >= 192 || !Rd(comp + (uintptr_t)(bit / 8), byte)) return false;
    on = (byte >> (bit % 8)) & 1;
    return true;
}

bool Reader::ReadString(uintptr_t at, std::string& out, size_t limit) {
    uint8_t raw[0x20];
    if (!Bytes(at, raw, sizeof raw)) return false;
    uint64_t size = 0, cap = 0;
    std::memcpy(&size, raw + 0x10, 8);
    std::memcpy(&cap, raw + 0x18, 8);
    if (size > 4096 || cap < size) return false;
    const size_t n = (size_t)std::min<uint64_t>(size, limit);
    std::string s(n, '\0');
    if (cap >= 16) {
        uintptr_t p = 0;
        std::memcpy(&p, raw, 8);
        if (!Plausible(p) || (n && !Bytes(p, s.data(), n))) return false;
    } else {
        std::memcpy(s.data(), raw, n);
    }
    out = s;
    return true;
}

bool Reader::OptionRecord(const char* name, uintptr_t& record) {
    uintptr_t ci = 0, options = 0;
    return ClientInstance(ci) && Rd(ci + lay_.clientOptions, options) && Plausible(options) && OptionIn(options, name, record);
}

bool Reader::OptionIn(uintptr_t options, const char* name, uintptr_t& record) {
    for (size_t id = 0; id < optrec::kTableIds; ++id) {
        uintptr_t option = 0, details = 0;
        std::string saved;
        if (!Rd(options + optrec::kTable + id * 8, option) || !Plausible(option) || !Rd(option + optrec::kDetails, details) ||
            !Plausible(details) || !ReadString(details + optrec::kSaveName, saved, 64) || saved != name)
            continue;
        // the getter's own walk
        for (int i = 0; i < 8; ++i) {
            uintptr_t standIn = 0;
            if (!Rd(option + optrec::kDetails, details) || !Plausible(details) || !Rd(details + optrec::kStandIn, standIn) ||
                !Plausible(standIn))
                break;
            option = standIn;
        }
        record = option;
        return true;
    }
    return false;
}

bool Reader::TypeId(uintptr_t actor, std::string& out) {
    return Plausible(actor) && ReadString(actor + lay_.actorTypeId, out, 96);
}

bool Reader::NameTag(uintptr_t actor, std::string& out) {
    return Plausible(actor) && ReadString(actor + lay_.actorNameTag, out, 160);
}

bool Reader::SelectedSlot(uintptr_t player, int& slot) {
    uintptr_t supplies = 0;
    int s = -1;
    if (!Plausible(player) || !Rd(player + lay_.playerSupplies, supplies) || !Plausible(supplies) ||
        !Rd(supplies + game::kSuppliesSelected, s) || s < 0 || s > 8)
        return false;
    slot = s;
    return true;
}

// The 36 ItemStacks of the inventory container. Its std::vector is found once, by being exactly
// 36 stacks long with stacks that read like stacks, and the offset is kept for that build.
uintptr_t Reader::InventoryItems(uintptr_t player) {
    uintptr_t supplies = 0, inventory = 0;
    if (!Plausible(player) || !Rd(player + lay_.playerSupplies, supplies) || !Plausible(supplies) ||
        !Rd(supplies + game::kSuppliesInventory, inventory) || !Plausible(inventory))
        return 0;
    auto itemsAt = [&](uintptr_t off) -> uintptr_t {
        uintptr_t v[2] = {};
        if (!Rd(inventory + off, v) || !Plausible(v[0]) || v[1] - v[0] != game::kInventorySlots * game::kItemStackSize)
            return 0;
        uintptr_t vt = 0;
        uint8_t count = 0;
        if (!Rd(v[0], vt) || !InImage(vt) || !Rd(v[0] + game::kStackCount, count) || count > 64) return 0;
        return v[0];
    };
    if (itemsOff_ && itemsFor_ == inventory)
        if (const uintptr_t items = itemsAt(itemsOff_)) return items;
    for (uintptr_t off = 0x08; off <= 0x200; off += 8) {
        if (const uintptr_t items = itemsAt(off)) {
            if (itemsOff_ != off) Log("[bedrock] inventory stacks at +0x%llX", (unsigned long long)off);
            itemsOff_ = off;
            itemsFor_ = inventory;
            return items;
        }
    }
    return 0;
}

bool Reader::HotbarItem(uintptr_t player, int slot, Item& out) {
    out = Item{};
    if (slot < 0 || slot > 8) return false;
    const uintptr_t items = InventoryItems(player);
    if (!items) return false;
    const uintptr_t stack = items + (uintptr_t)slot * game::kItemStackSize;
    struct Raw { int16_t aux; uint8_t count, valid; } raw{};
    if (!Rd(stack + game::kStackAux, raw)) return false;
    uintptr_t weak = 0, item = 0;
    if (!raw.valid || !raw.count || !Rd(stack + game::kStackItem, weak) || !Plausible(weak) || !Rd(weak, item) ||
        !Plausible(item))
        return true;
    if (!ReadString(item + game::kItemName, out.name, 96)) out.name.clear();
    out.count = raw.count;
    out.aux = raw.aux;
    return true;
}

}  // namespace citron::bedrock
