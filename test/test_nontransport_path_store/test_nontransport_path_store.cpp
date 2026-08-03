#include <unity.h>

#include <microStore/Adapters/UniversalFileSystem.h>

#include "microReticulum.h"

void setUp(void) {}
void tearDown(void) {}

void test_nontransport_client_initializes_persistent_path_store() {
    microStore::FileSystem filesystem{microStore::Adapters::UniversalFileSystem()};
    TEST_ASSERT_TRUE(filesystem.init());
    RNS::Utilities::OS::register_filesystem(filesystem);

    RNS::Reticulum reticulum;
    reticulum.transport_enabled(false);
    reticulum.start();

    TEST_ASSERT_FALSE(RNS::Reticulum::transport_enabled());
    TEST_ASSERT_TRUE_MESSAGE(
        RNS::Transport::new_path_table().isValid(),
        "non-transport clients must still have a usable persistent path table");
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_nontransport_client_initializes_persistent_path_store);
    return UNITY_END();
}
