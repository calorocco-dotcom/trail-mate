#include "chat/ports/i_track_transport.h"
#include "chat/ports/device_author.h"
#include "chat/ports/track_binding_registration.h"
#include <cassert>
#include <cstring>

namespace chat
{
struct TrackPort : ITrackTransport
{
    unsigned calls = 0;
    unsigned binds = 0;
    const lxmf::TrackDeliveryBinding* binding = nullptr;
    MeshSendResult sendTrackData(const uint8_t*, lxmf::ByteSpan, bool, std::array<uint8_t,32>* hash) override
    {
        ++calls;
        if (hash) hash->fill(7);
        return MeshSendResult::success(42);
    }
    void setTrackDeliveryBinding(const lxmf::TrackDeliveryBinding* value) override {binding=value;++binds;}
};
struct Backend
{
    TrackPort port;
    bool ready = true, available = true, supports = true;
    uint32_t generation = 9;
    uint8_t key[64]{};
    bool isReady() const { return ready; }
    ITrackTransport* trackTransport() { return supports ? &port : nullptr; }
    DeviceAuthorResult readDeviceAuthorKey(uint8_t* out, uint32_t& gen) const
    {
        if (!available) return DeviceAuthorResult::Unavailable;
        std::memcpy(out,key,64);gen=generation;return DeviceAuthorResult::Ok;
    }
};
class MeshAdapterRouter
{
  public:
    Backend backend;
    bool admitted = true;
    bool mutex_ = true;
    const lxmf::TrackDeliveryBinding* track_delivery_binding_ = nullptr;
    struct Core
    {
        Backend* backend = nullptr;
        Backend* backendForProtocol(MeshProtocol) { return backend; }
    } core_;
    struct LockGuard
    {
        bool held;
        LockGuard(bool mutex,unsigned) : held(mutex) {}
        bool locked() const { return held; }
    };
    MeshAdapterRouter() { core_.backend=&backend; }
    const Backend* deviceAuthorBackendLocked() const { return admitted ? &backend : nullptr; }
    MeshSendResult sendTrackData(const uint8_t*,lxmf::ByteSpan,const uint8_t*,uint32_t,bool,std::array<uint8_t,32>*);
    bool bindTrackDelivery(const lxmf::TrackDeliveryBinding*);
    TrackBindingChange replaceTrackDelivery(const lxmf::TrackDeliveryBinding*,const lxmf::TrackDeliveryBinding*);
    TrackBindingChange visitTrackDelivery(const lxmf::TrackDeliveryBinding*,uint8_t*,uint32_t&,TrackBindingVisit,void*);
};
#include "track_router_binding_actual.inc"
#include "track_router_dispatch_actual.inc"
} // namespace chat

int main()
{
    chat::MeshAdapterRouter router;
    uint8_t destination[16]{}, author[64]{}, payload[1]{1};
    std::array<uint8_t,32> hash;
    auto send = [&] { hash.fill(9);return router.sendTrackData(destination,{payload,1},author,9,false,&hash); };
    send();assert(router.backend.port.calls==1 && hash[0]==7);
    router.backend.key[0]=1;send();assert(router.backend.port.calls==1 && hash[0]==0);
    router.backend.key[0]=0;router.backend.generation=10;send();assert(router.backend.port.calls==1 && hash[0]==0);
    router.backend.generation=9;router.backend.available=false;send();assert(router.backend.port.calls==1 && hash[0]==0);
    router.backend.available=true;router.backend.ready=false;send();assert(router.backend.port.calls==1 && hash[0]==0);
    router.backend.ready=true;router.backend.supports=false;send();assert(router.backend.port.calls==1 && hash[0]==0);
    router.backend.supports=true;router.mutex_=false;send();assert(router.backend.port.calls==1 && hash[0]==0);
    router.mutex_=true;router.admitted=false;send();assert(router.backend.port.calls==1 && hash[0]==0);
    router.admitted=true;router.core_.backend=nullptr;send();assert(router.backend.port.calls==1 && hash[0]==0);
    router.core_.backend=&router.backend;
    router.sendTrackData(nullptr,{payload,1},author,9,false,&hash);assert(router.backend.port.calls==1);
    router.sendTrackData(destination,{payload,8193},author,9,false,&hash);assert(router.backend.port.calls==1);
    router.sendTrackData(destination,{payload,1},author,0,false,&hash);assert(router.backend.port.calls==1);
    send();assert(router.backend.port.calls==2 && hash[0]==7);
    chat::lxmf::TrackDeliveryBinding directory,job,foreign;
    using chat::TrackBindingChange;
    assert(router.replaceTrackDelivery(nullptr,&directory)==TrackBindingChange::Applied && router.backend.port.binding==&directory);
    const auto binds=router.backend.port.binds;
    assert(router.replaceTrackDelivery(nullptr,&job)==TrackBindingChange::Conflict && router.backend.port.binds==binds);
    assert(!router.bindTrackDelivery(nullptr) && !router.bindTrackDelivery(&job) && router.backend.port.binding==&directory);
    router.mutex_=false;
    assert(router.replaceTrackDelivery(&directory,&job)==TrackBindingChange::Busy && router.backend.port.binding==&directory);
    router.mutex_=true;
    assert(router.replaceTrackDelivery(&directory,&job)==TrackBindingChange::Applied && router.backend.port.binding==&job);
    assert(router.replaceTrackDelivery(&directory,nullptr)==TrackBindingChange::Conflict && router.backend.port.binding==&job);
    assert(router.replaceTrackDelivery(&foreign,nullptr)==TrackBindingChange::Conflict && router.backend.port.binding==&job);
    assert(router.replaceTrackDelivery(&job,nullptr)==TrackBindingChange::Applied && !router.backend.port.binding);
    job.expected_local_author=author;job.expected_author_generation=9;
    assert(router.replaceTrackDelivery(nullptr,&job)==TrackBindingChange::Applied);
    uint8_t sampled_author[64];uint32_t sampled_generation=0;unsigned visits=0;
    const auto visitor=[](void* count){++*static_cast<unsigned*>(count);};
    const auto visit=[&]{return router.visitTrackDelivery(&job,sampled_author,sampled_generation,visitor,&visits);};
    assert(visit()==TrackBindingChange::Applied && visits==1 && sampled_generation==9 && !std::memcmp(sampled_author,author,64));
    router.backend.generation=10;assert(visit()==TrackBindingChange::Conflict && visits==1);router.backend.generation=9;
    router.backend.key[0]=1;assert(visit()==TrackBindingChange::Conflict && visits==1);router.backend.key[0]=0;
    router.backend.available=false;assert(visit()==TrackBindingChange::Busy && visits==1);router.backend.available=true;
    router.mutex_=false;assert(visit()==TrackBindingChange::Busy && visits==1);router.mutex_=true;
    assert(router.visitTrackDelivery(&job,author,sampled_generation,visitor,&visits)==TrackBindingChange::Conflict && visits==1);
    assert(router.visitTrackDelivery(&directory,sampled_author,sampled_generation,visitor,&visits)==TrackBindingChange::Conflict && visits==1);
    assert(router.replaceTrackDelivery(&job,nullptr)==TrackBindingChange::Applied);
}
