// gcc -O2 -Wall -Wextra -o linux_doom linux_doom.c -lX11
// sudo apt-get install libx11-dev

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <crt/doom_env.h>  

struct DoomControlRegs g_LinuxDoomControlRegs;
void *g_DoomHeapAddress = NULL;
unsigned int g_DoomHeapSize = 0;
char *g_DoomWadAddress = NULL;
unsigned int g_DoomWadSize = 0;
struct DoomControlRegs *g_DoomControlRegs = &g_LinuxDoomControlRegs;

void EnvPutChar(int c) {
    printf("%c", c);
    fflush(stdout);
}

static void LinuxDoomUpdateTime(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    g_LinuxDoomControlRegs.time_sec  = (int)ts.tv_sec;
    g_LinuxDoomControlRegs.time_usec = (int)(ts.tv_nsec / 1000);
}

static void MiniDoomKeyAction(int key, int action) {
    int k = 0;
    switch (key) {
        case XK_Return:   k = CRT_DOOM_KEY_ENTER; break;
        case XK_Left:     k = CRT_DOOM_KEY_LEFT_ARROW; break;
        case XK_Right:    k = CRT_DOOM_KEY_RIGHT_ARROW; break;
        case XK_Up:       k = CRT_DOOM_KEY_UP_ARROW; break;
        case XK_Down:     k = CRT_DOOM_KEY_DOWN_ARROW; break;
        case XK_space:    k = CRT_DOOM_KEY_SPACE; break;
        case XK_Control_L:
        case XK_Control_R: k = CRT_DOOM_KEY_CTRL; break;
        case XK_Escape:   k = CRT_DOOM_KEY_ESCAPE; break;
        case XK_y:
        case XK_Y:        k = CRT_DOOM_KEY_Y; break;
        default: break;
    }
    if (!k) return;

    for (size_t i = 0; i < sizeof(g_LinuxDoomControlRegs.keys) / sizeof(g_LinuxDoomControlRegs.keys[0]); i++) {
        if (!g_LinuxDoomControlRegs.keys[i].action) {
            g_LinuxDoomControlRegs.keys[i].action = action; // 1=down, 2=up
            g_LinuxDoomControlRegs.keys[i].key    = k;
            break;
        }
    }
}

static void MiniDoomKeyDown(int key) { MiniDoomKeyAction(key, 1); }
static void MiniDoomKeyUp  (int key) { MiniDoomKeyAction(key, 2); }

static int Linux_LoadFile(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Error: cannot open file %s\n", path);
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -1; }
    long sz = ftell(f);
    if (sz <= 0) { fclose(f); fprintf(stderr, "Error: empty/invalid file size\n"); return -1; }
    rewind(f);

    void *buf = malloc((size_t)sz);
    if (!buf) { fclose(f); fprintf(stderr, "Error: malloc failed\n"); return -1; }

    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (rd != (size_t)sz) {
        fprintf(stderr, "Error: fread mismatch (%zu/%ld)\n", rd, sz);
        free(buf);
        return -1;
    }

    g_DoomWadAddress = (char*)buf;
    g_DoomWadSize    = (unsigned int)sz;

    unsigned long long summ = 0;
    for (unsigned i = 0; i < g_DoomWadSize; i++) {
        summ += 1 + (unsigned char)g_DoomWadAddress[i];
    }
    printf("Successfully loaded file: %s (%u bytes) %llu code\n", path, g_DoomWadSize, summ);
    return 0;
}

static int  g_DoomWinWidth  = 640;
static int  g_DoomWinHeight = 480;

int main(int argc, char *argv[]) {
    const char *wad_path = (argc > 1) ? argv[1] : "doom.wad";

    // X11 init
    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "Error: cannot open X display\n");
        return 1;
    }
    int screen = DefaultScreen(dpy);
    Window root = RootWindow(dpy, screen);

    XSetWindowAttributes attrs;
    attrs.event_mask = ExposureMask | KeyPressMask | KeyReleaseMask | StructureNotifyMask;
    Window win = XCreateWindow(
        dpy, root,
        0, 0, g_DoomWinWidth, g_DoomWinHeight,
        0,
        DefaultDepth(dpy, screen),
        InputOutput,
        DefaultVisual(dpy, screen),
        CWEventMask, &attrs
    );
    XStoreName(dpy, win, "Doom window (Linux/X11)");
    XMapWindow(dpy, win);

    int bytes_per_pixel = 4;
    size_t img_size = (size_t)g_DoomWinWidth * (size_t)g_DoomWinHeight * (size_t)bytes_per_pixel;
    char *pixels = (char*)calloc(1, img_size);
    if (!pixels) {
        fprintf(stderr, "Error: cannot allocate framebuffer\n");
        return 1;
    }

    XImage *img = XCreateImage(
        dpy, DefaultVisual(dpy, screen),
        24,               
        ZPixmap,
        0,                
        pixels,           
        g_DoomWinWidth,
        g_DoomWinHeight,
        32,               // bitmap_pad
        g_DoomWinWidth * bytes_per_pixel // bytes_per_line
    );
    if (!img) {
        fprintf(stderr, "Error: XCreateImage failed\n");
        free(pixels);
        return 1;
    }

    GC gc = XCreateGC(dpy, win, 0, NULL);

    g_DoomHeapSize    = 0x4000000;
    g_DoomHeapAddress = malloc(g_DoomHeapSize);
    if (!g_DoomHeapAddress) {
        fprintf(stderr, "Error: cannot allocate Doom heap\n");
        XDestroyImage(img); 
        XFreeGC(dpy, gc);
        XDestroyWindow(dpy, win);
        XCloseDisplay(dpy);
        return 1;
    }

    if (Linux_LoadFile(wad_path) != 0) {
        fprintf(stderr, "Failed to load WAD\n");
        free(g_DoomHeapAddress);
        XDestroyImage(img);
        XFreeGC(dpy, gc);
        XDestroyWindow(dpy, win);
        XCloseDisplay(dpy);
        return 1;
    }

    CrtDoomInit();
    printf("INITED!!\n");

    int running = 1;
    while (running) {
        while (XPending(dpy)) {
            XEvent ev;
            XNextEvent(dpy, &ev);
            switch (ev.type) {
                case Expose:
                    break;
                case ConfigureNotify:
                    break;
                case KeyPress: {
                    KeySym ks = XLookupKeysym(&ev.xkey, 0);
                    MiniDoomKeyDown((int)ks);
                    if (ks == XK_q) { running = 0; }
                } break;
                case KeyRelease: {
                    KeySym ks = XLookupKeysym(&ev.xkey, 0);
                    MiniDoomKeyUp((int)ks);
                } break;
                case ClientMessage:
                default:
                    break;
            }
        }

        LinuxDoomUpdateTime();
        g_LinuxDoomControlRegs.pixels = pixels;
        g_LinuxDoomControlRegs.width  = g_DoomWinWidth;
        g_LinuxDoomControlRegs.height = g_DoomWinHeight;

        CrtDoomIteration();

        XPutImage(dpy, win, gc, img, 0, 0, 0, 0, g_DoomWinWidth, g_DoomWinHeight);
        XFlush(dpy);

        usleep(10000);
    }

    free(g_DoomHeapAddress);
    XDestroyImage(img);
    XFreeGC(dpy, gc);
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
    return 0;
}
