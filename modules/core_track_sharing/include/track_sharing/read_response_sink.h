#pragma once
#include "track_sharing/network_read_response.h"
#include "track_sharing/read_response_store.h"

namespace track_sharing
{
struct StoredReadResponse
{
    uint8_t digest[32]{};
    uint16_t bytes = 0, status = 0;
    uint8_t operation = 0;
    bool ready = false;
};
static_assert(sizeof(StoredReadResponse) <= 40, "Stored response retains digest and metadata only");

// Validator validates the complete body, including any signature/reference
// binding. Store owns exclusive media-bound paths; Hash borrows its admitted
// stream context. Caller lends scratch and output, normally from a PSRAM owner.
template <class Store, class Validator, class Hash>
class ReadResponseSink
{
  public:
    ReadResponseSink(Store& store, Validator& validator, Hash& hash, StoredReadResponse& output,
                     uint8_t* scratch, size_t capacity)
        : store_(store), validator_(validator), hash_(hash), output_(output), scratch_(scratch), capacity_(capacity) {}
    bool validateAndStore(uint8_t operation, geocaching::ByteView request,
                          const ReadResponseView& envelope, geocaching::ByteView raw)
    {
        if (!raw.data || !raw.size || raw.size > 8192 || !scratch_ || capacity_ < 128 ||
            !networkInputDisjoint(raw, reinterpret_cast<uint8_t*>(&output_), sizeof(output_)) ||
            !networkInputDisjoint(raw, reinterpret_cast<uint8_t*>(this), sizeof(*this)) ||
            !networkInputDisjoint({scratch_, capacity_}, reinterpret_cast<uint8_t*>(&output_), sizeof(output_)) ||
            !networkInputDisjoint({scratch_, capacity_}, reinterpret_cast<uint8_t*>(this), sizeof(*this))) return false;
        output_.ready = false;
        if (!validator_.validate(operation, request, envelope, raw) ||
            store_.persist(raw, scratch_, capacity_) != ReadStoreResult::Ready) return false;
        // The digest remains in trusted owner metadata, not in the SD file.
        // USB verifies the entire cached response against these original bytes.
        if (!hash_.start() || !hash_.update(raw.data, raw.size) || !hash_.finish(output_.digest))
        {
            hash_.reset();
            return false;
        }
        output_.bytes = uint16_t(raw.size);
        output_.status = envelope.status;
        output_.operation = operation;
        output_.ready = true;
        return true;
    }

  private:
    Store& store_;
    Validator& validator_;
    Hash& hash_;
    StoredReadResponse& output_;
    uint8_t* scratch_;
    size_t capacity_;
};
} // namespace track_sharing
