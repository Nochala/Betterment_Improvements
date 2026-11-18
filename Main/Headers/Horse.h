#pragma once

namespace Main::Horse
{
    void Initialize(bool noAutoHorseEquip, bool noAutoHorseHolster, bool noHorseAutoJump,
        bool noHorseWhistleDistance, bool horseFollowBehaviorFix);
    void Update();
    bool NeedsFastUpdate();
    bool NeedsPerFrameUpdate();
}
