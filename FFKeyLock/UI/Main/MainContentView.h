#pragma once
#include "../../AppState.h"
#include <optional>
#include "../Library/GameLibraryView.h"

namespace FFKeyLock
{
class MainContentView
{
public:
    HWND Create(HWND parent);
    void Refresh(bool theme = false);
    std::wstring SelectedName() const { return selected_; }
    bool SavePending();
    bool ConfirmDiscard();
    void Select(const std::wstring& identity);
    void CopyProfile();
    void PasteProfile();
    HWND Window() const { return window_; }
private:
    HWND window_ = nullptr, list_ = nullptr, search_ = nullptr, editor_ = nullptr;
    UI::GameLibraryView library_;
    std::vector<std::wstring> visible_;
    std::wstring selected_, signature_;
    std::optional<GameProfile> copied_;
    bool refreshing_ = false;
    void CreateControls();
    void Layout();
    void RefreshList();
    void UpdateSelectionText();
    void LoadSelection();
    void UpdateDirty();
    static LRESULT CALLBACK Proc(HWND hwnd, UINT message, WPARAM w, LPARAM l);
};
}
