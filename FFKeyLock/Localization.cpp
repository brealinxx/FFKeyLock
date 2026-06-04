#include "Localization.h"

#include "AppState.h"

namespace FFKeyLock
{
bool IsEnglish()
{
    return g_language == UiLanguage::English;
}

const wchar_t* Text(const wchar_t* chinese, const wchar_t* english)
{
    return IsEnglish() ? english : chinese;
}
}
