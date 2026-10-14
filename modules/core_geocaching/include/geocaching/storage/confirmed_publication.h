#pragma once
#include "geocaching/protocol/publish_request.h"
#include "geocaching/protocol/publish_response.h"
#include "geocaching/storage/publication_confirmation.h"

namespace geocaching::storage
{
// Borrow the existing verification lease. Output is usable only at Valid;
// the original signed cache and independent directory proof are both checked.
inline protocol::VerificationResult verifyConfirmedPublication(ByteView request_key, ByteView request_bytes, ByteView response_bytes,
                                                               protocol::RecordCrypto& crypto, uint8_t* workspace, size_t capacity,
                                                               protocol::VerifiedRecordView& content)
{
    if(!request_key.data||request_key.size!=48)return protocol::VerificationResult::InvalidRecord;
    RequestId request;
    std::memcpy(request.bytes.data(),request_key.data+32,16);
    protocol::PublishRequestView publication;
    if(!protocol::decodePublishRequest(request_bytes,request,publication))return protocol::VerificationResult::InvalidRecord;
    auto result=protocol::verifyGeocache(publication.signed_cache,crypto,workspace,capacity,content);
    if(result!=protocol::VerificationResult::Valid)return result;
    PublicationConfirmationView proof;
    if(decodePublicationConfirmation(response_bytes,proof))
    {
        protocol::PublicationReceiptView receipt;
        result=verifyPublicationConfirmation(response_bytes,{request_key.data+16,16},content,crypto,workspace,capacity,receipt);
    }
    else
    {
        protocol::PublishDisposition disposition;
        result=protocol::decodePublishResponse(response_bytes,request,content.id,content.hash,content.record.revision,content.record.state,disposition)
                   ?protocol::VerificationResult::Valid:protocol::VerificationResult::InvalidRecord;
    }
    if(result!=protocol::VerificationResult::Valid)content={};
    return result;
}
} // namespace geocaching::storage
