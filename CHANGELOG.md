## 0.5.1 - 2026-11-13

- Fixed follower's favor interaction ("I want you to do something" dialogue option, or holding "Activate" on them). It uses Menu Cancel action for cancelling the favor.

## 0.5.0 - 2026-11-12

- Added support for left-handed mode. When using it, you need to also switch to the left-handed bindings in SteamVR (mirrored Frame/Index default bindings are provided).

  BREAKING: the hand-dependent action names were changed from right/left to primary/secondary (labeled Main Hand/Off Hand in the UI), so if you have a custom SteamVR binding,
  you'll have to rebind them (or just switch to the default and then apply your edits again). Sorry about this!

  NOTE: the current ImGui VR Helper version (1.12.0) doesn't handle left-handed mode correctly: inside ImGui the controls are swapped, and to open it with the "Open ImGui" action
  you need to change its open combo to the primary A/X + B/Y. To do that, with the game closed, set this line in `Data/SKSE/Plugins/ImGuiVRHelper.toml` (in MO2 it's usually in the overwrite folder):
  `openMenuKeys = [7, 1]` (the default right-handed value is `[65543, 65537]`)

- Fixed the "Open ImGui" action also triggering menu actions (e.g. "Action A/B").

## 0.4.0 - 2026-11-11

- Added single-direction movement actions, so you could bind e.g. "toggle move forward". Also works with Dialogue Movement Enabler VR.

## 0.3.1 - 2026-11-09

- Added A/X and B/Y bindings for ImGui.
- Changed default Index binding for Exit Map to Grip Force (like for the menus).

## 0.3.0 - 2026-11-06

- Added support for Dialogue Movement Enabler VR

## 0.2.0 - 2026-11-04

- Added some Index bindings.
- Added bTouchActionsAffectFingers setting to control previously hard-coded finger curling behaviour, undesired with Index.
  (now you can flip off Nazeem properly with your Index controllers. NOTE: this was always on and now off by default!)
- Enabled touch action logging when sLogLevel=trace

## 0.1.3 - 2026-11-04

- Force oculus controllers internally, so they don't switch and break things if the game is launched while VR is not fully initialized.
  (this should fix the button repeating issue among, I think, some other hiccups)

## 0.1.2 - 2026-11-02

- Fixed the map marker menu getting in a broken state (funnily it depends on whether you moved your real mouse while in game).
- Don't process skills and map actions when there's a menu on top.
- Disabled the work-around for an old device buttons reset bug that doesn't seem to happen anymore (fingers crossed).

## 0.1.1 - 2026-10-28

- Make "Ready Weapon" action work instantly (replace SkyrimVR button holding behaviour) 

## 0.1.0 - 2026-10-27

Initial version.
