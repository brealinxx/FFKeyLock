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
    EnsureLayoutCallback ensureLayout = nullptr;
    ButtonHitTestCallback hitTestButton = nullptr;
    ButtonStateCallback setButtonState = nullptr;
    MouseDownCallback mouseDown = nullptr;
    MouseClickCallback click = nullptr;
    RightClickCallback rightClick = nullptr;
    MouseWheelCallback wheel = nullptr;
    int scrollY = 0;
    int contentHeight = 0;
    int hotButtonId = 0;
    int pressedButtonId = 0;
    bool draggingScrollThumb = false;
    bool suppressNextContextMenu = false;
    int dragStartY = 0;
    int dragStartScrollY = 0;
};

State* GetState(HWND hwnd)
{
    return reinterpret_cast<State*>(GetPropW(hwnd, kStateProp));
}

int MaxScroll(const State& state, int viewportHeight)
{
    return std::max(0, state.contentHeight - std::max(0, viewportHeight));
}

RECT ScrollTrackRect(const RECT& client)
{
    const int trackWidth = ThemeManager::Scale(8);
    return RECT{
        client.right - trackWidth,
        client.top + ThemeManager::Scale(4),
        client.right - ThemeManager::Scale(3),
        client.bottom - ThemeManager::Scale(4)
    };
}

RECT ScrollThumbRect(const RECT& track, const State& state, int viewportHeight, int maxScroll)
{
    const int trackHeight = std::max<int>(1, track.bottom - track.top);
    const int thumbHeight = std::max(ThemeManager::Scale(24), MulDiv(trackHeight, viewportHeight, state.contentHeight));
    const int thumbTop = track.top + MulDiv(trackHeight - thumbHeight, state.scrollY, std::max(1, maxScroll));
    return RECT{ track.left, thumbTop, track.right, thumbTop + thumbHeight };
}

void UpdateScrollBar(HWND hwnd, State& state, int viewportHeight)
{
    const int maxScroll = MaxScroll(state, viewportHeight);
    state.scrollY = std::clamp(state.scrollY, 0, maxScroll);
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

void EnsureLayoutForHitTest(HWND hwnd, State& state)
{
    if (state.ensureLayout)
    {
        state.ensureLayout(hwnd);
    }
    else
    {
        Layout(hwnd);
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
    const int maxScroll = MaxScroll(*state, height);
    const int nextScrollY = std::clamp(scrollY, 0, maxScroll);
    if (nextScrollY == state->scrollY)
    {
        return;
    }

    state->scrollY = nextScrollY;
    UpdateScrollBar(hwnd, *state, height);
    InvalidatePanel(hwnd);
}

void DrawScrollBar(HDC hdc, const RECT& client, const State& state)
{
    const int viewportHeight = client.bottom - client.top;
    const int maxScroll = MaxScroll(state, viewportHeight);
    if (maxScroll <= 0)
    {
        return;
    }

    RECT track = ScrollTrackRect(client);
    if (track.bottom <= track.top)
    {
        return;
    }

    HBRUSH trackBrush = CreateSolidBrush(ThemeManager::SurfaceColor());
    FillRect(hdc, &track, trackBrush);
    DeleteObject(trackBrush);

    RECT thumb = ScrollThumbRect(track, state, viewportHeight, maxScroll);
    GdiUtils::FillRoundRect(hdc, thumb, ThemeManager::MutedTextColor(), ThemeManager::MutedTextColor(), ThemeManager::Scale(4));
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

    if (State* state = GetState(hwnd); state && state->paint)
    {
        if (state->contentHeight <= 0 && state->layout)
        {
            state->contentHeight = state->layout(hwnd, client.right - client.left, client.bottom - client.top, state->scrollY);
            UpdateScrollBar(hwnd, *state, client.bottom - client.top);
        }
        state->paint(hwnd, drawDc, client, state->scrollY);
        DrawScrollBar(drawDc, client, *state);
    }

    buffer.Present();
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
        {
            RECT client{};
            GetClientRect(hwnd, &client);
            nextScrollY -= client.bottom - client.top;
            break;
        }
        case SB_PAGEDOWN:
        {
            RECT client{};
            GetClientRect(hwnd, &client);
            nextScrollY += client.bottom - client.top;
            break;
        }
        case SB_TOP:
            nextScrollY = 0;
            break;
        case SB_BOTTOM:
        {
            RECT client{};
            GetClientRect(hwnd, &client);
            nextScrollY = state->contentHeight - (client.bottom - client.top);
            break;
        }
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
        if (state && state->draggingScrollThumb)
        {
            RECT client{};
            GetClientRect(hwnd, &client);
            RECT track = ScrollTrackRect(client);
            const int maxScroll = MaxScroll(*state, client.bottom - client.top);
            RECT thumb = ScrollThumbRect(track, *state, client.bottom - client.top, maxScroll);
            const int trackRange = std::max<int>(1, (track.bottom - track.top) - (thumb.bottom - thumb.top));
            const int deltaY = GET_Y_LPARAM(lParam) - state->dragStartY;
            ScrollTo(hwnd, state->dragStartScrollY + MulDiv(deltaY, maxScroll, trackRange));
            return 0;
        }
        if (!state || !state->hitTestButton)
        {
            return 0;
        }
        EnsureLayoutForHitTest(hwnd, *state);
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
        if (!state)
        {
            return 0;
        }
        if (!state || !state->hitTestButton)
        {
            return 0;
        }
        EnsureLayoutForHitTest(hwnd, *state);
        POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        RECT client{};
        GetClientRect(hwnd, &client);
        const int maxScroll = MaxScroll(*state, client.bottom - client.top);
        if (maxScroll > 0)
        {
            RECT track = ScrollTrackRect(client);
            if (PtInRect(&track, point))
            {
                RECT thumb = ScrollThumbRect(track, *state, client.bottom - client.top, maxScroll);
                if (PtInRect(&thumb, point))
                {
                    state->draggingScrollThumb = true;
                    state->dragStartY = point.y;
                    state->dragStartScrollY = state->scrollY;
                    SetCapture(hwnd);
                }
                else
                {
                    ScrollTo(hwnd, state->scrollY + (point.y < thumb.top ? -(client.bottom - client.top) : (client.bottom - client.top)));
                }
                return 0;
            }
        }
        const int id = state->hitTestButton(hwnd, point, state->scrollY);
        if (id)
        {
            SetCapture(hwnd);
            SetPressedButton(hwnd, *state, id);
            return 0;
        }
        if (state->mouseDown && state->mouseDown(hwnd, point, state->scrollY))
        {
            SetHotButton(hwnd, *state, 0);
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
        if (state->draggingScrollThumb)
        {
            state->draggingScrollThumb = false;
            if (GetCapture() == hwnd)
            {
                ReleaseCapture();
            }
            return 0;
        }
        const int pressedId = state->pressedButtonId;
        EnsureLayoutForHitTest(hwnd, *state);
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

    case WM_RBUTTONUP:
    {
        State* state = GetState(hwnd);
        if (state)
        {
            EnsureLayoutForHitTest(hwnd, *state);
            if (state->rightClick)
            {
                POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                state->rightClick(hwnd, point, state->scrollY);
                state->suppressNextContextMenu = true;
                return 0;
            }
        }
        return 0;
    }

    case WM_CONTEXTMENU:
    {
        State* state = GetState(hwnd);
        if (state)
        {
            EnsureLayoutForHitTest(hwnd, *state);
            if (state->suppressNextContextMenu && lParam != -1)
            {
                state->suppressNextContextMenu = false;
                return 0;
            }
            if (state->rightClick && lParam != -1)
            {
                POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                ScreenToClient(hwnd, &point);
                state->rightClick(hwnd, point, state->scrollY);
                return 0;
            }
        }
        if (lParam == -1)
        {
            return SendMessageW(GetParent(hwnd), message, wParam, lParam);
        }
        return 0;
    }

    case WM_COMMAND:
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
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
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

void SetEnsureLayoutCallback(HWND panel, EnsureLayoutCallback callback)
{
    State* state = GetState(panel);
    if (state)
    {
        state->ensureLayout = callback;
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

void SetMouseDownCallback(HWND panel, MouseDownCallback callback)
{
    State* state = GetState(panel);
    if (state)
    {
        state->mouseDown = callback;
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

void SetRightClickCallback(HWND panel, RightClickCallback callback)
{
    State* state = GetState(panel);
    if (state)
    {
        state->rightClick = callback;
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
