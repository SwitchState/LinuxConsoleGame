## References:

1. **For Graphics (DRM/KMS):** **[The Linux Kernel DRM Documentation](https://www.kernel.org/doc/html/latest/gpu/drm-kms.html)**. 
2. **For Session/Security (Logind):** **[The `org.freedesktop.login1` Specification](https://www.google.com/search?q=%5Bhttps://www.freedesktop.org/wiki/Software/systemd/logind/%5D(https://www.freedesktop.org/wiki/Software/systemd/logind/))**. This documents the D-Bus API we used (`TakeControl`, `TakeDevice`).
3. **For Input (evdev):** **[The Linux Input Subsystem Documentation](https://www.kernel.org/doc/html/latest/input/input.html)**.

I created this application as a means to make a DOS like game but on Linux. I've never personally seen any games run
directly from a TTY on Linux before like you would traditionally run a DOS game. This is my attempt to make such a game.
This application will fail to run if you attempt to run it in an X session or any other graphical session. It must be 
started from a TTY. You can easily switch between them by pressing CTRL+ALT+F1 through CTRL+ALT+F7 . 


# Explanation:

This application runs without a Display Server (X11 or Wayland). It communicates directly with the Linux Kernel to achieve low-latency input and hardware-accelerated buffer management. This approach is identical to how modern Wayland compositors (like Sway or GNOME Shell) start up.

The architecture consists of three distinct phases:

1. **Privilege Escalation** (via `systemd-logind`)
2. **Direct Rendering** (via `DRM/KMS`)
3. **Raw Input** (via `evdev`)

---

## Phase 1: The Secure Handshake (Session Management)

**Goal:** Access hardware (GPU/Keyboard) without running as `root` (sudo).

In a standard Linux setup, hardware files in `/dev/` are owned by root. We use the **D-Bus** messaging system to ask the init system (`systemd`) to lend us access temporarily.

### The Steps:

1. **Identify Session (`sd_pid_get_session`)**:
* We ask the OS: "Which session does this process belong to?"
* *Result:* We get a Session ID (e.g., "c2").


2. **Take Control (`TakeControl`)**:
* We send a message to `logind`: "I want to be the Display Server for this session."
* *Effect:* Linux stops other programs (like the text console) from drawing to the screen.


3. **Take Device (`TakeDevice`)**:
* We ask: "Please open the GPU (Major 226) and give me the file descriptor."
* *Why:* We cannot open `/dev/dri/card0` ourselves due to permissions. `logind` opens it and passes the open handle to us.



---

## Phase 2: The Graphics Pipeline (DRM/KMS)

**Goal:** Get a memory buffer that represents the pixels on the monitor.

We use the **Direct Rendering Manager (DRM)** and **Kernel Mode Setting (KMS)** API. This is the modern replacement for the legacy "Framebuffer" (`/dev/fb0`) system.

### The Steps:

1. **Resource Discovery (`drmModeGetResources`)**:
* We query the GPU for a list of Connectors (HDMI/DP ports), Encoders, and CRTCs (Scanout engines).


2. **Mode Selection**:
* We iterate through connectors to find one that is `CONNECTED`. We read its preferred mode (e.g., `1920x1080 @ 60Hz`).


3. **Dumb Buffer Allocation (`DRM_IOCTL_MODE_CREATE_DUMB`)**:
* We ask the GPU driver to allocate a simple chunk of VRAM.
* *Note:* It is called "Dumb" because it doesn't support 3D commands; it's just raw storage for pixels.


4. **Memory Mapping (`mmap`)**:
* We map that GPU VRAM into our CPU's address space.
* *Result:* We get a `uint32_t *map` pointer. Writing to `map[0]` changes the first pixel on the screen.


5. **Mode Setting (`drmModeSetCrtc`)**:
* We "flip the switch," connecting our Buffer -> CRTC -> Connector -> Monitor.



---

## Phase 3: The Input Loop (Evdev)

**Goal:** Read keyboard events with zero latency and prevent the terminal from seeing them.

We bypass the text terminal processing (which handles things like backspace, enter, and Ctrl+C) and read raw "scancodes" from the kernel.

### The Steps:

1. **Device Discovery (`stat` & `sysmacros`)**:
* We look up the Major/Minor ID of `/dev/input/event0` (or whichever device is the keyboard).


2. **Request Access (`TakeDevice`)**:
* Just like the GPU, we ask `logind` to open this specific input node for us.


3. **Exclusive Grab (`EVIOCGRAB`)**:
* We tell the kernel: "Route all events from this keyboard *only* to me."
* *Why:* This prevents the text console (TTY) from typing characters in the background while you play the game.


4. **The Event Loop (`read`)**:
* We read `struct input_event` packets.
* These contain the `code` (Physical Key) and `value` (1=Press, 0=Release, 2=Repeat).



---

### Comparison Table: Win32 vs. Linux KMS

| Concept | Win32 API Equivalent | Linux KMS Implementation |
| --- | --- | --- |
| **Get Access** | `GetDC(NULL)` | `sd_bus_call_method("TakeDevice")` |
| **Set Resolution** | `ChangeDisplaySettings` | `drmModeSetCrtc` |
| **Pixel Buffer** | `CreateDIBSection` | `DRM_IOCTL_MODE_CREATE_DUMB` |
| **Input** | `PeekMessage` / `WM_KEYDOWN` | `read(fd, &event)` |
| **Prevent Alt-Tab** | System Hooks | `ioctl(fd, EVIOCGRAB)` |
