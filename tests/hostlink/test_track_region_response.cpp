#include "track_sharing/region_read_response.h"
#include <array>
#include <cassert>
using namespace track_sharing;
static std::array<uint8_t,8192> raw;
static std::array<uint8_t,16> request;
static std::array<uint8_t,32> version,digest;
static RegionCapabilitiesView caps;
static RegionNodeView row;
static RegionChildrenView page;
static RegionDescriptorView descriptor;
static ReadFileResponse file;
static ReadQuery query;
static ReadQueryRow query_row;
static ReadQueryResponse query_page;
static bool header(geocaching::protocol::CmpWriter& writer,uint8_t op,uint8_t fields)
{
    return writer.array(6)&&writer.unsignedInteger(1)&&writer.unsignedInteger(1)&&writer.unsignedInteger(op)&&writer.binary({request.data(),16})&&writer.unsignedInteger(200)&&writer.array(fields);
}
static void node(geocaching::protocol::CmpWriter& writer,std::string_view id,std::string_view parent,uint8_t level,unsigned names=0,bool duplicate=false)
{
    assert(writer.array(7)&&writer.text(id)&&(parent.empty()?writer.nil():writer.text(parent))&&writer.unsignedInteger(level)&&writer.text("Region")&&writer.array(names));
    for(unsigned i=0;i<names;i++)
    {
        const char locale[]={'a',char('a'+(duplicate?0:i))};
        assert(writer.array(2)&&writer.text({locale,2})&&writer.text("n"));
    }
    assert(writer.array(4)&&writer.signedInteger(1790000000)&&writer.signedInteger(0)&&writer.signedInteger(-1790000000)&&writer.signedInteger(10000000)&&writer.unsignedInteger(0));
}
int main()
{
    request.fill(2);version.fill(1);digest.fill(3);
    const geocaching::ByteView token{request.data(),16},boundary{version.data(),32};
    for(bool bad : {false,true})
    {
        geocaching::protocol::CmpWriter writer(raw.data(),raw.size());
        assert(header(writer,9,8)&&writer.unsignedInteger(1)&&writer.text("trailmate.track.regions.v1")&&writer.text("test")&&writer.binary(boundary)&&writer.unsignedInteger(3)&&writer.unsignedInteger(32)&&writer.unsignedInteger(1024)&&writer.array(5));
        for(unsigned i=9;i<=13;i++)assert(writer.unsignedInteger(bad&&i==13?12:i));
        assert(decodeRegionCapabilities({raw.data(),writer.size()},token,caps)==!bad);
        if(!bad){assert(caps.name_space=="test"&&caps.depth==3&&caps.chunk_bytes==1024);raw[writer.size()]=0;assert(!decodeRegionCapabilities({raw.data(),writer.size()+1},token,caps));}
    }
    for(bool duplicate : {false,true})
    {
        geocaching::protocol::CmpWriter writer(raw.data(),raw.size());
        assert(header(writer,10,3)&&writer.binary(boundary)&&writer.text("test:a")&&writer.array(1));node(writer,"test:a","test:r",1,2,duplicate);
        assert(decodeRegionChildren({raw.data(),writer.size()},token,boundary,"test","test:r","",0,3,16,row,page)==!duplicate);
        if(!duplicate){assert(page.rows==1&&page.cursor=="test:a"&&row.level==1&&row.west>row.east);assert(!decodeRegionChildren({raw.data(),writer.size()},token,boundary,"test","test:other","",0,3,16,row,page));assert(!decodeRegionChildren({raw.data(),writer.size()},token,boundary,"test","test:r","test:a",0,3,16,row,page));}
    }
    {
        geocaching::protocol::CmpWriter writer(raw.data(),raw.size());assert(header(writer,10,3)&&writer.binary(boundary)&&writer.nil()&&writer.array(0));
        assert(decodeRegionChildren({raw.data(),writer.size()},token,boundary,"test","test:leaf","",3,3,16,row,page)&&!page.rows);
    }
    for(unsigned names : {2U,22U})
    {
        geocaching::protocol::CmpWriter writer(raw.data(),raw.size());assert(header(writer,10,3)&&writer.binary(boundary)&&writer.nil()&&writer.array(32));
        for(unsigned i=0;i<32;i++){char id[]={'t','e','s','t',':',char('0'+i/10),char('0'+i%10)};node(writer,{id,sizeof(id)},"test:r",1,names);}
        assert(writer.size()<8192);
        assert(decodeRegionChildren({raw.data(),writer.size()},token,boundary,"test","test:r","",0,3,32,row,page)==(names==2));
    }
    {
        geocaching::protocol::CmpWriter writer(raw.data(),raw.size());assert(header(writer,11,3)&&writer.binary(boundary));node(writer,"test:r","",0);
        assert(writer.array(3)&&writer.text("region-e7-json-v1")&&writer.binary({digest.data(),32})&&writer.unsignedInteger(500));
        assert(decodeRegionDescriptor({raw.data(),writer.size()},token,"test:r",boundary,"test",3,descriptor)&&descriptor.bytes==500);
        assert(!decodeRegionDescriptor({raw.data(),writer.size()},token,"test:other",boundary,"test",3,descriptor));
        version[0]^=1;assert(!decodeRegionDescriptor({raw.data(),writer.size()},token,"test:r",boundary,"test",3,descriptor));version[0]^=1;
        raw[writer.size()]=0;assert(!decodeRegionDescriptor({raw.data(),writer.size()+1},token,"test:r",boundary,"test",3,descriptor));
    }
    {
        geocaching::protocol::CmpWriter writer(raw.data(),raw.size());
        assert(header(writer,12,4)&&writer.binary({digest.data(),32})&&writer.unsignedInteger(32)&&writer.unsignedInteger(0)&&writer.binary(boundary));
        assert(decodeRegionBoundaryResponse({raw.data(),writer.size()},token,{digest.data(),32},32,0,32,file)&&file.content.size==32);
        assert(!decodeTrackReadFileResponse({raw.data(),writer.size()},token,{digest.data(),32},32,0,32,file));
        assert(!decodeTrackReadFileResponse({raw.data(),writer.size()},token,{digest.data(),32},32,0,32,file,13));
        assert(!decodeRegionBoundaryResponse({raw.data(),writer.size()},token,{digest.data(),32},33,0,32,file));
        assert(!decodeRegionBoundaryResponse({raw.data(),writer.size()},token,{digest.data(),32},32,1,31,file));
        digest[0]^=1;assert(!decodeRegionBoundaryResponse({raw.data(),writer.size()},token,{digest.data(),32},32,0,32,file));digest[0]^=1;
        raw[writer.size()]=0;assert(!decodeRegionBoundaryResponse({raw.data(),writer.size()+1},token,{digest.data(),32},32,0,32,file));
    }
    {
        query.west=query.south=-100;query.east=query.north=100;
        geocaching::protocol::CmpWriter writer(raw.data(),raw.size());
        assert(header(writer,13,3)&&writer.unsignedInteger(9007199254740993ULL)&&writer.array(2)&&writer.binary(boundary)&&writer.binary({digest.data(),32})&&writer.array(1));
        assert(writer.array(10)&&writer.binary(boundary)&&writer.binary({digest.data(),32})&&writer.unsignedInteger(1)&&writer.unsignedInteger(0)&&writer.unsignedInteger(0)&&writer.text("Track")&&
               writer.array(4)&&writer.signedInteger(-1)&&writer.signedInteger(-1)&&writer.signedInteger(1)&&writer.signedInteger(1)&&writer.array(4)&&writer.unsignedInteger(0)&&writer.nil()&&writer.nil()&&writer.unsignedInteger(2)&&writer.unsignedInteger(100));
        std::array<uint8_t,64> author;author.fill(4);assert(writer.binary({author.data(),64}));
        assert(decodeRegionQueryResponse({raw.data(),writer.size()},token,query,{digest.data(),32},query_row,query_page)&&query_page.rows==1&&query_page.snapshot==9007199254740993ULL&&query_page.has_cursor);
        assert(!decodeTrackReadQueryResponse({raw.data(),writer.size()},token,query,{digest.data(),32},query_row,query_page));
        assert(!decodeTrackReadQueryResponse({raw.data(),writer.size()},token,query,{digest.data(),32},query_row,query_page,12));
        digest[0]^=1;assert(!decodeRegionQueryResponse({raw.data(),writer.size()},token,query,{digest.data(),32},query_row,query_page));digest[0]^=1;
        query.has_snapshot=true;query.snapshot=9007199254740994ULL;assert(!decodeRegionQueryResponse({raw.data(),writer.size()},token,query,{digest.data(),32},query_row,query_page));query.has_snapshot=false;
        query.state=1;assert(!decodeRegionQueryResponse({raw.data(),writer.size()},token,query,{digest.data(),32},query_row,query_page));query.state=0;
        raw[writer.size()]=0;assert(!decodeRegionQueryResponse({raw.data(),writer.size()+1},token,query,{digest.data(),32},query_row,query_page));
    }
    assert(regionLocale("zh-Hant-TW")&&regionLocale("en")&&!regionLocale("e")&&!regionLocale("en--US")&&!regionLocale("en-!")&&!regionNamespace("Test"));
}
