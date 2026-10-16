#include "track_sharing/read_usb_request.h"
#include <array>
#include <cassert>
int main()
{
    std::array<uint8_t, 471> raw{};
    std::array<uint8_t, 16> id{};
    id.fill(1);
    std::array<uint8_t, 32> hash{};
    hash.fill(2);
    track_sharing::ReadUsbRequest output;
    for (uint8_t op : {0, 1, 2, 8})
    {
        geocaching::protocol::CmpWriter writer(raw.data(), raw.size());
        assert(writer.array(6) && writer.binary({id.data(), 16}) && writer.binary({id.data(), 16}) && writer.binary({id.data(), 16}) && writer.unsignedInteger(9) && writer.unsignedInteger(op));
        if (!op) assert(writer.array(0));
        if (op == 1) assert(writer.array(7) && writer.array(4) && writer.signedInteger(1700000000) && writer.signedInteger(-100000000) && writer.signedInteger(-1700000000) && writer.signedInteger(100000000) &&
                            writer.nil() && writer.nil() && writer.unsignedInteger(0) && writer.unsignedInteger(16) && writer.unsignedInteger(9007199254740993ULL) && writer.array(2) && writer.binary({hash.data(), 32}) && writer.binary({hash.data(), 32}));
        if (op == 2) assert(writer.array(2) && writer.binary({hash.data(), 32}) && writer.nil());
        if (op == 8) assert(writer.array(4) && writer.binary({hash.data(), 32}) && writer.unsignedInteger(1200) && writer.unsignedInteger(1000) && writer.unsignedInteger(200));
        assert(track_sharing::decodeReadUsbRequest({raw.data(), writer.size()}, output) && output.operation == op && output.generation == 9);
        if (op == 1) assert(output.expected.query == &output.query && output.query.has_cursor && output.query.snapshot == 9007199254740993ULL && output.query.west > output.query.east);
        if (op == 8) assert(output.expected.file_bytes == 1200 && output.expected.length == 200);
        raw[writer.size()] = 0;
        assert(!track_sharing::decodeReadUsbRequest({raw.data(), writer.size() + 1}, output));
        assert(!track_sharing::decodeReadUsbRequest({raw.data(), writer.size() - 1}, output));
        // Six-element header: three bin16 fields, then generation and operation.
        raw[56] = 4;
        assert(!track_sharing::decodeReadUsbRequest({raw.data(), writer.size()}, output));
    }
    for (uint8_t op : {9, 10, 11, 12, 13})
    {
        geocaching::protocol::CmpWriter writer(raw.data(), raw.size());
        assert(writer.array(6) && writer.binary({id.data(), 16}) && writer.binary({id.data(), 16}) && writer.binary({id.data(), 16}) && writer.unsignedInteger(9) && writer.unsignedInteger(op));
        if (op == 9) assert(writer.array(0));
        if (op == 10) assert(writer.array(7) && writer.text("test") && writer.binary({hash.data(), 32}) && writer.text("test:r") && writer.signedInteger(0) && writer.nil() && writer.unsignedInteger(3) && writer.unsignedInteger(16));
        if (op == 11) assert(writer.array(4) && writer.text("test") && writer.text("test:r") && writer.binary({hash.data(), 32}) && writer.unsignedInteger(3));
        if (op == 12) assert(writer.array(7) && writer.text("test") && writer.text("test:r") && writer.binary({hash.data(), 32}) && writer.binary({hash.data(), 32}) && writer.unsignedInteger(100) && writer.unsignedInteger(90) && writer.unsignedInteger(10));
        if (op == 13) assert(writer.array(10) && writer.text("test") && writer.text("test:r") && writer.binary({hash.data(), 32}) && writer.array(4) && writer.signedInteger(0) && writer.signedInteger(0) && writer.signedInteger(100) && writer.signedInteger(100) && writer.nil() && writer.nil() && writer.unsignedInteger(0) && writer.unsignedInteger(16) && writer.unsignedInteger(9007199254740993ULL) && writer.nil());
        assert(track_sharing::decodeReadUsbRequest({raw.data(), writer.size()}, output) && output.operation == op);
        if (op >= 10) assert(output.expected.region == &output.region && output.region.name_space == "test");
        if (op == 10) assert(output.region.parent == "test:r" && output.region.parent_level == 0 && output.expected.query->limit == 16);
        if (op == 12) assert(output.expected.file_bytes == 100 && output.expected.offset == 90 && output.expected.length == 10);
        if (op == 13) assert(output.expected.query == &output.query && output.query.snapshot == 9007199254740993ULL);
        raw[writer.size()] = 0;
        assert(!track_sharing::decodeReadUsbRequest({raw.data(), writer.size() + 1}, output));
    }
}
