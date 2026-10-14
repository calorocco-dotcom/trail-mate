#pragma once
#include "chat/infra/meshcore/crypto/ed25519/ed_25519.h"
#include "track_sharing/verify_receipt.h"
namespace platform::esp::storage
{
struct TrackReceiptCrypto
{
    track_sharing::ReceiptVerification verify(geocaching::ByteView key,geocaching::ByteView signature,geocaching::ByteView message) const
    {
        constexpr char domain[]="TrailMate.Track.Receipt.v1";
        if(!key.data||key.size!=32||!signature.data||signature.size!=64||!message.data||message.size<=sizeof(domain)||
           message.size>sizeof(domain)+1024||std::memcmp(message.data,domain,sizeof(domain)))return track_sharing::ReceiptVerification::InvalidSignature;
        return ed25519_verify(signature.data,message.data,message.size,key.data)?track_sharing::ReceiptVerification::Valid:track_sharing::ReceiptVerification::InvalidSignature;
    }
};
} // namespace platform::esp::storage
