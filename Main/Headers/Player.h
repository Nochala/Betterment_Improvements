#pragma once

namespace Main::Player
{
    void Initialize(bool disableFirstPersonAutoRun, bool quickLootBodies, bool quickLootAnimals);
    void Update();
    bool NeedsFastUpdate();
}
