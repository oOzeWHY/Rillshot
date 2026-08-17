#pragma once

#include "core/Image.h"

#include <algorithm>

using rillshot::core::Image;

inline Image makeDocument(int width, int height) {
    Image image(width, height);
    image.fillTestPattern();
    return image;
}

inline Image makeSolid(int width, int height, unsigned char value) {
    Image image(width, height);
    for (int y = 0; y < image.height(); ++y) {
        auto* row = image.row(y);
        for (int x = 0; x < image.width(); ++x) {
            row[x * 4 + 0] = value;
            row[x * 4 + 1] = value;
            row[x * 4 + 2] = value;
            row[x * 4 + 3] = 255;
        }
    }
    return image;
}

inline Image makeRepeatingRows(int width, int height, int period) {
    Image image(width, height);
    for (int y = 0; y < image.height(); ++y) {
        auto* row = image.row(y);
        const auto base = static_cast<unsigned char>((y % period) * 11 + 17);
        for (int x = 0; x < image.width(); ++x) {
            row[x * 4 + 0] = static_cast<unsigned char>(base + (x % 5));
            row[x * 4 + 1] = static_cast<unsigned char>(base + (x % 7));
            row[x * 4 + 2] = static_cast<unsigned char>(base + (x % 11));
            row[x * 4 + 3] = 255;
        }
    }
    return image;
}

inline Image cropDocumentWindow(const Image& document, int top, int height) {
    Image output(document.width(), height);
    for (int y = 0; y < height; ++y) {
        const auto* source = document.row(top + y);
        std::copy(source, source + document.stride(), output.row(y));
    }
    return output;
}

inline Image cropWithStickyHeader(
    const Image& document,
    int contentTop,
    int frameHeight,
    int stickyHeight) {
    Image output(document.width(), frameHeight);
    for (int y = 0; y < frameHeight; ++y) {
        auto* destination = output.row(y);
        if (y < stickyHeight) {
            for (int x = 0; x < output.width(); ++x) {
                destination[x * 4 + 0] = 32;
                destination[x * 4 + 1] = 32;
                destination[x * 4 + 2] = 32;
                destination[x * 4 + 3] = 255;
            }
        } else {
            const auto* source = document.row(contentTop + y - stickyHeight);
            std::copy(source, source + document.stride(), destination);
        }
    }
    return output;
}

inline Image cropWithStickyFooter(
    const Image& document,
    int contentTop,
    int frameHeight,
    int stickyHeight) {
    Image output(document.width(), frameHeight);
    const int contentHeight = frameHeight - stickyHeight;
    for (int y = 0; y < frameHeight; ++y) {
        auto* destination = output.row(y);
        if (y >= contentHeight) {
            for (int x = 0; x < output.width(); ++x) {
                destination[x * 4 + 0] = 48;
                destination[x * 4 + 1] = 48;
                destination[x * 4 + 2] = 48;
                destination[x * 4 + 3] = 255;
            }
        } else {
            const auto* source = document.row(contentTop + y);
            std::copy(source, source + document.stride(), destination);
        }
    }
    return output;
}

inline Image cropWithStickyHeaderAndFooter(
    const Image& document,
    int contentTop,
    int frameHeight,
    int headerHeight,
    int footerHeight) {
    Image output(document.width(), frameHeight);
    const int contentEndY = frameHeight - footerHeight;
    for (int y = 0; y < frameHeight; ++y) {
        auto* destination = output.row(y);
        if (y < headerHeight) {
            std::fill(destination, destination + output.stride(),
                      static_cast<unsigned char>(24));
            for (int x = 0; x < output.width(); ++x) {
                destination[x * 4 + 3] = 255;
            }
        } else if (y >= contentEndY) {
            std::fill(destination, destination + output.stride(),
                      static_cast<unsigned char>(56));
            for (int x = 0; x < output.width(); ++x) {
                destination[x * 4 + 3] = 255;
            }
        } else {
            const auto* source = document.row(contentTop + y - headerHeight);
            std::copy(source, source + document.stride(), destination);
        }
    }
    return output;
}
