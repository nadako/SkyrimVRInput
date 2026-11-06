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
