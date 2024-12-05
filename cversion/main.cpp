#include <iostream>
#include <vector>
#include <string>
#include <windows.h>
#include <algorithm>
#include <memory>
#include <string>

#include <allheaders.h> // Leptonica main header for image I/O
#include <tesseract/baseapi.h> // Tesseract main header

#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>

#define IDC_WINDOW_LIST 101
#define IDC_CAPTURE_BUTTON 102

std::vector<std::string> windowTitles;
tesseract::TessBaseAPI tess;

struct TextLine {
    std::string text;
    int y; // y-coordinate of the baseline
};

// Callback to collect window titles
BOOL CALLBACK EnumWindowsCallback(HWND hwnd, LPARAM lParam) {
    char title[256];
    if (GetWindowTextA(hwnd, title, sizeof(title)) && IsWindowVisible(hwnd) && strlen(title) > 0) {
        windowTitles.push_back(title);
    }
    return TRUE;
}

// Enumerate windows and populate dropdown
void enumerateWindows(HWND hComboBox) {
    windowTitles.clear();
    SendMessage(hComboBox, CB_RESETCONTENT, 0, 0); // Clear the dropdown
    EnumWindows(EnumWindowsCallback, 0);

    for (const auto& title : windowTitles) {
        SendMessageA(hComboBox, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(title.c_str()));
    }
    SendMessage(hComboBox, CB_SETCURSEL, 0, 0); // Set default selection
}

// Capture a screenshot of the selected window
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

    // Convert to grayscale
    cv::cvtColor(input, gray, cv::COLOR_BGR2GRAY);

    // Adjust brightness and contrast (contrast ≈ 2.06, brightness ≈ -89)
    double alpha = 1.95; // Contrast multiplier
    int beta = -200;      // Brightness offset
    gray.convertTo(adjusted, -1, alpha, beta);

    return adjusted;
}

std::vector<std::string> performOCR(const cv::Mat& image) {
    std::vector<std::string> lines;

    // Convert the OpenCV Mat to Leptonica PIX
    PIX* pixs = pixCreate(image.cols, image.rows, 8);
    for (int y = 0; y < image.rows; ++y) {
        const uint8_t* row = image.ptr<uint8_t>(y);
        for (int x = 0; x < image.cols; ++x) {
            pixSetPixel(pixs, x, y, row[x]);
        }
    }

    // Recognize
    tess.SetImage(pixs);
    if (tess.Recognize(0) != 0) {
        std::cout << "Tesseract failed to recognize text." << std::endl;
        pixDestroy(&pixs);
        return lines;
    }

    // Extract lines and bounding boxes
    std::vector<TextLine> textLines;
    tesseract::ResultIterator* ri = tess.GetIterator();
    tesseract::PageIteratorLevel level = tesseract::RIL_TEXTLINE;

    if (ri != nullptr) {
        do {
            const char* text = ri->GetUTF8Text(level);
            if (text != nullptr) {
                int x1, y1, x2, y2;
                ri->BoundingBox(level, &x1, &y1, &x2, &y2);
                textLines.push_back({text, y1}); // Use y1 as the sorting key
                delete[] text;
            }
        } while (ri->Next(level));
    }

    // Sort lines by y-coordinate (ascending)
    std::sort(textLines.begin(), textLines.end(), [](const TextLine& a, const TextLine& b) {
        return a.y < b.y;
    });

    // Output sorted lines
    for (const auto& textLine : textLines) {
        lines.push_back(textLine.text);
    }

    // Cleanup
    tess.Clear();
    pixDestroy(&pixs);

    return lines;
}

// Window Procedure for handling GUI events
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    static HWND hComboBox;
    static HWND hCaptureButton;

    switch (uMsg) {
    case WM_CREATE:
        // Create dropdown (combo box)
        hComboBox = CreateWindowA("COMBOBOX", NULL,
                                  WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                                  10, 10, 400, 200, hwnd, (HMENU)IDC_WINDOW_LIST, NULL, NULL);
        // Create capture button
        hCaptureButton = CreateWindowA("BUTTON", "Capture",
                                       WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                                       420, 10, 100, 30, hwnd, (HMENU)IDC_CAPTURE_BUTTON, NULL, NULL);
        // Populate dropdown
        enumerateWindows(hComboBox);
        break;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_CAPTURE_BUTTON) {
            // Capture button clicked
            int index = SendMessage(hComboBox, CB_GETCURSEL, 0, 0);
            if (index != CB_ERR) {
                char windowName[256];
                SendMessageA(hComboBox, CB_GETLBTEXT, index, reinterpret_cast<LPARAM>(windowName));

                cv::Mat screenshot = captureWindow(windowName);
                if (!screenshot.empty()) {
                    cv::imwrite("screenshot.png", screenshot);

                    // Preprocess the image
                    cv::Mat preprocessedImage = preprocessImage(screenshot);

                    // Save the preprocessed image (optional, for debugging)
                    cv::imwrite("preprocessed.png", preprocessedImage);

                    std::vector<std::string> ocrResult = performOCR(preprocessedImage);

                    std::cout << "OCR Results:" << std::endl;
                    for (const auto& line : ocrResult) {
                        std::cout << line << std::endl;
                    }
                } else {
                    MessageBox(hwnd, "Failed to capture the window.", "Error", MB_OK | MB_ICONERROR);
                }
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

int main() {
    if (tess.Init("./tessdata", "eng")) {
        std::cout << "OCRTesseract: Could not initialize tesseract." << std::endl;
        return 1;
    }

    // Setup
    tess.SetPageSegMode(tesseract::PageSegMode::PSM_AUTO);

    const char* CLASS_NAME = "WindowCaptureApp";

    WNDCLASS wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = CLASS_NAME;

    RegisterClass(&wc);

    HWND hwnd = CreateWindowEx(0, CLASS_NAME, "Window Capture Tool",
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