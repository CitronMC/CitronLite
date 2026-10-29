// The update manifest and the rules for believing it, in a header so the tests can reach it: it
// decides which file the client downloads and runs next.
#pragma once
#include <string>

namespace citron::updater {

// The manifest names a full URL, and only a file on this host is accepted.
inline constexpr char kUpdateOrigin[] = "https://citron.lol/";

struct Release {
    int version = 0;        // the integer compared against citron::kVersion
    std::string name;       // "2.0.1", for the user to read
    std::string sha256;     // lowercase hex, checked over the download
    std::string path;       // path on the update host, taken from the manifest url
};

namespace detail {

inline bool FieldStart(const std::string& text, const char* key, size_t& pos) {
    const std::string needle = std::string("\"") + key + "\"";
    const size_t at = text.find(needle);
    if (at == std::string::npos) return false;
    size_t i = at + needle.size();
    auto space = [&] { while (i < text.size() && (text[i] == ' ' || text[i] == '\t' ||
                                                  text[i] == '\r' || text[i] == '\n')) ++i; };
    space();
    if (i >= text.size() || text[i] != ':') return false;
    ++i;
    space();
    pos = i;
    return i < text.size();
}

inline bool ReadNumber(const std::string& text, const char* key, int& out) {
    size_t i = 0;
    if (!FieldStart(text, key, i)) return false;
    if (text[i] < '0' || text[i] > '9') return false;
    long long value = 0;
    while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
        value = value * 10 + (text[i++] - '0');
        if (value > 1000000000LL) return false;
    }
    out = (int)value;
    return true;
}

inline bool ReadString(const std::string& text, const char* key, std::string& out) {
    size_t i = 0;
    if (!FieldStart(text, key, i)) return false;
    if (text[i] != '"') return false;
    ++i;
    std::string value;
    while (i < text.size() && text[i] != '"') {
        if (text[i] == '\\') return false;      // no escape is expected in any of these fields
        value.push_back(text[i++]);
        if (value.size() > 512) return false;
    }
    if (i >= text.size()) return false;
    out = value;
    return true;
}

inline bool LooksLikeSha256(const std::string& hex) {
    if (hex.size() != 64) return false;
    for (const char c : hex)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}

} // namespace detail

// Reads the four fields Lite needs. Just enough JSON for a flat object, and it refuses anything it
// does not fully understand rather than guessing at it.
inline bool ParseManifest(const std::string& text, Release& out, std::string& error) {
    Release release;
    std::string url;
    if (!detail::ReadNumber(text, "version", release.version) ||
        !detail::ReadString(text, "versionName", release.name) ||
        !detail::ReadString(text, "sha256", release.sha256) ||
        !detail::ReadString(text, "url", url)) {
        error = "The update file is malformed";
        return false;
    }
    if (!detail::LooksLikeSha256(release.sha256)) {
        error = "The update file is malformed";
        return false;
    }
    if (release.version <= 0 || release.name.empty() || release.name.size() > 32) {
        error = "The update file is malformed";
        return false;
    }

    // The download stays on the update host: what is downloaded replaces the client itself.
    const std::string origin(kUpdateOrigin);
    if (url.size() <= origin.size() || url.compare(0, origin.size(), origin) != 0) {
        error = "The update points somewhere unexpected";
        return false;
    }
    release.path = url.substr(origin.size() - 1);   // keep the leading slash
    if (release.path.find("..") != std::string::npos ||
        release.path.find('\\') != std::string::npos) {
        error = "The update file is malformed";
        return false;
    }

    out = release;
    return true;
}

} // namespace citron::updater
