#include <unity.h>

#include <cstdio>
#include <string>

#include <microStore/Adapters/UniversalFileSystem.h>

#include "microReticulum.h"

class CaptureInterface : public RNS::InterfaceImpl {
public:
    explicit CaptureInterface(const char* name) : RNS::InterfaceImpl(name) {
        _IN = true;
        _OUT = true;
    }

    bool send_outgoing(const RNS::Bytes&) override {
        sent_packets++;
        return true;
    }

    size_t sent_packets = 0;
};

void setUp(void) {}
void tearDown(void) {}

void test_nontransport_client_learns_paths_without_forwarding_foreign_traffic() {
    std::remove("path_store_index.dat");
    for (int segment = 0; segment <= 8; segment++) {
        const std::string path = "path_store_" + std::to_string(segment) + ".dat";
        std::remove(path.c_str());
    }

    microStore::FileSystem filesystem{microStore::Adapters::UniversalFileSystem()};
    TEST_ASSERT_TRUE(filesystem.init());
    RNS::Utilities::OS::register_filesystem(filesystem);

    auto* inbound_impl = new CaptureInterface("inbound");
    auto* learned_path_impl = new CaptureInterface("learned-path");
    RNS::Interface inbound(inbound_impl);
    RNS::Interface learned_path(learned_path_impl);
    RNS::Transport::register_interface(inbound);
    RNS::Transport::register_interface(learned_path);

    RNS::Reticulum reticulum;
    reticulum.transport_enabled(false);
    reticulum.start();

    TEST_ASSERT_FALSE(RNS::Reticulum::transport_enabled());
    TEST_ASSERT_TRUE_MESSAGE(
        RNS::Transport::new_path_table().isValid(),
        "non-transport clients must still have a usable persistent path table");

    RNS::Bytes private_key;
    private_key.assignHex(
        "E0D43398EDC974EBA9F4A83463691A08F4D306D4E56BA6B275B8690A2FBD9852"
        "E9EBE7C03BC45CAEC9EF8E78C830037210BFB9986F6CA2DEE2B5C28D7B4DE6B0");
    RNS::Identity remote_identity(false);
    TEST_ASSERT_TRUE(remote_identity.load_private_key(private_key));

    RNS::Bytes destination_hash;
    destination_hash.assignHex("0945525d8858fca8e030bf0eed2cfc6a");
    RNS::Bytes announce_raw;
    // Golden announce produced by Python RNS 1.3.9 for the fixed identity
    // below. Keeping the announcing IN destination outside this process avoids
    // accidentally registering it as a local destination in the test stack.
    announce_raw.assignHex(
        "01000945525d8858fca8e030bf0eed2cfc6a00893959216d1ae899a2c90aed57"
        "88d04c522d7a439130f740f9fe7d54a1da9e29695510fdb597eeb4bf49703b1d"
        "e735150faae880180e807ac9c63aaa590cc7e195f0d87e82fc50affbef10d0364"
        "c27006a70d5c2939d5f4768c2b2528b9b69c62986120160cad9bb10b7a67f289"
        "2bdc728344bc96f30a40a55745a3cc635ffeb7c847ea65a2c6e56880cf6b913a"
        "164d470135c0972656d6f7465");

    learned_path.handle_incoming(announce_raw);
    TEST_ASSERT_TRUE_MESSAGE(
        RNS::Transport::has_path(destination_hash),
        "endpoint clients must learn paths from valid announces");

    RNS::Destination remote_out(
        remote_identity, RNS::Type::Destination::OUT,
        RNS::Type::Destination::SINGLE, "test", "remote");
    RNS::Packet foreign_packet = RNS::Packet(remote_out, RNS::Bytes("foreign transit payload"))
        .transport_type(RNS::Type::Transport::TRANSPORT)
        .header_type(RNS::Type::Packet::HEADER_2)
        .transport_id(RNS::Transport::identity().hash());
    foreign_packet.pack();

    const size_t sent_before = learned_path_impl->sent_packets;
    inbound.handle_incoming(foreign_packet.raw());
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        sent_before, learned_path_impl->sent_packets,
        "endpoint clients must not forward foreign packets to a learned path");
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_nontransport_client_learns_paths_without_forwarding_foreign_traffic);
    return UNITY_END();
}
