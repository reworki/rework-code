#pragma once

class QWidget;

// Enables the Windows 11 system-drawn Desktop Acrylic backdrop for a top-level
// Qt window. Returns true when DWM accepted the request, not a visual guarantee.
// DWM chooses the material and tint; this API does not fake the effect with a
// transparent screenshot or override the user's Windows accessibility settings.
// The caller must paint the relevant Qt surfaces with alpha; opaque widget
// backgrounds completely cover the system backdrop.
//
// Call after constructing the window and again after native-window recreation.
// When translucency is required, Qt's supported Windows configuration is to set
// Qt::WA_TranslucentBackground before showing the widget and use a frameless
// window with user-provided window controls. Do not dynamically toggle those
// window attributes after show. A normal decorated opaque window is a safe
// fallback, but will only expose the backdrop where its client area permits it.
namespace GlassEffect {
bool applyGlass(QWidget *window, bool enabled = true);
}
