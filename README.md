# Path Idle Crash Fix

SKSE plugin for Skyrim Special Edition (Anniversary Edition runtimes). Fixes a vanilla crash shortly after loading a
save: an NPC saved while its path follower waited to play a start / stop / turn idle comes back without that idle,
and its first update enters PlayIdle with a null pointer (`SkyrimSE.exe+0CEC680` on 1.6.1170,
`MovementMessagePlayIdle` / `PathFollowerStatePlayIdle` in the crash log).

`MovementAgentPathFollowerStandard` state 14 holds the prepared idle while it waits; its SaveGame / LoadGame keep the
state but not the idle, and state 14 (unlike states 13 and 15) uses the idle without checking it. The plugin wraps the
path follower's Update: state 14 without its idle is reset the way SetPath drops a prepared idle (ClearIdle, state 1).

Addresses come from Address Library. The field offsets are checked at load against the instructions that use them;
a build where they differ stays inactive and says so in `PathIdleCrashFix.log`. SE and VR are not supported.

## Requirements
- [SKSE64](https://skse.silverlock.org/)
- [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444)

## Building
Visual Studio 2022 (C++), CMake 3.24+, [vcpkg](https://github.com/microsoft/vcpkg) with `VCPKG_ROOT` set.

```
git clone --recurse-submodules https://github.com/Yuof/PathIdleCrashFix
cd PathIdleCrashFix
cmake --preset release
cmake --build --preset release
```

The DLL is `build/Release/PathIdleCrashFix.dll`.

## License
[GPL-3.0-or-later](LICENSE), as required by [CommonLibSSE NG](https://github.com/alandtse/CommonLibVR) (GPL-3.0-or-later
with its modding exceptions), which this plugin links statically.
