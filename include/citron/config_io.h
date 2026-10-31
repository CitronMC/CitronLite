// Configs: one small text file per profile in %LOCALAPPDATA%\CitronLite, easy to read and share.
//
//   citron-config 2
//   accent C8F135
//   [aimassist]
//   on 1
//   key X              (none for no bind)
//   s strength 55
//   c onlyLmb 1
//   h aimpart 0
//   n Some Name        (Friends' list, one name a line)
#pragma once
#include "citron/model.h"
#include <map>
#include <string>
#include <vector>

namespace citron::cfgio {

std::string Dir();             // ends with a separator, created on first call
std::wstring DirW();
std::string PathFor(const std::string& name);
std::vector<std::string> List();
std::string StemOf(const std::string& path);

std::string Serialize(const std::map<std::string, ModuleState>& mods, unsigned accent);
// Reads over `mods`: what a file names replaces the state there, what it does not name stays.
bool Deserialize(const char* text, std::map<std::string, ModuleState>& mods, unsigned* accent = nullptr);

// Keeps what the catalog has, at its defaults where the file said nothing: retired modules and
// settings go, values outside a slider's ends come back inside them.
void KeepKnown(std::map<std::string, ModuleState>& mods, const std::vector<ModuleDef>& defs);

bool Write(const std::string& path, const std::map<std::string, ModuleState>& mods, unsigned accent);
bool Read(const std::string& path, std::map<std::string, ModuleState>& mods, unsigned* accent = nullptr);
void Delete(const std::string& name);
// What the list shows about a config without loading it.
struct Info { int enabled = 0; long long savedAt = 0; };
Info Peek(const std::string& name);

std::string SaveDialog(const std::string& defaultName);
std::string OpenDialog();
void OpenFolder();

}  // namespace citron::cfgio
