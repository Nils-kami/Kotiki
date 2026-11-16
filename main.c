#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <time.h>

// Порядок этих трех строк СТРОГО обязателен!
#include <winsock2.h>
#include <windows.h>
#include <commdlg.h>

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
#pragma comment(lib, "comdlg32.lib")

// --- Глобальные переменные ---
ULONG_PTR gdiplusToken;
GpImage*  currentImage = NULL;
HWND hButtonNext, hButtonSave, hEditTag, hRadioImg, hRadioGif;
#define ID_BTN_NEXT 101
#define ID_BTN_SAVE 102
#define ID_EDIT_TAG 103
#define ID_RADIO_IMG 104
#define ID_RADIO_GIF 105
#define ID_TIMER_GIF 201

// Переменные для GIF
UINT gifFrameCount = 0;
UINT gifCurrentFrame = 0;
UINT* gifFrameDelays = NULL;
GUID gifFrameDimension;
BOOL isGifLoaded = FALSE;
WCHAR currentFilePath[MAX_PATH] = L"cat.jpg";

// --- Прототипы ---
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
void LoadCat(HWND hwnd);
void SaveCurrentFile(HWND hwnd);
static size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp);
void StopGifAnimation(HWND hwnd);

// --- Функция скачивания ---
int download_file(const char* url, const char* filename) {
    CURL* curl = curl_easy_init();
    if (!curl) return -1;

    FILE* file = fopen(filename, "wb");
    if (!file) { curl_easy_cleanup(curl); return -3; }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (Windows NT 10.0; Win64; x64)");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    CURLcode res = curl_easy_perform(curl);
    fclose(file);
    curl_easy_cleanup(curl);

    return (res == CURLE_OK) ? 0 : -1;
}

static size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    return fwrite(contents, size, nmemb, (FILE*)userp);
}

// --- Остановка анимации ---
void StopGifAnimation(HWND hwnd) {
    KillTimer(hwnd, ID_TIMER_GIF);
    if (gifFrameDelays) {
        free(gifFrameDelays);
        gifFrameDelays = NULL;
    }
    gifFrameCount = 0;
    gifCurrentFrame = 0;
    isGifLoaded = FALSE;
}

// --- Загрузка ---
void LoadCat(HWND hwnd) {
    StopGifAnimation(hwnd);

    if (currentImage) {
        GdipDisposeImage(currentImage);
        currentImage = NULL;
    }

    WCHAR wTag[128] = L"";
    GetWindowTextW(hEditTag, wTag, 128);
    
    char utf8Tag[256] = "";
    WideCharToMultiByte(CP_UTF8, 0, wTag, -1, utf8Tag, 256, NULL, NULL);

    BOOL wantGif = (SendMessage(hRadioGif, BM_GETCHECK, 0, 0) == BST_CHECKED);

    char url[512];
    if (wantGif) {
        sprintf(url, "https://cataas.com/cat/gif?t=%d", (int)time(NULL));
    } else {
        if (strlen(utf8Tag) > 0) {
            sprintf(url, "https://cataas.com/cat/%s?t=%d", utf8Tag, (int)time(NULL));
        } else {
            sprintf(url, "https://cataas.com/cat?t=%d", (int)time(NULL));
        }
    }

    SetWindowTextW(hwnd, L"Загрузка...");
    EnableWindow(hButtonNext, FALSE);
    
    if (download_file(url, "cat.tmp") != 0) {
        MessageBoxW(hwnd, L"Ошибка сети. Проверьте интернет.", L"Ошибка", MB_ICONERROR);
        EnableWindow(hButtonNext, TRUE);
        SetWindowTextW(hwnd, L"Ошибка");
        return;
    }

    WCHAR wTempPath[MAX_PATH];
    GetFullPathNameW(L"cat.tmp", MAX_PATH, wTempPath, NULL);
    
    WCHAR wFinalPath[MAX_PATH];
    if (wantGif) {
        wcscpy(wFinalPath, L"cat.gif");
    } else {
        wcscpy(wFinalPath, L"cat.jpg");
    }
    wcscpy(currentFilePath, wFinalPath);

    DeleteFileW(wFinalPath);
    MoveFileW(wTempPath, wFinalPath);

    GpStatus status = GdipLoadImageFromFile(wFinalPath, &currentImage);
    if (status != Ok) {
        MessageBoxW(hwnd, L"Не удалось прочитать файл.", L"Ошибка", MB_ICONERROR);
        EnableWindow(hButtonNext, TRUE);
        return;
    }

    if (wantGif) {
        UINT count = 0;
        GdipImageGetFrameDimensionsCount(currentImage, &count);
        if (count > 0) {
            GUID* dimIDs = (GUID*)malloc(sizeof(GUID) * count);
            GdipImageGetFrameDimensionsList(currentImage, dimIDs, count);
            gifFrameDimension = dimIDs[0];
            free(dimIDs);

            GdipImageGetFrameCount(currentImage, &gifFrameDimension, &gifFrameCount);
            if (gifFrameCount > 1) {
                UINT size = 0;
                if (GdipGetPropertyItemSize(currentImage, PropertyTagFrameDelay, &size) == Ok) {
                    PropertyItem* prop = (PropertyItem*)malloc(size);
                    if (GdipGetPropertyItem(currentImage, PropertyTagFrameDelay, size, prop) == Ok) {
                        gifFrameDelays = (UINT*)malloc(prop->length);
                        memcpy(gifFrameDelays, prop->value, prop->length);
                        
                        gifCurrentFrame = 0;
                        isGifLoaded = TRUE;
                        
                        GdipImageSelectActiveFrame(currentImage, &gifFrameDimension, gifCurrentFrame);
                        
                        UINT delay = gifFrameDelays[gifCurrentFrame] * 10;
                        if (delay < 20) delay = 100; 
                        SetTimer(hwnd, ID_TIMER_GIF, delay, NULL);
                    }
                    free(prop);
                }
            }
        }
    }

    EnableWindow(hButtonNext, TRUE);
    SetWindowTextW(hwnd, L"Случайный мем с котиком");
    InvalidateRect(hwnd, NULL, FALSE); // FALSE для предотвращения лишней очистки фона
}

// --- Сохранение ---
void SaveCurrentFile(HWND hwnd) {
    if (!currentImage) {
        MessageBoxW(hwnd, L"Сначала загрузите котика.", L"Нет картинки", MB_ICONWARNING);
        return;
    }

    OPENFILENAMEW ofn;
    WCHAR szFileName[MAX_PATH] = L"cat_saved";
    
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    
    BOOL isGif = (SendMessage(hRadioGif, BM_GETCHECK, 0, 0) == BST_CHECKED);
    if (isGif) {
        ofn.lpstrFilter = L"GIF Files (*.gif)\0*.gif\0All Files (*.*)\0*.*\0";
        wcscat(szFileName, L".gif");
        ofn.lpstrDefExt = L"gif";
    } else {
        ofn.lpstrFilter = L"JPEG Files (*.jpg)\0*.jpg\0All Files (*.*)\0*.*\0";
        wcscat(szFileName, L".jpg");
        ofn.lpstrDefExt = L"jpg";
    }
    
    ofn.lpstrFile = szFileName;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_EXPLORER | OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY;
    
    if (GetSaveFileNameW(&ofn)) {
        if (CopyFileW(currentFilePath, ofn.lpstrFile, FALSE)) {
            MessageBoxW(hwnd, L"Файл успешно сохранен!", L"Успех", MB_ICONINFORMATION);
        } else {
            MessageBoxW(hwnd, L"Не удалось скопировать файл.", L"Ошибка", MB_ICONERROR);
        }
    }
}

// --- Оконная процедура ---
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        SetWindowTextW(hwnd, L"Случайный мем с котиком");

        CreateWindowW(L"STATIC", L"Тег:", WS_VISIBLE | WS_CHILD, 10, 10, 40, 25, hwnd, NULL, NULL, NULL);
        hEditTag = CreateWindowW(L"EDIT", L"", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, 50, 10, 150, 25, hwnd, (HMENU)ID_EDIT_TAG, NULL, NULL);

        hRadioImg = CreateWindowW(L"BUTTON", L"Картинка", WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON | WS_GROUP, 220, 10, 100, 25, hwnd, (HMENU)ID_RADIO_IMG, NULL, NULL);
        hRadioGif = CreateWindowW(L"BUTTON", L"GIF", WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON, 330, 10, 60, 25, hwnd, (HMENU)ID_RADIO_GIF, NULL, NULL);
        SendMessage(hRadioImg, BM_SETCHECK, BST_CHECKED, 0);

        hButtonNext = CreateWindowW(L"BUTTON", L"Следующий котик", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 10, 500, 180, 35, hwnd, (HMENU)ID_BTN_NEXT, NULL, NULL);
        hButtonSave = CreateWindowW(L"BUTTON", L"Сохранить", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 200, 500, 150, 35, hwnd, (HMENU)ID_BTN_SAVE, NULL, NULL);

        LoadCat(hwnd);
        break;

    case WM_TIMER:
        if (wParam == ID_TIMER_GIF && isGifLoaded && gifFrameCount > 0) {
            gifCurrentFrame = (gifCurrentFrame + 1) % gifFrameCount;
            GdipImageSelectActiveFrame(currentImage, &gifFrameDimension, gifCurrentFrame);
            
            if (gifFrameDelays) {
                UINT delay = gifFrameDelays[gifCurrentFrame] * 10;
                if (delay < 20) delay = 100;
                KillTimer(hwnd, ID_TIMER_GIF);
                SetTimer(hwnd, ID_TIMER_GIF, delay, NULL);
            }
            InvalidateRect(hwnd, NULL, FALSE);
        }
        break;

    case WM_ERASEBKGND:
        return 1; // Отключаем стандартную очистку фона (лечит мерцание)

    case WM_COMMAND:
        if (LOWORD(wParam) == ID_BTN_NEXT) {
            LoadCat(hwnd);
        } else if (LOWORD(wParam) == ID_BTN_SAVE) {
            SaveCurrentFile(hwnd);
        }
        break;

    case WM_SIZE: {
        int width = LOWORD(lParam);
        int height = HIWORD(lParam);
        MoveWindow(hButtonNext, 10, height - 45, 180, 35, TRUE);
        MoveWindow(hButtonSave, 200, height - 45, 150, 35, TRUE);
        break;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT rect;
        GetClientRect(hwnd, &rect);

        // Двойная буферизация для устранения мерцания
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBitmap = CreateCompatibleBitmap(hdc, rect.right, rect.bottom);
        HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);

        // Заливаем фон в памяти
        FillRect(memDC, &rect, (HBRUSH)GetStockObject(WHITE_BRUSH));

        if (currentImage) {
            GpGraphics* graphics = NULL;
            GdipCreateFromHDC(memDC, &graphics);
            GdipSetInterpolationMode(graphics, InterpolationModeHighQualityBicubic);

            // Область для картинки (отступаем сверху 40px и снизу 60px под кнопки)
            int winW = rect.right - 20;
            int winH = rect.bottom - 100;

            if (winW > 0 && winH > 0) {
                UINT imgW = 0, imgH = 0;
                GdipGetImageWidth(currentImage, &imgW);
                GdipGetImageHeight(currentImage, &imgH);

                if (imgW > 0 && imgH > 0) {
                    float scaleW = (float)winW / imgW;
                    float scaleH = (float)winH / imgH;
                    float scale = min(scaleW, scaleH);

                    int drawW = (int)(imgW * scale);
                    int drawH = (int)(imgH * scale);
                    int drawX = (rect.right - drawW) / 2;
                    int drawY = 40 + (winH - drawH) / 2;

                    GdipDrawImageRectI(graphics, currentImage, drawX, drawY, drawW, drawH);
                }
            }
            GdipDeleteGraphics(graphics);
        } else {
            SetTextColor(memDC, RGB(255, 0, 0));
            SetBkMode(memDC, TRANSPARENT);
            TextOutW(memDC, 20, 50, L"Картинка не загружена.", 22);
        }

        // Копируем готовую картинку из памяти на экран
        BitBlt(hdc, 0, 0, rect.right, rect.bottom, memDC, 0, 0, SRCCOPY);

        // Очистка памяти
        SelectObject(memDC, oldBitmap);
        DeleteObject(memBitmap);
        DeleteDC(memDC);

        EndPaint(hwnd, &ps);
        break;
    }

    case WM_DESTROY:
        StopGifAnimation(hwnd);
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

    HWND hwnd = CreateWindowExW(0, L"CatAppClass", L"",
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