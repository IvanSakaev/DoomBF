#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0600
#include <windows.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "crt/doom_env.h"

struct DoomControlRegs g_WinDoomControlRegs;
void *g_DoomHeapAddress = NULL;
unsigned int g_DoomHeapSize = 0;
char *g_DoomWadAddress = NULL;
unsigned int g_DoomWadSize = 0;
struct DoomControlRegs *g_DoomControlRegs = &g_WinDoomControlRegs;

void EnvPutChar(int c) {
    fputc(c, stdout);
    fflush(stdout);
}

void EnvExit(int code) {
    exit(code == 0 ? 0 : 1);
}

static void WinDoomUpdateTime(void) {
    const ULONGLONG time_ms = GetTickCount64();
    g_WinDoomControlRegs.time_sec = (int)(time_ms / 1000ULL);
    g_WinDoomControlRegs.time_usec = (int)((time_ms % 1000ULL) * 1000ULL);
}

#define DOOMWIN_KEY_COUNT 10
static unsigned char g_DoomWinPressed[DOOMWIN_KEY_COUNT];

static int Win_MapVirtualKey(WPARAM virtual_key) {
    switch (virtual_key) {
        case VK_RETURN:  return CRT_DOOM_KEY_ENTER;
        case VK_LEFT:    return CRT_DOOM_KEY_LEFT_ARROW;
        case VK_RIGHT:   return CRT_DOOM_KEY_RIGHT_ARROW;
        case VK_UP:      return CRT_DOOM_KEY_UP_ARROW;
        case VK_DOWN:    return CRT_DOOM_KEY_DOWN_ARROW;
        case VK_SPACE:   return CRT_DOOM_KEY_SPACE;
        case VK_CONTROL: return CRT_DOOM_KEY_CTRL;
        case VK_ESCAPE:  return CRT_DOOM_KEY_ESCAPE;
        case 'Y':        return CRT_DOOM_KEY_Y;
        default:         return 0;
    }
}

static int Win_QueueDoomKey(int key, int action) {
    for (size_t i = 0; i < sizeof(g_WinDoomControlRegs.keys) / sizeof(g_WinDoomControlRegs.keys[0]); ++i) {
        if (!g_WinDoomControlRegs.keys[i].action) {
            g_WinDoomControlRegs.keys[i].action = action;
            g_WinDoomControlRegs.keys[i].key = key;
            return 1;
        }
    }
    fprintf(stderr, "Warning: Doom key queue is full; dropping key %d action %d\n", key, action);
    return 0;
}

static void Win_KeyDown(WPARAM virtual_key, LPARAM key_data) {
    const int key = Win_MapVirtualKey(virtual_key);
    const int was_down = (key_data & (1L << 30)) != 0;
    if (!key || was_down || g_DoomWinPressed[key]) {
        return;
    }
    if (Win_QueueDoomKey(key, 1)) {
        g_DoomWinPressed[key] = 1;
    }
}

static void Win_KeyUp(WPARAM virtual_key) {
    const int key = Win_MapVirtualKey(virtual_key);
    if (!key || !g_DoomWinPressed[key]) {
        return;
    }
    if (Win_QueueDoomKey(key, 2)) {
        g_DoomWinPressed[key] = 0;
    }
}

static void Win_ReleaseAllKeys(void) {
    for (int key = 1; key < DOOMWIN_KEY_COUNT; ++key) {
        if (g_DoomWinPressed[key] && Win_QueueDoomKey(key, 2)) {
            g_DoomWinPressed[key] = 0;
        }
    }
}

#define DOOMWIN_WND_CLASS "DoomWinWndClass"

static const int g_DoomWinWidth = 640;
static const int g_DoomWinHeight = 480;
static HBITMAP g_DoomWinBitmap = NULL;
static unsigned char *g_DoomWinPixelsBuffer = NULL;
static HWND g_DoomWinMain = NULL;

static void Win_Repaint(HWND window) {
    PAINTSTRUCT paint;
    RECT client;
    HDC dc = BeginPaint(window, &paint);
    GetClientRect(window, &client);
    //FillRect(dc, &client, (HBRUSH)GetStockObject(BLACK_BRUSH));

    if (g_DoomWinBitmap && g_DoomWinPixelsBuffer &&
        client.right > client.left && client.bottom > client.top) {
        HDC memory_dc = CreateCompatibleDC(dc);
        if (memory_dc) {
            HGDIOBJ old_bitmap = SelectObject(memory_dc, g_DoomWinBitmap);
            int client_width = client.right - client.left;
            int client_height = client.bottom - client.top;
            int draw_width = client_width;
            int draw_height = client_height;
            int x = 0;
            int y = 0;
            const double source_aspect = (double)g_DoomWinWidth / (double)g_DoomWinHeight;
            const double target_aspect = client_height > 0
                ? (double)client_width / (double)client_height
                : source_aspect;

            if (target_aspect > source_aspect) {
                draw_width = (int)(draw_height * source_aspect);
                x = (client_width - draw_width) / 2;
            } else {
                draw_height = (int)(draw_width / source_aspect);
                y = (client_height - draw_height) / 2;
            }

            SetStretchBltMode(dc, COLORONCOLOR);
            StretchBlt(dc, x, y, draw_width, draw_height,
                       memory_dc, 0, 0, g_DoomWinWidth, g_DoomWinHeight, SRCCOPY);
            SelectObject(memory_dc, old_bitmap);
            DeleteDC(memory_dc);
        }
    }
    EndPaint(window, &paint);
}

static LRESULT CALLBACK Win_WndProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            Win_Repaint(window);
            return 0;
        case WM_SIZE:
            InvalidateRect(window, NULL, FALSE);
            return 0;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            if (wparam == 'Q') {
                PostMessage(window, WM_CLOSE, 0, 0);
            } else {
                Win_KeyDown(wparam, lparam);
            }
            return 0;
        case WM_KEYUP:
        case WM_SYSKEYUP:
            Win_KeyUp(wparam);
            return 0;
        case WM_KILLFOCUS:
            Win_ReleaseAllKeys();
            return 0;
        case WM_CLOSE:
            DestroyWindow(window);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProc(window, message, wparam, lparam);
    }
}

static int Win_LoadFile(const char *path) {
    FILE *file = fopen(path, "rb");
    long file_size;
    void *buffer;

    if (!file) {
        fprintf(stderr, "Error: cannot open WAD file %s\n", path);
        return -1;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return -1;
    }
    file_size = ftell(file);
    if (file_size <= 0 || (unsigned long)file_size > UINT_MAX) {
        fprintf(stderr, "Error: invalid WAD size\n");
        fclose(file);
        return -1;
    }
    rewind(file);

    buffer = malloc((size_t)file_size);
    if (!buffer) {
        fprintf(stderr, "Error: cannot allocate WAD buffer\n");
        fclose(file);
        return -1;
    }
    if (fread(buffer, 1, (size_t)file_size, file) != (size_t)file_size) {
        fprintf(stderr, "Error: cannot read the complete WAD file\n");
        free(buffer);
        fclose(file);
        return -1;
    }
    fclose(file);

    g_DoomWadAddress = (char *)buffer;
    g_DoomWadSize = (unsigned int)file_size;

    {
        unsigned long long checksum = 0;
        for (unsigned int i = 0; i < g_DoomWadSize; ++i) {
            checksum += 1u + (unsigned char)g_DoomWadAddress[i];
        }
        printf("Successfully loaded file: %s (%u bytes) %llu code\n",
               path, g_DoomWadSize, checksum);
    }
    return 0;
}

static int create_framebuffer(void) {
    BITMAPINFO bitmap_info;
    memset(&bitmap_info, 0, sizeof(bitmap_info));
    bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmap_info.bmiHeader.biWidth = g_DoomWinWidth;
    bitmap_info.bmiHeader.biHeight = -g_DoomWinHeight;
    bitmap_info.bmiHeader.biPlanes = 1;
    bitmap_info.bmiHeader.biBitCount = 32;
    bitmap_info.bmiHeader.biCompression = BI_RGB;
    bitmap_info.bmiHeader.biSizeImage = (DWORD)(g_DoomWinWidth * g_DoomWinHeight * 4);

    g_DoomWinBitmap = CreateDIBSection(
        NULL, &bitmap_info, DIB_RGB_COLORS, (void **)&g_DoomWinPixelsBuffer, NULL, 0);
    if (!g_DoomWinBitmap || !g_DoomWinPixelsBuffer) {
        fprintf(stderr, "Error: CreateDIBSection failed (%lu)\n", (unsigned long)GetLastError());
        return -1;
    }
    memset(g_DoomWinPixelsBuffer, 0, bitmap_info.bmiHeader.biSizeImage);
    return 0;
}

int main(int argc, char *argv[]) {
    const char *wad_path = argc > 1 ? argv[1] : "doom.wad";
    HINSTANCE instance = GetModuleHandle(NULL);
    WNDCLASSEXA window_class;
    RECT window_rect = {0, 0, 640, 480};
    MSG message;
    int running = 1;
    int result = 1;

    SetProcessDPIAware();
    memset(&window_class, 0, sizeof(window_class));
    window_class.cbSize = sizeof(window_class);
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = Win_WndProc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursor(NULL, IDC_ARROW);
    window_class.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    window_class.lpszClassName = DOOMWIN_WND_CLASS;
    if (!RegisterClassExA(&window_class)) {
        fprintf(stderr, "Error: RegisterClassEx failed (%lu)\n", (unsigned long)GetLastError());
        goto cleanup;
    }

    AdjustWindowRect(&window_rect, WS_OVERLAPPEDWINDOW, FALSE);
    g_DoomWinMain = CreateWindowExA(
        0, DOOMWIN_WND_CLASS, "Doom window (Q closes the window)", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        window_rect.right - window_rect.left,
        window_rect.bottom - window_rect.top,
        NULL, NULL, instance, NULL);
    if (!g_DoomWinMain) {
        fprintf(stderr, "Error: CreateWindowEx failed (%lu)\n", (unsigned long)GetLastError());
        goto cleanup;
    }

    if (create_framebuffer() != 0) {
        goto cleanup;
    }

    g_DoomHeapSize = 0x4000000u;
    g_DoomHeapAddress = HeapAlloc(GetProcessHeap(), 0, g_DoomHeapSize);
    if (!g_DoomHeapAddress) {
        fprintf(stderr, "Error: cannot allocate Doom heap\n");
        goto cleanup;
    }
    if (Win_LoadFile(wad_path) != 0) {
        goto cleanup;
    }

    memset(&g_WinDoomControlRegs, 0, sizeof(g_WinDoomControlRegs));
    g_WinDoomControlRegs.pixels = (char *)g_DoomWinPixelsBuffer;
    g_WinDoomControlRegs.width = g_DoomWinWidth;
    g_WinDoomControlRegs.height = g_DoomWinHeight;

    CrtDoomInit();
    printf("INITED!!\n");

    ShowWindow(g_DoomWinMain, SW_SHOWDEFAULT);
    UpdateWindow(g_DoomWinMain);

    while (running) {
        while (PeekMessage(&message, NULL, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                running = 0;
                break;
            }
            TranslateMessage(&message);
            DispatchMessage(&message);
        }
        if (!running) {
            break;
        }

        WinDoomUpdateTime();
        g_WinDoomControlRegs.pixels = (char *)g_DoomWinPixelsBuffer;
        CrtDoomIteration();
        InvalidateRect(g_DoomWinMain, NULL, FALSE);
        Sleep(1);
    }
    result = 0;

cleanup:
    free(g_DoomWadAddress);
    g_DoomWadAddress = NULL;
    if (g_DoomHeapAddress) {
        HeapFree(GetProcessHeap(), 0, g_DoomHeapAddress);
        g_DoomHeapAddress = NULL;
    }
    if (g_DoomWinBitmap) {
        DeleteObject(g_DoomWinBitmap);
        g_DoomWinBitmap = NULL;
        g_DoomWinPixelsBuffer = NULL;
    }
    if (g_DoomWinMain && IsWindow(g_DoomWinMain)) {
        DestroyWindow(g_DoomWinMain);
    }
    return result;
}
