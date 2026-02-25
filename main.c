#include <stdio.h>
#include <stdlib.h>
#include <systemd/sd-login.h>
#include <systemd/sd-bus.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <linux/input.h>
#include <signal.h>

// Safety: If we get stuck, kill the program after 20 seconds
void panic_timeout(int sig) {
    fprintf(stderr, "\nSAFETY ALARM TRIGGERED: Program timed out. Releasing hardware...\n");
    exit(1);
}

int main() {
    char *session_id = NULL;
    const char *session_path = NULL;
    sd_bus *bus = NULL;
    sd_bus_message *reply = NULL;
    int r, device_fd = -1, keyboard_fd = -1;
    
    
    
    // Variables moved to main scope so they don't "disappear"
    drmModeModeInfo current_mode;
    uint32_t connector_id = 0;
    uint32_t crtc_id = 0;
    int found_display = 0;

    signal(SIGALRM, panic_timeout);
    alarm(20); // 20-second safety window

    printf("--- Console Game Engine: Secure Boot ---\n");

    // 1. Identify Session & Connect Bus
    sd_pid_get_session(0, &session_id);
    sd_bus_default_system(&bus);

    r = sd_bus_call_method(bus, "org.freedesktop.login1", "/org/freedesktop/login1",
                           "org.freedesktop.login1.Manager", "GetSession",
                           NULL, &reply, "s", session_id);
    if (r < 0) return EXIT_FAILURE;
    sd_bus_message_read(reply, "o", &session_path);

    // 2. Take Control
    r = sd_bus_call_method(bus, "org.freedesktop.login1", session_path,
                           "org.freedesktop.login1.Session", "TakeControl",
                           NULL, NULL, "b", 0);
    if (r < 0) {
        fprintf(stderr, "Handshake Failed. Are you in X11?\n");
        goto cleanup;
    }

    // 3. Take GPU
    sd_bus_message_unref(reply);
    r = sd_bus_call_method(bus, "org.freedesktop.login1", session_path,
                           "org.freedesktop.login1.Session", "TakeDevice",
                           NULL, &reply, "uu", 226, 0);
    if (r < 0) goto cleanup;
    
    int paused_gpu;
    sd_bus_message_read(reply, "hb", &device_fd, &paused_gpu);
    // ------------------------------------------------------

    // 4. Resource Discovery
    drmModeRes *res = drmModeGetResources(device_fd);
    for (int i = 0; i < res->count_connectors; i++) {
        drmModeConnector *conn = drmModeGetConnector(device_fd, res->connectors[i]);
        if (conn->connection == DRM_MODE_CONNECTED && conn->count_modes > 0) {
            current_mode = conn->modes[0];
            connector_id = conn->connector_id;
            crtc_id = res->crtcs[0]; // Simplified: use first controller
            found_display = 1;
            drmModeFreeConnector(conn);
            break;
        }
        drmModeFreeConnector(conn);
    }

    if (found_display) {
        printf("Display: %dx%d @ %dHz\n", current_mode.hdisplay, current_mode.vdisplay, current_mode.vrefresh);

        // 5. Create Dumb Buffer (Sky Blue)
        struct drm_mode_create_dumb creq = { .width = current_mode.hdisplay, .height = current_mode.vdisplay, .bpp = 32 };
        ioctl(device_fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq);

        uint32_t fb_id;
        drmModeAddFB(device_fd, creq.width, creq.height, 24, 32, creq.pitch, creq.handle, &fb_id);

        struct drm_mode_map_dumb mreq = { .handle = creq.handle };
        ioctl(device_fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq);

        uint32_t *map = mmap(0, creq.size, PROT_READ | PROT_WRITE, MAP_SHARED, device_fd, mreq.offset);
        
        // Fill with Sky Blue: 0x3399FF
        for (int i = 0; i < (creq.size / 4); i++) map[i] = 0x3399FF;

        drmModeSetCrtc(device_fd, crtc_id, fb_id, 0, 0, &connector_id, 1, &current_mode);
    }


    // 6. Take Keyboard
    sd_bus_message_unref(reply);
    reply = NULL;  // Reset pointer safely
    
    // Auto-detect the Major/Minor numbers for event0
    struct stat st;
    if (stat("/dev/input/event0", &st) < 0) {
    	fprintf(stderr, "Failed to stat event0. Does it exist?\n");
    	goto cleanup;
    }
    
    int input_major = major(st.st_rdev);
    int input_minor = minor(st.st_rdev);
    printf("Detected Keyboard at %d, %d\n", input_major, input_minor);
    
    // Now request the CORRECT device ID
    r = sd_bus_call_method(bus, "org.freedesktop.login1", session_path,
                           "org.freedesktop.login1.Session", "TakeDevice",
                           NULL, &reply, "uu", input_major, input_minor); 
    if (r >= 0) {
    	int paused;
        sd_bus_message_read(reply, "hb", &keyboard_fd, &paused);
        printf("SUCCESS: Received Keyboard FD: %d\n", keyboard_fd);
        
        // 6a. Set to Non-blocking (Crucial for the game loop)
        fcntl(keyboard_fd, F_SETFL, O_NONBLOCK);
        
        // 6b. Grab the Device (Exclusive Access)
        // This stops the TTY from seeing your keystrokes
        ioctl(keyboard_fd, EVIOCGRAB, 1);
        printf("Keyboard Grabbed (Exclusive Mode).\n");
    }
    else {
    	fprintf(stderr, "Failed to get Keyboard FD: %s\n", strerror(-r));
    }
    // ------------------- END OF KEYBOARD SECTION -----------------------


    // 7. Main Loop
    printf("Input active. Press ESC to exit (Safety Timeout in 20s).\n");
    int running = 1;
    
	while (running) {
        struct input_event ev;
        
        // Keep reading until the buffer is empty
        while (read(keyboard_fd, &ev, sizeof(ev)) > 0) {
            
            // DEBUG: See exactly what the kernel sends us
            if (ev.type == EV_KEY) {
                printf("INPUT: Code=%d Val=%d\n", ev.code, ev.value);

                // Check for ESC (Code 1) on Press (Value 1)
                if (ev.code == KEY_ESC && ev.value == 1) {
                    printf("Escape pressed. Exiting safely...\n");
                    running = 0;
                }
            }
        }
        
        // Don't burn 100% CPU
        usleep(10000); 
    }
    
    // Release the Grab before we exit!
    if (keyboard_fd >= 0) {
        ioctl(keyboard_fd, EVIOCGRAB, 0);
    }

    // 8. Explicitly Release (Prevents TTY Black Hole)
    sd_bus_call_method(bus, "org.freedesktop.login1", session_path,
                       "org.freedesktop.login1.Session", "ReleaseControl",
                       NULL, NULL, "");

cleanup:
    if (reply) sd_bus_message_unref(reply);
    if (bus) sd_bus_unref(bus);
    free(session_id);
    return EXIT_SUCCESS;
}
