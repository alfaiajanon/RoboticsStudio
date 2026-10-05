#pragma once

#include <QImage>
#include <algorithm>
#include <mutex>
#include "Document/Components/ComponentInstance.h"


/*
 * Telemetry storage for an image signal: only the newest frame is kept.
 * Pushing/snapshotting copies an ImageIOValue (a few ints and a shared,
 * immutable pixel buffer), so the physics thread never copies pixels here;
 * conversion to a QImage happens on the UI side (toQImage).
 */
class ImageBuffer {
    mutable std::mutex bufferMutex;
    ImageIOValue latest;

    public:
        void push(const ImageIOValue& frame) {
            std::lock_guard<std::mutex> lock(bufferMutex);
            latest = frame;
        }

        // A null-pixels value until the first frame.
        ImageIOValue snapshot() const {
            std::lock_guard<std::mutex> lock(bufferMutex);
            return latest;
        }

        // RGB888 as is; depth as grayscale, near = bright, no return = black,
        // scaled to the frame's farthest point. Null QImage without pixels.
        static QImage toQImage(const ImageIOValue& v) {
            if (!v.pixels || v.width <= 0 || v.height <= 0) return QImage();

            if (v.format == ImageFormat::RGB888) {
                if (v.pixels->size() < qsizetype(v.width) * v.height * 3) return QImage();
                // Wraps the shared buffer, then copy() detaches from it.
                return QImage(reinterpret_cast<const uchar*>(v.pixels->constData()),
                              v.width, v.height, v.width * 3, QImage::Format_RGB888).copy();
            }

            const qsizetype n = qsizetype(v.width) * v.height;
            if (v.pixels->size() < n * qsizetype(sizeof(float))) return QImage();
            const float* src = reinterpret_cast<const float*>(v.pixels->constData());
            float range = 0.0f;
            for (qsizetype i = 0; i < n; ++i) range = std::max(range, src[i]);

            QImage img(v.width, v.height, QImage::Format_Grayscale8);
            for (int y = 0; y < v.height; ++y) {
                uchar* row = img.scanLine(y);
                for (int x = 0; x < v.width; ++x) {
                    float d = src[y * v.width + x];
                    row[x] = (d <= 0.0f || range <= 0.0f) ? 0 : uchar(std::clamp(255.0f * (1.0f - d / range) + 0.5f, 0.0f, 255.0f));
                }
            }
            return img;
        }
};
