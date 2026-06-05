#include "OverlayNotificationManager.h"

#include "AppState.h"

#include <dwmapi.h>
#include <strsafe.h>

#include <algorithm>
#include <deque>
#include <vector>

#pragma comment(lib, "Dwmapi.lib")

namespace FFKeyLock
{
namespace
{
enum class OverlayKind
{
    Info,
    Success,
    Warning,
    Error
};

struct OverlayItem
{
    OverlayKind kind = OverlayKind::Info;
    std::wstring title;
    std::wstring message;
};

constexpr wchar_t kOverlayClassName[] = L"FFKeyLockOverlayNotification";
constexpr UINT_PTR kAnimationTimer = 91;
constexpr DWORD kFadeInMs = 220;
constexpr DWORD kHoldMs = 2400;
constexpr DWORD kFadeOutMs = 320;
constexpr int kWidth = 344;
constexpr int kHeight = 72;

std::deque<OverlayItem> g_queue;
OverlayItem g_current;
HWND g_overlayWindow = nullptr;
DWORD g_startedAt = 0;
bool g_classRegistered = false;

int ScaleForDpi(int value, UINT dpi)
{
    return MulDiv(value, static_cast<int>(dpi ? dpi : USER_DEFAULT_SCREEN_DPI), USER_DEFAULT_SCREEN_DPI);
}

COLORREF StatusColor(OverlayKind kind)
{
    switch (kind)
    {
    case OverlayKind::Success:
        return RGB(34, 197, 94);
    case OverlayKind::Warning:
        return RGB(234, 179, 8);
    case OverlayKind::Error:
        return RGB(239, 68, 68);
    case OverlayKind::Info:
    default:
        return RGB(86, 156, 214);
    }
}

const wchar_t* StatusGlyph(OverlayKind kind)
{
    switch (kind)
    {
    case OverlayKind::Success:
        return L"\u2713";
    case OverlayKind::Warning:
        return L"!";
    case OverlayKind::Error:
        return L"\u00d7";
    case OverlayKind::Info:
    default:
        return L"i";
    }
}

HFONT CreateOverlayFont(UINT dpi, int pointSize, int weight)
{
    LOGFONTW font{};
    font.lfHeight = -MulDiv(pointSize, static_cast<int>(dpi), 72);
    font.lfWeight = weight;
    font.lfQuality = CLEARTYPE_QUALITY;
    StringCchCopyW(font.lfFaceName, std::size(font.lfFaceName), L"Segoe UI");
    return CreateFontIndirectW(&font);
}

BYTE BlendByte(BYTE dst, BYTE src, BYTE alpha)
{
    return static_cast<BYTE>((src * alpha + dst * (255 - alpha)) / 255);
}

void BlendPixel(unsigned char* pixel, COLORREF color, BYTE alpha)
{
    pixel[0] = BlendByte(pixel[0], GetBValue(color), alpha);
    pixel[1] = BlendByte(pixel[1], GetGValue(color), alpha);
    pixel[2] = BlendByte(pixel[2], GetRValue(color), alpha);
    pixel[3] = static_cast<BYTE>(std::min<int>(255, alpha + pixel[3] * (255 - alpha) / 255));
}

void FillRoundedRect(std::vector<unsigned char>& pixels, int width, int height, const RECT& rect, int radius, COLORREF color, BYTE alpha)
{
    const int radiusSq = radius * radius;
    const int left = static_cast<int>(rect.left);
    const int top = static_cast<int>(rect.top);
    const int right = static_cast<int>(rect.right);
    const int bottom = static_cast<int>(rect.bottom);
    for (int y = std::max(0, top); y < std::min(height, bottom); ++y)
    {
        for (int x = std::max(0, left); x < std::min(width, right); ++x)
        {
            int cx = x;
            int cy = y;
            if (x < left + radius)
            {
                cx = left + radius;
            }
            else if (x >= right - radius)
            {
                cx = right - radius - 1;
            }

            if (y < top + radius)
            {
                cy = top + radius;
            }
            else if (y >= bottom - radius)
            {
                cy = bottom - radius - 1;
            }

            const int dx = x - cx;
            const int dy = y - cy;
            if (dx * dx + dy * dy <= radiusSq)
            {
                BlendPixel(&pixels[(static_cast<size_t>(y) * width + x) * 4], color, alpha);
            }
        }
    }
}

void FillCircle(std::vector<unsigned char>& pixels, int width, int height, int centerX, int centerY, int radius, COLORREF color, BYTE alpha)
{
    const int radiusSq = radius * radius;
    for (int y = std::max(0, centerY - radius); y < std::min(height, centerY + radius + 1); ++y)
    {
        for (int x = std::max(0, centerX - radius); x < std::min(width, centerX + radius + 1); ++x)
        {
            const int dx = x - centerX;
            const int dy = y - centerY;
            if (dx * dx + dy * dy <= radiusSq)
            {
                BlendPixel(&pixels[(static_cast<size_t>(y) * width + x) * 4], color, alpha);
            }
        }
    }
}

void FixTextAlpha(std::vector<unsigned char>& pixels)
{
    for (size_t i = 0; i + 3 < pixels.size(); i += 4)
    {
        if (pixels[i + 3] == 0 && (pixels[i] || pixels[i + 1] || pixels[i + 2]))
        {
            pixels[i + 3] = 255;
        }
    }
}

BYTE CurrentOpacity()
{
    const DWORD elapsed = GetTickCount() - g_startedAt;
    if (elapsed < kFadeInMs)
    {
        return static_cast<BYTE>(std::min<DWORD>(255, elapsed * 255 / kFadeInMs));
    }

    const DWORD fadeOutStart = kFadeInMs + kHoldMs;
    if (elapsed < fadeOutStart)
    {
        return 255;
    }

    const DWORD fadeElapsed = elapsed - fadeOutStart;
    if (fadeElapsed >= kFadeOutMs)
    {
        return 0;
    }

    return static_cast<BYTE>(255 - fadeElapsed * 255 / kFadeOutMs);
}

POINT OverlayPosition(int width, int height)
{
    HWND anchorWindow = GetForegroundWindow();
    HMONITOR monitor = MonitorFromWindow(anchorWindow ? anchorWindow : g_hWnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    GetMonitorInfoW(monitor, &monitorInfo);
    const RECT work = monitorInfo.rcMonitor;
    return { work.right - width - 28, work.bottom - height - 36 };
}

void RenderOverlay()
{
    if (!g_overlayWindow)
    {
        return;
    }

    const UINT dpi = GetDpiForWindow(g_overlayWindow);
    const int width = ScaleForDpi(kWidth, dpi);
    const int height = ScaleForDpi(kHeight, dpi);
    const int radius = ScaleForDpi(18, dpi);

    std::vector<unsigned char> pixels(static_cast<size_t>(width) * height * 4, 0);
    RECT card{ 0, 0, width, height };
    FillRoundedRect(pixels, width, height, card, radius, RGB(20, 20, 20), 218);

    const COLORREF accent = StatusColor(g_current.kind);
    const int centerY = height / 2;
    FillCircle(pixels, width, height, ScaleForDpi(34, dpi), centerY, ScaleForDpi(14, dpi), accent, 255);

    BITMAPINFO bitmapInfo{};
    bitmapInfo.bmiHeader.biSize = sizeof(bitmapInfo.bmiHeader);
    bitmapInfo.bmiHeader.biWidth = width;
    bitmapInfo.bmiHeader.biHeight = -height;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;

    HDC screen = GetDC(nullptr);
    HDC memoryDc = CreateCompatibleDC(screen);
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(screen, &bitmapInfo, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ oldBitmap = SelectObject(memoryDc, bitmap);
    CopyMemory(bits, pixels.data(), pixels.size());

    HFONT titleFont = CreateOverlayFont(dpi, 10, FW_SEMIBOLD);
    HFONT bodyFont = CreateOverlayFont(dpi, 9, FW_NORMAL);
    SetBkMode(memoryDc, TRANSPARENT);
    SetTextColor(memoryDc, RGB(255, 255, 255));

    RECT iconRect{ ScaleForDpi(26, dpi), centerY - ScaleForDpi(10, dpi), ScaleForDpi(42, dpi), centerY + ScaleForDpi(10, dpi) };
    HGDIOBJ oldFont = SelectObject(memoryDc, titleFont);
    DrawTextW(memoryDc, StatusGlyph(g_current.kind), -1, &iconRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    if (g_current.message.empty())
    {
        RECT titleRect{ ScaleForDpi(58, dpi), 0, width - ScaleForDpi(20, dpi), height };
        DrawTextW(memoryDc, g_current.title.c_str(), -1, &titleRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    else
    {
        RECT titleRect{ ScaleForDpi(58, dpi), centerY - ScaleForDpi(22, dpi), width - ScaleForDpi(20, dpi), centerY - ScaleForDpi(1, dpi) };
        DrawTextW(memoryDc, g_current.title.c_str(), -1, &titleRect, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

        SetTextColor(memoryDc, RGB(224, 224, 224));
        SelectObject(memoryDc, bodyFont);
        RECT bodyRect{ ScaleForDpi(58, dpi), centerY + ScaleForDpi(2, dpi), width - ScaleForDpi(20, dpi), centerY + ScaleForDpi(23, dpi) };
        DrawTextW(memoryDc, g_current.message.c_str(), -1, &bodyRect, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    GdiFlush();

    CopyMemory(pixels.data(), bits, pixels.size());
    FixTextAlpha(pixels);
    CopyMemory(bits, pixels.data(), pixels.size());

    POINT source{ 0, 0 };
    SIZE size{ width, height };
    POINT destination = OverlayPosition(width, height);
    BLENDFUNCTION blend{ AC_SRC_OVER, 0, CurrentOpacity(), AC_SRC_ALPHA };
    UpdateLayeredWindow(g_overlayWindow, screen, &destination, &size, memoryDc, &source, 0, &blend, ULW_ALPHA);

    SelectObject(memoryDc, oldFont);
    SelectObject(memoryDc, oldBitmap);
    DeleteObject(titleFont);
    DeleteObject(bodyFont);
    DeleteObject(bitmap);
    DeleteDC(memoryDc);
    ReleaseDC(nullptr, screen);
}

void ShowNext();

LRESULT CALLBACK OverlayProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
    {
        MARGINS margins{ -1 };
        DwmExtendFrameIntoClientArea(hwnd, &margins);
        SetTimer(hwnd, kAnimationTimer, 16, nullptr);
        return 0;
    }

    case WM_TIMER:
        if (wParam == kAnimationTimer)
        {
            if (CurrentOpacity() == 0)
            {
                DestroyWindow(hwnd);
                return 0;
            }
            RenderOverlay();
            return 0;
        }
        break;

    case WM_NCHITTEST:
        return HTTRANSPARENT;

    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;

    case WM_DESTROY:
        KillTimer(hwnd, kAnimationTimer);
        if (g_overlayWindow == hwnd)
        {
            g_overlayWindow = nullptr;
            ShowNext();
        }
        return 0;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

void EnsureOverlayClass()
{
    if (g_classRegistered)
    {
        return;
    }

    WNDCLASSEXW wcex{};
    wcex.cbSize = sizeof(wcex);
    wcex.lpfnWndProc = OverlayProc;
    wcex.hInstance = g_hInst;
    wcex.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wcex.lpszClassName = kOverlayClassName;
    g_classRegistered = RegisterClassExW(&wcex) != 0;
}

void ShowNext()
{
    if (g_overlayWindow || g_queue.empty())
    {
        return;
    }

    EnsureOverlayClass();
    if (!g_classRegistered)
    {
        return;
    }

    g_current = g_queue.front();
    g_queue.pop_front();
    g_startedAt = GetTickCount();

    g_overlayWindow = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kOverlayClassName,
        L"FFKeyLock Overlay",
        WS_POPUP,
        0,
        0,
        1,
        1,
        nullptr,
        nullptr,
        g_hInst,
        nullptr);

    if (!g_overlayWindow)
    {
        return;
    }

    SetWindowPos(g_overlayWindow, HWND_TOPMOST, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    ShowWindow(g_overlayWindow, SW_SHOWNOACTIVATE);
    RenderOverlay();
}

void Enqueue(OverlayKind kind, const std::wstring& title, const std::wstring& message)
{
    g_queue.push_back({ kind, title, message });
    ShowNext();
}
}

void OverlayNotificationManager::ShowInfo(const std::wstring& title, const std::wstring& message)
{
    Enqueue(OverlayKind::Info, title, message);
}

void OverlayNotificationManager::ShowSuccess(const std::wstring& title, const std::wstring& message)
{
    Enqueue(OverlayKind::Success, title, message);
}

void OverlayNotificationManager::ShowWarning(const std::wstring& title, const std::wstring& message)
{
    Enqueue(OverlayKind::Warning, title, message);
}

void OverlayNotificationManager::ShowError(const std::wstring& title, const std::wstring& message)
{
    Enqueue(OverlayKind::Error, title, message);
}

void OverlayNotificationManager::Shutdown()
{
    g_queue.clear();
    if (g_overlayWindow)
    {
        DestroyWindow(g_overlayWindow);
        g_overlayWindow = nullptr;
    }
}
}
