#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// Small seams for the OTA transaction. The production adapters live with the
// BLE integration, while the transaction itself is host-testable without
// Arduino, WiFi, or flash hardware.
class IOtaHttpTransport {
public:
    virtual ~IOtaHttpTransport() = default;

    virtual bool resolveRedirect(const std::string& url, std::string& resolvedUrl) = 0;
    virtual bool fetchSignature(const std::string& url, uint8_t* signatureOut, size_t signatureLen) = 0;
    virtual bool beginImage(const std::string& url, int64_t& contentLength) = 0;
    // Returns the number of bytes read, 0 when no bytes are currently
    // available, or a negative value when the stream failed.
    virtual int readImage(uint8_t* buffer, size_t maxLen) = 0;
    virtual void endImage() = 0;
};

class IOtaUpdateSink {
public:
    virtual ~IOtaUpdateSink() = default;

    virtual bool begin(size_t contentLength) = 0;
    virtual size_t write(uint8_t* data, size_t length) = 0;
    virtual bool end() = 0;
    virtual void abort() = 0;
    virtual const char* errorString() const = 0;
};

class IOtaSha256 {
public:
    virtual ~IOtaSha256() = default;

    virtual bool begin() = 0;
    virtual bool update(const uint8_t* data, size_t length) = 0;
    virtual bool finish(uint8_t* digestOut, size_t digestLen) = 0;
};

class IOtaSignatureVerifier {
public:
    virtual ~IOtaSignatureVerifier() = default;

    virtual bool verify(const uint8_t* digest, const uint8_t* signature, size_t signatureLen) = 0;
};

class IOtaProgressReporter {
public:
    virtual ~IOtaProgressReporter() = default;

    virtual void showProgress(uint8_t percent) = 0;
};

class IOtaRestart {
public:
    virtual ~IOtaRestart() = default;

    virtual void restart() = 0;
};

class OtaUpdater {
public:
    enum class Result : uint8_t {
        Success,
        ImageRedirectFailed,
        SignatureRedirectFailed,
        SignatureFetchFailed,
        ImageFetchFailed,
        InvalidContentLength,
        UpdateBeginFailed,
        HashBeginFailed,
        ImageReadFailed,
        ImageWriteFailed,
        HashUpdateFailed,
        ImageIncomplete,
        HashFinishFailed,
        SignatureInvalid,
        UpdateEndFailed,
    };

    static constexpr size_t kImageChunkSize = 512;
    static constexpr size_t kDefaultMaxStalls = 300;

    OtaUpdater(IOtaHttpTransport& http,
               IOtaUpdateSink& update,
               IOtaSha256& sha256,
               IOtaSignatureVerifier& verifier,
               IOtaProgressReporter& progress,
               IOtaRestart& restart,
               size_t maxStalls = kDefaultMaxStalls);

    // Executes the complete fail-closed transaction. restart() is called once
    // before this method returns, for both success and every terminal failure.
    Result perform(const std::string& imageUrl);

    // The update sink's diagnostic captured on begin/write/end failure, if any.
    const char* updateError() const;

private:
    IOtaHttpTransport& _http;
    IOtaUpdateSink& _update;
    IOtaSha256& _sha256;
    IOtaSignatureVerifier& _verifier;
    IOtaProgressReporter& _progress;
    IOtaRestart& _restart;
    size_t _maxStalls;
    const char* _updateError = nullptr;

    Result _finish(Result result);
    void _captureUpdateError();
};
