#include <iostream>
#include <vector>
#include <algorithm>
#include <memory>
#include <string>

#include <allheaders.h> // Leptonica main header for image I/O
#include <tesseract/baseapi.h> // Tesseract main header

#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>

struct TextLine {
    std::string text;
    int y; // y-coordinate of the baseline
};

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

int main(int argc, char *argv[]) {
    if (argc == 1)
        return 1;

    tesseract::TessBaseAPI tess;

    if (tess.Init("./tessdata", "eng")) {
        std::cout << "OCRTesseract: Could not initialize tesseract." << std::endl;
        return 1;
    }

    // Setup
    tess.SetPageSegMode(tesseract::PageSegMode::PSM_AUTO);

    // Read image using OpenCV
    cv::Mat inputImage = cv::imread(argv[1]);
    if (inputImage.empty()) {
        std::cout << "Cannot open input file: " << argv[1] << std::endl;
        return 1;
    }

    // Preprocess the image
    cv::Mat preprocessedImage = preprocessImage(inputImage);

    // Save the preprocessed image (optional, for debugging)
    cv::imwrite("preprocessed.png", preprocessedImage);

    // Convert the OpenCV Mat to Leptonica PIX
    PIX* pixs = pixCreate(preprocessedImage.cols, preprocessedImage.rows, 8);
    for (int y = 0; y < preprocessedImage.rows; ++y) {
        uint8_t* row = preprocessedImage.ptr<uint8_t>(y);
        for (int x = 0; x < preprocessedImage.cols; ++x) {
            pixSetPixel(pixs, x, y, row[x]);
        }
    }

    // Recognize
    tess.SetImage(pixs);
    if (tess.Recognize(0) != 0) {
        std::cout << "Tesseract failed to recognize text." << std::endl;
        pixDestroy(&pixs);
        return 1;
    }

    // Extract lines and bounding boxes
    std::vector<TextLine> lines;
    tesseract::ResultIterator* ri = tess.GetIterator();
    tesseract::PageIteratorLevel level = tesseract::RIL_TEXTLINE;

    if (ri != nullptr) {
        do {
            const char* text = ri->GetUTF8Text(level);
            if (text != nullptr) {
                int x1, y1, x2, y2;
                ri->BoundingBox(level, &x1, &y1, &x2, &y2);
                lines.push_back({text, y1}); // Use y1 as the sorting key
                delete[] text;
            }
        } while (ri->Next(level));
    }

    // Sort lines by y-coordinate (ascending)
    std::sort(lines.begin(), lines.end(), [](const TextLine& a, const TextLine& b) {
        return a.y < b.y;
    });

    // Output sorted lines
    for (const auto& line : lines) {
        std::cout << line.text << std::endl;
    }

    // Cleanup
    tess.Clear();
    pixDestroy(&pixs);

    return 0;
}
