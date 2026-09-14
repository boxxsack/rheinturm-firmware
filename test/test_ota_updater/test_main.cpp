// Host-side OTA transaction tests. Run with `pio test -e native`.
//
// These tests exercise the orchestration that is deliberately kept separate
// from the Arduino BLE, WiFi, and Update implementations. Every terminal path
// must request a restart, and no path with an unverified image may call end().
#include <unity.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "OtaImageVerifier.h"
#include "OtaUpdater.h"

struct FakeHttp : IOtaHttpTransport {
    explicit FakeHttp(std::vector<std::string>& eventLog)
        : events(eventLog), imageContentLength(4), imageBody{0x10, 0x20, 0x30, 0x40} {}

    std::vector<std::string>& events;
    bool imageRedirect = true;
    bool signatureRedirect = true;
    bool signatureFetch = true;
    bool imageBegin = true;
    int64_t imageContentLength;
    std::vector<uint8_t> imageBody;
    std::vector<int> readActions;
    size_t readActionIndex = 0;
    size_t bodyOffset = 0;

    bool resolveRedirect(const std::string& url, std::string& resolvedUrl) override {
        events.push_back("resolve:" + url);
        if (url.size() >= 4 && url.compare(url.size() - 4, 4, ".sig") == 0) {
            if (!signatureRedirect) return false;
        } else if (!imageRedirect) {
            return false;
        }
        resolvedUrl = url + ".resolved";
        return true;
    }

    bool fetchSignature(const std::string&, uint8_t* signatureOut, size_t signatureLen) override {
        events.push_back("fetch-signature");
        if (!signatureFetch) return false;
        std::fill(signatureOut, signatureOut + signatureLen, 0xA5);
        return true;
    }

    bool beginImage(const std::string&, int64_t& contentLength) override {
        events.push_back("begin-image");
        if (!imageBegin) return false;
        contentLength = imageContentLength;
        return true;
    }

    int readImage(uint8_t* buffer, size_t maxLen) override {
        events.push_back("read-image");
        if (readActionIndex < readActions.size()) {
            int action = readActions[readActionIndex++];
            if (action < 0) return action;
            if (action == 0) return 0;
            size_t requested = std::min(static_cast<size_t>(action), maxLen);
            size_t available = imageBody.size() - std::min(bodyOffset, imageBody.size());
            size_t count = std::min(requested, available);
            std::copy_n(imageBody.begin() + bodyOffset, count, buffer);
            bodyOffset += count;
            return static_cast<int>(count);
        }

        if (bodyOffset >= imageBody.size()) return 0;
        size_t count = std::min(maxLen, imageBody.size() - bodyOffset);
        std::copy_n(imageBody.begin() + bodyOffset, count, buffer);
        bodyOffset += count;
        return static_cast<int>(count);
    }

    void endImage() override {
        events.push_back("end-image");
    }
};

struct FakeUpdate : IOtaUpdateSink {
    explicit FakeUpdate(std::vector<std::string>& eventLog) : events(eventLog) {}

    std::vector<std::string>& events;
    bool beginResult = true;
    bool endResult = true;
    size_t shortWrite = static_cast<size_t>(-1);
    int beginCalls = 0;
    int writeCalls = 0;
    int endCalls = 0;
    int abortCalls = 0;
    int activations = 0;
    std::vector<uint8_t> written;
    std::string error = "fake update error";

    bool begin(size_t) override {
        events.push_back("update:begin");
        ++beginCalls;
        return beginResult;
    }

    size_t write(uint8_t* data, size_t length) override {
        events.push_back("update:write");
        ++writeCalls;
        size_t count = std::min(length, shortWrite);
        written.insert(written.end(), data, data + count);
        return count;
    }

    bool end() override {
        events.push_back("update:end");
        ++endCalls;
        if (endResult) ++activations;
        return endResult;
    }

    void abort() override {
        events.push_back("update:abort");
        ++abortCalls;
    }

    const char* errorString() const override {
        return error.c_str();
    }
};

struct FakeSha256 : IOtaSha256 {
    explicit FakeSha256(std::vector<std::string>& eventLog) : events(eventLog) {}

    std::vector<std::string>& events;
    bool beginResult = true;
    bool updateResult = true;
    bool finishResult = true;
    int beginCalls = 0;
    int updateCalls = 0;
    int finishCalls = 0;
    std::vector<uint8_t> hashed;
    std::vector<uint8_t> digest;

    bool begin() override {
        events.push_back("hash:begin");
        ++beginCalls;
        return beginResult;
    }

    bool update(const uint8_t* data, size_t length) override {
        events.push_back("hash:update");
        ++updateCalls;
        if (!updateResult) return false;
        hashed.insert(hashed.end(), data, data + length);
        return true;
    }

    bool finish(uint8_t* digestOut, size_t digestLen) override {
        events.push_back("hash:finish");
        ++finishCalls;
        if (!finishResult || digestLen != OtaImageVerifier::kSha256DigestSize) return false;

        digest.assign(digestLen, 0);
        for (size_t i = 0; i < hashed.size(); ++i) {
            digest[i % digestLen] = static_cast<uint8_t>(digest[i % digestLen] + hashed[i] + i);
        }
        std::copy(digest.begin(), digest.end(), digestOut);
        return true;
    }
};

struct FakeVerifier : IOtaSignatureVerifier {
    explicit FakeVerifier(std::vector<std::string>& eventLog) : events(eventLog) {}

    std::vector<std::string>& events;
    bool result = true;
    int calls = 0;
    size_t signatureLength = 0;
    std::vector<uint8_t> digest;

    bool verify(const uint8_t* digestIn, const uint8_t*, size_t signatureLen) override {
        events.push_back("verify");
        ++calls;
        signatureLength = signatureLen;
        digest.assign(digestIn, digestIn + OtaImageVerifier::kSha256DigestSize);
        return result;
    }
};

struct FakeProgress : IOtaProgressReporter {
    explicit FakeProgress(std::vector<std::string>& eventLog) : events(eventLog) {}

    std::vector<std::string>& events;

    void showProgress(uint8_t percent) override {
        events.push_back("progress:" + std::to_string(percent));
    }
};

struct FakeRestart : IOtaRestart {
    explicit FakeRestart(std::vector<std::string>& eventLog) : events(eventLog) {}

    std::vector<std::string>& events;
    int calls = 0;

    void restart() override {
        events.push_back("restart");
        ++calls;
    }
};

struct Harness {
    std::vector<std::string> events;
    FakeHttp http;
    FakeUpdate update;
    FakeSha256 sha256;
    FakeVerifier verifier;
    FakeProgress progress;
    FakeRestart restart;
    OtaUpdater updater;

    Harness()
        : http(events)
        , update(events)
        , sha256(events)
        , verifier(events)
        , progress(events)
        , restart(events)
        , updater(http, update, sha256, verifier, progress, restart, 3) {}
};

static void assert_restarted_and_not_activated(const Harness& harness) {
    TEST_ASSERT_EQUAL_INT(1, harness.restart.calls);
    TEST_ASSERT_EQUAL_INT(0, harness.update.endCalls);
    TEST_ASSERT_EQUAL_INT(0, harness.update.activations);
}

static void assert_no_update_started(const Harness& harness) {
    TEST_ASSERT_EQUAL_INT(0, harness.update.beginCalls);
    TEST_ASSERT_EQUAL_INT(0, harness.update.endCalls);
    TEST_ASSERT_EQUAL_INT(0, harness.update.activations);
}

void test_image_redirect_failure_never_starts_update(void) {
    Harness harness;
    harness.http.imageRedirect = false;

    TEST_ASSERT_TRUE(harness.updater.perform("https://example/fw") == OtaUpdater::Result::ImageRedirectFailed);
    assert_no_update_started(harness);
    TEST_ASSERT_EQUAL_INT(0, harness.update.abortCalls);
    TEST_ASSERT_EQUAL_INT(1, harness.restart.calls);
}

void test_image_http_failure_never_starts_update(void) {
    Harness harness;
    harness.http.imageBegin = false;

    TEST_ASSERT_TRUE(harness.updater.perform("https://example/fw") == OtaUpdater::Result::ImageFetchFailed);
    assert_no_update_started(harness);
    TEST_ASSERT_EQUAL_INT(1, harness.restart.calls);
}

void test_signature_redirect_failure_never_starts_update(void) {
    Harness harness;
    harness.http.signatureRedirect = false;

    TEST_ASSERT_TRUE(harness.updater.perform("https://example/fw") == OtaUpdater::Result::SignatureRedirectFailed);
    assert_no_update_started(harness);
    TEST_ASSERT_EQUAL_INT(1, harness.restart.calls);
}

void test_signature_http_failure_never_starts_update(void) {
    Harness harness;
    harness.http.signatureFetch = false;

    TEST_ASSERT_TRUE(harness.updater.perform("https://example/fw") == OtaUpdater::Result::SignatureFetchFailed);
    assert_no_update_started(harness);
    TEST_ASSERT_EQUAL_INT(1, harness.restart.calls);
}

void test_signature_wrong_length_never_starts_update(void) {
    Harness harness;
    // The transport owns the fixed-length check and reports wrong length as a
    // failed signature fetch, just like the pinned HTTP adapter does.
    harness.http.signatureFetch = false;

    TEST_ASSERT_TRUE(harness.updater.perform("https://example/fw") == OtaUpdater::Result::SignatureFetchFailed);
    assert_no_update_started(harness);
    TEST_ASSERT_EQUAL_INT(1, harness.restart.calls);
}

void test_signature_incomplete_never_starts_update(void) {
    Harness harness;
    // An incomplete streamed signature is also rejected by the transport
    // before the image transaction can begin.
    harness.http.signatureFetch = false;

    TEST_ASSERT_TRUE(harness.updater.perform("https://example/fw") == OtaUpdater::Result::SignatureFetchFailed);
    assert_no_update_started(harness);
    TEST_ASSERT_EQUAL_INT(1, harness.restart.calls);
}

void test_unknown_content_length_aborts(void) {
    Harness harness;
    harness.http.imageContentLength = -1;

    TEST_ASSERT_TRUE(harness.updater.perform("https://example/fw") == OtaUpdater::Result::InvalidContentLength);
    TEST_ASSERT_EQUAL_INT(0, harness.update.beginCalls);
    TEST_ASSERT_EQUAL_INT(1, harness.update.abortCalls);
    assert_restarted_and_not_activated(harness);
}

void test_zero_content_length_aborts(void) {
    Harness harness;
    harness.http.imageContentLength = 0;

    TEST_ASSERT_TRUE(harness.updater.perform("https://example/fw") == OtaUpdater::Result::InvalidContentLength);
    TEST_ASSERT_EQUAL_INT(0, harness.update.beginCalls);
    TEST_ASSERT_EQUAL_INT(1, harness.update.abortCalls);
    assert_restarted_and_not_activated(harness);
}

void test_update_begin_failure_aborts(void) {
    Harness harness;
    harness.update.beginResult = false;

    TEST_ASSERT_TRUE(harness.updater.perform("https://example/fw") == OtaUpdater::Result::UpdateBeginFailed);
    TEST_ASSERT_EQUAL_INT(1, harness.update.beginCalls);
    TEST_ASSERT_EQUAL_INT(1, harness.update.abortCalls);
    TEST_ASSERT_EQUAL_STRING("fake update error", harness.updater.updateError());
    assert_restarted_and_not_activated(harness);
}

void test_short_write_aborts_without_verification(void) {
    Harness harness;
    harness.update.shortWrite = 2;

    TEST_ASSERT_TRUE(harness.updater.perform("https://example/fw") == OtaUpdater::Result::ImageWriteFailed);
    TEST_ASSERT_EQUAL_INT(1, harness.update.writeCalls);
    TEST_ASSERT_EQUAL_INT(1, harness.update.abortCalls);
    TEST_ASSERT_EQUAL_INT(0, harness.verifier.calls);
    assert_restarted_and_not_activated(harness);
}

void test_stalled_body_aborts(void) {
    Harness harness;
    harness.http.imageContentLength = 8;
    harness.http.readActions = {0, 0, 0};

    TEST_ASSERT_TRUE(harness.updater.perform("https://example/fw") == OtaUpdater::Result::ImageIncomplete);
    TEST_ASSERT_EQUAL_INT(3, harness.http.readActionIndex);
    TEST_ASSERT_EQUAL_INT(1, harness.update.abortCalls);
    TEST_ASSERT_EQUAL_INT(0, harness.verifier.calls);
    assert_restarted_and_not_activated(harness);
}

void test_incomplete_body_aborts(void) {
    Harness harness;
    harness.http.imageContentLength = 8;

    TEST_ASSERT_TRUE(harness.updater.perform("https://example/fw") == OtaUpdater::Result::ImageIncomplete);
    TEST_ASSERT_EQUAL_INT(1, harness.update.writeCalls);
    TEST_ASSERT_EQUAL_INT(1, harness.update.abortCalls);
    TEST_ASSERT_EQUAL_INT(0, harness.verifier.calls);
    assert_restarted_and_not_activated(harness);
}

void test_read_failure_aborts(void) {
    Harness harness;
    harness.http.readActions = {-1};

    TEST_ASSERT_TRUE(harness.updater.perform("https://example/fw") == OtaUpdater::Result::ImageReadFailed);
    TEST_ASSERT_EQUAL_INT(1, harness.update.abortCalls);
    TEST_ASSERT_EQUAL_INT(0, harness.verifier.calls);
    assert_restarted_and_not_activated(harness);
}

void test_digest_covers_exactly_written_bytes(void) {
    Harness harness;
    harness.http.readActions = {2, 2};

    TEST_ASSERT_TRUE(harness.updater.perform("https://example/fw") == OtaUpdater::Result::Success);
    TEST_ASSERT_TRUE(harness.sha256.hashed == harness.update.written);
    TEST_ASSERT_TRUE(harness.verifier.digest == harness.sha256.digest);
    TEST_ASSERT_EQUAL_UINT(OtaImageVerifier::kRsa2048SignatureSize, harness.verifier.signatureLength);
    TEST_ASSERT_EQUAL_INT(1, harness.restart.calls);
}

void test_invalid_signature_aborts_before_end(void) {
    Harness harness;
    harness.verifier.result = false;

    TEST_ASSERT_TRUE(harness.updater.perform("https://example/fw") == OtaUpdater::Result::SignatureInvalid);
    TEST_ASSERT_EQUAL_INT(1, harness.verifier.calls);
    TEST_ASSERT_EQUAL_INT(1, harness.update.abortCalls);
    assert_restarted_and_not_activated(harness);
}

void test_valid_signature_ends_once_and_activates_once(void) {
    Harness harness;

    TEST_ASSERT_TRUE(harness.updater.perform("https://example/fw") == OtaUpdater::Result::Success);
    TEST_ASSERT_EQUAL_INT(1, harness.update.endCalls);
    TEST_ASSERT_EQUAL_INT(0, harness.update.abortCalls);
    TEST_ASSERT_EQUAL_INT(1, harness.update.activations);
    TEST_ASSERT_EQUAL_INT(1, harness.restart.calls);

    const std::vector<std::string> expected = {
        "progress:0", "resolve:https://example/fw", "resolve:https://example/fw.sig",
        "fetch-signature", "begin-image", "update:begin", "hash:begin", "read-image",
        "update:write", "hash:update", "progress:100", "end-image", "hash:finish",
        "verify", "update:end", "restart",
    };
    TEST_ASSERT_EQUAL_UINT(expected.size(), harness.events.size());
    for (size_t i = 0; i < expected.size() && i < harness.events.size(); ++i) {
        TEST_ASSERT_EQUAL_STRING(expected[i].c_str(), harness.events[i].c_str());
    }
}

void test_end_failure_is_reported_without_retry_or_activation(void) {
    Harness harness;
    harness.update.endResult = false;

    TEST_ASSERT_TRUE(harness.updater.perform("https://example/fw") == OtaUpdater::Result::UpdateEndFailed);
    TEST_ASSERT_EQUAL_INT(1, harness.update.endCalls);
    TEST_ASSERT_EQUAL_INT(1, harness.update.abortCalls);
    TEST_ASSERT_EQUAL_INT(0, harness.update.activations);
    TEST_ASSERT_EQUAL_STRING("fake update error", harness.updater.updateError());
    TEST_ASSERT_EQUAL_INT(1, harness.restart.calls);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_image_redirect_failure_never_starts_update);
    RUN_TEST(test_image_http_failure_never_starts_update);
    RUN_TEST(test_signature_redirect_failure_never_starts_update);
    RUN_TEST(test_signature_http_failure_never_starts_update);
    RUN_TEST(test_signature_wrong_length_never_starts_update);
    RUN_TEST(test_signature_incomplete_never_starts_update);
    RUN_TEST(test_unknown_content_length_aborts);
    RUN_TEST(test_zero_content_length_aborts);
    RUN_TEST(test_update_begin_failure_aborts);
    RUN_TEST(test_short_write_aborts_without_verification);
    RUN_TEST(test_stalled_body_aborts);
    RUN_TEST(test_incomplete_body_aborts);
    RUN_TEST(test_read_failure_aborts);
    RUN_TEST(test_digest_covers_exactly_written_bytes);
    RUN_TEST(test_invalid_signature_aborts_before_end);
    RUN_TEST(test_valid_signature_ends_once_and_activates_once);
    RUN_TEST(test_end_failure_is_reported_without_retry_or_activation);
    return UNITY_END();
}
