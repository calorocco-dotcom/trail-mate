#pragma once
#include "geocaching/protocol/publication_receipt.h"

namespace geocaching::storage
{
struct PublicationConfirmationView
{
    ByteView raw, signature, public_key;
};
// Fits the existing outgoing terminal_data. No new table or shard bitmap.
inline bool decodePublicationConfirmation(ByteView encoded, PublicationConfirmationView& output)
{
    if(encoded.data){const auto a=reinterpret_cast<uintptr_t>(encoded.data),b=reinterpret_cast<uintptr_t>(&output);if(a<=b?b-a<encoded.size:a-b<sizeof(output))return false;}
    output = {};
    protocol::CmpReader reader(encoded);
    size_t fields = 0;
    uint64_t version = 0;
    PublicationConfirmationView value;
    if (!encoded.data || !encoded.size || encoded.size > 384 || !reader.array(fields,4) || fields != 4 ||
        !reader.unsignedInteger(version) || version != 1 || !reader.binary(value.raw,192) || !value.raw.size ||
        !reader.binary(value.signature,64) || value.signature.size != 64 || !reader.binary(value.public_key,64) || value.public_key.size != 64 || !reader.finished()) return false;
    output = value;
    return true;
}
inline bool encodePublicationConfirmation(const PublicationConfirmationView& input, uint8_t* output, size_t capacity, size_t& written)
{
    written = 0;
    if (!output || !input.raw.data || !input.raw.size || input.raw.size > 192 || !input.signature.data || input.signature.size != 64 ||
        !input.public_key.data || input.public_key.size != 64) return false;
    const ByteView views[] = {input.raw,input.signature,input.public_key};
    for(const auto view:views){const auto a=reinterpret_cast<uintptr_t>(output),b=reinterpret_cast<uintptr_t>(view.data);if(a<=b?b-a<capacity:a-b<view.size)return false;}
    protocol::CmpWriter writer(output,capacity<384?capacity:384);
    if(!writer.array(4)||!writer.unsignedInteger(1)||!writer.binary(input.raw)||!writer.binary(input.signature)||!writer.binary(input.public_key))return false;
    written=writer.size();return true;
}
// Recovery must verify this proof against the original verified publication,
// not interpret a stored terminal state alone as directory confirmation.
inline protocol::VerificationResult verifyPublicationConfirmation(ByteView encoded, ByteView directory,
                                                                  const protocol::VerifiedRecordView& content, protocol::RecordCrypto& crypto,
                                                                  uint8_t* workspace, size_t capacity, protocol::PublicationReceiptView& output)
{
    if(encoded.data){const auto a=reinterpret_cast<uintptr_t>(encoded.data),b=reinterpret_cast<uintptr_t>(&output);if(a<=b?b-a<encoded.size:a-b<sizeof(output))return protocol::VerificationResult::InvalidRecord;}
    output={};
    PublicationConfirmationView proof;
    if(!decodePublicationConfirmation(encoded,proof))return protocol::VerificationResult::InvalidRecord;
    protocol::CmpReader raw(proof.raw);
    size_t fields=0;uint64_t version=0;ByteView destination,epoch;
    if(!raw.array(fields,8)||fields!=8||!raw.unsignedInteger(version)||version!=1||!raw.binary(destination,16)||destination.size!=16||
       !raw.binary(epoch,16)||epoch.size!=16)return protocol::VerificationResult::InvalidRecord;
    return protocol::verifyPublicationReceipt(proof.raw,proof.signature,proof.public_key,directory,epoch,content,crypto,workspace,capacity,output);
}
} // namespace geocaching::storage
