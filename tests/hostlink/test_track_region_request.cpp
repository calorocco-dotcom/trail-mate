#include "track_sharing/region_read_request.h"
#include "platform/esp/common/storage/snapshot_stream_sha256.h"
#include <array>
#include <cassert>
#if defined(_WIN32)
extern "C" void mbedtls_platform_zeroize(void* pointer, size_t length)
{
    auto* bytes = static_cast<volatile uint8_t*>(pointer);
    while (length--) *bytes++ = 0;
}
#endif
static mbedtls_sha256_context context;
static track_sharing::ReadQuery query;
static std::array<uint8_t,320> workspace;
static std::array<uint8_t,32> digest,previous;
int main()
{
    using namespace track_sharing;
    std::array<uint8_t,16> request{};
    std::array<uint8_t,32> version{},cursor{};
    std::array<uint8_t,64> author{};
    request.fill(2); version.fill(1); cursor.fill(3); author.fill(4);
    constexpr char region[] = "test:region";
    const geocaching::ByteView id{reinterpret_cast<const uint8_t*>(region),sizeof(region)-1}, token{request.data(),16}, boundary{version.data(),32};
    assert(regionId(id));
    for(const auto invalid : {std::string_view("TEST:region"),std::string_view("test:"),std::string_view("test:_bad"),std::string_view("test:r:bad"),std::string_view("test:r/../../")})
        assert(!regionId({reinterpret_cast<const uint8_t*>(invalid.data()),invalid.size()}));
    size_t written = 0;
    auto header=[&](geocaching::protocol::CmpReader& reader,uint64_t op,size_t fields)
    {
        uint64_t value=0;size_t count=0;geocaching::ByteView actual;
        assert(reader.array(count,6)&&count==6&&reader.unsignedInteger(value)&&value==1&&reader.unsignedInteger(value)&&!value&&reader.unsignedInteger(value)&&value==op&&
               reader.binary(actual,16)&&actual.size==16&&!std::memcmp(actual.data,request.data(),16)&&reader.unsignedInteger(value)&&value==8192&&reader.array(count,9)&&count==fields);
    };
    assert(encodeRegionCapabilities(token,8192,workspace.data(),workspace.size(),written));
    {geocaching::protocol::CmpReader reader({workspace.data(),written});header(reader,9,0);assert(reader.finished());}
    assert(encodeRegionChildren(token,boundary,id,{},16,8192,workspace.data(),workspace.size(),written));
    {geocaching::protocol::CmpReader reader({workspace.data(),written});header(reader,10,4);geocaching::ByteView actual;std::string_view name;uint64_t limit=0;assert(reader.binary(actual,32)&&actual.size==32&&reader.text(name,89)&&name==region&&reader.nil()&&reader.unsignedInteger(limit)&&limit==16&&reader.finished());}
    assert(encodeRegionDescriptor(token,id,boundary,8192,workspace.data(),workspace.size(),written));
    {geocaching::protocol::CmpReader reader({workspace.data(),written});header(reader,11,2);geocaching::ByteView actual;std::string_view name;assert(reader.text(name,89)&&name==region&&reader.binary(actual,32)&&actual.size==32&&reader.finished());}
    assert(encodeRegionBoundaryChunk(token,id,boundary,8190,2,8192,workspace.data(),workspace.size(),written));
    {geocaching::protocol::CmpReader reader({workspace.data(),written});header(reader,12,4);geocaching::ByteView actual;std::string_view name;uint64_t offset=0,count=0;assert(reader.text(name,89)&&reader.binary(actual,32)&&reader.unsignedInteger(offset)&&offset==8190&&reader.unsignedInteger(count)&&count==2&&reader.finished());}
    assert(!encodeRegionBoundaryChunk(token,id,boundary,8388607,2,8192,workspace.data(),workspace.size(),written)&&!written);
    assert(!encodeRegionChildren(token,boundary,id,{},33,8192,workspace.data(),workspace.size(),written));
    assert(!encodeRegionDescriptor(token,id,boundary,8192,workspace.data(),10,written)&&!written);
    assert(!encodeRegionDescriptor(token,id,{workspace.data(),32},8192,workspace.data(),workspace.size(),written));
    platform::esp::storage::SnapshotStreamSha256 hash(context);
    assert(deriveRegionQueryBinding(id,boundary,query,hash,workspace.data(),workspace.size(),digest.data()));
    const uint8_t expected[]={0x30,0x4c,0x87,0x1d,0x43,0x74,0x0c,0x79,0x6e,0x3c,0x47,0xf8,0x3a,0x6f,0x22,0xa6,0x1b,0x0b,0xa3,0x5c,0x98,0x4f,0xcf,0xfa,0x91,0x44,0x83,0x4f,0x77,0x9d,0x7a,0x55};
    assert(!std::memcmp(digest.data(),expected,32));previous=digest;
    query.has_snapshot=true;query.snapshot=9007199254740993ULL;query.has_cursor=true;query.cursor_object={cursor.data(),32};query.cursor_binding={digest.data(),32};
    assert(deriveRegionQueryBinding(id,boundary,query,hash,workspace.data(),workspace.size(),previous.data())&&previous==digest);
    assert(encodeRegionQuery(token,id,boundary,query,8192,workspace.data(),workspace.size(),written));
    {geocaching::protocol::CmpReader reader({workspace.data(),written});header(reader,13,9);geocaching::ByteView actual;std::string_view name,relation;uint64_t value=0;size_t count=0;
     assert(reader.text(name,89)&&name==region&&reader.binary(actual,32)&&reader.text(relation,10)&&relation=="intersects"&&reader.nil()&&reader.nil()&&reader.unsignedInteger(value)&&!value&&reader.unsignedInteger(value)&&value==16&&reader.unsignedInteger(value)&&value==query.snapshot&&reader.array(count,2)&&count==2&&reader.binary(actual,32)&&reader.binary(actual,32)&&reader.finished());}
    query.author={author.data(),64};
    char maximum[90];std::memset(maximum,'a',sizeof(maximum));maximum[24]=':';
    assert(encodeRegionQuery(token,{reinterpret_cast<const uint8_t*>(maximum),89},boundary,query,8192,workspace.data(),workspace.size(),written)&&written<=320);
    query.has_snapshot=false;
    assert(!encodeRegionQuery(token,id,boundary,query,8192,workspace.data(),workspace.size(),written));
}
