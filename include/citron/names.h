// What a player's name tag says: the team colour it starts with, the name in it, and whether it
// belongs to someone on the Friends list.
#pragma once
#include <cctype>
#include <string>
#include <vector>

namespace citron::names {

// A colour code's team, by the colour it looks: Minecraft's own codes and Bedrock's material ones.
inline int Family(char c) {
    switch (c) {
    case 'c': case '4': case 'm': return 1;   // red
    case '9': case '1': case 't': return 2;   // blue
    case 'a': case '2': case 'q': return 3;   // green
    case 'e': return 4;                       // yellow
    case 'b': case '3': case 's': return 5;   // aqua
    case 'd': return 6;                       // pink
    case '5': case 'u': return 7;             // purple
    case '6': case 'n': case 'v': return 8;   // orange
    case 'f': case 'h': case 'i': return 9;   // white
    case '7': case '8': case 'j': return 10;  // gray
    case '0': return 11;                      // black
    case 'g': case 'p': return 12;            // gold
    default: return 0;
    }
}

// The first colour code in the tag that is a colour (not bold, italic, obfuscated or reset).
inline int TeamOf(const std::string& tag) {
    for (size_t i = 0; i + 2 < tag.size(); ++i) {
        if ((unsigned char)tag[i] != 0xC2 || (unsigned char)tag[i + 1] != 0xA7) continue;
        const char c = (char)std::tolower((unsigned char)tag[i + 2]);
        if (c == 'k' || c == 'l' || c == 'o' || c == 'r') continue;
        if (const int f = Family(c)) return f;
    }
    return 0;
}

// The tag as it reads: colour codes gone, first line only, trimmed.
inline std::string Plain(const std::string& tag) {
    std::string out;
    for (size_t i = 0; i < tag.size(); ++i) {
        if (tag[i] == '\n') break;
        if ((unsigned char)tag[i] == 0xC2 && i + 2 < tag.size() && (unsigned char)tag[i + 1] == 0xA7) {
            i += 2;
            continue;
        }
        out += tag[i];
    }
    const size_t a = out.find_first_not_of(" \t"), b = out.find_last_not_of(" \t");
    return a == std::string::npos ? std::string() : out.substr(a, b - a + 1);
}

// The name a friend entry should hold: the plain tag without [rank] or (tag) groups.
inline std::string FriendName(const std::string& tag) {
    std::string plain = Plain(tag), out;
    int depth = 0;
    for (char c : plain) {
        if (c == '[' || c == '(') {
            ++depth;
            continue;
        }
        if ((c == ']' || c == ')') && depth) {
            --depth;
            continue;
        }
        if (!depth) out += c;
    }
    std::string name;
    bool space = false;
    for (char c : out) {
        if (std::isspace((unsigned char)c)) {
            space = !name.empty();
            continue;
        }
        if (space) name += ' ';
        space = false;
        name += c;
    }
    return name;
}

// Whether a plain tag holds one of the names as a whole name, not as part of a longer one.
inline bool OnList(const std::vector<std::string>& list, const std::string& plainTag) {
    if (list.empty() || plainTag.empty()) return false;
    auto lower = [](std::string s) {
        for (char& c : s) c = (char)std::tolower((unsigned char)c);
        return s;
    };
    const std::string tag = lower(plainTag);
    auto edge = [&](size_t i) { return i >= tag.size() || !(std::isalnum((unsigned char)tag[i]) || tag[i] == '_'); };
    for (const std::string& n : list) {
        if (n.empty()) continue;
        const std::string want = lower(n);
        for (size_t at = tag.find(want); at != std::string::npos; at = tag.find(want, at + 1))
            if ((at == 0 || edge(at - 1)) && edge(at + want.size())) return true;
    }
    return false;
}

}  // namespace citron::names
