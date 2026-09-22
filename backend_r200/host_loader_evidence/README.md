# Loader and short-yield evidence

CRC-verified bridge downloads from the physical Amiga at 192.168.1.21:2345.
See `../../docs/reference/HOST_LOADER_REGRESSION.md` for build identities and interpretation.

- `classification-before.log`: the unchanged ABI-4 host with cached PPC classification.
- `classification-after.log`: same `D0406387` bytes, classification changed to normal 68k.
- `fullscreen.log` / `fullscreen-host.log`: final quiet current DLL, 640x480x16, three buffers.
- `cp-window.log` / `cp-window-host.log`: final quiet CP emitter, windowed 800x600x32.
- `texture.log` / `texture-host.log`: final CP texture-update acceptance run.
- `tick-profile.log` and `short-profile.log`: matched instrumented DLL runs before/after the wait change.
- `tick-transport.log` and `short-transport.log`: corresponding raw wait counters.
- `final-health.log`: passing native bootstrap after the final CP benchmark.

All renderer runs completed normally. One-buffer visual corruption from the
earlier sweep is recorded separately and was not accepted as rendering success.

## Final 131,072-iteration polling policy

- `recovery-fullscreen.log`: quiet current DLL at 134.529 FPS, fullscreen 640x480x16, three buffers.
- `recovery-cp-window.log` / `recovery-cp-host.log`: CP emitter at 68.337 FPS, windowed 800x600x32.
- `recovery-texture.log`: repeated 7,127-check CP acceptance pass.
- `yield-time-client.log` / `yield-time-transport.log`: direct measurement of the ~20.3 ms actual kernel waits.
- `spin131-profile.log` / `spin131-transport.log`: matching profiled recovery and fewer yields.
- `recovery-health.log`: native bootstrap after the final recovered build's CP run.
