#ifndef GW_PROTOCOL_H
#define GW_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Fixed-layout binary wire protocol for device <-> gateway messages
 * (discovery, handshake, heartbeat, bye) over UDP. Multi-byte integer
 * fields are stored in network byte order so the wire format is portable
 * across architectures (a little-endian device talking to a big-endian
 * gateway, or vice versa) -- use the gw_proto_get_* accessors rather than
 * reading the struct fields directly. */

#define GW_PROTO_MAGIC     0x474C574Bu  /* "GLWK" */
#define GW_PROTO_VERSION   1u

typedef enum {
    GW_MSG_DISCOVER  = 1,
    GW_MSG_HANDSHAKE = 2,
    GW_MSG_HEARTBEAT = 3,
    GW_MSG_BYE       = 4
} gw_msg_type_t;

#pragma pack(push, 1)
typedef struct {
    uint32_t magic;          /* network byte order, == GW_PROTO_MAGIC */
    uint8_t  version;        /* == GW_PROTO_VERSION */
    uint8_t  msg_type;       /* gw_msg_type_t */
    uint16_t device_id;      /* network byte order; 0 = unassigned */
    uint32_t seq;            /* network byte order, monotonically increasing */
    uint8_t  mac[6];
    uint8_t  hostname_len;   /* <= 31 */
    char     hostname[31];   /* not necessarily NUL-terminated on the wire */
    uint16_t crc16;          /* CRC-16/CCITT-FALSE over all preceding bytes */
} gw_packet_t;
#pragma pack(pop)

#define GW_PACKET_SIZE  (sizeof(gw_packet_t))

uint16_t gw_crc16(const uint8_t *data, size_t len);

/* Zero-fills `out`, then fills in a well-formed packet (including crc16).
 * hostname is truncated to 31 bytes if longer. device_id/seq are given in
 * host byte order and converted internally. */
void gw_proto_build(gw_packet_t *out, gw_msg_type_t type, uint16_t device_id,
                     uint32_t seq, const uint8_t mac[6], const char *hostname);

/* Validates magic, version, and crc16. Returns true if `pkt` is well-formed. */
bool gw_proto_validate(const gw_packet_t *pkt);

uint16_t gw_proto_get_device_id(const gw_packet_t *pkt);
uint32_t gw_proto_get_seq(const gw_packet_t *pkt);

#endif /* GW_PROTOCOL_H */
