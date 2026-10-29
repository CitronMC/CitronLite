#include "../../backend/backend_impl.h"
#include <windows.h>
#include <algorithm>

namespace citron {

// Stream Hide keeps this window out of screen capture; Taskbar Hide takes its taskbar button away
// and, with Also Hide Window, the window too.
void Backend::Impl::WindowFeaturesTick(const Modules& m) {
    if (!hwnd || !IsWindow(hwnd)) {
        streamHideKnown = false;
        return;
    }
    const bool hide = On(m, "streamhide");
    if (!streamHideKnown || hide != lastStreamHide) {
        const auto now = clock_t_::now();
        if (hide != streamHideRequest) {
            streamHideRequest = hide;
            streamHideRetries = 0;
            streamHideRetryAt = {};
        }
        if (now >= streamHideRetryAt) {
            const DWORD wanted = hide ? 0x11u : 0x00u;   // WDA_EXCLUDEFROMCAPTURE
            DWORD applied = 0xFFFFFFFFu;
            bool ok = SetWindowDisplayAffinity(hwnd, wanted) && GetWindowDisplayAffinity(hwnd, &applied) && applied == wanted;
            // Before Windows 10 2004 only "show as black" exists.
            if (!ok && hide) ok = SetWindowDisplayAffinity(hwnd, 0x01u) && GetWindowDisplayAffinity(hwnd, &applied) && applied == 0x01u;
            if (ok) {
                lastStreamHide = hide;
                streamHideKnown = true;
                streamHideRetries = 0;
            } else {
                streamHideKnown = false;
                const unsigned ms = std::min(5000u, 250u << std::min(streamHideRetries, 4u));
                ++streamHideRetries;
                streamHideRetryAt = now + std::chrono::milliseconds(ms);
            }
        }
    }

    const ModuleSnapshot* t = Mod(m, "taskbarhide");
    const bool taskbar = t && t->on;
    const bool window = taskbar && Check(*t, "hideWindow", false);
    const LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    const bool isTool = (ex & WS_EX_TOOLWINDOW) && !(ex & WS_EX_APPWINDOW);
    const bool isHidden = !IsWindowVisible(hwnd);
    if (taskbar != isTool || window != isHidden) {
        const WPARAM wp = (taskbar ? platform::CITRON_TASKBAR_TOOLWINDOW : 0) | (window ? platform::CITRON_TASKBAR_HIDEWINDOW : 0);
        PostMessageW(hwnd, platform::WM_CITRON_TASKBARHIDE, wp, 0);
    }
}

}  // namespace citron
