#include "OverlayNotificationManager.h"

#include "AppState.h"

#include <d2d1.h>
#include <dwmapi.h>
#include <dwrite.h>
#include <mmsystem.h>
#include <strsafe.h>

#include <algorithm>
#include <cmath>
#include <deque>
#include <vector>

#pragma comment(lib, "Dwmapi.lib")
#pragma comment(lib, "D2d1.lib")
#pragma comment(lib, "Dwrite.lib")
#pragma comment(lib, "Winmm.lib")

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
constexpr DWORD kFadeInMs = 260;
constexpr DWORD kHoldMs = 2200;
constexpr DWORD kFadeOutMs = 260;
constexpr int kWidth = 380;
constexpr int kHeight = 88;
constexpr int kBitmapPadding = 12;
constexpr UINT kAnimationFrameMs = 8;

std::deque<OverlayItem> g_queue;
OverlayItem g_current;
HWND g_overlayWindow = nullptr;
DWORD g_startedAt = 0;
bool g_classRegistered = false;
bool g_holdTimerArmed = false;
bool g_timerResolutionRaised = false;

struct OverlayMetrics
{
    int cardWidth = 0;
    int cardHeight = 0;
    int padding = 0;
    int bitmapWidth = 0;
    int bitmapHeight = 0;
};

int ScaleForDpi(int value, UINT dpi)
{
    return MulDiv(value, static_cast<int>(dpi ? dpi : USER_DEFAULT_SCREEN_DPI), USER_DEFAULT_SCREEN_DPI);
}

template <typename T>
void SafeRelease(T*& object)
{
    if (object)
    {
        object->Release();
        object = nullptr;
    }
}

D2D1_COLOR_F D2DColor(BYTE r, BYTE g, BYTE b, float alpha = 1.0f)
{
    return D2D1::ColorF(r / 255.0f, g / 255.0f, b / 255.0f, alpha);
}

D2D1_COLOR_F StatusD2DColor(OverlayKind kind)
{
    UNREFERENCED_PARAMETER(kind);
    return D2DColor(0, 120, 212, 1.0f);
}

OverlayMetrics GetOverlayMetrics(UINT dpi)
{
    OverlayMetrics metrics{};
    metrics.cardWidth = ScaleForDpi(kWidth, dpi);
    metrics.cardHeight = ScaleForDpi(kHeight, dpi);
    metrics.padding = ScaleForDpi(kBitmapPadding, dpi);
    metrics.bitmapWidth = metrics.cardWidth + metrics.padding * 2;
    metrics.bitmapHeight = metrics.cardHeight + metrics.padding * 2;
    return metrics;
}

COLORREF StatusColor(OverlayKind kind)
{
    UNREFERENCED_PARAMETER(kind);
    return RGB(0, 120, 212);
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

struct OverlayRenderer
{
    ID2D1Factory* d2dFactory = nullptr;
    IDWriteFactory* dwriteFactory = nullptr;
    ID2D1DCRenderTarget* renderTarget = nullptr;
    ID2D1SolidColorBrush* backgroundBrush = nullptr;
    ID2D1SolidColorBrush* borderBrush = nullptr;
    ID2D1SolidColorBrush* accentBrush = nullptr;
    ID2D1SolidColorBrush* titleBrush = nullptr;
    ID2D1SolidColorBrush* bodyBrush = nullptr;
    ID2D1SolidColorBrush* iconBrush = nullptr;
    IDWriteTextFormat* titleFormat = nullptr;
    IDWriteTextFormat* bodyFormat = nullptr;
    IDWriteTextFormat* iconFormat = nullptr;
    UINT dpi = 0;
    int width = 0;
    int height = 0;

    ~OverlayRenderer()
    {
        Reset();
    }

    void Reset()
    {
        SafeRelease(iconFormat);
        SafeRelease(bodyFormat);
        SafeRelease(titleFormat);
        SafeRelease(iconBrush);
        SafeRelease(bodyBrush);
        SafeRelease(titleBrush);
        SafeRelease(accentBrush);
        SafeRelease(borderBrush);
        SafeRelease(backgroundBrush);
        SafeRelease(renderTarget);
        SafeRelease(dwriteFactory);
        SafeRelease(d2dFactory);
        dpi = 0;
        width = 0;
        height = 0;
    }

    HRESULT CreateTextFormat(const wchar_t* family, DWRITE_FONT_WEIGHT weight, FLOAT size, IDWriteTextFormat** format)
    {
        HRESULT hr = dwriteFactory->CreateTextFormat(
            family,
            nullptr,
            weight,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            size,
            L"",
            format);
        if (FAILED(hr) && wcscmp(family, L"Segoe UI") != 0)
        {
            hr = dwriteFactory->CreateTextFormat(
                L"Segoe UI",
                nullptr,
                weight,
                DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL,
                size,
                L"",
                format);
        }
        return hr;
    }

    bool Ensure(UINT nextDpi, int nextWidth, int nextHeight)
    {
        if (!d2dFactory)
        {
            if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2dFactory)))
            {
                return false;
            }
        }

        if (!dwriteFactory)
        {
            if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&dwriteFactory))))
            {
                return false;
            }
        }

        if (renderTarget && dpi == nextDpi && width == nextWidth && height == nextHeight)
        {
            return true;
        }

        SafeRelease(iconFormat);
        SafeRelease(bodyFormat);
        SafeRelease(titleFormat);
        SafeRelease(iconBrush);
        SafeRelease(bodyBrush);
        SafeRelease(titleBrush);
        SafeRelease(accentBrush);
        SafeRelease(borderBrush);
        SafeRelease(backgroundBrush);
        SafeRelease(renderTarget);

        dpi = nextDpi;
        width = nextWidth;
        height = nextHeight;

        D2D1_RENDER_TARGET_PROPERTIES properties = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
            96.0f,
            96.0f,
            D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE,
            D2D1_FEATURE_LEVEL_DEFAULT);

        if (FAILED(d2dFactory->CreateDCRenderTarget(&properties, &renderTarget)))
        {
            return false;
        }

        const FLOAT titleSize = static_cast<FLOAT>(ScaleForDpi(14, dpi));
        const FLOAT bodySize = static_cast<FLOAT>(ScaleForDpi(13, dpi));
        const FLOAT iconSize = static_cast<FLOAT>(ScaleForDpi(16, dpi));
        if (FAILED(CreateTextFormat(L"Segoe UI Variable", DWRITE_FONT_WEIGHT_DEMI_BOLD, titleSize, &titleFormat)) ||
            FAILED(CreateTextFormat(L"Segoe UI Variable", DWRITE_FONT_WEIGHT_NORMAL, bodySize, &bodyFormat)) ||
            FAILED(CreateTextFormat(L"Segoe UI", DWRITE_FONT_WEIGHT_DEMI_BOLD, iconSize, &iconFormat)))
        {
            return false;
        }

        DWRITE_TRIMMING trimming{ DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
        titleFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        titleFormat->SetTrimming(&trimming, nullptr);
        bodyFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        bodyFormat->SetTrimming(&trimming, nullptr);
        iconFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        iconFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        return SUCCEEDED(renderTarget->CreateSolidColorBrush(D2DColor(28, 28, 30, 0.92f), &backgroundBrush)) &&
            SUCCEEDED(renderTarget->CreateSolidColorBrush(D2DColor(255, 255, 255, 0.06f), &borderBrush)) &&
            SUCCEEDED(renderTarget->CreateSolidColorBrush(D2DColor(112, 170, 232, 1.0f), &accentBrush)) &&
            SUCCEEDED(renderTarget->CreateSolidColorBrush(D2DColor(248, 248, 248, 0.96f), &titleBrush)) &&
            SUCCEEDED(renderTarget->CreateSolidColorBrush(D2DColor(226, 226, 226, 0.80f), &bodyBrush)) &&
            SUCCEEDED(renderTarget->CreateSolidColorBrush(D2DColor(255, 255, 255, 0.96f), &iconBrush));
    }

    bool Render(HDC dc, UINT nextDpi, const OverlayMetrics& metrics, const OverlayItem& item)
    {
        const int nextWidth = metrics.bitmapWidth;
        const int nextHeight = metrics.bitmapHeight;
        if (!Ensure(nextDpi, nextWidth, nextHeight))
        {
            return false;
        }

        RECT bindRect{ 0, 0, nextWidth, nextHeight };
        if (FAILED(renderTarget->BindDC(dc, &bindRect)))
        {
            return false;
        }

        const FLOAT padding = static_cast<FLOAT>(metrics.padding);
        const FLOAT cardWidth = static_cast<FLOAT>(metrics.cardWidth);
        const FLOAT cardHeight = static_cast<FLOAT>(metrics.cardHeight);
        const FLOAT radius = static_cast<FLOAT>(ScaleForDpi(16, dpi));
        const D2D1_ROUNDED_RECT cardRect = D2D1::RoundedRect(
            D2D1::RectF(padding, padding, padding + cardWidth, padding + cardHeight),
            radius,
            radius);

        renderTarget->BeginDraw();
        renderTarget->SetTransform(D2D1::Matrix3x2F::Identity());
        renderTarget->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        renderTarget->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        renderTarget->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));

        renderTarget->FillRoundedRectangle(cardRect, backgroundBrush);
        renderTarget->DrawRoundedRectangle(cardRect, borderBrush, 1.0f);

        accentBrush->SetColor(StatusD2DColor(item.kind));
        const FLOAT centerX = padding + static_cast<FLOAT>(ScaleForDpi(40, dpi));
        const FLOAT centerY = padding + cardHeight / 2.0f;
        const FLOAT iconRadius = static_cast<FLOAT>(ScaleForDpi(16, dpi));
        renderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(centerX, centerY), iconRadius, iconRadius), accentBrush);

        D2D1_RECT_F iconRect = D2D1::RectF(centerX - iconRadius, centerY - iconRadius - ScaleForDpi(1, dpi), centerX + iconRadius, centerY + iconRadius);
        renderTarget->DrawTextW(StatusGlyph(item.kind), static_cast<UINT32>(wcslen(StatusGlyph(item.kind))), iconFormat, iconRect, iconBrush);

        const FLOAT textLeft = padding + static_cast<FLOAT>(ScaleForDpi(68, dpi));
        const FLOAT textRight = padding + cardWidth - ScaleForDpi(22, dpi);
        if (item.message.empty())
        {
            D2D1_RECT_F titleRect = D2D1::RectF(textLeft, padding, textRight, padding + cardHeight);
            titleFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            renderTarget->DrawTextW(item.title.c_str(), static_cast<UINT32>(item.title.size()), titleFormat, titleRect, titleBrush);
        }
        else
        {
            titleFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            D2D1_RECT_F titleRect = D2D1::RectF(textLeft, centerY - ScaleForDpi(25, dpi), textRight, centerY - ScaleForDpi(2, dpi));
            D2D1_RECT_F bodyRect = D2D1::RectF(textLeft, centerY + ScaleForDpi(3, dpi), textRight, centerY + ScaleForDpi(27, dpi));
            renderTarget->DrawTextW(item.title.c_str(), static_cast<UINT32>(item.title.size()), titleFormat, titleRect, titleBrush);
            renderTarget->DrawTextW(item.message.c_str(), static_cast<UINT32>(item.message.size()), bodyFormat, bodyRect, bodyBrush);
        }

        return SUCCEEDED(renderTarget->EndDraw());
    }
};

OverlayRenderer g_renderer;

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

double EaseOutCubic(double value);
double EaseInCubic(double value);

BYTE CurrentOpacity()
{
    const DWORD elapsed = GetTickCount() - g_startedAt;
    if (elapsed < kFadeInMs)
    {
        const double progress = EaseOutCubic(static_cast<double>(elapsed) / kFadeInMs);
        return static_cast<BYTE>(std::lround(progress * 255.0));
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

    const double progress = EaseInCubic(static_cast<double>(fadeElapsed) / kFadeOutMs);
    return static_cast<BYTE>(std::lround((1.0 - progress) * 255.0));
}

double EaseOutCubic(double value)
{
    value = std::clamp(value, 0.0, 1.0);
    const double inverse = 1.0 - value;
    return 1.0 - inverse * inverse * inverse;
}

double EaseInCubic(double value)
{
    value = std::clamp(value, 0.0, 1.0);
    return value * value * value;
}

int CurrentHorizontalOffset(const OverlayMetrics& metrics)
{
    const DWORD elapsed = GetTickCount() - g_startedAt;
    const UINT dpi = GetDpiForWindow(g_overlayWindow ? g_overlayWindow : g_hWnd);
    const int marginX = ScaleForDpi(16, dpi);
    const int travel = metrics.cardWidth + metrics.padding + marginX;
    if (elapsed < kFadeInMs)
    {
        const double progress = EaseOutCubic(static_cast<double>(elapsed) / kFadeInMs);
        return static_cast<int>(std::lround((1.0 - progress) * travel));
    }

    const DWORD fadeOutStart = kFadeInMs + kHoldMs;
    if (elapsed < fadeOutStart)
    {
        return 0;
    }

    const DWORD fadeElapsed = elapsed - fadeOutStart;
    if (fadeElapsed >= kFadeOutMs)
    {
        return travel;
    }

    const double progress = EaseInCubic(static_cast<double>(fadeElapsed) / kFadeOutMs);
    return static_cast<int>(std::lround(progress * travel));
}

POINT OverlayPosition(const OverlayMetrics& metrics)
{
    HWND anchorWindow = GetForegroundWindow();
    HMONITOR monitor = MonitorFromWindow(anchorWindow ? anchorWindow : g_hWnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    GetMonitorInfoW(monitor, &monitorInfo);
    const RECT work = monitorInfo.rcWork;
    const int marginX = ScaleForDpi(16, GetDpiForWindow(g_overlayWindow ? g_overlayWindow : g_hWnd));
    const int marginBottom = ScaleForDpi(36, GetDpiForWindow(g_overlayWindow ? g_overlayWindow : g_hWnd));
    const int toastReserve = g_notificationsEnabled ? metrics.cardHeight + ScaleForDpi(24, GetDpiForWindow(g_overlayWindow ? g_overlayWindow : g_hWnd)) : 0;
    const int cardRight = work.right - marginX + CurrentHorizontalOffset(metrics);
    const int cardBottom = work.bottom - marginBottom - toastReserve;
    return { cardRight - metrics.padding - metrics.cardWidth, cardBottom - metrics.padding - metrics.cardHeight };
}

bool RenderOverlayD2D()
{
    if (!g_overlayWindow)
    {
        return true;
    }

    const UINT dpi = GetDpiForWindow(g_overlayWindow);
    const OverlayMetrics metrics = GetOverlayMetrics(dpi);
    const int width = metrics.bitmapWidth;
    const int height = metrics.bitmapHeight;

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
    if (!screen || !memoryDc || !bitmap || !bits)
    {
        if (bitmap)
        {
            DeleteObject(bitmap);
        }
        if (memoryDc)
        {
            DeleteDC(memoryDc);
        }
        if (screen)
        {
            ReleaseDC(nullptr, screen);
        }
        return false;
    }

    ZeroMemory(bits, static_cast<size_t>(width) * height * 4);
    HGDIOBJ oldBitmap = SelectObject(memoryDc, bitmap);
    const bool rendered = g_renderer.Render(memoryDc, dpi, metrics, g_current);
    if (!rendered)
    {
        g_renderer.Reset();
    }
    if (rendered)
    {
        POINT source{ 0, 0 };
        SIZE size{ width, height };
        POINT destination = OverlayPosition(metrics);
        BLENDFUNCTION blend{ AC_SRC_OVER, 0, CurrentOpacity(), AC_SRC_ALPHA };
        UpdateLayeredWindow(g_overlayWindow, screen, &destination, &size, memoryDc, &source, 0, &blend, ULW_ALPHA);
    }

    SelectObject(memoryDc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(memoryDc);
    ReleaseDC(nullptr, screen);
    return rendered;
}

void RenderOverlayGdi()
{
    if (!g_overlayWindow)
    {
        return;
    }

    const UINT dpi = GetDpiForWindow(g_overlayWindow);
    const OverlayMetrics metrics = GetOverlayMetrics(dpi);
    const int width = metrics.bitmapWidth;
    const int height = metrics.bitmapHeight;
    const int radius = ScaleForDpi(16, dpi);

    std::vector<unsigned char> pixels(static_cast<size_t>(width) * height * 4, 0);
    RECT card{ metrics.padding, metrics.padding, metrics.padding + metrics.cardWidth, metrics.padding + metrics.cardHeight };
    FillRoundedRect(pixels, width, height, card, radius, RGB(28, 28, 30), 235);

    const COLORREF accent = StatusColor(g_current.kind);
    const int centerY = metrics.padding + metrics.cardHeight / 2;
    FillCircle(pixels, width, height, metrics.padding + ScaleForDpi(40, dpi), centerY, ScaleForDpi(16, dpi), accent, 255);

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

    HFONT titleFont = CreateOverlayFont(dpi, 14, FW_SEMIBOLD);
    HFONT bodyFont = CreateOverlayFont(dpi, 13, FW_NORMAL);
    SetBkMode(memoryDc, TRANSPARENT);
    SetTextColor(memoryDc, RGB(255, 255, 255));

    RECT iconRect{ metrics.padding + ScaleForDpi(32, dpi), centerY - ScaleForDpi(10, dpi), metrics.padding + ScaleForDpi(48, dpi), centerY + ScaleForDpi(10, dpi) };
    HGDIOBJ oldFont = SelectObject(memoryDc, titleFont);
    DrawTextW(memoryDc, StatusGlyph(g_current.kind), -1, &iconRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    if (g_current.message.empty())
    {
        RECT titleRect{ metrics.padding + ScaleForDpi(68, dpi), metrics.padding, metrics.padding + metrics.cardWidth - ScaleForDpi(22, dpi), metrics.padding + metrics.cardHeight };
        DrawTextW(memoryDc, g_current.title.c_str(), -1, &titleRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    else
    {
        RECT titleRect{ metrics.padding + ScaleForDpi(68, dpi), centerY - ScaleForDpi(25, dpi), metrics.padding + metrics.cardWidth - ScaleForDpi(22, dpi), centerY - ScaleForDpi(2, dpi) };
        DrawTextW(memoryDc, g_current.title.c_str(), -1, &titleRect, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

        SetTextColor(memoryDc, RGB(224, 224, 224));
        SelectObject(memoryDc, bodyFont);
        RECT bodyRect{ metrics.padding + ScaleForDpi(68, dpi), centerY + ScaleForDpi(3, dpi), metrics.padding + metrics.cardWidth - ScaleForDpi(22, dpi), centerY + ScaleForDpi(27, dpi) };
        DrawTextW(memoryDc, g_current.message.c_str(), -1, &bodyRect, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    GdiFlush();

    CopyMemory(pixels.data(), bits, pixels.size());
    FixTextAlpha(pixels);
    CopyMemory(bits, pixels.data(), pixels.size());

    POINT source{ 0, 0 };
    SIZE size{ width, height };
    POINT destination = OverlayPosition(metrics);
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

void RenderOverlay()
{
    if (!RenderOverlayD2D())
    {
        RenderOverlayGdi();
    }
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
        SetTimer(hwnd, kAnimationTimer, kAnimationFrameMs, nullptr);
        return 0;
    }

    case WM_TIMER:
        if (wParam == kAnimationTimer)
        {
            const DWORD elapsed = GetTickCount() - g_startedAt;
            const DWORD fadeOutStart = kFadeInMs + kHoldMs;
            if (elapsed >= kFadeInMs && elapsed < fadeOutStart)
            {
                if (!g_holdTimerArmed)
                {
                    RenderOverlay();
                    g_holdTimerArmed = true;
                }
                KillTimer(hwnd, kAnimationTimer);
                SetTimer(hwnd, kAnimationTimer, std::max<DWORD>(1, fadeOutStart - elapsed), nullptr);
                return 0;
            }

            if (g_holdTimerArmed)
            {
                g_holdTimerArmed = false;
                KillTimer(hwnd, kAnimationTimer);
                SetTimer(hwnd, kAnimationTimer, kAnimationFrameMs, nullptr);
            }

            if (elapsed >= fadeOutStart + kFadeOutMs || CurrentOpacity() == 0)
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
        if (g_timerResolutionRaised)
        {
            timeEndPeriod(1);
            g_timerResolutionRaised = false;
        }
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
    g_holdTimerArmed = false;
    if (!g_timerResolutionRaised && timeBeginPeriod(1) == TIMERR_NOERROR)
    {
        g_timerResolutionRaised = true;
    }
    const OverlayMetrics initialMetrics = GetOverlayMetrics(GetDpiForWindow(g_hWnd ? g_hWnd : GetDesktopWindow()));

    g_overlayWindow = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kOverlayClassName,
        L"FFKeyLock Overlay",
        WS_POPUP,
        0,
        0,
        initialMetrics.bitmapWidth,
        initialMetrics.bitmapHeight,
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
    g_renderer.Reset();
    if (g_overlayWindow)
    {
        DestroyWindow(g_overlayWindow);
        g_overlayWindow = nullptr;
    }
}
}
