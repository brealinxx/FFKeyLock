#include "ContentPanel.h"

#include "../../Platform/GdiUtils.h"
#include "../../ThemeManager.h"

#include <algorithm>
#include <windowsx.h>

namespace FFKeyLock
{
namespace ContentPanel
{
namespace
{
constexpr wchar_t kClassName[] = L"FFKeyLockContentPanel";
constexpr wchar_t kStateProp[] = L"FFKeyLock.ContentPanelState";

struct State
{
    LayoutCallback layout = nullptr;
    StaticCtlColorCallback ctlColorStatic = nullptr;
    int scrollY = 0;
    int contentHeight = 0;
};

State* GetState(HWND hwnd)
{
    return reinterpret_cast<State*>(GetPropW(hwnd, kStateProp));
}

void UpdateScrollBar(HWND hwnd, State& state, int viewportHeight)
{
    const int maxScroll = std::max(0, state.contentHeight - std::max(0, viewportHeight));
    state.scrollY = std::clamp(state.scrollY, 0, maxScroll);

    SCROLLINFO info{};
    info.cbSize = sizeof(info);
    info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    info.nMin = 0;
    info.nMax = std::max(0, state.contentHeight - 1);
    info.nPage = static_cast<UINT>(std::max(0, viewportHeight));
    info.nPos = state.scrollY;
    SetScrollInfo(hwnd, SB_VERT, &info, TRUE);
    ShowScrollBar(hwnd, SB_VERT, maxScroll > 0);
}

void RedrawPanel(HWND hwnd)
{
    RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

void Layout(HWND hwnd)
{
    State* state = GetState(hwnd);
    if (!state || !state->layout)
    {
        return;
    }

    RECT client{};
    GetClientRect(hwnd, &client);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;

    state->contentHeight = state->layout(hwnd, width, height, state->scrollY);
    UpdateScrollBar(hwnd, *state, height);
    state->contentHeight = state->layout(hwnd, width, height, state->scrollY);
    UpdateScrollBar(hwnd, *state, height);
}

void ScrollTo(HWND hwnd, int scrollY)
{
    State* state = GetState(hwnd);
    if (!state)
    {
        return;
    }

    RECT client{};
    GetClientRect(hwnd, &client);
    const int height = client.bottom - client.top;
    const int maxScroll = std::max(0, state->contentHeight - std::max(0, height));
    const int nextScrollY = std::clamp(scrollY, 0, maxScroll);
    if (nextScrollY == state->scrollY && GetScrollPos(hwnd, SB_VERT) == state->scrollY)
    {
        return;
    }

    state->scrollY = nextScrollY;
    Layout(hwnd);
    RedrawPanel(hwnd);
}

void Paint(HWND hwnd)
{
    PAINTSTRUCT paint{};
    HDC hdc = BeginPaint(hwnd, &paint);
    RECT client{};
    GetClientRect(hwnd, &client);

    GdiUtils::BufferedPaint buffer(hdc, client);
    HDC drawDc = buffer.Dc();
    FillRect(drawDc, &client, ThemeManager::WindowBrush() ? ThemeManager::WindowBrush() : reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));

    EndPaint(hwnd, &paint);
}

LRESULT CALLBACK Proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_NCCREATE:
    {
        auto* state = new State();
        SetPropW(hwnd, kStateProp, state);
        return TRUE;
    }

    case WM_NCDESTROY:
    {
        auto* state = GetState(hwnd);
        RemovePropW(hwnd, kStateProp);
        delete state;
        break;
    }

    case WM_SIZE:
        Layout(hwnd);
        RedrawPanel(hwnd);
        return 0;

    case WM_VSCROLL:
    {
        State* state = GetState(hwnd);
        if (!state)
        {
            return 0;
        }

        SCROLLINFO info{};
        info.cbSize = sizeof(info);
        info.fMask = SIF_ALL;
        GetScrollInfo(hwnd, SB_VERT, &info);

        int nextScrollY = state->scrollY;
        switch (LOWORD(wParam))
        {
        case SB_LINEUP:
            nextScrollY -= ThemeManager::Scale(32);
            break;
        case SB_LINEDOWN:
            nextScrollY += ThemeManager::Scale(32);
            break;
        case SB_PAGEUP:
            nextScrollY -= static_cast<int>(info.nPage);
            break;
        case SB_PAGEDOWN:
            nextScrollY += static_cast<int>(info.nPage);
            break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION:
            nextScrollY = info.nTrackPos;
            break;
        case SB_TOP:
            nextScrollY = 0;
            break;
        case SB_BOTTOM:
            nextScrollY = info.nMax;
            break;
        default:
            break;
        }
        ScrollTo(hwnd, nextScrollY);
        return 0;
    }

    case WM_MOUSEWHEEL:
    {
        State* state = GetState(hwnd);
        const int current = state ? state->scrollY : 0;
        ScrollTo(hwnd, current - GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA * ThemeManager::Scale(72));
        return 0;
    }

    case WM_ERASEBKGND:
        return TRUE;

    case WM_PAINT:
        Paint(hwnd);
        return 0;

    case WM_CTLCOLORSTATIC:
    {
        State* state = GetState(hwnd);
        HDC hdc = reinterpret_cast<HDC>(wParam);
        HWND control = reinterpret_cast<HWND>(lParam);
        if (state && state->ctlColorStatic)
        {
            return reinterpret_cast<LRESULT>(state->ctlColorStatic(hwnd, hdc, control));
        }
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, ThemeManager::TextColor());
        return reinterpret_cast<LRESULT>(ThemeManager::WindowBrush() ? ThemeManager::WindowBrush() : GetStockObject(WHITE_BRUSH));
    }

    case WM_COMMAND:
    case WM_CONTEXTMENU:
    case WM_MEASUREITEM:
    case WM_DRAWITEM:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORBTN:
        return SendMessageW(GetParent(hwnd), message, wParam, lParam);
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}
}

void Register(HINSTANCE instance)
{
    static bool registered = false;
    if (registered)
    {
        return;
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = Proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);
    registered = true;
}

HWND Create(HWND parent, HINSTANCE instance)
{
    Register(instance);
    return CreateWindowExW(
        0,
        kClassName,
        L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_VSCROLL,
        0,
        0,
        0,
        0,
        parent,
        nullptr,
        instance,
        nullptr);
}

void SetLayoutCallback(HWND panel, LayoutCallback callback)
{
    State* state = GetState(panel);
    if (state)
    {
        state->layout = callback;
    }
}

void SetStaticCtlColorCallback(HWND panel, StaticCtlColorCallback callback)
{
    State* state = GetState(panel);
    if (state)
    {
        state->ctlColorStatic = callback;
    }
}

void Relayout(HWND panel)
{
    if (!panel)
    {
        return;
    }
    Layout(panel);
    RedrawPanel(panel);
}

void SetScrollY(HWND panel, int scrollY)
{
    if (!panel)
    {
        return;
    }
    State* state = GetState(panel);
    if (state)
    {
        state->scrollY = std::max(0, scrollY);
        Layout(panel);
        RedrawPanel(panel);
    }
}

int ScrollY(HWND panel)
{
    State* state = GetState(panel);
    return state ? state->scrollY : 0;
}
}
}
