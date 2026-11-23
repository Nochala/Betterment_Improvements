#pragma once

namespace Main::Horse
{
    void Initialize(bool noAutoHorseEquip, bool noAutoHorseHolster, bool noHorseAutoJump,
        bool noHorseWhistleDistance, bool horseFollowBehaviorFix);
    void Update();
    void Shutdown();
    bool NeedsAutoJumpFrameUpdate();
    bool NeedsFastUpdate();
    bool NeedsPerFrameUpdate();
}
