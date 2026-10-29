// Updates: a manifest on citron.lol names the newest build and its hash; the build is downloaded,
// checked and swapped in for the next launch.
#pragma once
#include <string>

namespace citron::updater {

// Where the update is. The settings card shows exactly this.
enum class Stage {
    Idle,          // nothing has been checked this run
    Checking,
    UpToDate,
    Available,     // a newer release exists and has not been fetched (the switch is off)
    Downloading,
    Ready,         // the new build is in place; the next launch runs it
    Failed
};

struct Status {
    Stage stage = Stage::Idle;
    std::string latest;   // "2.0.1" once a check has answered
    std::string detail;   // one line for the user
    int percent = 0;      // 0..100 while downloading
};

// Sweeps the files a previous update left behind, then checks if the switch is on.
void Start();
void Stop();

bool Enabled();
void SetEnabled(bool enabled);

Status Get();

// What the buttons do. CheckNow works with automatic updates off.
void CheckNow();
void InstallNow();

// Starts the installed build, which waits for this process to end. True when the caller should quit.
bool RestartToApply();

} // namespace citron::updater
