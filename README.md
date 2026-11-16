# Fusion Fix Red Dead Redemption II
![Language](https://img.shields.io/badge/LANGUAGE-C%23-239120?logo=csharp&logoColor=white)
[![Latest Release](https://badgen.net/github/tag/Nochala/RDR2_Fusion_Fix?label=Version&color=blue)](https://github.com/Nochala/RDR2_Fusion_Fix/releases/latest)
[![Last Commit](https://badgen.net/github/last-commit/Nochala/RDR2_Fusion_Fix?label=Last%20Commit&color=purple)](https://github.com/Nochala/RDR2_Fusion_Fix/commits/main)
![Status](https://badgen.net/badge/Status/WIP/orange)

A big project of mine that's deeply inspired by GTA IV Fusion Fix by ThirteenAG. The goal of this mod is to address Red Dead Redemption II engine limitations and performance issues while also introducing a wide range of gameplay improvements and quality-of-life features. 


## Features

The options below are listed in the same order as `FusionFixRDR2.ini`. Expand a section to see its features.

<details>
<summary><strong>General</strong></summary>

<p><strong><code>Enabled</code></strong> - Turns Fusion Fix RDR2 on or off without needing to remove the mod.</p>

<p><strong><code>SettingsMenu</code></strong> - Adds Fusion Fix Settings under Settings → General so you can change the mod’s options in-game.</p>

<p><strong><code>Logging</code></strong> - Enables the mod’s regular log output.</p>

<p><strong><code>DebugLogging</code></strong> - Enables extra debug logging for troubleshooting.</p>

</details>

<details>
<summary><strong>Startup</strong></summary>

<p><strong><code>SkipIntroSplashes</code></strong> - Skips the Rockstar intro splash when starting the game.</p>

<p><strong><code>SkipLegal</code></strong> - Skips the legal notices shown during startup.</p>

<p><strong><code>StraightToStoryMode</code></strong> - Loads straight into Story Mode instead of stopping at the main menu.</p>

</details>

<details>
<summary><strong>Engine</strong></summary>

<p><strong><code>YTDCrashFix</code></strong> - Fixes crashes related to YTD texture files.</p>

<p><strong><code>ExtendHeapLimits</code></strong> - Raises the engine’s heap memory limit to help avoid allocation failures.</p>

<p><strong><code>ExtendPackfileLimits</code></strong> - Increases the number of packfiles the engine can handle, helping avoid loading failures and crashes.</p>

<p><strong><code>ExtendStreamingPools</code></strong> - Raises streaming pool limits to help prevent crashes and other problems when those pools fill up.</p>

<p><strong><code>ExtendAssetPools</code></strong> - Raises asset pool limits to improve stability and help reduce texture pop-in.</p>

<p><strong><code>OptimizeStreamingMemory</code></strong> - Improves how streaming memory is allocated to reduce overhead and smooth out frametimes.</p>

<p><strong><code>TextureStreamingStutterFix</code></strong> - Reduces stuttering tied to texture streaming and asset loading.</p>

<p><strong><code>ImproveTextureStreaming</code></strong> - Expands streaming read buffers to help prevent delays when loading assets in demanding areas. Separate from TextureStreamingStutterFix.</p>

<p><strong><code>ExtendPedMemoryLimits</code></strong> - Raises pedestrian memory limits to reduce NPC pop-in and keep population streaming more consistent.</p>

<h4>Experimental</h4>

<p><strong><code>ExpandSSRCoverage</code></strong> - Experimental attempt to extend screen-space reflection coverage using FOV and shader changes. Currently buggy and Vulkan-only.</p>

<p><strong><code>SSROverscanScale</code></strong> - Adjusts the overscan scale used by the experimental screen-space reflection feature.</p>

<p><strong><code>SSROverscanTestMode</code></strong> - Selects the test mode for the experimental screen-space reflection feature. Test modes are not described in the current INI.</p>

</details>

<details>
<summary><strong>Gameplay</strong></summary>

<p><strong><code>DisableAutoAmmoPickup</code></strong> - Stops ammo from being picked up automatically from bodies and dropped weapons.</p>

<p><strong><code>DisableAutoHorseEquip</code></strong> - Stops the game from automatically equipping weapons when you dismount during missions.</p>

<p><strong><code>DisableAutoHorseHolster</code></strong> - Keeps the game from automatically holstering your weapons while riding.</p>

<p><strong><code>DisableOutOfBoundSnipers</code></strong> - Removes the invisible snipers that kill you when entering certain restricted or out-of-bounds areas.</p>

<h4>Honor</h4>

<p><strong><code>DisableHonorLossWhenMasked</code></strong> - Prevents honor loss from crimes committed while wearing a mask or face covering.</p>

<p><strong><code>DisableBodySearchHonorLoss</code></strong> - Stops you from losing honor when searching dead bodies.</p>

<p><strong><code>DisableAntagonizehHonorLoss</code></strong> - Stops antagonizing NPCs from lowering your honor.</p>

<p><strong><code>FixSelfDefenseHonorLoss</code></strong> - Prevents honor loss for defending yourself against someone who attacked first.</p>

<p><strong><code>DisableHonorSystem</code></strong> - Freezes your current honor level so it can no longer go up or down.</p>

<h4>Bounty</h4>

<p><strong><code>DisableBountyWhenMasked</code></strong> - Prevents bounties from being issued for crimes committed while masked.</p>

<p><strong><code>FixSelfDefenseBounties</code></strong> - Prevents bounties for defending yourself against someone who attacked first.</p>

<p><strong><code>DisableBounties</code></strong> - Prevents new bounties while keeping the wanted system active.</p>

<h4>Horse</h4>

<p><strong><code>FixHorseFollowBehavior</code></strong> — Improves how your horse follows you so it is less likely to fall behind or get stuck.</p>

<p><strong><code>DisableHorseAutoJump</code></strong> — Stops your horse from jumping obstacles automatically. You can still jump manually.</p>

<p><strong><code>DisableHorseWhistleDistance</code></strong> — Removes the distance limit for calling your horse with a whistle.</p>

</details>

<details>
<summary><strong>Player</strong></summary>

<p><strong><code>DisableFirstPersonAutoRun</code></strong> — Makes walking the default in first person instead of automatically running.</p>

<p><strong><code>QuickLootBodies</code></strong> — Uses the quicker fallback animation when looting bodies.</p>

<p><strong><code>QuickLootAnimals</code></strong> — Uses the quicker fallback animation when looting animals.</p>

</details>

<details>
<summary><strong>Weapons</strong></summary>

<p><strong><code>DisableEmptyAutoReload</code></strong> — Stops your weapon from reloading automatically when its ammunition runs out.</p>

<p><strong><code>DisableAutoReload</code></strong> — Disables automatic weapon reloading in general.</p>

<p><strong><code>DisableDeadEyeAutoReload</code></strong> — Stops the automatic reload that happens while using Dead Eye.</p>

<p><strong><code>DisableForcedScope</code></strong> — Lets you use compatible rifles, such as the Carcano and Rolling Block, without being forced into their scopes.</p>

<p><strong><code>DisableHitmarkers</code></strong> — Removes the hitmarkers shown when you shoot enemies.</p>

<p><strong><code>DisableReticleHighlighting</code></strong> — Stops the aiming reticle from changing color when aimed at an enemy.</p>

<p><strong><code>DisableArrowTrails</code></strong> — Removes the arrow trajectory trail shown while aiming a bow.</p>

<p><strong><code>DisableReticleSpread</code></strong> — Stops the aiming reticle from spreading when you aim and fire.</p>

<p><strong><code>DisableReticleSway</code></strong> — Stops the aiming reticle from swaying when you aim and fire.</p>

<p><strong><code>DisableWeaponDegradation</code></strong> — Prevents weapons from wearing down over time, so they no longer need cleaning.</p>

</details>

<details>
<summary><strong>Visual</strong></summary>

<p><em>The recoil reduction settings at the bottom of this section are marked broken in the current INI.</em></p>

<p><strong><code>RemoveBlackBars</code></strong> — Removes the black bars that appear during cutscenes and cinematic sequences.</p>

<p><strong><code>EnableDLSSAutoExposure</code></strong> — Enables auto exposure with DLSS or DLAA to help reduce visual artifacts.</p>

<p><strong><code>FixFullscreenTabOut</code></strong> — Restores fullscreen mode automatically when you tab back into the game.</p>

<p><strong><code>MinimizeOnTabOut</code></strong> — Minimizes the game window when you Alt+Tab away.</p>

<p><strong><code>DisableIdleCamera</code></strong> — Stops the automatic cinematic camera that appears when you leave the game idle.</p>

<p><strong><code>ReduceCameraShake</code></strong> — Cuts down on camera shake during gameplay.</p>

<p><strong><code>ReduceCameraSway</code></strong> — Cuts down on camera sway during gameplay.</p>

<p><strong><code>DisableHeadBob</code></strong> — Removes the head-bobbing effect while moving.</p>

<p><strong><code>CameraFOVOffset</code></strong> — Adjusts the camera FOV beyond the normal limits. Positive values widen it; negative values narrow it. Range: -3.0 to 3.0. Use 0.0 to disable.</p>

<h4>World</h4>

<p><strong><code>DisableInteriorLighting</code></strong> — Disables interior lighting effects in buildings and other enclosed spaces.</p>

<p><strong><code>DisableDistantLights</code></strong> — Disables distant light sources. This can cause bloom artifacts.</p>

<h4>Filters</h4>

<p><strong><code>DisableDrunkFilter</code></strong> — Removes the visual distortion that appears when your character is drunk.</p>

<p><strong><code>DisableEagleEyeFilter</code></strong> — Removes the screen filter applied during Eagle Eye.</p>

<p><strong><code>DisableDeadEyeFilter</code></strong> — Removes the screen filter applied during Dead Eye.</p>

<p><strong><code>DisablePauseMenuFilter</code></strong> — Removes the visual filter shown behind the pause menu.</p>

<p><strong><code>DisableWeaponWheelFilter</code></strong> — Removes the screen filter shown while the weapon wheel is open.</p>

<p><strong><code>DisableLowHealthFilter</code></strong> — Removes the visual effects that appear when your health is critically low.</p>

<h4>Experimental - currently broken</h4>

<p><strong><code>ReduceWeaponRecoil</code></strong> — Experimental recoil reduction for firing weapons. Marked as broken in the current INI.</p>

<p><strong><code>RecoilShakeAmplitudeScale</code></strong> — Sets the recoil reduction amount for ReduceWeaponRecoil. In the INI, 0.00 means maximum reduction and 0.50 means 50% reduction. The parent feature is currently marked broken.</p>

<p><strong><code>ReduceFirstPersonWeaponRecoil</code></strong> — Experimental first-person recoil reduction. Marked as broken in the current INI.</p>

<p><strong><code>FirstPersonRecoilShakeAmplitudeScale</code></strong> — Sets the first-person recoil reduction amount. In the INI, 0.00 means maximum reduction and 0.50 means 50% reduction. The parent feature is currently marked broken.</p>

</details>

<details>
<summary><strong>Ambience</strong></summary>

<p><em>The first four audio fixes do not have descriptions in the current INI, so their explanations are based on the option names.</em></p>

<p><strong><code>FixAmbientAudioDropouts</code></strong> — Intended to stop ambient sounds from unexpectedly cutting out.</p>

<p><strong><code>FixWeatherAudioTransitions</code></strong> — Intended to make audio transitions between weather conditions more consistent.</p>

<p><strong><code>FixInteriorAudioOcclusion</code></strong> — Intended to address how ambient sound is muffled or blocked inside interiors.</p>

<p><strong><code>FixAmbientAudioLoops</code></strong> — Intended to fix problems with repeating ambient sound loops.</p>

<p><strong><code>DisableAmbientMusic</code></strong> — Turns off the background music that plays while exploring.</p>

<p><strong><code>DisableCombatMusic</code></strong> — Turns off music that starts during combat.</p>

<p><strong><code>DisableWantedMusic</code></strong> — Turns off music that plays while you are wanted.</p>

</details>

<details>
<summary><strong>Diagnostics</strong></summary>

<p><em>These diagnostic settings are not described in the current INI. Their purposes are inferred from their names.</em></p>

<p><strong><code>DumpNativeHashes</code></strong> — Diagnostic option for dumping native hashes.</p>

<p><strong><code>CrashDiagnostics</code></strong> — Diagnostic option related to crashes.</p>

<p><strong><code>DetectConflictingPatches</code></strong> — Diagnostic option for checking for potentially conflicting patches.</p>

</details>


### Requirements
- [**Latest ASI loader**](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases) (preferably dinput8.dll)
- [**Latest ScriptHookRDR2**](https://www.dev-c.com/rdr2/scripthookrdr2/)


### License 

Fusion Fix RDR2 is licensed under the GNU General Public License v3.0 (GPL-3.0).

Copyright © 2026 Nochala.

You are free to view, modify, and redistribute the source code in accordance with the GPL-3.0 license.

All redistributed versions must preserve the original copyright and licensing notices. Modified versions must clearly indicate that changes have been made and comply with the GPL-3.0 license.
