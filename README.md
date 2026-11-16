# Fusion Fix Red Dead Redemption II
![Language](https://img.shields.io/badge/LANGUAGE-C%23-239120?logo=csharp&logoColor=white)
[![Latest Release](https://badgen.net/github/tag/Nochala/RDR2_Fusion_Fix?label=Version&color=blue)](https://github.com/Nochala/RDR2_Fusion_Fix/releases/latest)
[![Last Commit](https://badgen.net/github/last-commit/Nochala/RDR2_Fusion_Fix?label=Last%20Commit&color=purple)](https://github.com/Nochala/RDR2_Fusion_Fix/commits/main)
![Status](https://badgen.net/badge/Status/WIP/orange)

A big project of mine that's deeply inspired by GTA IV Fusion Fix by ThirteenAG. The goal of this mod is to address Red Dead Redemption II engine limitations and performance issues while also introducing a wide range of gameplay improvements and quality-of-life features. 


## Features

Every option below uses the same name and order as the `FusionFixRDR2.ini` file. Open a section, then select any setting to see what it does.

<details>
<summary><strong>General</strong></summary>

<details>
<summary><code>Enabled</code></summary>
<p>Turns Fusion Fix RDR2 on or off without needing to remove the mod.</p>
</details>

<details>
<summary><code>SettingsMenu</code></summary>
<p>Adds Fusion Fix Settings under Settings → General so you can change the mod’s options in-game.</p>
</details>

<details>
<summary><code>Logging</code></summary>
<p>Enables the mod’s regular log output.</p>
</details>

<details>
<summary><code>DebugLogging</code></summary>
<p>Enables extra debug logging for troubleshooting.</p>
</details>

</details>

<details>
<summary><strong>Startup</strong></summary>

<details>
<summary><code>SkipIntroSplashes</code></summary>
<p>Skips the Rockstar intro splash when starting the game.</p>
</details>

<details>
<summary><code>SkipLegal</code></summary>
<p>Skips the legal notices shown during startup.</p>
</details>

<details>
<summary><code>StraightToStoryMode</code></summary>
<p>Loads straight into Story Mode instead of stopping at the main menu.</p>
</details>

</details>

<details>
<summary><strong>Engine</strong></summary>

<p><em>The SSR settings at the bottom of this section are experimental, currently buggy, and only described as working with Vulkan.</em></p>

<details>
<summary><code>YTDCrashFix</code></summary>
<p>Fixes crashes related to YTD texture files.</p>
</details>

<details>
<summary><code>ExtendHeapLimits</code></summary>
<p>Raises the engine’s heap memory limit to help avoid allocation failures.</p>
</details>

<details>
<summary><code>ExtendPackfileLimits</code></summary>
<p>Increases the number of packfiles the engine can handle, helping avoid loading failures and crashes.</p>
</details>

<details>
<summary><code>ExtendStreamingPools</code></summary>
<p>Raises streaming pool limits to help prevent crashes and other problems when those pools fill up.</p>
</details>

<details>
<summary><code>ExtendAssetPools</code></summary>
<p>Raises asset pool limits to improve stability and help reduce texture pop-in.</p>
</details>

<details>
<summary><code>OptimizeStreamingMemory</code></summary>
<p>Improves how streaming memory is allocated to reduce overhead and smooth out frametimes.</p>
</details>

<details>
<summary><code>TextureStreamingStutterFix</code></summary>
<p>Reduces stuttering tied to texture streaming and asset loading.</p>
</details>

<details>
<summary><code>ImproveTextureStreaming</code></summary>
<p>Expands streaming read buffers to help prevent delays when loading assets in demanding areas. Separate from TextureStreamingStutterFix.</p>
</details>

<details>
<summary><code>ExtendPedMemoryLimits</code></summary>
<p>Raises pedestrian memory limits to reduce NPC pop-in and keep population streaming more consistent.</p>
</details>

<h4>Experimental - work in progress</h4>

<details>
<summary><code>ExpandSSRCoverage</code></summary>
<p>Experimental attempt to extend screen-space reflection coverage using FOV and shader changes. Currently buggy and Vulkan-only.</p>
</details>

<details>
<summary><code>SSROverscanScale</code></summary>
<p>Adjusts the overscan scale used by the experimental screen-space reflection feature.</p>
</details>

<details>
<summary><code>SSROverscanTestMode</code></summary>
<p>Selects the test mode for the experimental screen-space reflection feature. Test modes are not described in the current INI.</p>
</details>

</details>

<details>
<summary><strong>Gameplay</strong></summary>

<details>
<summary><code>DisableAutoAmmoPickup</code></summary>
<p>Stops ammo from being picked up automatically from bodies and dropped weapons.</p>
</details>

<details>
<summary><code>DisableAutoHorseEquip</code></summary>
<p>Stops the game from automatically equipping weapons when you dismount during missions.</p>
</details>

<details>
<summary><code>DisableAutoHorseHolster</code></summary>
<p>Keeps the game from automatically holstering your weapons while riding.</p>
</details>

<details>
<summary><code>DisableOutOfBoundSnipers</code></summary>
<p>Removes the invisible snipers that kill you when entering certain restricted or out-of-bounds areas.</p>
</details>

<h4>Honor</h4>

<details>
<summary><code>DisableHonorLossWhenMasked</code></summary>
<p>Prevents honor loss from crimes committed while wearing a mask or face covering.</p>
</details>

<details>
<summary><code>DisableBodySearchHonorLoss</code></summary>
<p>Stops you from losing honor when searching dead bodies.</p>
</details>

<details>
<summary><code>DisableAntagonizehHonorLoss</code></summary>
<p>Stops antagonizing NPCs from lowering your honor.</p>
</details>

<details>
<summary><code>FixSelfDefenseHonorLoss</code></summary>
<p>Prevents honor loss for defending yourself against someone who attacked first.</p>
</details>

<details>
<summary><code>DisableHonorSystem</code></summary>
<p>Freezes your current honor level so it can no longer go up or down.</p>
</details>

<h4>Bounty</h4>

<details>
<summary><code>DisableBountyWhenMasked</code></summary>
<p>Prevents bounties from being issued for crimes committed while masked.</p>
</details>

<details>
<summary><code>FixSelfDefenseBounties</code></summary>
<p>Prevents bounties for defending yourself against someone who attacked first.</p>
</details>

<details>
<summary><code>DisableBounties</code></summary>
<p>Prevents new bounties while keeping the wanted system active.</p>
</details>

<h4>Horse</h4>

<details>
<summary><code>FixHorseFollowBehavior</code></summary>
<p>Improves how your horse follows you so it is less likely to fall behind or get stuck.</p>
</details>

<details>
<summary><code>DisableHorseAutoJump</code></summary>
<p>Stops your horse from jumping obstacles automatically. You can still jump manually.</p>
</details>

<details>
<summary><code>DisableHorseWhistleDistance</code></summary>
<p>Removes the distance limit for calling your horse with a whistle.</p>
</details>

</details>

<details>
<summary><strong>Player</strong> </summary>

<details>
<summary><code>DisableFirstPersonAutoRun</code></summary>
<p>Makes walking the default in first person instead of automatically running.</p>
</details>

<details>
<summary><code>QuickLootBodies</code></summary>
<p>Uses the quicker fallback animation when looting bodies.</p>
</details>

<details>
<summary><code>QuickLootAnimals</code></summary>
<p>Uses the quicker fallback animation when looting animals.</p>
</details>

</details>

<details>
<summary><strong>Weapons</strong></summary>

<details>
<summary><code>DisableEmptyAutoReload</code></summary>
<p>Stops your weapon from reloading automatically when its ammunition runs out.</p>
</details>

<details>
<summary><code>DisableAutoReload</code></summary>
<p>Disables automatic weapon reloading in general.</p>
</details>

<details>
<summary><code>DisableDeadEyeAutoReload</code></summary>
<p>Stops the automatic reload that happens while using Dead Eye.</p>
</details>

<details>
<summary><code>DisableForcedScope</code></summary>
<p>Lets you use compatible rifles, such as the Carcano and Rolling Block, without being forced into their scopes.</p>
</details>

<details>
<summary><code>DisableHitmarkers</code></summary>
<p>Removes the hitmarkers shown when you shoot enemies.</p>
</details>

<details>
<summary><code>DisableReticleHighlighting</code></summary>
<p>Stops the aiming reticle from changing color when aimed at an enemy.</p>
</details>

<details>
<summary><code>DisableArrowTrails</code></summary>
<p>Removes the arrow trajectory trail shown while aiming a bow.</p>
</details>

<details>
<summary><code>DisableReticleSpread</code></summary>
<p>Stops the aiming reticle from spreading when you aim and fire.</p>
</details>

<details>
<summary><code>DisableReticleSway</code></summary>
<p>Stops the aiming reticle from swaying when you aim and fire.</p>
</details>

<details>
<summary><code>DisableWeaponDegradation</code></summary>
<p>Prevents weapons from wearing down over time, so they no longer need cleaning.</p>
</details>

</details>

<details>
<summary><strong>Visual</strong></summary>

<p><em>The recoil reduction settings at the bottom of this section are marked broken in the current INI.</em></p>

<details>
<summary><code>RemoveBlackBars</code></summary>
<p>Removes the black bars that appear during cutscenes and cinematic sequences.</p>
</details>

<details>
<summary><code>EnableDLSSAutoExposure</code></summary>
<p>Enables auto exposure with DLSS or DLAA to help reduce visual artifacts.</p>
</details>

<details>
<summary><code>FixFullscreenTabOut</code></summary>
<p>Restores fullscreen mode automatically when you tab back into the game.</p>
</details>

<details>
<summary><code>MinimizeOnTabOut</code></summary>
<p>Minimizes the game window when you Alt+Tab away.</p>
</details>

<details>
<summary><code>DisableIdleCamera</code></summary>
<p>Stops the automatic cinematic camera that appears when you leave the game idle.</p>
</details>

<details>
<summary><code>ReduceCameraShake</code></summary>
<p>Cuts down on camera shake during gameplay.</p>
</details>

<details>
<summary><code>ReduceCameraSway</code></summary>
<p>Cuts down on camera sway during gameplay.</p>
</details>

<details>
<summary><code>DisableHeadBob</code></summary>
<p>Removes the head-bobbing effect while moving.</p>
</details>

<details>
<summary><code>CameraFOVOffset</code></summary>
<p>Adjusts the camera FOV beyond the normal limits. Positive values widen it; negative values narrow it. Range: -3.0 to 3.0. Use 0.0 to disable.</p>
</details>

<h4>World</h4>

<details>
<summary><code>DisableInteriorLighting</code></summary>
<p>Disables interior lighting effects in buildings and other enclosed spaces.</p>
</details>

<details>
<summary><code>DisableDistantLights</code></summary>
<p>Disables distant light sources. This can cause bloom artifacts.</p>
</details>

<h4>Filters</h4>

<details>
<summary><code>DisableDrunkFilter</code></summary>
<p>Removes the visual distortion that appears when your character is drunk.</p>
</details>

<details>
<summary><code>DisableEagleEyeFilter</code></summary>
<p>Removes the screen filter applied during Eagle Eye.</p>
</details>

<details>
<summary><code>DisableDeadEyeFilter</code></summary>
<p>Removes the screen filter applied during Dead Eye.</p>
</details>

<details>
<summary><code>DisablePauseMenuFilter</code></summary>
<p>Removes the visual filter shown behind the pause menu.</p>
</details>

<details>
<summary><code>DisableWeaponWheelFilter</code></summary>
<p>Removes the screen filter shown while the weapon wheel is open.</p>
</details>

<details>
<summary><code>DisableLowHealthFilter</code></summary>
<p>Removes the visual effects that appear when your health is critically low.</p>
</details>

<h4>Experimental - currently broken</h4>

<details>
<summary><code>ReduceWeaponRecoil</code></summary>
<p>Experimental recoil reduction for firing weapons. Marked as broken in the current INI.</p>
</details>

<details>
<summary><code>RecoilShakeAmplitudeScale</code></summary>
<p>Sets the recoil reduction amount for ReduceWeaponRecoil. In the INI, 0.00 means maximum reduction and 0.50 means 50% reduction. The parent feature is currently marked broken.</p>
</details>

<details>
<summary><code>ReduceFirstPersonWeaponRecoil</code></summary>
<p>Experimental first-person recoil reduction. Marked as broken in the current INI.</p>
</details>

<details>
<summary><code>FirstPersonRecoilShakeAmplitudeScale</code></summary>
<p>Sets the first-person recoil reduction amount. In the INI, 0.00 means maximum reduction and 0.50 means 50% reduction. The parent feature is currently marked broken.</p>
</details>

</details>

<details>
<summary><strong>Ambience</strong></summary>

<p><em>The first four audio fixes do not have descriptions in the current INI, so their explanations are based on the option names.</em></p>

<details>
<summary><code>FixAmbientAudioDropouts</code></summary>
<p>Intended to stop ambient sounds from unexpectedly cutting out.</p>
</details>

<details>
<summary><code>FixWeatherAudioTransitions</code></summary>
<p>Intended to make audio transitions between weather conditions more consistent.</p>
</details>

<details>
<summary><code>FixInteriorAudioOcclusion</code></summary>
<p>Intended to address how ambient sound is muffled or blocked inside interiors.</p>
</details>

<details>
<summary><code>FixAmbientAudioLoops</code></summary>
<p>Intended to fix problems with repeating ambient sound loops.</p>
</details>

<details>
<summary><code>DisableAmbientMusic</code></summary>
<p>Turns off the background music that plays while exploring.</p>
</details>

<details>
<summary><code>DisableCombatMusic</code></summary>
<p>Turns off music that starts during combat.</p>
</details>

<details>
<summary><code>DisableWantedMusic</code></summary>
<p>Turns off music that plays while you are wanted.</p>
</details>

</details>

<details>
<summary><strong>Diagnostics</strong></summary>

<p><em>These diagnostic settings are not described in the current INI. Their purposes are inferred from their names.</em></p>

<details>
<summary><code>DumpNativeHashes</code></summary>
<p>Diagnostic option for dumping native hashes.</p>
</details>

<details>
<summary><code>CrashDiagnostics</code></summary>
<p>Diagnostic option related to crashes.</p>
</details>

<details>
<summary><code>DetectConflictingPatches</code></summary>
<p>Diagnostic option for checking for potentially conflicting patches.</p>
</details>

</details>

</details>

### Requirements
- [**Latest ASI loader**](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases) (preferably dinput8.dll)
- [**Latest ScriptHookRDR2**](https://www.dev-c.com/rdr2/scripthookrdr2/)


### License 

Fusion Fix RDR2 is licensed under the GNU General Public License v3.0 (GPL-3.0).

Copyright © 2026 Nochala.

You are free to view, modify, and redistribute the source code in accordance with the GPL-3.0 license.

All redistributed versions must preserve the original copyright and licensing notices. Modified versions must clearly indicate that changes have been made and comply with the GPL-3.0 license.
