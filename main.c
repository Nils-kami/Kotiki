#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <time.h>

#include <winsock2.h>
#include <windows.h>

#ifndef min
#define min(a,b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef max
#define max(a,b) (((a) > (b)) ? (a) : (b))
#endif

#include <gdiplus.h>
#include <curl/curl.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "curl.lib")
#pragma comment(lib, "gdiplus.lib")

// --- Глобальные переменные ---
ULONG_PTR gdiplusToken;
GpImage*  currentImage = NULL;
HWND hButton = NULL;
#define ID_BTN_NEXT 101

// --- Функция для правильного перевода UTF-8 в Unicode (спасает от кракозябр) ---
wchar_t* wstr(const char* str) {
    static wchar_t buf[256];
    MultiByteToWideChar(CP_UTF8, 0, str, -1, buf, 256);
    return buf;
}

LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
void DownloadAndLoadCat(HWND hwnd);
static size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp);

// --- Функция скачивания ---
int download_file(const char* url, const char* filename, CURLcode* out_res, long* out_http) {
    CURL* curl = curl_easy_init();
    if (!curl) return -1;

    FILE* file = fopen(filename, "wb");
    if (!file) { 
        curl_easy_cleanup(curl); 
        return -3; // Ошибка: файл заблокирован или нет прав на запись
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (Windows NT 10.0; Win64; x64)");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    *out_res = curl_easy_perform(curl);
    *out_http = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, out_http);

    fclose(file);
    curl_easy_cleanup(curl);

    if (*out_res != CURLE_OK) return -1;
    if (*out_http >= 400) return -2;

    return 0;
}

static size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    return fwrite(contents, size, nmemb, (FILE*)userp);
}

// --- Загрузка котика в окно ---
void DownloadAndLoadCat(HWND hwnd) {
    SetWindowTextW(hwnd, wstr("Загрузка..."));
    SetWindowTextW(hButton, wstr("Ждите..."));
    EnableWindow(hButton, FALSE);

    // КРИТИЧЕСКИ ВАЖНО: Освобождаем старый файл ДО скачивания нового!
    // Иначе Windows не даст перезаписать cat.jpg, и мы получим ложную ошибку сети.
    if (currentImage) {
        GdipDisposeImage(currentImage);
        currentImage = NULL;
    }

    char url[256];
    sprintf(url, "https://cataas.com/cat?t=%d", (int)time(NULL));
    const char* filename = "cat.jpg";

    CURLcode res = CURLE_OK; 
    long http_code = 0;
    int dl_status = download_file(url, filename, &res, &http_code);

    if (dl_status == 0) {
        WCHAR wFilename[MAX_PATH];
        MultiByteToWideChar(CP_ACP, 0, filename, -1, wFilename, MAX_PATH);
        
        WCHAR fullPath[MAX_PATH];
        GetFullPathNameW(wFilename, MAX_PATH, fullPath, NULL);
        
        GpStatus status = GdipLoadImageFromFile(fullPath, &currentImage);
        
        if (status == Ok) {
            SetWindowTextW(hwnd, wstr("Случайный мем с котиком"));
        } else {
            WCHAR errMsg[512];
            swprintf(errMsg, 512, L"GDI+ error: %d", status);
            MessageBoxW(hwnd, errMsg, wstr("Ошибка картинки"), MB_ICONERROR);
            SetWindowTextW(hwnd, wstr("Ошибка чтения"));
        }
    } else if (dl_status == -3) {
        MessageBoxW(hwnd, wstr("Не удалось открыть файл cat.jpg для записи.\nВозможно, он заблокирован другой программой."), wstr("Ошибка файла"), MB_ICONERROR);
        SetWindowTextW(hwnd, wstr("Ошибка файла"));
    } else if (dl_status == -2) {
        WCHAR errMsg[512];
        swprintf(errMsg, 512, L"HTTP код: %ld\nСайт может блокировать спам.", http_code);
        MessageBoxW(hwnd, errMsg, wstr("Ошибка сети"), MB_ICONERROR);
        SetWindowTextW(hwnd, wstr("Ошибка скачивания"));
    } else {
        WCHAR errMsg[512];
        const char* curl_err = curl_easy_strerror(res);
        WCHAR wErr[256];
        MultiByteToWideChar(CP_ACP, 0, curl_err, -1, wErr, 256);
        swprintf(errMsg, 512, L"curl: %s (Code: %d)", wErr, res);
        MessageBoxW(hwnd, errMsg, wstr("Ошибка сети"), MB_ICONERROR);
        SetWindowTextW(hwnd, wstr("Ошибка скачивания"));
    }

    SetWindowTextW(hButton, wstr("Следующий котик"));
    EnableWindow(hButton, TRUE);
    InvalidateRect(hwnd, NULL, TRUE);
}

// --- Оконная процедура ---
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        hButton = CreateWindowW(L"BUTTON", wstr("Следующий котик"),
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            0, 0, 0, 0, hwnd, (HMENU)ID_BTN_NEXT, NULL, NULL);
        DownloadAndLoadCat(hwnd);
        break;

    case WM_COMMAND:
        if (LOWORD(wParam) == ID_BTN_NEXT) {
            DownloadAndLoadCat(hwnd);
        }
        break;

    case WM_SIZE: {
        int width = LOWORD(lParam);
        int height = HIWORD(lParam);
        MoveWindow(hButton, width / 2 - 100, height - 50, 200, 40, TRUE);
        break;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        FillRect(hdc, &ps.rcPaint, (HBRUSH)GetStockObject(WHITE_BRUSH));

        if (currentImage) {
            GpGraphics* graphics = NULL;
            GdipCreateFromHDC(hdc, &graphics);
            GdipSetInterpolationMode(graphics, InterpolationModeHighQualityBicubic);

            RECT rect;
            GetClientRect(hwnd, &rect);
            int winW = rect.right;
            int winH = rect.bottom - 60;

            UINT imgW = 0, imgH = 0;
            GdipGetImageWidth(currentImage, &imgW);
            GdipGetImageHeight(currentImage, &imgH);

            if (imgW > 0 && imgH > 0) {
                float scaleW = (float)winW / imgW;
                float scaleH = (float)winH / imgH;
                float scale = min(scaleW, scaleH);

                int drawW = (int)(imgW * scale);
                int drawH = (int)(imgH * scale);
                int drawX = (winW - drawW) / 2;
                int drawY = (winH - drawH) / 2;

                GdipDrawImageRectI(graphics, currentImage, drawX, drawY, drawW, drawH);
            }
            GdipDeleteGraphics(graphics);
        } else {
            SetTextColor(hdc, RGB(255, 0, 0));
            SetBkMode(hdc, TRANSPARENT);
            const wchar_t* errText = wstr("Картинка не загружена.");
            TextOutW(hdc, 20, 20, errText, wcslen(errText));
        }
        EndPaint(hwnd, &ps);
        break;
    }

    case WM_DESTROY:
        if (currentImage) {
            GdipDisposeImage(currentImage);
            currentImage = NULL;
        }
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

// --- Точка входа ---
int main() {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    GdiplusStartupInput gdiplusStartupInput;
    memset(&gdiplusStartupInput, 0, sizeof(GdiplusStartupInput));
    gdiplusStartupInput.GdiplusVersion = 1;
    
    if (GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL) != Ok) {
        MessageBoxW(NULL, L"GDI+ Init Failed", L"Error", MB_ICONERROR);
        return 1;
    }

    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"CatAppClass";
    RegisterClassExW(&wc);

    // Устанавливаем правильный заголовок сразу при создании
    HWND hwnd = CreateWindowExW(0, L"CatAppClass", wstr("Случайный мем с котиком"),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
        NULL, NULL, wc.hInstance, NULL);

    if (!hwnd) {
        GdiplusShutdown(gdiplusToken);
        WSACleanup();
        return 1;
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    GdiplusShutdown(gdiplusToken);
    WSACleanup();
    return 0;
}