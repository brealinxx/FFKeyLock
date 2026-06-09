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
    PaintCallback paint = nullptr;
    ButtonHitTestCallback hitTestButton = nullptr;
    ButtonStateCallback setButtonState = nullptr;
    MouseClickCallback click = nullptr;
    MouseWheelCallback wheel = nullptr;
    int scrollY = 0;
    int contentHeight = 0;
    int hotButtonId = 0;
    int pressedButtonId = 0;
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

void InvalidatePanel(HWND hwnd)
{
    InvalidateRect(hwnd, nullptr, TRUE);
}

void RedrawPanel(HWND hwnd)
{
    RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE);
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

    const int oldScrollY = state->scrollY;
    state->contentHeight = state->layout(hwnd, width, height, state->scrollY);
    UpdateScrollBar(hwnd, *state, height);
    if (state->scrollY != oldScrollY)
    {
        state->contentHeight = state->layout(hwnd, width, height, state->scrollY);
        UpdateScrollBar(hwnd, *state, height);
    }
}

void SetHotButton(HWND hwnd, State& state, int id)
{
    if (id == state.hotButtonId)
    {
        return;
    }
    if (state.setButtonState && state.hotButtonId)
    {
        state.setButtonState(hwnd, state.hotButtonId, false, state.pressedButtonId == state.hotButtonId);
    }
    state.hotButtonId = id;
    if (state.setButtonState && state.hotButtonId)
    {
        state.setButtonState(hwnd, state.hotButtonId, true, state.pressedButtonId == state.hotButtonId);
    }
    InvalidatePanel(hwnd);
}

void SetPressedButton(HWND hwnd, State& state, int id)
{
    if (id == state.pressedButtonId)
    {
        return;
    }
    if (state.setButtonState && state.pressedButtonId)
    {
        state.setButtonState(hwnd, state.pressedButtonId, state.hotButtonId == state.pressedButtonId, false);
    }
    state.pressedButtonId = id;
    if (state.setButtonState && state.pressedButtonId)
    {
        state.setButtonState(hwnd, state.pressedButtonId, state.hotButtonId == state.pressedButtonId, true);
    }
    InvalidatePanel(hwnd);
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
    UpdateScrollBar(hwnd, *state, height);
    InvalidatePanel(hwnd);
}

void Paint(HWND hwnd)
{
    PAINTSTRUCT paint{};
    HDC hdc = BeginPaint(hwnd, &paint);
    RECT client{};
    GetClientRect(hwnd, &client);
    if (IsRectEmpty(&paint.rcPaint))
    {
        EndPaint(hwnd, &paint);
        return;
    }

    GdiUtils::BufferedPaint buffer(hdc, client, paint.rcPaint);
    HDC drawDc = buffer.Dc();
    FillRect(drawDc, &client, ThemeManager::WindowBrush() ? ThemeManager::WindowBrush() : reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));

    HRGN paintRegion = CreateRectRgn(paint.rcPaint.left, paint.rcPaint.top, paint.rcPaint.right, paint.rcPaint.bottom);
    SelectClipRgn(drawDc, paintRegion);
    if (State* state = GetState(hwnd); state && state->paint)
    {
        if (state->contentHeight <= 0 && state->layout)
        {
            state->contentHeight = state->layout(hwnd, client.right - client.left, client.bottom - client.top, state->scrollY);
            UpdateScrollBar(hwnd, *state, client.bottom - client.top);
        }
        state->paint(hwnd, drawDc, client, state->scrollY);
    }
    SelectClipRgn(drawDc, nullptr);
    DeleteObject(paintRegion);

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
        if (state && state->wheel)
        {
            POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hwnd, &point);
            if (state->wheel(hwnd, point, state->scrollY, GET_WHEEL_DELTA_WPARAM(wParam)))
            {
                InvalidatePanel(hwnd);
                return 0;
            }
        }
        const int current = state ? state->scrollY : 0;
        ScrollTo(hwnd, current - GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA * ThemeManager::Scale(72));
        return 0;
    }

    case WM_MOUSEMOVE:
    {
        State* state = GetState(hwnd);
        if (!state || !state->hitTestButton)
        {
            return 0;
        }
        POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        const int hotId = state->hitTestButton(hwnd, point, state->scrollY);
        SetHotButton(hwnd, *state, hotId);
        TRACKMOUSEEVENT track{ sizeof(track), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&track);
        return 0;
    }

    case WM_MOUSELEAVE:
    {
        State* state = GetState(hwnd);
        if (state)
        {
            SetHotButton(hwnd, *state, 0);
            SetPressedButton(hwnd, *state, 0);
        }
        return 0;
    }

    case WM_LBUTTONDOWN:
    {
        State* state = GetState(hwnd);
        if (!state || !state->hitTestButton)
        {
            return 0;
        }
        POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        const int id = state->hitTestButton(hwnd, point, state->scrollY);
        if (id)
        {
            SetCapture(hwnd);
            SetPressedButton(hwnd, *state, id);
        }
        return 0;
    }

    case WM_LBUTTONUP:
    {
        State* state = GetState(hwnd);
        if (!state)
        {
            return 0;
        }
        const int pressedId = state->pressedButtonId;
        POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        const int releasedId = state->hitTestButton ? state->hitTestButton(hwnd, point, state->scrollY) : 0;
        if (GetCapture() == hwnd)
        {
            ReleaseCapture();
        }
        SetPressedButton(hwnd, *state, 0);
        SetHotButton(hwnd, *state, releasedId);
        if (pressedId && pressedId == releasedId)
        {
            SendMessageW(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(pressedId, BN_CLICKED), 0);
        }
        else if (!pressedId && state->click)
        {
            state->click(hwnd, point, state->scrollY);
        }
        return 0;
    }

    case WM_ERASEBKGND:
    {
        RECT client{};
        GetClientRect(hwnd, &client);
        HDC hdc = reinterpret_cast<HDC>(wParam);
        FillRect(hdc, &client, ThemeManager::WindowBrush() ? ThemeManager::WindowBrush() : reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));
        return TRUE;
    }

    case WM_PAINT:
        Paint(hwnd);
        return 0;

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
    wc.style = 0;
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
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPSIBLINGS,
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

void SetPaintCallback(HWND panel, PaintCallback callback)
{
    State* state = GetState(panel);
    if (state)
    {
        state->paint = callback;
    }
}

void SetButtonHitTestCallback(HWND panel, ButtonHitTestCallback callback)
{
    State* state = GetState(panel);
    if (state)
    {
        state->hitTestButton = callback;
    }
}

void SetButtonStateCallback(HWND panel, ButtonStateCallback callback)
{
    State* state = GetState(panel);
    if (state)
    {
        state->setButtonState = callback;
    }
}

void SetMouseClickCallback(HWND panel, MouseClickCallback callback)
{
    State* state = GetState(panel);
    if (state)
    {
        state->click = callback;
    }
}

void SetMouseWheelCallback(HWND panel, MouseWheelCallback callback)
{
    State* state = GetState(panel);
    if (state)
    {
        state->wheel = callback;
    }
}

void Relayout(HWND panel)
{
    if (!panel)
    {
        return;
    }
    Layout(panel);
    InvalidatePanel(panel);
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
        InvalidatePanel(panel);
    }
}

int ScrollY(HWND panel)
{
    State* state = GetState(panel);
    return state ? state->scrollY : 0;
}
}
}
