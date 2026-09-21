#pragma once
#include <array>
#include <bitset>

namespace FFKeyLock
{
// A press keeps its original disposition until release, including across focus
// and profile changes. A delivered down must always receive its matching up.
class KeyPressTracker
{
    std::array<bool, 256> held_{};
    std::array<bool, 256> blocked_{};
public:
    void Seed(unsigned key, bool held) { if (key < 256) held_[key] = held; }
    bool IsHeld(unsigned key) const { return key < 256 && held_[key]; }
    bool Process(unsigned key, bool down, bool up, bool shouldBlock, bool momentary = false, bool releaseOnly = false)
    {
        if (key >= 256 || (!down && !up)) return false;
        if (momentary) return shouldBlock;
        if (releaseOnly && up && !held_[key]) return shouldBlock;
        if (down && !held_[key]) { held_[key] = true; blocked_[key] = shouldBlock; }
        const bool result = held_[key] && blocked_[key];
        if (up) { held_[key] = false; blocked_[key] = false; }
        return result;
    }
};
}
