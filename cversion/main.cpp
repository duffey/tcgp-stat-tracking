#include <iostream>
#include <vector>
#include <string>
#include <windows.h>
#include <algorithm>
#include <memory>
#include <string>
#include <map>
#include <fstream>

#include <allheaders.h>
#include <tesseract/baseapi.h>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>

#define IDC_WINDOW_LIST 101
#define IDC_START_BUTTON 102
#define TIMER_ID 1

std::vector<std::string> windowTitles;
tesseract::TessBaseAPI tess;

bool isRunning = false;
std::string currentWindow;

std::string currentDeck;
std::map<std::string, std::pair<int, int>> data; // Deck -> {victories, defeats}

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
                std::string line = text;
                delete[] text;

                size_t start = line.find_first_not_of(" \t\n\r");
                size_t end = line.find_last_not_of(" \t\n\r");

                if (start != std::string::npos && end != std::string::npos) {
                    line = line.substr(start, end - start + 1);
                } else {
                    line.clear();
                }

                if (!line.empty()) {
                    lines.push_back(line);
                }
            }
        } while (ri->Next(level));
    }

    tess.Clear();
    pixDestroy(&pixs);

    return lines;
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

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    static HWND hComboBox, hStartButton;

    switch (uMsg) {
    case WM_CREATE:
        hComboBox = CreateWindowA("COMBOBOX", NULL,
                                  WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                                  10, 10, 400, 200, hwnd, (HMENU)IDC_WINDOW_LIST, NULL, NULL);
        hStartButton = CreateWindowA("BUTTON", "Start",
                                     WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                                     420, 10, 100, 30, hwnd, (HMENU)IDC_START_BUTTON, NULL, NULL);
        enumerateWindows(hComboBox);
        break;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_START_BUTTON) {
            int index = SendMessage(hComboBox, CB_GETCURSEL, 0, 0);
            if (index != CB_ERR) {
                char windowName[256];
                SendMessageA(hComboBox, CB_GETLBTEXT, index, reinterpret_cast<LPARAM>(windowName));
                currentWindow = windowName;
                isRunning = true;
                DestroyWindow(hwnd); // Close the window
            }
        }
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

bool find(const std::vector<std::string>& lines, const std::string& keyword) {
    return std::any_of(lines.begin(), lines.end(), [&keyword](const std::string& line) { return line.find(keyword) != std::string::npos; });
}

std::string findDeck(const std::vector<std::string>& lines) {
    for (size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].find("Trading Card Game Player") != std::string::npos) {
            if (i < lines.size() - 1) {
                return lines[i + 1]; // Return next line
            }
        }
    }
    return "";
}

void writeDataToCSV() {
    std::ofstream file("deck_stats.csv");
    if (file.is_open()) {
        for (const auto& entry : data) {
            const std::string& deck = entry.first;
            int wins = entry.second.first;
            int losses = entry.second.second;

            // Write only if there are non-zero stats
            if (wins > 0 || losses > 0) {
                file << deck << "," << wins << "," << losses << "\n";
            }
        }
        file.close();
    } else {
        std::cerr << "Error: Could not open file deck_stats.csv for writing." << std::endl;
    }
}

int main() {
    if (tess.Init("./tessdata", "eng")) {
        std::cerr << "OCRTesseract: Could not initialize tesseract." << std::endl;
        return 1;
    }

    tess.SetPageSegMode(tesseract::PageSegMode::PSM_AUTO);
    tess.SetVariable("user_defined_dpi", "72");

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

    while (isRunning) {
        cv::Mat screenshot = captureWindow(currentWindow);
        if (!screenshot.empty()) {
            cv::Mat preprocessedImage = preprocessImage(screenshot);
            auto lines = performOCR(preprocessedImage);

            for (const auto& line : lines) {
                std::cout << line << std::endl;
            }

            if (!lines.empty()) {
                if (find(lines, "Random Match")) {
                    std::string updatedDeck = findDeck(lines);
                    if (!updatedDeck.empty() && updatedDeck != currentDeck) {
                        currentDeck = updatedDeck;
                        if (data.find(currentDeck) == data.end()) {
                            data[currentDeck] = {0, 0};
                        }
                    }
                }

                if (!currentDeck.empty()) {
                    if (find(lines, "Victory")) {
                        data[currentDeck].first++;
                        currentDeck.clear();
                        writeDataToCSV();
                    }

                    if (find(lines, "Defeat")) {
                        data[currentDeck].second++;
                        currentDeck.clear();
                        writeDataToCSV();
                    }
                }
            }
        } else {
            std::cerr << "Failed to capture the window." << std::endl;
        }
        Sleep(500); // Simulate the timer interval
    }

    return 0;
}
