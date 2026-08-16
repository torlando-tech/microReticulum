#include <unity.h>

#include "microReticulum/Bytes.h"
#include "microReticulum/Link.h"

using namespace RNS;

void setUp() {}
void tearDown() {}

void test_secure_clear_scrubs_all_shared_views() {
    const uint8_t secret[] = {0x73, 0x65, 0x63, 0x72, 0x65, 0x74};
    Bytes bytes(secret, sizeof(secret));
    Bytes shared = bytes;

    bytes.secure_clear();

    TEST_ASSERT_EQUAL_UINT32(0, bytes.size());
    TEST_ASSERT_EQUAL_UINT32(0, shared.size());
}

void test_request_exposes_opt_in_sensitive_handoff() {
    using SensitiveRequest = const RequestReceipt (Link::*)(
        const Bytes&, const Bytes&, RequestReceipt::Callbacks::response,
        RequestReceipt::Callbacks::failed, RequestReceipt::Callbacks::progress,
        double, size_t, bool);
    SensitiveRequest request = &Link::request;
    TEST_ASSERT_NOT_NULL(request);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_secure_clear_scrubs_all_shared_views);
    RUN_TEST(test_request_exposes_opt_in_sensitive_handoff);
    return UNITY_END();
}
