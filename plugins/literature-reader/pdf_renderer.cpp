#include "pdf_renderer.hpp"
#include <QDir>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Data.Pdf.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Streams.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace literature {
namespace {
bool fail(QString* error, const QString& message)
{
    if (error)
        *error = message;
    return false;
}

bool stopped(const std::atomic_bool* cancelled)
{
    return cancelled && cancelled->load();
}

struct Apartment {
    Apartment() { winrt::init_apartment(winrt::apartment_type::multi_threaded); }
    ~Apartment() { winrt::uninit_apartment(); }
};

template<class T> struct CloseWinrt {
    T object;
    ~CloseWinrt() noexcept { try { object.Close(); } catch (...) {} }
};
} // namespace

bool renderPdfPage(const QString& root, const Document& document, int requestedPage, double zoom,
                   const std::atomic_bool* cancelled, RenderedPage* output, QString* error)
{
    if (!output || requestedPage < 0 || !std::isfinite(zoom) || zoom < 0.25 || zoom > 3.0)
        return fail(error, QStringLiteral("Invalid PDF page or zoom."));
    if (stopped(cancelled))
        return fail(error, QStringLiteral("Cancelled."));
    QString verificationError;
    const auto path = ReaderLibrary::verifiedFilePathFor(root, document, cancelled, &verificationError);
    if (path.isEmpty())
        return fail(error, verificationError);
    try {
        Apartment apartment;
        auto file = winrt::Windows::Storage::StorageFile::GetFileFromPathAsync(QDir::toNativeSeparators(path).toStdWString()).get();
        auto pdf = winrt::Windows::Data::Pdf::PdfDocument::LoadFromFileAsync(file).get();
        if (!pdf.PageCount() || pdf.PageCount() > 100000)
            return fail(error, QStringLiteral("PDF page count is empty or exceeds the supported limit."));
        const int pageCount = static_cast<int>(pdf.PageCount());
        const int pageIndex = std::min(requestedPage, pageCount - 1);
        if (stopped(cancelled))
            return fail(error, QStringLiteral("Cancelled."));
        auto page = pdf.GetPage(static_cast<uint32_t>(pageIndex));
        CloseWinrt<winrt::Windows::Data::Pdf::PdfPage> closePage{page};
        const auto size = page.Size();
        if (!(size.Width > 0 && size.Height > 0) || !std::isfinite(size.Width) || !std::isfinite(size.Height))
            return fail(error, QStringLiteral("PDF page has invalid dimensions."));
        // Bound the longest side and total decoded pixels before allocating an image.
        const double longest = std::max(size.Width, size.Height);
        const double factor = std::min(zoom * 1.5, 4096.0 / longest);
        const auto width = static_cast<uint32_t>(std::max(1.0, std::floor(size.Width * factor)));
        const auto height = static_cast<uint32_t>(std::max(1.0, std::floor(size.Height * factor)));
        if (static_cast<uint64_t>(width) * height > 16777216)
            return fail(error, QStringLiteral("Rendered PDF page exceeds the pixel budget."));
        winrt::Windows::Storage::Streams::InMemoryRandomAccessStream stream;
        CloseWinrt<winrt::Windows::Storage::Streams::InMemoryRandomAccessStream> closeStream{stream};
        winrt::Windows::Data::Pdf::PdfPageRenderOptions options;
        options.DestinationWidth(width);
        options.DestinationHeight(height);
        page.RenderToStreamAsync(stream, options).get();
        if (stopped(cancelled))
            return fail(error, QStringLiteral("Cancelled."));
        const auto length = stream.Size();
        if (!length || length > 64 * 1024 * 1024 || length > std::numeric_limits<uint32_t>::max())
            return fail(error, QStringLiteral("Rendered PDF stream exceeds the byte budget."));
        winrt::Windows::Storage::Streams::DataReader reader(stream.GetInputStreamAt(0));
        CloseWinrt<winrt::Windows::Storage::Streams::DataReader> closeReader{reader};
        reader.LoadAsync(static_cast<uint32_t>(length)).get();
        std::vector<uint8_t> bytes(static_cast<size_t>(length));
        reader.ReadBytes(bytes);
        auto image = QImage::fromData(bytes.data(), static_cast<int>(bytes.size()), "PNG");
        if (image.isNull() || image.width() != static_cast<int>(width) || image.height() != static_cast<int>(height))
            return fail(error, QStringLiteral("Windows PDF renderer returned an invalid page image."));
        *output = {std::move(image), pageCount, pageIndex};
        return true;
    } catch (const winrt::hresult_error& failure) {
        return fail(error, QStringLiteral("PDF error 0x%1: %2")
                               .arg(static_cast<quint32>(failure.code().value), 8, 16, QLatin1Char('0'))
                               .arg(QString::fromWCharArray(failure.message().c_str())));
    } catch (const std::exception& failure) {
        return fail(error, QString::fromUtf8(failure.what()));
    }
}
} // namespace literature
