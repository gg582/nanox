# Signal Handling in Nanox

Nanox implements robust signal handling to ensure data integrity during unexpected exits and to support responsive UI adjustments during terminal resizing.

## 1. Window Resizing (`SIGWINCH`)
The most common signal handled is `SIGWINCH` (Signal Window Change).
- **Trigger**: When the user resizes their terminal emulator window.
- **Handler**: `sizesignal(int signr)` in `display.c`.
- **Logic**: 
  1. It fetches the new dimensions using `ioctl(0, TIOCGWINSZ, &size)`.
  2. It flags the change in `chg_width` and `chg_height`.
  3. The main loop or the next `update()` call detects these flags and triggers `newscreensize()`, which reallocates the virtual screen buffers (`vscreen`) and reframes all windows to fit the new dimensions.

## 2. Emergency Exits (`SIGHUP`, `SIGTERM`)
To prevent data loss, Nanox catches hangup and termination signals.
- **Handler**: `emergencyexit(int signr)` in `main.c`.
- **Logic**: 
  1. It iterates through all active buffers.
  2. For every buffer marked as "Changed" (`BFCHG`), it attempts an automatic `filesave()`.
  3. After attempting to save all work, it calls `quit(TRUE, 0)` to release file locks and restore terminal settings before exiting.

## 3. Keyboard Interrupts (`SIGINT`)
Nanox typically runs with `ISIG` disabled in `termios` (see `posix.c`), meaning `Ctrl+C` does not send a `SIGINT`. Instead, it is read as a raw key code. This allows the editor to bind `Ctrl+C` to internal functions (like `insspace`) rather than crashing.

## 4. Implementation Reference
- **Signal Registration**: Found in `main.c` using `signal(SIGWINCH, sizesignal)`.
- **State Preservation**: The `errno` is saved and restored inside handlers to ensure that asynchronous signals don't interfere with ongoing system calls.
