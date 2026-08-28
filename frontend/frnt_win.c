#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0600
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <io.h>
#include <fcntl.h>

#include "frnt_protocol.h"

#define WM_APP_FRAME_READY (WM_APP + 1)
#define WM_APP_READER_DONE (WM_APP + 2)
#define EVENT_QUEUE_CAPACITY 512
#define IO_CHUNK_SIZE (1024u * 1024u)

struct FrontendState {
    HWND window;
    HANDLE input;
    HANDLE output;
    HANDLE reader_thread;

    CRITICAL_SECTION frame_lock;
    unsigned char *frame;
    int frame_width;
    int frame_height;

    CRITICAL_SECTION event_lock;
    unsigned char event_queue[EVENT_QUEUE_CAPACITY];
    size_t event_count;
    unsigned char pressed[DOOMBF_KEY_COUNT];

    LONG stop_requested;
    LONG initial_resize_done;
    DWORD reader_error;
};

static struct FrontendState g_state;

static int stop_requested(void) {
    return InterlockedCompareExchange(&g_state.stop_requested, 0, 0) != 0;
}

static void request_stop(void) {
    InterlockedExchange(&g_state.stop_requested, 1);
    if (g_state.reader_thread) {
        CancelSynchronousIo(g_state.reader_thread);
    }
}

static int read_exact(HANDLE handle, void *buffer, size_t size, DWORD *error_code) {
    unsigned char *cursor = (unsigned char *)buffer;
    size_t remaining = size;

    while (remaining > 0 && !stop_requested()) {
        const DWORD request = remaining > IO_CHUNK_SIZE ? IO_CHUNK_SIZE : (DWORD)remaining;
        DWORD received = 0;
        if (!ReadFile(handle, cursor, request, &received, NULL)) {
            *error_code = GetLastError();
            return 0;
        }
        if (received == 0) {
            *error_code = ERROR_BROKEN_PIPE;
            return 0;
        }
        cursor += received;
        remaining -= received;
    }

    if (remaining != 0) {
        *error_code = ERROR_OPERATION_ABORTED;
        return 0;
    }
    return 1;
}

static int write_exact(HANDLE handle, const void *buffer, size_t size, DWORD *error_code) {
    const unsigned char *cursor = (const unsigned char *)buffer;
    size_t remaining = size;

    while (remaining > 0 && !stop_requested()) {
        const DWORD request = remaining > IO_CHUNK_SIZE ? IO_CHUNK_SIZE : (DWORD)remaining;
        DWORD written = 0;
        if (!WriteFile(handle, cursor, request, &written, NULL)) {
            *error_code = GetLastError();
            return 0;
        }
        if (written == 0) {
            *error_code = ERROR_BROKEN_PIPE;
            return 0;
        }
        cursor += written;
        remaining -= written;
    }

    if (remaining != 0) {
        *error_code = ERROR_OPERATION_ABORTED;
        return 0;
    }
    return 1;
}

static int read_until_frame_marker(DWORD *error_code) {
    unsigned char byte = 0;
    while (!stop_requested()) {
        if (!read_exact(g_state.input, &byte, 1, error_code)) {
            return 0;
        }
        if (byte == 0) {
            fflush(stderr);
            return 1;
        }
        fputc(byte, stderr);
    }
    *error_code = ERROR_OPERATION_ABORTED;
    return 0;
}

static int read_u16_be(uint16_t *value, DWORD *error_code) {
    unsigned char bytes[2];
    if (!read_exact(g_state.input, bytes, sizeof(bytes), error_code)) {
        return 0;
    }
    *value = (uint16_t)(((uint16_t)bytes[0] << 8) | bytes[1]);
    return 1;
}

static void queue_protocol_event(unsigned char event) {
    EnterCriticalSection(&g_state.event_lock);
    if (g_state.event_count < EVENT_QUEUE_CAPACITY) {
        g_state.event_queue[g_state.event_count++] = event;
    } else {
        fprintf(stderr, "[frnt] input queue is full; dropping key event 0x%02x\n", event);
    }
    LeaveCriticalSection(&g_state.event_lock);
}

static int flush_protocol_events(DWORD *error_code) {
    unsigned char events[EVENT_QUEUE_CAPACITY + 1];
    size_t count = 0;

    EnterCriticalSection(&g_state.event_lock);
    count = g_state.event_count;
    if (count > 0) {
        memcpy(events, g_state.event_queue, count);
    }
    g_state.event_count = 0;
    LeaveCriticalSection(&g_state.event_lock);

    /* Zero terminates the event packet and lets Doom start its next frame. */
    events[count++] = 0;
    return write_exact(g_state.output, events, count, error_code);
}

static int validate_frame_size(uint16_t width, uint16_t height, size_t *pixel_count) {
    if (width == 0 || height == 0 ||
        width > DOOMBF_MAX_FRAME_WIDTH || height > DOOMBF_MAX_FRAME_HEIGHT) {
        return 0;
    }
    if ((size_t)width > SIZE_MAX / (size_t)height) {
        return 0;
    }
    *pixel_count = (size_t)width * (size_t)height;
    return *pixel_count <= SIZE_MAX / 4;
}

static DWORD WINAPI reader_thread_main(LPVOID unused) {
    DWORD error_code = ERROR_SUCCESS;
    (void)unused;

    while (!stop_requested()) {
        uint16_t width = 0;
        uint16_t height = 0;
        size_t pixel_count = 0;
        size_t input_size = 0;
        size_t output_size = 0;
        unsigned char *rgb = NULL;
        unsigned char *bgra = NULL;

        if (!read_until_frame_marker(&error_code)) {
            break;
        }
        if (!read_u16_be(&width, &error_code) || !read_u16_be(&height, &error_code)) {
            break;
        }
        if (!validate_frame_size(width, height, &pixel_count)) {
            fprintf(stderr, "[frnt] invalid frame dimensions: %u x %u\n", width, height);
            error_code = ERROR_INVALID_DATA;
            break;
        }

        input_size = pixel_count * 3;
        output_size = pixel_count * 4;
        rgb = (unsigned char *)malloc(input_size);
        bgra = (unsigned char *)malloc(output_size);
        if (!rgb || !bgra) {
            fprintf(stderr, "[frnt] cannot allocate %zu-byte framebuffer\n", output_size);
            free(rgb);
            free(bgra);
            error_code = ERROR_OUTOFMEMORY;
            break;
        }

        fprintf(stderr, "[frnt] frame %u x %u (%zu bytes)\n", width, height, input_size);
        if (!read_exact(g_state.input, rgb, input_size, &error_code)) {
            free(rgb);
            free(bgra);
            break;
        }

        for (size_t src = 0, dst = 0; src < input_size; src += 3, dst += 4) {
            bgra[dst + 0] = rgb[src + 0];
            bgra[dst + 1] = rgb[src + 1];
            bgra[dst + 2] = rgb[src + 2];
            bgra[dst + 3] = 0xff;
        }
        free(rgb);

        EnterCriticalSection(&g_state.frame_lock);
        {
            unsigned char *old_frame = g_state.frame;
            g_state.frame = bgra;
            g_state.frame_width = (int)width;
            g_state.frame_height = (int)height;
            bgra = old_frame;
        }
        LeaveCriticalSection(&g_state.frame_lock);
        free(bgra);

        PostMessage(g_state.window, WM_APP_FRAME_READY, (WPARAM)width, (LPARAM)height);

        if (!flush_protocol_events(&error_code)) {
            break;
        }
    }

    if (stop_requested() && error_code == ERROR_OPERATION_ABORTED) {
        error_code = ERROR_SUCCESS;
    }
    g_state.reader_error = error_code;
    PostMessage(g_state.window, WM_APP_READER_DONE, (WPARAM)error_code, 0);
    return error_code;
}

static int protocol_key_from_virtual_key(WPARAM key) {
    switch (key) {
        case VK_RETURN:  return DOOMBF_KEY_ENTER;
        case VK_LEFT:    return DOOMBF_KEY_LEFT;
        case VK_RIGHT:   return DOOMBF_KEY_RIGHT;
        case VK_UP:      return DOOMBF_KEY_UP;
        case VK_DOWN:    return DOOMBF_KEY_DOWN;
        case VK_SPACE:   return DOOMBF_KEY_SPACE;
        case VK_CONTROL: return DOOMBF_KEY_CTRL;
        case VK_ESCAPE:  return DOOMBF_KEY_ESC;
        case 'Y':        return DOOMBF_KEY_Y;
        default:         return 0;
    }
}

static void key_down(WPARAM virtual_key, LPARAM key_data) {
    const int key = protocol_key_from_virtual_key(virtual_key);
    const int was_down = (key_data & (1L << 30)) != 0;
    if (!key || was_down || g_state.pressed[key]) {
        return;
    }
    g_state.pressed[key] = 1;
    queue_protocol_event((unsigned char)key);
}

static void key_up(WPARAM virtual_key) {
    const int key = protocol_key_from_virtual_key(virtual_key);
    if (!key || !g_state.pressed[key]) {
        return;
    }
    g_state.pressed[key] = 0;
    queue_protocol_event((unsigned char)(key | DOOMBF_KEY_RELEASE));
}

static void release_all_keys(void) {
    for (int key = 1; key < DOOMBF_KEY_COUNT; ++key) {
        if (g_state.pressed[key]) {
            g_state.pressed[key] = 0;
            queue_protocol_event((unsigned char)(key | DOOMBF_KEY_RELEASE));
        }
    }
}

static void resize_for_first_frame(HWND window, int frame_width, int frame_height) {
    RECT work_area;
    RECT window_rect = {0, 0, frame_width, frame_height};
    const DWORD style = (DWORD)GetWindowLongPtr(window, GWL_STYLE);
    const DWORD ex_style = (DWORD)GetWindowLongPtr(window, GWL_EXSTYLE);
    int width;
    int height;

    AdjustWindowRectEx(&window_rect, style, FALSE, ex_style);
    width = window_rect.right - window_rect.left;
    height = window_rect.bottom - window_rect.top;

    if (SystemParametersInfo(SPI_GETWORKAREA, 0, &work_area, 0)) {
        const int max_width = (work_area.right - work_area.left) * 9 / 10;
        const int max_height = (work_area.bottom - work_area.top) * 9 / 10;
        if (width > max_width) width = max_width;
        if (height > max_height) height = max_height;
    }

    SetWindowPos(window, NULL, 0, 0, width, height,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

static void paint_frame(HWND window) {
    PAINTSTRUCT paint;
    RECT client;
    HDC dc = BeginPaint(window, &paint);
    GetClientRect(window, &client);
    //FillRect(dc, &client, (HBRUSH)GetStockObject(BLACK_BRUSH));

    EnterCriticalSection(&g_state.frame_lock);
    if (g_state.frame && g_state.frame_width > 0 && g_state.frame_height > 0 &&
        client.right > client.left && client.bottom > client.top) {
        BITMAPINFO info;
        int draw_width = client.right - client.left;
        int draw_height = client.bottom - client.top;
        int x = 0;
        int y = 0;
        const double source_aspect = (double)g_state.frame_width / (double)g_state.frame_height;
        const double target_aspect = draw_height > 0 ? (double)draw_width / (double)draw_height : source_aspect;

        if (target_aspect > source_aspect) {
            draw_width = (int)(draw_height * source_aspect);
            x = ((client.right - client.left) - draw_width) / 2;
        } else {
            draw_height = (int)(draw_width / source_aspect);
            y = ((client.bottom - client.top) - draw_height) / 2;
        }

        memset(&info, 0, sizeof(info));
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = g_state.frame_width;
        info.bmiHeader.biHeight = -g_state.frame_height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;

        SetStretchBltMode(dc, COLORONCOLOR);
        StretchDIBits(dc,
                      x, y, draw_width, draw_height,
                      0, 0, g_state.frame_width, g_state.frame_height,
                      g_state.frame, &info, DIB_RGB_COLORS, SRCCOPY);
    } else {
        const char message[] = "Waiting for the first DoomBF frame...";
        FillRect(dc, &client, (HBRUSH)GetStockObject(BLACK_BRUSH));
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 255, 255));
        DrawTextA(dc, message, -1, &client, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    LeaveCriticalSection(&g_state.frame_lock);

    EndPaint(window, &paint);
}

static LRESULT CALLBACK frontend_window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            paint_frame(window);
            return 0;
        case WM_SIZE:
            InvalidateRect(window, NULL, FALSE);
            return 0;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            if (wparam == 'Q') {
                PostMessage(window, WM_CLOSE, 0, 0);
                return 0;
            }
            key_down(wparam, lparam);
            return 0;
        case WM_KEYUP:
        case WM_SYSKEYUP:
            key_up(wparam);
            return 0;
        case WM_KILLFOCUS:
            release_all_keys();
            return 0;
        case WM_APP_FRAME_READY:
            if (InterlockedCompareExchange(&g_state.initial_resize_done, 1, 0) == 0) {
                resize_for_first_frame(window, (int)wparam, (int)lparam);
            }
            InvalidateRect(window, NULL, FALSE);
            UpdateWindow(window);
            return 0;
        case WM_APP_READER_DONE:
            if (!stop_requested()) {
                const DWORD error_code = (DWORD)wparam;
                if (error_code != ERROR_SUCCESS && error_code != ERROR_BROKEN_PIPE) {
                    char text[256];
                    snprintf(text, sizeof(text), "The DoomBF stream stopped (Windows error %lu).",
                             (unsigned long)error_code);
                    MessageBoxA(window, text, "DoomBF frontend", MB_OK | MB_ICONERROR);
                }
                DestroyWindow(window);
            }
            return 0;
        case WM_CLOSE:
            request_stop();
            DestroyWindow(window);
            return 0;
        case WM_DESTROY:
            request_stop();
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProc(window, message, wparam, lparam);
    }
}

int main(void) {
    HINSTANCE instance = GetModuleHandle(NULL);
    WNDCLASSEXA window_class;
    RECT initial_rect = {0, 0, 640, 480};
    MSG message;
    int exit_code = 0;

    memset(&g_state, 0, sizeof(g_state));
    InitializeCriticalSection(&g_state.frame_lock);
    InitializeCriticalSection(&g_state.event_lock);

    if (_setmode(_fileno(stdin), _O_BINARY) == -1 ||
        _setmode(_fileno(stdout), _O_BINARY) == -1) {
        perror("[frnt] setting binary stdio mode");
        exit_code = 1;
        goto cleanup;
    }

    g_state.input = GetStdHandle(STD_INPUT_HANDLE);
    g_state.output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (g_state.input == INVALID_HANDLE_VALUE || g_state.output == INVALID_HANDLE_VALUE ||
        !g_state.input || !g_state.output) {
        fprintf(stderr, "[frnt] stdin/stdout are unavailable\n");
        exit_code = 1;
        goto cleanup;
    }

    SetProcessDPIAware();
    memset(&window_class, 0, sizeof(window_class));
    window_class.cbSize = sizeof(window_class);
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = frontend_window_proc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursor(NULL, IDC_ARROW);
    window_class.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    window_class.lpszClassName = "DoomBFFrontendWindow";

    if (!RegisterClassExA(&window_class)) {
        fprintf(stderr, "[frnt] RegisterClassEx failed: %lu\n", (unsigned long)GetLastError());
        exit_code = 1;
        goto cleanup;
    }

    AdjustWindowRect(&initial_rect, WS_OVERLAPPEDWINDOW, FALSE);
    g_state.window = CreateWindowExA(
        0,
        window_class.lpszClassName,
        "DoomBF Frontend (Q closes the window)",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        initial_rect.right - initial_rect.left,
        initial_rect.bottom - initial_rect.top,
        NULL, NULL, instance, NULL);
    if (!g_state.window) {
        fprintf(stderr, "[frnt] CreateWindowEx failed: %lu\n", (unsigned long)GetLastError());
        exit_code = 1;
        goto cleanup;
    }

    ShowWindow(g_state.window, SW_SHOWDEFAULT);
    UpdateWindow(g_state.window);

    g_state.reader_thread = CreateThread(NULL, 0, reader_thread_main, NULL, 0, NULL);
    if (!g_state.reader_thread) {
        fprintf(stderr, "[frnt] CreateThread failed: %lu\n", (unsigned long)GetLastError());
        exit_code = 1;
        DestroyWindow(g_state.window);
        goto cleanup;
    }

    for (;;) {
        const BOOL message_result = GetMessage(&message, NULL, 0, 0);
        if (message_result == 0) {
            break;
        }
        if (message_result == -1) {
            fprintf(stderr, "[frnt] GetMessage failed: %lu\n", (unsigned long)GetLastError());
            exit_code = 1;
            break;
        }
        TranslateMessage(&message);
        DispatchMessage(&message);
    }

    request_stop();
    WaitForSingleObject(g_state.reader_thread, INFINITE);
    if (g_state.reader_error != ERROR_SUCCESS &&
        g_state.reader_error != ERROR_BROKEN_PIPE &&
        g_state.reader_error != ERROR_OPERATION_ABORTED) {
        exit_code = 1;
    }

cleanup:
    if (g_state.reader_thread) {
        CloseHandle(g_state.reader_thread);
    }
    EnterCriticalSection(&g_state.frame_lock);
    free(g_state.frame);
    g_state.frame = NULL;
    LeaveCriticalSection(&g_state.frame_lock);
    DeleteCriticalSection(&g_state.event_lock);
    DeleteCriticalSection(&g_state.frame_lock);
    return exit_code;
}
