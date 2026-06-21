#include "gateway_protocol.h"
#include "test.h"
#include <string.h>

int main(void) {
    uint8_t mac[6] = { 0x02, 0x11, 0x22, 0x33, 0x44, 0x55 };

    gw_packet_t pkt;
    gw_proto_build(&pkt, GW_MSG_HEARTBEAT, 7, 12345, mac, "kitchen-plug");

    GW_ASSERT(gw_proto_validate(&pkt));
    GW_ASSERT_EQ(gw_proto_get_device_id(&pkt), (uint16_t)7);
    GW_ASSERT_EQ(gw_proto_get_seq(&pkt), (uint32_t)12345);
    GW_ASSERT_EQ(pkt.msg_type, (uint8_t)GW_MSG_HEARTBEAT);
    GW_ASSERT_EQ(pkt.hostname_len, (uint8_t)strlen("kitchen-plug"));
    GW_ASSERT(memcmp(pkt.mac, mac, 6) == 0);

    /* Bit-flip corruption anywhere in the payload must be caught by CRC. */
    gw_packet_t corrupt = pkt;
    corrupt.seq ^= 0x00000001u;
    GW_ASSERT(!gw_proto_validate(&corrupt));

    /* Wrong magic / version are rejected even with a recomputed-looking crc. */
    gw_packet_t bad_magic = pkt;
    bad_magic.magic = 0;
    GW_ASSERT(!gw_proto_validate(&bad_magic));

    gw_packet_t bad_version = pkt;
    bad_version.version = 99;
    GW_ASSERT(!gw_proto_validate(&bad_version));

    /* Hostname longer than the field truncates cleanly rather than overflowing. */
    gw_packet_t long_host;
    gw_proto_build(&long_host, GW_MSG_DISCOVER, 0, 1, mac,
                   "this-hostname-is-definitely-longer-than-31-bytes");
    GW_ASSERT(gw_proto_validate(&long_host));
    GW_ASSERT(long_host.hostname_len <= sizeof(long_host.hostname));

    /* CRC sanity: same input always produces the same CRC; different
     * input (very likely) produces a different one. */
    const uint8_t a[] = "hello-gateway";
    const uint8_t b[] = "hello-gatewaY";
    GW_ASSERT_EQ(gw_crc16(a, sizeof(a) - 1), gw_crc16(a, sizeof(a) - 1));
    GW_ASSERT(gw_crc16(a, sizeof(a) - 1) != gw_crc16(b, sizeof(b) - 1));

    return GW_TEST_SUMMARY("protocol");
}
