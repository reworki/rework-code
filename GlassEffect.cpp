#include "GlassEffect.h"
#include <QWidget>

#ifdef Q_OS_WIN
#  include <windows.h>
#  include <dwmapi.h>
#  include <QLibrary>
#endif

namespace GlassEffect {
bool applyGlass(QWidget *window, bool enabled) {
    if (!window || !window->isWindow()) return false;
#ifdef Q_OS_WIN
    // Resolve dynamically: unsupported Windows versions remain usable.
    QLibrary dwm(QStringLiteral("dwmapi"));
    if (!dwm.load()) return false;
    using SetAttribute = HRESULT (WINAPI *)(HWND, DWORD, LPCVOID, DWORD);
    using ExtendFrame = HRESULT (WINAPI *)(HWND, const MARGINS *);
    auto setAttribute = reinterpret_cast<SetAttribute>(dwm.resolve("DwmSetWindowAttribute"));
    auto extendFrame = reinterpret_cast<ExtendFrame>(dwm.resolve("DwmExtendFrameIntoClientArea"));
    if (!setAttribute || !extendFrame) return false;
    HWND hwnd = reinterpret_cast<HWND>(window->winId());
    if (!hwnd) return false;
    // 20 = DWMWA_USE_IMMERSIVE_DARK_MODE; 38 = DWMWA_SYSTEMBACKDROP_TYPE.
    // Numeric values keep compilation compatible with older MinGW SDKs.
    constexpr DWORD darkModeAttribute = 20;
    constexpr DWORD backdropAttribute = 38;
    constexpr int backdropNone = 1;      // DWMSBT_NONE
    constexpr int backdropAcrylic = 3;   // DWMSBT_TRANSIENTWINDOW
    const BOOL dark = TRUE;
    setAttribute(hwnd, darkModeAttribute, &dark, sizeof(dark));
    const int type = enabled ? backdropAcrylic : backdropNone;
    const HRESULT backdropResult = setAttribute(hwnd, backdropAttribute, &type, sizeof(type));
    if (FAILED(backdropResult)) return false;
    MARGINS margins = enabled ? MARGINS{-1,-1,-1,-1} : MARGINS{0,0,0,0};
    const HRESULT frameResult = extendFrame(hwnd, &margins);
    if (FAILED(frameResult)) {
        const int none = backdropNone;
        setAttribute(hwnd, backdropAttribute, &none, sizeof(none));
        return false;
    }
    window->update();
    return true;
#else
    Q_UNUSED(enabled);
    return false;
#endif
}
}
