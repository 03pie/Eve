#pragma once

#include "reader_library.hpp"
#include <QImage>
#include <atomic>

namespace literature {

struct RenderedPage {
    QImage image;
    int pageCount = 0;
    int page = 0;
};

// Called only in the owned MTA worker. No GUI or library instance is borrowed.
bool renderPdfPage(const QString& root, const Document& document, int page, double zoom,
                   const std::atomic_bool* cancelled, RenderedPage* output, QString* error);

} // namespace literature
