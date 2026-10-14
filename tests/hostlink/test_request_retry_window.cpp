#include "track_sharing/request_retry_window.h"
#include <cassert>
#include <cstring>
struct Receiver
{
    bool waiting = false;
    unsigned cancelled = 0;
    bool pending() const { return waiting; }
    void cancelPending()
    {
        waiting = false;
        ++cancelled;
    }
};
int main()
{
    using namespace track_sharing;
    RequestRetryWindow window;
    Receiver receiver;
    uint8_t request[16];
    std::memset(request, 3, 16);
    assert(window.poll(receiver, 0) == ExchangePoll::Invalid);
    assert(!window.begin({request, 16}, 100, 0, 3, 2));
    assert(!window.begin({request, 16}, 100, 10, 0, 2));
    assert(!window.begin({request, 16}, 100, 10, 3, 0));
    assert(window.begin({request, 16}, 100, 10, 3, 2));
    request[0] = 9;
    assert(window.request().data[0] == 3);
    assert(!window.begin({request, 16}, 100, 10, 3, 2));
    assert(!window.sent(true, 100));
    assert(window.poll(receiver, 99) == ExchangePoll::Invalid);
    assert(window.poll(receiver, 100) == ExchangePoll::Ready);
    receiver.waiting = true;
    assert(window.sent(true, 100));
    assert(window.poll(receiver, 109) == ExchangePoll::Waiting && !receiver.cancelled);
    assert(window.poll(receiver, 110) == ExchangePoll::Waiting && !receiver.waiting && receiver.cancelled == 1);
    assert(window.poll(receiver, 112) == ExchangePoll::Waiting);
    assert(window.poll(receiver, 113) == ExchangePoll::Ready && window.request().data[0] == 3);
    receiver.waiting = true;
    assert(window.sent(true, 113));
    assert(window.poll(receiver, 123) == ExchangePoll::Exhausted && !receiver.waiting && receiver.cancelled == 2);
    assert(!window.sent(true, 124));
    assert(window.resume(124) && window.attempts() == 0 && window.request().data[0] == 3);
    assert(window.poll(receiver, 124) == ExchangePoll::Ready);
    assert(window.sent(false, 124));
    assert(window.poll(receiver, 126) == ExchangePoll::Waiting);
    assert(window.poll(receiver, 127) == ExchangePoll::Ready);
    receiver.waiting = true;
    assert(window.sent(true, 127));
    window.pause(receiver);
    assert(!receiver.waiting && window.poll(receiver, 150) == ExchangePoll::Paused);
    assert(!window.resume(149));
    assert(window.resume(151) && window.poll(receiver, 151) == ExchangePoll::Ready);
    receiver.waiting = true;
    assert(window.sent(true, 151));
    receiver.waiting = false; // Verified receipt has been durably consumed.
    assert(window.poll(receiver, 152) == ExchangePoll::Completed && !window.request().size);
    assert(window.poll(receiver, 153) == ExchangePoll::Invalid);
    assert(window.begin({request, 16}, 154, 10, 3, 2));
    assert(window.request().data[0] == 9 && !window.attempts());
    assert(window.poll(receiver,154) == ExchangePoll::Ready);
    receiver.waiting = true;
    assert(window.sent(true,154));
    receiver.waiting = false;
    window.pause(receiver);
    assert(!window.request().size && window.poll(receiver,155) == ExchangePoll::Invalid);
    assert(window.begin({request,16},200,10,3,3) && window.poll(receiver,200)==ExchangePoll::Ready);
    receiver.waiting=true;assert(window.sent(true,200));
    assert(window.dispatchRejected(receiver,200) && !receiver.waiting && window.attempts()==1 && window.request().data[0]==9);
    assert(window.poll(receiver,202)==ExchangePoll::Waiting && window.poll(receiver,203)==ExchangePoll::Ready);
    receiver.waiting=true;assert(window.sent(true,203));
    receiver.waiting=false;
    assert(window.dispatchRejected(receiver,203) && !window.request().size);
}
