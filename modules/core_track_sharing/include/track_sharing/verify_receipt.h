#pragma once
#include "track_sharing/receipt.h"
namespace track_sharing
{
enum class ReceiptVerification : uint8_t { Valid, InvalidReceipt, BindingMismatch, InvalidSignature, WorkspaceTooSmall };
struct VerifiedReceiptView
{
    ReceiptView receipt;
    geocaching::ByteView signature;
    bool verified = false;
};
static_assert(sizeof(VerifiedReceiptView)<=192,"Verified receipt must borrow signed bytes");
template<class Crypto>
ReceiptVerification verifyReceipt(geocaching::ByteView signed_receipt,const PublicationJournal& job,geocaching::ByteView request,
                                  geocaching::ByteView directory_public,uint8_t state,Crypto& crypto,uint8_t* workspace,size_t capacity,
                                  VerifiedReceiptView& output)
{
    const auto* target=reinterpret_cast<const uint8_t*>(&output);
    for(const auto field:{signed_receipt,request,directory_public,geocaching::ByteView{reinterpret_cast<const uint8_t*>(&job),sizeof(job)}})
        if(signingSpansOverlap(field.data,field.size,target,sizeof(output)))return ReceiptVerification::InvalidReceipt;
    output={};
    if(!signed_receipt.data||!signed_receipt.size||signed_receipt.size>1100)return ReceiptVerification::InvalidReceipt;
    geocaching::protocol::CmpReader reader(signed_receipt);size_t fields=0;geocaching::ByteView raw,signature;
    if(!reader.array(fields,2)||fields!=2||!reader.binary(raw,1024)||!reader.binary(signature,64)||signature.size!=64||
       !reader.finished()||!decodeReceipt(raw,output.receipt))return ReceiptVerification::InvalidReceipt;
    if(!receiptMatchesJob(output.receipt,job,request,directory_public,state))return ReceiptVerification::BindingMismatch;
    constexpr char domain[]="TrailMate.Track.Receipt.v1";
    if(!workspace||capacity<sizeof(domain)+raw.size)return ReceiptVerification::WorkspaceTooSmall;
    for(const auto field:{signed_receipt,request,directory_public,geocaching::ByteView{target,sizeof(output)},geocaching::ByteView{reinterpret_cast<const uint8_t*>(&job),sizeof(job)}})
        if(signingSpansOverlap(field.data,field.size,workspace,capacity))return ReceiptVerification::WorkspaceTooSmall;
    std::memcpy(workspace,domain,sizeof(domain));std::memcpy(workspace+sizeof(domain),raw.data,raw.size);
    if(crypto.verify({directory_public.data+32,32},signature,{workspace,sizeof(domain)+raw.size})!=ReceiptVerification::Valid)return ReceiptVerification::InvalidSignature;
    output.signature=signature;output.verified=true;return ReceiptVerification::Valid;
}
} // namespace track_sharing
