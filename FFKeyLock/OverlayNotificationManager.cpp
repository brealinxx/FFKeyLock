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

#pragma comment(lib, "D2d1.lib")
#pragma comment(lib, "Dwmapi.lib")
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
constexpr ULONGLONG kFadeInMs = 240;
constexpr ULONGLONG kHoldMs = 2300;
constexpr ULONGLONG kFadeOutMs = 220;
constexpr UINT kAnimationFrameMs = 8;
constexpr int kWidth = 376;
constexpr int kHeight = 88;
constexpr int kBitmapPadding = 12;
constexpr int kSlideDistance = 32;
constexpr int kSystemToastReserve = 144;
constexpr size_t kMaximumQueuedItems = 4;

struct OverlayMetrics
{
    int cardWidth = 0;
    int cardHeight = 0;
    int padding = 0;
    int bitmapWidth = 0;
    int bitmapHeight = 0;
};

std::deque<OverlayItem> g_queue;
OverlayItem g_current;
HWND g_overlayWindow = nullptr;
ULONGLONG g_startedAt = 0;
RECT g_workArea{};
bool g_classRegistered = false;
bool g_holdTimerArmed = false;
bool g_timerResolutionRaised = false;
bool g_animationsEnabled = true;

bool SystemAnimationsEnabled()
{
    BOOL enabled = TRUE;
    return !SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0) || enabled != FALSE;
}

int ScaleForDpi(int value, UINT dpi)
{
    return MulDiv(value, static_cast<int>(dpi ? dpi : USER_DEFAULT_SCREEN_DPI), USER_DEFAULT_SCREEN_DPI);
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

template <typename T>
void SafeRelease(T*& object)
{
    if (object)
    {
        object->Release();
        object = nullptr;
    }
}

D2D1_COLOR_F D2DColor(BYTE red, BYTE green, BYTE blue, float alpha = 1.0f)
{
    return D2D1::ColorF(red / 255.0f, green / 255.0f, blue / 255.0f, alpha);
}

D2D1_COLOR_F StatusD2DColor(OverlayKind kind)
{
    switch (kind)
    {
    case OverlayKind::Warning:
        return D2DColor(242, 190, 60);
    case OverlayKind::Error:
        return D2DColor(255, 92, 92);
    case OverlayKind::Info:
    case OverlayKind::Success:
    default:
        return D2DColor(118, 185, 0); // NVIDIA green.
    }
}

COLORREF StatusColor(OverlayKind kind)
{
    switch (kind)
    {
    case OverlayKind::Warning:
        return RGB(242, 190, 60);
    case OverlayKind::Error:
        return RGB(255, 92, 92);
    case OverlayKind::Info:
    case OverlayKind::Success:
    default:
        return RGB(118, 185, 0);
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

struct OverlaySurface
{
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ previousBitmap = nullptr;
    void* bits = nullptr;
    OverlayMetrics metrics{};
    UINT dpi = USER_DEFAULT_SCREEN_DPI;

    ~OverlaySurface()
    {
        Reset();
    }

    void Reset()
    {
        if (dc && previousBitmap)
        {
            SelectObject(dc, previousBitmap);
        }
        previousBitmap = nullptr;
        if (bitmap)
        {
            DeleteObject(bitmap);
            bitmap = nullptr;
        }
        if (dc)
        {
            DeleteDC(dc);
            dc = nullptr;
        }
        bits = nullptr;
        metrics = {};
    }

    bool Create(UINT nextDpi)
    {
        Reset();
        dpi = nextDpi ? nextDpi : USER_DEFAULT_SCREEN_DPI;
        metrics = GetOverlayMetrics(dpi);

        BITMAPINFO bitmapInfo{};
        bitmapInfo.bmiHeader.biSize = sizeof(bitmapInfo.bmiHeader);
        bitmapInfo.bmiHeader.biWidth = metrics.bitmapWidth;
        bitmapInfo.bmiHeader.biHeight = -metrics.bitmapHeight;
        bitmapInfo.bmiHeader.biPlanes = 1;
        bitmapInfo.bmiHeader.biBitCount = 32;
        bitmapInfo.bmiHeader.biCompression = BI_RGB;

        HDC screen = GetDC(nullptr);
        if (!screen)
        {
            return false;
        }

        dc = CreateCompatibleDC(screen);
        bitmap = CreateDIBSection(screen, &bitmapInfo, DIB_RGB_COLORS, &bits, nullptr, 0);
        ReleaseDC(nullptr, screen);
        if (!dc || !bitmap || !bits)
        {
            Reset();
            return false;
        }

        previousBitmap = SelectObject(dc, bitmap);
        ZeroMemory(bits, static_cast<size_t>(metrics.bitmapWidth) * metrics.bitmapHeight * 4);
        return true;
    }
};

struct OverlayRenderer
{
    ID2D1Factory* d2dFactory = nullptr;
    IDWriteFactory* dwriteFactory = nullptr;
    ID2D1DCRenderTarget* renderTarget = nullptr;
    IDWriteTextFormat* titleFormat = nullptr;
    IDWriteTextFormat* bodyFormat = nullptr;
    IDWriteTextFormat* iconFormat = nullptr;
    ID2D1SolidColorBrush* shadowBrush = nullptr;
    ID2D1SolidColorBrush* backgroundBrush = nullptr;
    ID2D1SolidColorBrush* innerBorderBrush = nullptr;
    ID2D1SolidColorBrush* accentBrush = nullptr;
    ID2D1SolidColorBrush* accentSoftBrush = nullptr;
    ID2D1SolidColorBrush* titleBrush = nullptr;
    ID2D1SolidColorBrush* bodyBrush = nullptr;
    UINT dpi = 0;
    int width = 0;
    int height = 0;

    ~OverlayRenderer()
    {
        Reset();
    }

    void ResetDeviceResources()
    {
        SafeRelease(bodyBrush);
        SafeRelease(titleBrush);
        SafeRelease(accentSoftBrush);
        SafeRelease(accentBrush);
        SafeRelease(innerBorderBrush);
        SafeRelease(backgroundBrush);
        SafeRelease(shadowBrush);
        SafeRelease(iconFormat);
        SafeRelease(bodyFormat);
        SafeRelease(titleFormat);
        SafeRelease(renderTarget);
        dpi = 0;
        width = 0;
        height = 0;
    }

    void Reset()
    {
        ResetDeviceResources();
        SafeRelease(dwriteFactory);
        SafeRelease(d2dFactory);
    }

    HRESULT CreateTextFormat(const wchar_t* family, DWRITE_FONT_WEIGHT weight, FLOAT size, IDWriteTextFormat** format)
    {
        HRESULT result = dwriteFactory->CreateTextFormat(
            family, nullptr, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            size, L"", format);
        if (FAILED(result) && wcscmp(family, L"Segoe UI") != 0)
        {
            result = dwriteFactory->CreateTextFormat(
                L"Segoe UI", nullptr, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                size, L"", format);
        }
        return result;
    }

    bool Ensure(UINT nextDpi, int nextWidth, int nextHeight)
    {
        if (!d2dFactory && FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2dFactory)))
        {
            return false;
        }
        if (!dwriteFactory && FAILED(DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&dwriteFactory))))
        {
            return false;
        }
        if (renderTarget && dpi == nextDpi && width == nextWidth && height == nextHeight)
        {
            return true;
        }

        ResetDeviceResources();
        dpi = nextDpi;
        width = nextWidth;
        height = nextHeight;

        D2D1_RENDER_TARGET_PROPERTIES properties = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
            96.0f, 96.0f, D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE, D2D1_FEATURE_LEVEL_DEFAULT);
        if (FAILED(d2dFactory->CreateDCRenderTarget(&properties, &renderTarget)))
        {
            return false;
        }

        const FLOAT titleSize = static_cast<FLOAT>(ScaleForDpi(14, dpi));
        const FLOAT bodySize = static_cast<FLOAT>(ScaleForDpi(12, dpi));
        const FLOAT iconSize = static_cast<FLOAT>(ScaleForDpi(15, dpi));
        if (FAILED(CreateTextFormat(L"Segoe UI Variable", DWRITE_FONT_WEIGHT_SEMI_BOLD, titleSize, &titleFormat)) ||
            FAILED(CreateTextFormat(L"Segoe UI Variable", DWRITE_FONT_WEIGHT_NORMAL, bodySize, &bodyFormat)) ||
            FAILED(CreateTextFormat(L"Segoe UI", DWRITE_FONT_WEIGHT_BOLD, iconSize, &iconFormat)))
        {
            ResetDeviceResources();
            return false;
        }

        DWRITE_TRIMMING trimming{ DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
        titleFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        titleFormat->SetTrimming(&trimming, nullptr);
        bodyFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        bodyFormat->SetTrimming(&trimming, nullptr);
        iconFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        iconFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        return SUCCEEDED(renderTarget->CreateSolidColorBrush(D2DColor(0, 0, 0, 0.16f), &shadowBrush)) &&
            SUCCEEDED(renderTarget->CreateSolidColorBrush(D2DColor(20, 22, 23, 0.97f), &backgroundBrush)) &&
            SUCCEEDED(renderTarget->CreateSolidColorBrush(D2DColor(255, 255, 255, 0.11f), &innerBorderBrush)) &&
            SUCCEEDED(renderTarget->CreateSolidColorBrush(D2DColor(118, 185, 0), &accentBrush)) &&
            SUCCEEDED(renderTarget->CreateSolidColorBrush(D2DColor(118, 185, 0, 0.14f), &accentSoftBrush)) &&
            SUCCEEDED(renderTarget->CreateSolidColorBrush(D2DColor(248, 249, 249, 0.98f), &titleBrush)) &&
            SUCCEEDED(renderTarget->CreateSolidColorBrush(D2DColor(199, 203, 205, 0.88f), &bodyBrush));
    }

    bool Render(OverlaySurface& surface, const OverlayItem& item)
    {
        if (!Ensure(surface.dpi, surface.metrics.bitmapWidth, surface.metrics.bitmapHeight))
        {
            return false;
        }

        RECT bindRect{ 0, 0, surface.metrics.bitmapWidth, surface.metrics.bitmapHeight };
        if (FAILED(renderTarget->BindDC(surface.dc, &bindRect)))
        {
            return false;
        }

        const FLOAT padding = static_cast<FLOAT>(surface.metrics.padding);
        const FLOAT cardWidth = static_cast<FLOAT>(surface.metrics.cardWidth);
        const FLOAT cardHeight = static_cast<FLOAT>(surface.metrics.cardHeight);
        const FLOAT radius = static_cast<FLOAT>(ScaleForDpi(10, surface.dpi));
        const D2D1_RECT_F cardBounds = D2D1::RectF(padding, padding, padding + cardWidth, padding + cardHeight);
        const D2D1_ROUNDED_RECT card = D2D1::RoundedRect(cardBounds, radius, radius);

        renderTarget->BeginDraw();
        renderTarget->SetTransform(D2D1::Matrix3x2F::Identity());
        renderTarget->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        renderTarget->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        renderTarget->Clear(D2D1::ColorF(0, 0.0f));

        const FLOAT shadowSpread = static_cast<FLOAT>(ScaleForDpi(4, surface.dpi));
        const FLOAT shadowOffset = static_cast<FLOAT>(ScaleForDpi(3, surface.dpi));
        const D2D1_ROUNDED_RECT shadow = D2D1::RoundedRect(
            D2D1::RectF(cardBounds.left - shadowSpread, cardBounds.top - shadowSpread + shadowOffset,
                cardBounds.right + shadowSpread, cardBounds.bottom + shadowSpread + shadowOffset),
            radius + shadowSpread, radius + shadowSpread);
        renderTarget->FillRoundedRectangle(shadow, shadowBrush);
        renderTarget->FillRoundedRectangle(card, backgroundBrush);
        renderTarget->DrawRoundedRectangle(card, innerBorderBrush, 1.0f);

        const D2D1_COLOR_F accent = StatusD2DColor(item.kind);
        accentBrush->SetColor(accent);
        accentSoftBrush->SetColor(D2D1::ColorF(accent.r, accent.g, accent.b, 0.14f));

        const FLOAT railWidth = static_cast<FLOAT>(ScaleForDpi(3, surface.dpi));
        const FLOAT railInset = static_cast<FLOAT>(ScaleForDpi(11, surface.dpi));
        renderTarget->FillRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(padding, padding + railInset, padding + railWidth,
                padding + cardHeight - railInset), railWidth / 2.0f, railWidth / 2.0f), accentBrush);

        const FLOAT iconX = padding + static_cast<FLOAT>(ScaleForDpi(38, surface.dpi));
        const FLOAT iconY = padding + cardHeight / 2.0f;
        const FLOAT iconRadius = static_cast<FLOAT>(ScaleForDpi(17, surface.dpi));
        const D2D1_ELLIPSE iconCircle = D2D1::Ellipse(D2D1::Point2F(iconX, iconY), iconRadius, iconRadius);
        renderTarget->FillEllipse(iconCircle, accentSoftBrush);
        renderTarget->DrawEllipse(iconCircle, accentBrush, 1.0f);

        const D2D1_RECT_F iconRect = D2D1::RectF(
            iconX - iconRadius, iconY - iconRadius - ScaleForDpi(1, surface.dpi),
            iconX + iconRadius, iconY + iconRadius);
        const wchar_t* glyph = StatusGlyph(item.kind);
        renderTarget->DrawTextW(glyph, static_cast<UINT32>(wcslen(glyph)), iconFormat, iconRect, accentBrush);

        const FLOAT textLeft = padding + static_cast<FLOAT>(ScaleForDpi(68, surface.dpi));
        const FLOAT textRight = padding + cardWidth - static_cast<FLOAT>(ScaleForDpi(20, surface.dpi));
        if (item.message.empty())
        {
            titleFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            renderTarget->DrawTextW(item.title.c_str(), static_cast<UINT32>(item.title.size()), titleFormat,
                D2D1::RectF(textLeft, padding, textRight, padding + cardHeight), titleBrush);
        }
        else
        {
            titleFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            renderTarget->DrawTextW(item.title.c_str(), static_cast<UINT32>(item.title.size()), titleFormat,
                D2D1::RectF(textLeft, iconY - ScaleForDpi(24, surface.dpi), textRight,
                    iconY - ScaleForDpi(1, surface.dpi)), titleBrush);
            renderTarget->DrawTextW(item.message.c_str(), static_cast<UINT32>(item.message.size()), bodyFormat,
                D2D1::RectF(textLeft, iconY + ScaleForDpi(4, surface.dpi), textRight,
                    iconY + ScaleForDpi(25, surface.dpi)), bodyBrush);
        }

        const HRESULT result = renderTarget->EndDraw();
        if (result == D2DERR_RECREATE_TARGET)
        {
            ResetDeviceResources();
        }
        return SUCCEEDED(result);
    }
};

OverlaySurface g_surface;
OverlayRenderer g_renderer;

BYTE BlendByte(BYTE destination, BYTE source, BYTE alpha)
{
    return static_cast<BYTE>((source * alpha + destination * (255 - alpha)) / 255);
}

void BlendPixel(unsigned char* pixel, COLORREF color, BYTE alpha)
{
    pixel[0] = BlendByte(pixel[0], GetBValue(color), alpha);
    pixel[1] = BlendByte(pixel[1], GetGValue(color), alpha);
    pixel[2] = BlendByte(pixel[2], GetRValue(color), alpha);
    pixel[3] = static_cast<BYTE>(std::min<int>(255, alpha + pixel[3] * (255 - alpha) / 255));
}

void FillRoundedRect(std::vector<unsigned char>& pixels, int width, int height, const RECT& rect,
    int radius, COLORREF color, BYTE alpha)
{
    const int radiusSquared = radius * radius;
    for (int y = std::max(0L, rect.top); y < std::min(static_cast<LONG>(height), rect.bottom); ++y)
    {
        for (int x = std::max(0L, rect.left); x < std::min(static_cast<LONG>(width), rect.right); ++x)
        {
            const int nearestX = std::clamp(x, static_cast<int>(rect.left) + radius,
                static_cast<int>(rect.right) - radius - 1);
            const int nearestY = std::clamp(y, static_cast<int>(rect.top) + radius,
                static_cast<int>(rect.bottom) - radius - 1);
            const int dx = x - nearestX;
            const int dy = y - nearestY;
            if (dx * dx + dy * dy <= radiusSquared)
            {
                BlendPixel(&pixels[(static_cast<size_t>(y) * width + x) * 4], color, alpha);
            }
        }
    }
}

void FillCircle(std::vector<unsigned char>& pixels, int width, int height, int centerX, int centerY,
    int radius, COLORREF color, BYTE alpha)
{
    const int radiusSquared = radius * radius;
    for (int y = std::max(0, centerY - radius); y <= std::min(height - 1, centerY + radius); ++y)
    {
        for (int x = std::max(0, centerX - radius); x <= std::min(width - 1, centerX + radius); ++x)
        {
            const int dx = x - centerX;
            const int dy = y - centerY;
            if (dx * dx + dy * dy <= radiusSquared)
            {
                BlendPixel(&pixels[(static_cast<size_t>(y) * width + x) * 4], color, alpha);
            }
        }
    }
}

HFONT CreateOverlayFont(UINT dpi, int pointSize, int weight)
{
    LOGFONTW font{};
    font.lfHeight = -MulDiv(pointSize, static_cast<int>(dpi), 72);
    font.lfWeight = weight;
    font.lfQuality = ANTIALIASED_QUALITY;
    StringCchCopyW(font.lfFaceName, std::size(font.lfFaceName), L"Segoe UI");
    return CreateFontIndirectW(&font);
}

bool RenderOverlayGdi(OverlaySurface& surface, const OverlayItem& item)
{
    const int width = surface.metrics.bitmapWidth;
    const int height = surface.metrics.bitmapHeight;
    std::vector<unsigned char> pixels(static_cast<size_t>(width) * height * 4, 0);
    const int padding = surface.metrics.padding;
    const int radius = ScaleForDpi(10, surface.dpi);
    RECT shadow{ padding - ScaleForDpi(3, surface.dpi), padding,
        padding + surface.metrics.cardWidth + ScaleForDpi(3, surface.dpi),
        padding + surface.metrics.cardHeight + ScaleForDpi(6, surface.dpi) };
    FillRoundedRect(pixels, width, height, shadow, radius + ScaleForDpi(3, surface.dpi), RGB(0, 0, 0), 40);
    RECT card{ padding, padding, padding + surface.metrics.cardWidth, padding + surface.metrics.cardHeight };
    FillRoundedRect(pixels, width, height, card, radius, RGB(20, 22, 23), 248);

    const COLORREF accent = StatusColor(item.kind);
    RECT rail{ padding, padding + ScaleForDpi(11, surface.dpi), padding + ScaleForDpi(3, surface.dpi),
        padding + surface.metrics.cardHeight - ScaleForDpi(11, surface.dpi) };
    FillRoundedRect(pixels, width, height, rail, ScaleForDpi(2, surface.dpi), accent, 255);
    const int iconX = padding + ScaleForDpi(38, surface.dpi);
    const int iconY = padding + surface.metrics.cardHeight / 2;
    FillCircle(pixels, width, height, iconX, iconY, ScaleForDpi(17, surface.dpi), accent, 48);
    CopyMemory(surface.bits, pixels.data(), pixels.size());

    HFONT titleFont = CreateOverlayFont(surface.dpi, 14, FW_SEMIBOLD);
    HFONT bodyFont = CreateOverlayFont(surface.dpi, 12, FW_NORMAL);
    if (!titleFont || !bodyFont)
    {
        if (titleFont) DeleteObject(titleFont);
        if (bodyFont) DeleteObject(bodyFont);
        return false;
    }

    SetBkMode(surface.dc, TRANSPARENT);
    SetTextColor(surface.dc, accent);
    HGDIOBJ previousFont = SelectObject(surface.dc, titleFont);
    RECT iconRect{ iconX - ScaleForDpi(12, surface.dpi), iconY - ScaleForDpi(12, surface.dpi),
        iconX + ScaleForDpi(12, surface.dpi), iconY + ScaleForDpi(12, surface.dpi) };
    DrawTextW(surface.dc, StatusGlyph(item.kind), -1, &iconRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    const int textLeft = padding + ScaleForDpi(68, surface.dpi);
    const int textRight = padding + surface.metrics.cardWidth - ScaleForDpi(20, surface.dpi);
    SetTextColor(surface.dc, RGB(248, 249, 249));
    if (item.message.empty())
    {
        RECT titleRect{ textLeft, padding, textRight, padding + surface.metrics.cardHeight };
        DrawTextW(surface.dc, item.title.c_str(), -1, &titleRect,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    else
    {
        RECT titleRect{ textLeft, iconY - ScaleForDpi(24, surface.dpi), textRight,
            iconY - ScaleForDpi(1, surface.dpi) };
        DrawTextW(surface.dc, item.title.c_str(), -1, &titleRect,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        SelectObject(surface.dc, bodyFont);
        SetTextColor(surface.dc, RGB(199, 203, 205));
        RECT bodyRect{ textLeft, iconY + ScaleForDpi(4, surface.dpi), textRight,
            iconY + ScaleForDpi(25, surface.dpi) };
        DrawTextW(surface.dc, item.message.c_str(), -1, &bodyRect,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    SelectObject(surface.dc, previousFont);
    DeleteObject(titleFont);
    DeleteObject(bodyFont);
    GdiFlush();
    return true;
}

bool BuildOverlaySurface(UINT dpi)
{
    if (!g_surface.Create(dpi))
    {
        return false;
    }
    if (g_renderer.Render(g_surface, g_current))
    {
        return true;
    }
    g_renderer.Reset();
    ZeroMemory(g_surface.bits,
        static_cast<size_t>(g_surface.metrics.bitmapWidth) * g_surface.metrics.bitmapHeight * 4);
    return RenderOverlayGdi(g_surface, g_current);
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

double SmoothStep(double value)
{
    value = std::clamp(value, 0.0, 1.0);
    return value * value * (3.0 - 2.0 * value);
}

ULONGLONG ElapsedAnimationTime()
{
    return GetTickCount64() - g_startedAt;
}

void BeginHighResolutionAnimation()
{
    if (!g_timerResolutionRaised && timeBeginPeriod(1) == TIMERR_NOERROR)
    {
        g_timerResolutionRaised = true;
    }
}

void EndHighResolutionAnimation()
{
    if (g_timerResolutionRaised)
    {
        timeEndPeriod(1);
        g_timerResolutionRaised = false;
    }
}

BYTE CurrentOpacity()
{
    if (!g_animationsEnabled)
    {
        return 255;
    }
    const ULONGLONG elapsed = ElapsedAnimationTime();
    if (elapsed < kFadeInMs)
    {
        return static_cast<BYTE>(std::lround(EaseOutCubic(static_cast<double>(elapsed) / kFadeInMs) * 255.0));
    }
    const ULONGLONG fadeOutStart = kFadeInMs + kHoldMs;
    if (elapsed < fadeOutStart)
    {
        return 255;
    }
    const ULONGLONG fadeElapsed = elapsed - fadeOutStart;
    if (fadeElapsed >= kFadeOutMs)
    {
        return 0;
    }
    return static_cast<BYTE>(std::lround(
        (1.0 - EaseInCubic(static_cast<double>(fadeElapsed) / kFadeOutMs)) * 255.0));
}

int CurrentHorizontalOffset(UINT dpi)
{
    if (!g_animationsEnabled)
    {
        return 0;
    }
    const int travel = ScaleForDpi(kSlideDistance, dpi);
    const ULONGLONG elapsed = ElapsedAnimationTime();
    if (elapsed < kFadeInMs)
    {
        const double progress = SmoothStep(static_cast<double>(elapsed) / kFadeInMs);
        return static_cast<int>(std::lround((1.0 - progress) * travel));
    }
    const ULONGLONG fadeOutStart = kFadeInMs + kHoldMs;
    if (elapsed < fadeOutStart)
    {
        return 0;
    }
    const ULONGLONG fadeElapsed = elapsed - fadeOutStart;
    if (fadeElapsed >= kFadeOutMs)
    {
        return travel;
    }
    return static_cast<int>(std::lround(
        SmoothStep(static_cast<double>(fadeElapsed) / kFadeOutMs) * travel));
}

POINT OverlayPosition()
{
    const int marginX = ScaleForDpi(16, g_surface.dpi);
    const int bottomReserve = ScaleForDpi(
        g_notificationsEnabled ? kSystemToastReserve : 22, g_surface.dpi);
    const int cardRight = g_workArea.right - marginX + CurrentHorizontalOffset(g_surface.dpi);
    const int cardBottom = g_workArea.bottom - bottomReserve;
    return {
        cardRight - g_surface.metrics.cardWidth - g_surface.metrics.padding,
        cardBottom - g_surface.metrics.cardHeight - g_surface.metrics.padding
    };
}

void PresentOverlay()
{
    if (!g_overlayWindow || !g_surface.dc)
    {
        return;
    }

    HDC screen = GetDC(nullptr);
    if (!screen)
    {
        return;
    }
    POINT source{ 0, 0 };
    SIZE size{ g_surface.metrics.bitmapWidth, g_surface.metrics.bitmapHeight };
    POINT destination = OverlayPosition();
    BLENDFUNCTION blend{ AC_SRC_OVER, 0, CurrentOpacity(), AC_SRC_ALPHA };
    const BOOL updated = UpdateLayeredWindow(g_overlayWindow, screen, &destination, &size, g_surface.dc,
        &source, 0, &blend, ULW_ALPHA);
    ReleaseDC(nullptr, screen);

    const ULONGLONG elapsed = ElapsedAnimationTime();
    const ULONGLONG fadeOutStart = kFadeInMs + kHoldMs;
    if (g_animationsEnabled && updated && (elapsed < kFadeInMs || elapsed >= fadeOutStart))
    {
        DwmFlush();
    }
}

void ShowNext();

LRESULT CALLBACK OverlayProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_CREATE:
        if (!g_animationsEnabled)
        {
            PresentOverlay();
            SetTimer(hwnd, kAnimationTimer, kHoldMs, nullptr);
            return 0;
        }
        BeginHighResolutionAnimation();
        SetTimer(hwnd, kAnimationTimer, kAnimationFrameMs, nullptr);
        return 0;

    case WM_TIMER:
        if (wParam == kAnimationTimer)
        {
            if (!g_animationsEnabled)
            {
                DestroyWindow(hwnd);
                return 0;
            }
            const ULONGLONG elapsed = ElapsedAnimationTime();
            const ULONGLONG fadeOutStart = kFadeInMs + kHoldMs;
            if (elapsed >= kFadeInMs && elapsed < fadeOutStart)
            {
                if (!g_holdTimerArmed)
                {
                    PresentOverlay();
                    g_holdTimerArmed = true;
                }
                KillTimer(hwnd, kAnimationTimer);
                EndHighResolutionAnimation();
                SetTimer(hwnd, kAnimationTimer,
                    static_cast<UINT>(std::max<ULONGLONG>(1, fadeOutStart - elapsed)), nullptr);
                return 0;
            }
            if (g_holdTimerArmed)
            {
                g_holdTimerArmed = false;
                KillTimer(hwnd, kAnimationTimer);
                BeginHighResolutionAnimation();
                SetTimer(hwnd, kAnimationTimer, kAnimationFrameMs, nullptr);
            }
            if (elapsed >= fadeOutStart + kFadeOutMs || CurrentOpacity() == 0)
            {
                DestroyWindow(hwnd);
                return 0;
            }
            PresentOverlay();
            return 0;
        }
        break;

    case WM_NCHITTEST:
        return HTTRANSPARENT;

    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;

    case WM_DESTROY:
        KillTimer(hwnd, kAnimationTimer);
        EndHighResolutionAnimation();
        g_surface.Reset();
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
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = OverlayProc;
    windowClass.hInstance = g_hInst;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = kOverlayClassName;
    g_classRegistered = RegisterClassExW(&windowClass) != 0;
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

    g_current = std::move(g_queue.front());
    g_queue.pop_front();
    g_holdTimerArmed = false;
    g_animationsEnabled = SystemAnimationsEnabled();

    HWND anchorWindow = GetForegroundWindow();
    if (!anchorWindow)
    {
        anchorWindow = g_hWnd;
    }
    HMONITOR monitor = MonitorFromWindow(anchorWindow, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (!GetMonitorInfoW(monitor, &monitorInfo))
    {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &g_workArea, 0);
    }
    else
    {
        g_workArea = monitorInfo.rcWork;
    }

    const UINT dpi = anchorWindow ? GetDpiForWindow(anchorWindow) : USER_DEFAULT_SCREEN_DPI;
    if (!BuildOverlaySurface(dpi))
    {
        g_surface.Reset();
        ShowNext();
        return;
    }

    g_startedAt = GetTickCount64();
    g_overlayWindow = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kOverlayClassName, L"FFKeyLock Overlay", WS_POPUP,
        0, 0, g_surface.metrics.bitmapWidth, g_surface.metrics.bitmapHeight,
        nullptr, nullptr, g_hInst, nullptr);
    if (!g_overlayWindow)
    {
        g_surface.Reset();
        ShowNext();
        return;
    }

    SetWindowPos(g_overlayWindow, HWND_TOPMOST, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    PresentOverlay();
}

bool SameOverlay(const OverlayItem& left, const OverlayItem& right)
{
    return left.kind == right.kind && left.title == right.title && left.message == right.message;
}

void Enqueue(OverlayKind kind, const std::wstring& title, const std::wstring& message)
{
    OverlayItem item{ kind, title, message };
    if ((g_overlayWindow && SameOverlay(g_current, item)) ||
        (!g_queue.empty() && SameOverlay(g_queue.back(), item)))
    {
        return;
    }
    if (g_queue.size() >= kMaximumQueuedItems)
    {
        g_queue.pop_front();
    }
    g_queue.push_back(std::move(item));
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
    g_surface.Reset();
    g_renderer.Reset();
}
}
