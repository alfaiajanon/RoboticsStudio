#pragma once

#include <QImage>
#include <atomic>

class ImageBuffer {
    QImage front, back;
    public:
        void push(const QImage& frame);
        QImage snapshot() const;
};
