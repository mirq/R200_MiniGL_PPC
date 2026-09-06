# WarpOS R200 Debug & Validation Plan

Date: 2026-09-05. Successor to `PPC_GEARS_SILENTHILL_FINDINGS.md`.
Approved with these inputs: the four Silent Hill stalls (18:33/19:20/19:27/19:38)
were separated by cold reboots (weakens the volatile leftover-state hypothesis);
host binary canonicality unknown (swap-and-see, own controlled cycle); no VNC
on target.

## Verified state (read-only target audit, 2026-09-05)

| Item | Findings doc said | Verified |
|---|---|---|
| Target session | stale gears/host processes | Clean (42 tasks, reboot already done) |
| Fixed DLL | only at Work:minigl_testing | Confirmed, CRC32 D3564C31 / 240888 |
| gearsfix WarpELF | converted | Confirmed byte-identical to local build: B0F6B865 / 302438 |
| Work:SilentHill WOS/minigl_ppc.dll | not updated | Confirmed stale: 1A7727A3 / 240888 |
| fs-uae debug RPC | — | unavailable (physical target); all amiga_fsuae_* out of scope |
| On-target minigl_ppc_host | — | 37,960 B / 5FEB1371 != tree build 35,288 B / 1A18F4AC |

## Code findings driving the plan

1. **Silent failure mode**: `Lib_mglSwitchDisplay` (r200_surface.c:1415) returns
   early on FlushScreenVertices/SubmitAndWait/barrier failure without
   `RecordPerfFrame()`, so `PPCPERF_LIVE` never fires and the client spins
   forever, silently. Leading hypothesis for the zero-frame gears fix-run
   (`gears-ppc-fix-client.log` ends at "entering MGLMainLoop"; host log has
   `ring_init_done` but no `ring_first_entry`).
2. **ESC = NULL deref**: `R200PPCDispatchDeleteContext` memsets proofContext
   while `Lib_mglMainLoop` (minigl_r200.c:819-836) keeps a stale local
   `context`; next iteration dereferences `context->window->UserPort` with
   window==NULL.
3. **Clean exit disabled by the window fix**: PPC mirror has baseIDCMP==0, so
   `mglKeyFunc` -> `ModifyIDCMP` strips IDCMP_CLOSEWINDOW, the only clean exit
   path from Lib_mglMainLoop. Explains resident processes (findings problem 3).

## Phase A — Gears PPC: classify zero-frame fix-run, repair, validate

A.0 Identity check (no reboot): amiga_list_tasks; CRC32/size of
    Work:minigl_testing/minigl_ppc.dll (expect D3564C31/240888),
    DH2:wosbuild/minigl_ppc_r200_gearsfix.warpelf (expect B0F6B865/302438),
    Work:minigl_testing/gears_ppc; amiga_list_screens baseline.
    No re-transfer unless a checksum disagrees (protocol in Phase C).

A.1 Launch with live observation (no ESC):
    Run host: `Run >NIL: <NIL: "Work:SilentHill WOS/minigl_ppc_host" >Work:minigl_testing/gearsfix2-host.log`
    Run client: `Run >NIL: <NIL: Work:minigl_testing/gears_ppc -window >Work:minigl_testing/gearsfix2-client.log`
    At ~10 s and ~30 s: amiga_list_tasks (run vs wait state), amiga_screenshot x2
    (animate check), amiga_list_screen_windows, client log growth, host log tail,
    amiga_last_crash.

A.2 Classification:
    - PPCPERF_LIVE 1..10 + ring_first_entry + animated screenshots -> fix works, go A.4
    - client state=run, no output, host stuck at ring_init_done -> silent spin
      (finding 1) -> A.3a
    - client state=wait -> blocked bridged 68k call -> A.3b
    - window present but frozen, one ring entry -> barrier never completed -> A.3a
    - no window -> context create failed; re-read client log from offset 0

A.3 Repairs (additive, no dispatch ABI change):
    a. r200_surface.c:1415 — one-shot latched `PPCPERF_FAIL ...` print+fflush on
       the early-return path; same for R200PPCDispatchDeleteContext early return
       (compile probe ~line 1897).
    b. If blocked in Exec: inspect window pointer from host create reply,
       verify UserPort ln_Type==NT_MSGPORT (4).
    c. minigl_r200.c Lib_mglMainLoop — re-resolve context inside the loop and
       return when it or window disappears (closes ESC UAF; also fixes latent
       68k-native bug — flag in commit, re-run 68k smoke if native validation
       expected).
       compile probe:1694 — after window assignment seed
       `proofContext.baseIDCMP = proofContext.window ? proofContext.window->IDCMPFlags : 0;`
       (restores close gadget as clean exit).
    Rebuild: `make -f Makefile.dll.r200.gcc warpelf`; deploy per Phase C.

A.4 Teardown verification (after frames render): close gadget via
    amiga_input_click on the gears window (fallback ESC after A.3c), wait 3 s,
    amiga_list_tasks (client gone, host alive), read both logs, last_crash.

A.5 Success criteria (all must hold):
    - >=10 PPCPERF_LIVE lines frames=1..10
    - final PPCPERF frames=N (N>=10), PPCPERF_TIME, PPCPERF_TEX, PPCPERF_WORST
      (these only print on successful delete -> proof of clean teardown)
    - host log: ring_first_entry + session summary execute_msgs>0
    - no gears_ppc in task list after exit; last_crash clean
    - two screenshots >=1 s apart differ

A.6 If frames render but teardown hangs: PPCPERF* present -> hang after delete
    (MiniGLClose/DLL unload); absent -> bailed at mailbox-wait/submit/barrier
    (A.3a print pins which). state=run -> spin; state=wait -> Exec block.

## Phase C — Deployment consistency

C.1 Host binary swap in its OWN controlled cycle (user: swap-and-see):
    backup on-target minigl_ppc_host (5FEB1371) -> push tree build (1A18F4AC,
    CRC-verified) -> one gears A.1/A.2 cycle -> interpret.
C.2 SH DLL sync (safe pre-B: stall precedes MiniGLOpen):
    copy Work:SilentHill WOS/minigl_ppc.dll -> .pre-gearsfix (verify 1A7727A3)
    -> copy DH2:wosbuild/minigl_ppc_r200_gearsfix.dll -> Work:SilentHill
    WOS/minigl_ppc.dll (verify D3564C31).
C.3 Canonical install procedure (CRC32-anchored chain):
    host warpelf (record CRC32+size) -> amiga_push_file -> amiga_checksum must
    match -> target Elf2Exe2 -> record dll CRC32+size as release identity ->
    amiga_pull_file once for host-side sha256 -> install by amiga_copy to every
    consumer + verify each; sweep for other minigl_ppc.dll copies before
    declaring sync complete. Record warpelf CRC -> dll CRC -> installed paths
    in findings doc after every deploy. Never blame Elf2Exe2 before CRC check.

## Phase B — Silent Hill stall triage (after A and C.2)

Stalls are reboot-separated, so volatile leftover-state (H1) is downgraded;
remaining: H2 CGX/RTG contention, H3 SDL/CGX race, H1' disk-persistent state
(something a successful run writes that the next boot-session reads).

B.1 Two cold boots x 1 SH attempt each (host + client logs to fresh names).
B.2 If attempt-1 from clean cold boot still stalls -> persistent-state/H2
    bisection. During a stall capture: list_tasks diff vs 42-task baseline,
    list_screens (640x480x16 opened?), lib_info cybergraphics/Picasso96API/
    Warp3DPPC/powerpc (open counts vs baseline), sysinfo, screenshot,
    last_crash. Diff ENVARC: and CGX/P96 saved prefs before/after successful
    vs stalled run.
B.3 Static: ppc-amigaos-objdump SilentHill.elf SDL_VideoInit/CGX path —
    enumerate candidate blocking OS calls. No ELF patching.
B.4 Contact porter (Kramo, mvickers03@gmail.com per README-WarpOS.txt) for
    WarpOS SDL/CGX video-init source or marker build. Psy-X rebuild is the
    last resort only.

## Prohibitions

- No Silent Hill texture-streaming conclusions: stalled runs stop before
  [WOS-GL] MiniGLOpen — zero MiniGL calls were issued. Bar to reopen: a run
  reaching PPCPERF_LIVE frames=1.
- Work:minigl_testing/gears is never PPC evidence (variant=68k).
- Never cite Elf2Exe2 failure without both-ends size+CRC32.
- idleFunc-less clients would Wait() on the host task's signal bit — document,
  do not assume it is the gears bug (gears sets mglIdleFunc(oneFrame)).

## Session log (2026-09-05, execution notes)

### A-run findings (gears)

1. The zero-frame failure is DETERMINISTIC: trace1 run reproduced the exact
   142-byte client / 299-byte host log signature of the old fix-run.
2. The DLL's stdio never reaches the client's redirected stdout while the
   process is alive (separate libc instances). All frontend diagnostics must
   go through the new trace logger
   (`backend_r200/include/libraries/minigl_ppc_trace.h`, per-line
   open/append/close to Work:minigl_testing/mglppc_client.log so a wedged
   process leaves the file readable).
3. ROOT CAUSE of the startup stall: the PPC frontend never opened
   intuition.library, but the 68k-native `IntuitionBase` definition
   (minigl_r200.c:31) compiled into the PPC build as a NULL BSS symbol.
   The window-mirror fix made `mglKeyFunc` reach `ModifyIDCMP()` for the
   first time -> call through NULL base -> WarpOS "illegal run68k function
   execution detected" -> MasterControl halts the client between the
   "setup done" banner and main-loop entry. That is ALSO the explanation for
   the resident-process problem (a halted task ignores Break signals).
   FIX: R200PPCPlatformOpen opens intuition.library; R200PPCPlatformClose
   closes it. After the fix, gears renders its first frame.
4. NEW problem after the fix: ~0.01 FPS and a hard machine freeze at the
   first frame. The client spin waits (mailbox 5s loop, ring publish stall,
   barrier drain, present wait) are pure user-mode loops that never yield;
   on WarpOS the scheduler only runs at exec calls, so the spins starve the
   68k host that must complete the ring entries -> livelock.
   FIX (trace4): Delay(1) every 64k spin iterations (and 1k in the mailbox
   wait) + timestamps on every trace line (@tick) to locate the remaining
   per-frame cost.
5. Bridge script gotchas: each line of an amiga dos_command script runs in
   a fresh shell (cd does not persist); target writes must use a launcher
   script (Work:minigl_testing/glaunchN). File reads of open log files fail
   (exclusive lock) — List gives sizes. Push large files as zip
   (UnZip -o X.zip -d DH2:wosbuild), 140 KB zip instead of 310 KB raw.

### DLL release identities (WarpELF CRC32 -> DLL CRC32)

- gearsfix (window mirror only): B0F6B865/302438 -> D3564C31/240888
- trace1 (instrumentation v1):            760013FE/307988 -> DEA9EE9B/244848
- trace2 (per-line trace logger):         4B9660C5/308392 -> 524570CD/245240
- trace3 (+Intuition open fix):           436CD9A4/308844 -> CF01AB70/245544
- trace4 (+spin yields, +trace ticks):    70D466E6/310532 -> 0696025B/246892
- trace5 (+yields tuning):                997055D9/310532 -> 9ED23C06/246892
- trace6 (+PPCDELTA, stage reports):      867950EB/312712 -> 524570CD-family
- trace7 (+rotate yield/trace, host fix): DFC145D4/312928 -> 7E84517F/248768
- trace8 (+DH2 loggers, host stage):      34962381/313032 -> 24A16133/248832
- trace10 (+window parked, console diag): 009F642F/313988 -> 050EB37E/249444
- trace11 (console gated by MGLPPC_VERBOSE): CE768651/334665 -> C4C6576E/268188
- trace12 (+rotate-any-retired):          E7ABA8EA/334849 -> 5B7AB4B0/268352
- trace13 (fullscreen default):           C7F2E8E3/334849 -> 701DBE1B/268352
- buildA  (+frames, teardown trail, per-client reset, host create trace):
  warpelf AD54EFC6/336260 -> DLL CBB92430/269228; host 741279D0/41604;
  gears client 81C59A42/223216 -> 3168239A/185532

Host binaries: 5FEB1371 (pre-trace5, backup at minigl_ppc_host.pre-trace5)
-> 24D52703 -> DDEE7D23 (idle-sleep) -> 941D6973/70C90F7C (DH2 trace)
-> 741279D0 (idle-sleep + create-param trace; installed).

### Diagnostic run result (gearsfix20, fullscreen 640x480x16, -frames 300)

- **202.7 FPS** (GEARS_RUN frames=300 ticks=74) fullscreen 16-bit, vs
  ~3 FPS windowed 32-bit: the windowed present (full-frame 32->screen
  ClipBlit conversion on the 68k) was the dominant cost; the fullscreen
  flip path costs ~1.5 ms/frame.
- Fullscreen default fixed: FrontendPlatform.windowMode was defaulting TRUE
  and surviving partial closes; now FALSE via R200PPCPlatformResetClientDefaults()
  called from both dispatch admission paths (MGLPPCClientAttach +
  MGLGetDispatchTable).
- `-frames N` graceful auto-exit verified: frame limit -> mglExit ->
  mainloop return -> delete -> platform close -> host shutdown; both
  processes exited (Status clean); client log shows "frame limit 300
  reached", GEARS_RUN summary, "MGLMainLoop returned, tearing down".
- Host session: executes=1016 (3.4/frame), presents=300, host 98.5% idle
  (idle-sleep working), submit ~4 ms/frame, present ~1.5 ms/frame.
- Client gears_client.log (MarkLog) was not created: Open(OLDFILE) fails
  when the file does not exist — pre-create it or switch to NEWFILE-once
  before relying on it.
- Host auto-start (DLL spawns minigl_ppc_host via SystemTags, same pattern
  as hyperion_dll/dll.c:131-155) is planned but NOT yet implemented — the
  host is still manual-start.

## Quiet console (trace11+)

All DLL console output (DLL_OUTPUT window) is gated behind the environment
variable `MGLPPC_VERBOSE` (SetEnv MGLPPC_VERBOSE 1 before launching).
Default: silent -- console.device prints dominated frame time. Diagnostics
continue regardless in DH2:wosbuild/mglppc_client.log (client trace) and
DH2:wosbuild/mglppc_host.log (host poll/stage reports); both use
per-line open/append/close so they survive hard resets and stay readable
while processes live. Work: volume directory entries do NOT survive hard
resets on this target -- never keep evidence there.

FPS overlay replaced by window-title readout (SetWindowTitles once per 30
frames, examples/milestone_gears_stats_library.c); the RastPort overlay cost
~11.5 s/frame in PPC->68k transitions. Gears window is parked at (1260,120)
for a 1920-wide screen (MoveWindow after create in the DLL).

