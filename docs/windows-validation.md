# Windows acceptance checks

Automated loopback tests validate the runtime using fake input backends. Complete
these checks on two or more Windows PCs before using real trading applications.
Use text editors and disposable windows first.

| Check | Expected result |
|---|---|
| Connect with matching keys | Client appears once, with its native monitors |
| Connect with wrong key or duplicate ID | Connection refused; current host control remains available |
| Select a client, type, click, scroll | Only the active client responds; host remains unaffected |
| Move continuously beyond host screen width | Remote pointer keeps moving; host pointer remains local and suppressed |
| Cross adjacent configured edges in both directions | Active computer and monitor change; cursor lands at the matching edge |
| Cross a gap / mismatched edge span | No switch; cursor stays within connected monitor geometry |
| Multiple monitors, negative origins, mixed DPI | Native coordinates and switching remain correct |
| Hold Ctrl, Shift, or mouse button while switching | Old destination receives releases; held input requires a fresh press |
| Mirror two clients and host | Keys reach all selected computers, respecting each application's current focus |
| Mouse move, click, drag, and scroll while mirroring | Only host mouse responds; client cursors stay in place |
| End mirror from client-originated individual session | Always returns to host, not the prior client |
| Ctrl+Alt+Home during client control | Immediate local recovery and remote held-input cleanup |
| Unplug network with a key held | Host recovers within the heartbeat deadline; reachable client cleans up on timeout |
| Stop host/client normally with a key held | Keys/buttons released and local host control available |
| Lock a desktop, show UAC, change display configuration | Recovery or session stop; no silent continued remote control |
| Change focus / use elevated applications | Verify permission behavior; failures are visible, not reported as success |
| Left-handed mouse settings and extra buttons | Logical button actions preserved across machines |
| Numpad, extended keys, Pause, key repeat, AltGr | Verify the keyboard layouts and hardware actually used at the desk |
| Move/resize GUI and use high-rate mouse | Capture remains responsive; no queued input burst after a stall |

Record Windows version, keyboard layout, display scaling, mouse hardware/polling
rate, application privilege level, and results. Forced client process termination
cannot execute cleanup code; manually release stuck modifiers if testing that case.
