#include <iostream>
#include <vector>
#include <string>
#include <windows.h>
#include <algorithm>
#include <memory>
#include <string>

#include <allheaders.h>
#include <tesseract/baseapi.h>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>

#define IDC_WINDOW_LIST 101
#define IDC_START_BUTTON 102
#define IDC_STOP_BUTTON 103
#define TIMER_ID 1

std::vector<std::string> windowTitles;
tesseract::TessBaseAPI tess;

bool isRunning = false;
std::string currentWindow;

void updateButtonStates(HWND hStartButton, HWND hStopButton) {
    EnableWindow(hStartButton, !isRunning); // Enable Start if not running
    EnableWindow(hStopButton, isRunning);  // Enable Stop if running
}

BOOL CALLBACK EnumWindowsCallback(HWND hwnd, LPARAM lParam) {
    char title[256];
    if (GetWindowTextA(hwnd, title, sizeof(title)) && IsWindowVisible(hwnd) && strlen(title) > 0) {
        windowTitles.push_back(title);
    }
    return TRUE;
}

void enumerateWindows(HWND hComboBox) {
    windowTitles.clear();
    SendMessage(hComboBox, CB_RESETCONTENT, 0, 0);
    EnumWindows(EnumWindowsCallback, 0);

    for (const auto& title : windowTitles) {
        SendMessageA(hComboBox, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(title.c_str()));
    }
    SendMessage(hComboBox, CB_SETCURSEL, 0, 0);
}

cv::Mat captureWindow(const std::string& windowName) {
    HWND hwnd = FindWindowA(NULL, windowName.c_str());
    if (!hwnd) {
        std::cerr << "Error: Window not found!" << std::endl;
        return cv::Mat();
    }

    RECT rect;
    GetWindowRect(hwnd, &rect);

    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;

    HDC hdcScreen = GetDC(NULL);
    HDC hdcMemDC = CreateCompatibleDC(hdcScreen);
    HBITMAP hBitmap = CreateCompatibleBitmap(hdcScreen, width, height);

    SelectObject(hdcMemDC, hBitmap);
    BitBlt(hdcMemDC, 0, 0, width, height, hdcScreen, rect.left, rect.top, SRCCOPY);

    BITMAP bmp;
    GetObject(hBitmap, sizeof(BITMAP), &bmp);

    cv::Mat mat(bmp.bmHeight, bmp.bmWidth, CV_8UC4);
    GetBitmapBits(hBitmap, bmp.bmHeight * bmp.bmWidth * 4, mat.data);

    DeleteObject(hBitmap);
    DeleteDC(hdcMemDC);
    ReleaseDC(NULL, hdcScreen);

    cv::cvtColor(mat, mat, cv::COLOR_BGRA2BGR);
    return mat;
}

cv::Mat preprocessImage(const cv::Mat& input) {
    cv::Mat gray, adjusted;

    cv::cvtColor(input, gray, cv::COLOR_BGR2GRAY);
    double alpha = 1.95; // Contrast multiplier
    int beta = -200;     // Brightness offset
    gray.convertTo(adjusted, -1, alpha, beta);

    return adjusted;
}

std::vector<std::string> performOCR(const cv::Mat& image) {
    std::vector<std::string> lines;

    PIX* pixs = pixCreate(image.cols, image.rows, 8);
    for (int y = 0; y < image.rows; ++y) {
        const uint8_t* row = image.ptr<uint8_t>(y);
        for (int x = 0; x < image.cols; ++x) {
            pixSetPixel(pixs, x, y, row[x]);
        }
    }

    tess.SetImage(pixs);
    if (tess.Recognize(0) != 0) {
        std::cerr << "Tesseract failed to recognize text." << std::endl;
        pixDestroy(&pixs);
        return lines;
    }

    tesseract::ResultIterator* ri = tess.GetIterator();
    tesseract::PageIteratorLevel level = tesseract::RIL_TEXTLINE;

    if (ri != nullptr) {
        do {
            const char* text = ri->GetUTF8Text(level);
            if (text != nullptr) {
                lines.push_back(text);
                delete[] text;
            }
        } while (ri->Next(level));
    }

    tess.Clear();
    pixDestroy(&pixs);

    return lines;
}


void performOCRAndDisplay(const std::string& windowName) {
    cv::Mat screenshot = captureWindow(windowName);
    if (!screenshot.empty()) {
        cv::Mat preprocessedImage = preprocessImage(screenshot);

        std::vector<std::string> ocrResult = performOCR(preprocessedImage);

        std::cout << "OCR Results:" << std::endl;
        for (const auto& line : ocrResult) {
            std::cout << line << std::endl;
        }
    } else {
        std::cerr << "Failed to capture the window." << std::endl;
    }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    static HWND hComboBox, hStartButton, hStopButton;

    switch (uMsg) {
    case WM_CREATE:
        hComboBox = CreateWindowA("COMBOBOX", NULL,
                                  WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                                  10, 10, 400, 200, hwnd, (HMENU)IDC_WINDOW_LIST, NULL, NULL);
        hStartButton = CreateWindowA("BUTTON", "Start",
                                     WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                                     420, 10, 100, 30, hwnd, (HMENU)IDC_START_BUTTON, NULL, NULL);
        hStopButton = CreateWindowA("BUTTON", "Stop",
                                    WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                                    420, 50, 100, 30, hwnd, (HMENU)IDC_STOP_BUTTON, NULL, NULL);
        enumerateWindows(hComboBox);
        updateButtonStates(hStartButton, hStopButton);
        break;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_START_BUTTON) {
            int index = SendMessage(hComboBox, CB_GETCURSEL, 0, 0);
            if (index != CB_ERR) {
                char windowName[256];
                SendMessageA(hComboBox, CB_GETLBTEXT, index, reinterpret_cast<LPARAM>(windowName));
                currentWindow = windowName;
                isRunning = true;
                SetTimer(hwnd, TIMER_ID, 500, NULL); // Start timer
                updateButtonStates(hStartButton, hStopButton);
            }
        } else if (LOWORD(wParam) == IDC_STOP_BUTTON) {
            KillTimer(hwnd, TIMER_ID);
            isRunning = false;
            updateButtonStates(hStartButton, hStopButton);
        }
        break;

    case WM_TIMER:
        if (wParam == TIMER_ID && isRunning) {
            performOCRAndDisplay(currentWindow);
        }
        break;

    case WM_DESTROY:
        KillTimer(hwnd, TIMER_ID);
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

int main() {
    if (tess.Init("./tessdata", "eng")) {
        std::cerr << "OCRTesseract: Could not initialize tesseract." << std::endl;
        return 1;
    }

    tess.SetPageSegMode(tesseract::PageSegMode::PSM_AUTO);

    WNDCLASS wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = "WindowCaptureApp";

    RegisterClass(&wc);

    HWND hwnd = CreateWindowEx(0, "WindowCaptureApp", "OCR Tool with Timer",
                               WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 550, 150,
                               NULL, NULL, GetModuleHandle(NULL), NULL);

    if (!hwnd) {
        std::cerr << "Error: Failed to create the window." << std::endl;
        return 1;
    }

    ShowWindow(hwnd, SW_SHOW);

    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}
