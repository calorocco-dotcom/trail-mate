#include "hostlink/management_envelope.h"
#include <algorithm>
#include <array>
#include <cassert>

int main()
{
    std::array<uint8_t, 16> session, request;
    session.fill(1);
    request.fill(2);
    std::array<uint8_t, 512> output;
    const uint8_t body[] = {0x90};
    size_t written;
    assert(hostlink::management::encodeEnvelope(session.data(), request.data(), 0, body, 1, output.data(), output.size(), written));
    assert(written == 40 && output[0] == 0x95 && output[2] == 0xc4 && output[38] == 0 && output[39] == 0x90);
    hostlink::management::EnvelopeView view;
    assert(hostlink::management::decodeEnvelope(output.data(), written, view));
    assert(view.session == output.data() + 4 && view.request == output.data() + 22);
    assert(view.body == output.data() + 39 && view.body_size == 1);
    for (size_t size = 0; size < written; ++size) assert(!hostlink::management::decodeEnvelope(output.data(), size, view));
    output[written] = 0;
    assert(!hostlink::management::decodeEnvelope(output.data(), written + 1, view));
    const uint8_t invalid[][4] = {{0xcc, 1, 0, 0}, {0xa2, 0xc0, 0xaf, 0}, {0xd9, 1, 'x', 0}, {0x91, 0xc3, 0, 0}};
    for (const auto& bytes : invalid) assert(!hostlink::management::encodeEnvelope(session.data(), request.data(), 0, bytes, 3, output.data(), output.size(), written));
    std::array<uint8_t, 474> maximum;
    maximum.fill(3);
    maximum[0] = 0xc5;
    maximum[1] = 1;
    maximum[2] = 0xd7;
    // 471-byte binary body + 3-byte header exceeds this frame's remaining budget.
    assert(!hostlink::management::encodeEnvelope(session.data(), request.data(), 0, maximum.data(), maximum.size(), output.data(), output.size(), written));
    const uint8_t signed_body[] = {0x94, 0xd0, 0xdf, 0xd1, 0xff, 0x7f, 0xd2, 0xff, 0xff, 0x7f, 0xff, 0xc0};
    assert(hostlink::management::encodeEnvelope(session.data(), request.data(), 65535, signed_body, sizeof(signed_body), output.data(), output.size(), written));
    assert(hostlink::management::decodeEnvelope(output.data(), written, view) && view.code == 65535);
}
