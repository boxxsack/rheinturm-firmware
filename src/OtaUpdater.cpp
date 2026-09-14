#include "OtaUpdater.h"

#include "OtaImageVerifier.h"

#include <algorithm>

namespace {
constexpr size_t kSignatureSize = OtaImageVerifier::kRsa2048SignatureSize;
constexpr size_t kDigestSize = OtaImageVerifier::kSha256DigestSize;
}

OtaUpdater::OtaUpdater(IOtaHttpTransport& http,
                       IOtaUpdateSink& update,
                       IOtaSha256& sha256,
                       IOtaSignatureVerifier& verifier,
                       IOtaProgressReporter& progress,
                       IOtaRestart& restart,
                       size_t maxStalls)
    : _http(http)
    , _update(update)
    , _sha256(sha256)
    , _verifier(verifier)
    , _progress(progress)
    , _restart(restart)
    , _maxStalls(maxStalls)
{
}

OtaUpdater::Result OtaUpdater::perform(const std::string& imageUrl) {
    _updateError = nullptr;
    _progress.showProgress(0);

    std::string resolvedImageUrl;
    if (!_http.resolveRedirect(imageUrl, resolvedImageUrl)) {
        return _finish(Result::ImageRedirectFailed);
    }

    uint8_t signature[kSignatureSize];
    std::string signatureUrl;
    if (!_http.resolveRedirect(imageUrl + ".sig", signatureUrl)) {
        return _finish(Result::SignatureRedirectFailed);
    }
    if (!_http.fetchSignature(signatureUrl, signature, sizeof(signature))) {
        return _finish(Result::SignatureFetchFailed);
    }

    int64_t declaredContentLength = 0;
    if (!_http.beginImage(resolvedImageUrl, declaredContentLength)) {
        return _finish(Result::ImageFetchFailed);
    }

    if (declaredContentLength <= 0 ||
        static_cast<uint64_t>(declaredContentLength) > static_cast<uint64_t>(SIZE_MAX)) {
        _http.endImage();
        _update.abort();
        return _finish(Result::InvalidContentLength);
    }
    size_t contentLength = static_cast<size_t>(declaredContentLength);

    if (!_update.begin(contentLength)) {
        _captureUpdateError();
        _http.endImage();
        _update.abort();
        return _finish(Result::UpdateBeginFailed);
    }

    if (!_sha256.begin()) {
        _http.endImage();
        _update.abort();
        return _finish(Result::HashBeginFailed);
    }

    uint8_t buffer[kImageChunkSize];
    size_t remaining = contentLength;
    size_t stallCount = 0;
    uint8_t lastPercent = 0;
    while (remaining > 0 && stallCount < _maxStalls) {
        int read = _http.readImage(buffer, std::min(remaining, sizeof(buffer)));
        if (read < 0) {
            _http.endImage();
            _update.abort();
            return _finish(Result::ImageReadFailed);
        }
        if (read == 0) {
            ++stallCount;
            continue;
        }
        if (static_cast<size_t>(read) > std::min(remaining, sizeof(buffer))) {
            _http.endImage();
            _update.abort();
            return _finish(Result::ImageReadFailed);
        }
        stallCount = 0;

        size_t written = _update.write(buffer, static_cast<size_t>(read));
        if (written != static_cast<size_t>(read)) {
            _captureUpdateError();
            _http.endImage();
            _update.abort();
            return _finish(Result::ImageWriteFailed);
        }
        if (!_sha256.update(buffer, static_cast<size_t>(read))) {
            _http.endImage();
            _update.abort();
            return _finish(Result::HashUpdateFailed);
        }

        remaining -= static_cast<size_t>(read);
        uint8_t percent = static_cast<uint8_t>(
            (static_cast<uint64_t>(contentLength - remaining) * 100U) /
            static_cast<uint64_t>(contentLength));
        if (percent != lastPercent) {
            lastPercent = percent;
            _progress.showProgress(percent);
        }
    }
    _http.endImage();

    if (remaining != 0) {
        _update.abort();
        return _finish(Result::ImageIncomplete);
    }

    uint8_t digest[kDigestSize];
    if (!_sha256.finish(digest, sizeof(digest))) {
        _update.abort();
        return _finish(Result::HashFinishFailed);
    }

    if (!_verifier.verify(digest, signature, sizeof(signature))) {
        _update.abort();
        return _finish(Result::SignatureInvalid);
    }

    if (!_update.end()) {
        _captureUpdateError();
        _update.abort();
        return _finish(Result::UpdateEndFailed);
    }

    return _finish(Result::Success);
}

const char* OtaUpdater::updateError() const {
    return _updateError ? _updateError : "";
}

OtaUpdater::Result OtaUpdater::_finish(Result result) {
    _restart.restart();
    return result;
}

void OtaUpdater::_captureUpdateError() {
    _updateError = _update.errorString();
}
