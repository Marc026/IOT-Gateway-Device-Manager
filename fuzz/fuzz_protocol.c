/* libFuzzer harness for the wire protocol parser. The gateway accepts
 * gw_packet_t structs straight off a UDP socket -- attacker-controlled
 * bytes -- so gw_proto_validate() and the accessors built on top of it
 * are the actual trust boundary of this whole project. This harness
 * exists to make sure malformed/truncated/random input is rejected
 * cleanly instead of read out of bounds or otherwise misbehaving.
 *
 * Build (requires Clang):
 *   cmake -B build-fuzz -DCMAKE_C_COMPILER=clang -DENABLE_FUZZING=ON
 *   cmake --build build-fuzz
 *   ./build-fuzz/fuzz_protocol -max_total_time=60
 */
#include "gateway_protocol.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < sizeof(gw_packet_t)) {
        return 0; /* too short to even attempt; not interesting input */
    }

    gw_packet_t pkt;
    memcpy(&pkt, data, sizeof(pkt));

    /* The return value isn't checked against an expected outcome --
     * the only thing under test is that validate() and the accessors
     * never crash, read out of bounds, or hit UB on arbitrary bytes,
     * valid or not. */
    if (gw_proto_validate(&pkt)) {
        (void)gw_proto_get_device_id(&pkt);
        (void)gw_proto_get_seq(&pkt);
    }
    return 0;
}
