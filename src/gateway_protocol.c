#include "gateway_protocol.h"

#include <string.h>
#include <arpa/inet.h>

/* CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflect, no xorout.
 * Table-free bitwise implementation -- favors code size over speed,
 * which is the right trade on a flash-constrained target processing a
 * handful of small control packets rather than bulk data. */
uint16_t gw_crc16(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)(data[i] << 8);
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

void gw_proto_build(gw_packet_t *out, gw_msg_type_t type, uint16_t device_id,
                     uint32_t seq, const uint8_t mac[6], const char *hostname) {
    if (!out) return;
    memset(out, 0, sizeof(*out));

    out->magic = htonl(GW_PROTO_MAGIC);
    out->version = GW_PROTO_VERSION;
    out->msg_type = (uint8_t)type;
    out->device_id = htons(device_id);
    out->seq = htonl(seq);
    if (mac) memcpy(out->mac, mac, 6);

    size_t hlen = 0;
    if (hostname) {
        hlen = strlen(hostname);
        if (hlen > sizeof(out->hostname)) hlen = sizeof(out->hostname);
        memcpy(out->hostname, hostname, hlen);
    }
    out->hostname_len = (uint8_t)hlen;

    /* CRC covers every field except itself. */
    size_t crc_span = sizeof(*out) - sizeof(out->crc16);
    out->crc16 = htons(gw_crc16((const uint8_t *)out, crc_span));
}

bool gw_proto_validate(const gw_packet_t *pkt) {
    if (!pkt) return false;
    if (ntohl(pkt->magic) != GW_PROTO_MAGIC) return false;
    if (pkt->version != GW_PROTO_VERSION) return false;
    if (pkt->hostname_len > sizeof(pkt->hostname)) return false;

    size_t crc_span = sizeof(*pkt) - sizeof(pkt->crc16);
    uint16_t expected = gw_crc16((const uint8_t *)pkt, crc_span);
    return ntohs(pkt->crc16) == expected;
}

uint16_t gw_proto_get_device_id(const gw_packet_t *pkt) {
    return pkt ? ntohs(pkt->device_id) : 0;
}

uint32_t gw_proto_get_seq(const gw_packet_t *pkt) {
    return pkt ? ntohl(pkt->seq) : 0;
}
