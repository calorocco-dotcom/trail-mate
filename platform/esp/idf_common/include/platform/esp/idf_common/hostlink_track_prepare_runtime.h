#pragma once
#include "geocaching/protocol/cmp_writer.h"
#include "platform/esp/idf_common/hostlink_track_package_install_work.h"
#include "platform/esp/idf_common/hostlink_track_package_query_work.h"
#include "platform/esp/idf_common/hostlink_track_prepare_work.h"
#include "track_sharing/package_query_request.h"
namespace hostlink::management::prepare_runtime
{
// Single application-scheduler slot. All task state lives in transient PSRAM;
// no separate CDC task, queue, response cache or resident protocol buffer.
inline PrepareSchedulerWork* work = nullptr;
inline void releasePrepare()
{
    destroySchedulerWork(work);
    work = nullptr;
}
inline Status advancePrepare(chat::IMeshAdapter* adapter, uint8_t* scratch, size_t capacity, uint64_t now, bool connected)
{
    if (!work) return Status::Expired;
    if (!work->alive(sd::sd_media_session(), now, connected))
    {
        releasePrepare();
        return Status::Expired;
    }
    if (!adapter)
    {
        work->suspendIdentity();
        return Status::Busy;
    }
    return work->advance(*adapter, scratch, capacity);
}
inline void closePrepareSession(const uint8_t* session)
{
    if (work && work->matchesSession(session)) releasePrepare();
}
inline bool prepareHoldsStorage() { return work && work->holdsStages(); }
inline Status processPrepareRequest(chat::IMeshAdapter& adapter, const EnvelopeView& request, uint64_t now, uint8_t* scratch, size_t capacity, size_t& written, package_install_runtime::PackageInstallPolicy install_policy = nullptr)
{
    written = 0;
    if (!request.session || !scratch || capacity < 448) return Status::InvalidField;
    track_sharing::PrepareRequest decoded;
    const bool querying = request.code == uint16_t(Operation::TrackPackageInstallStatus);
    if (!(querying ? track_sharing::decodePackageQueryRequest({request.body, request.body_size}, decoded) : track_sharing::decodePrepareRequest({request.body, request.body_size}, decoded))) return Status::InvalidField;
    const bool installing = request.code == uint16_t(Operation::TrackPackageInstall) || querying;
    if (installing != decoded.install_request || ((request.code == uint16_t(Operation::TrackPackageSignPrepare) || installing) != decoded.package_request) ||
        (installing && !querying && !install_policy)) return Status::InvalidField;
    auto* public_key = scratch + capacity - 64;
    for (const auto field : {decoded.operation, decoded.kml_upload, decoded.manifest_upload, decoded.expected_parent, decoded.package_upload, decoded.expected_object, decoded.expected_revision, geocaching::ByteView{request.session, 16}})
        if (track_sharing::signingSpansOverlap(field.data, field.size, public_key, 64)) return Status::InvalidField;
    uint8_t author_hash[32];
    uint32_t generation = 0;
    const auto identity = upload_runtime::readUploadAuthor(adapter, public_key, 64, author_hash, generation);
    if (identity != Status::Ok) return identity;
    if (generation != decoded.generation) return Status::StaleIdentity;
    if (work && !work->alive(sd::sd_media_session(), now, true)) releasePrepare();
    // A fresh query always rereads disk, including after a delivered success.
    if (querying && work && !work->holdsStages()) releasePrepare();
    if (work && !work->matches(request.session, decoded))
    {
        if (work->holdsStages() || !work->matchesSession(request.session)) return Status::Conflict;
        releasePrepare();
    }
    if (!work)
    {
        if (upload_runtime::owner)
        {
            if (querying) return Status::Busy;
            const auto& metadata = upload_runtime::owner->resources.metadata();
            if (upload_runtime::owner->discarding || metadata.phase != track_sharing::UploadPhase::Sealed ||
                !upload_runtime::owner->session.matches(request.session, sd::sd_media_session(), now) || metadata.generation != generation ||
                std::memcmp(metadata.author_hash, author_hash, 32) ||
                (std::memcmp(metadata.id, decoded.kml_upload.data, 16) && std::memcmp(metadata.id, decoded.manifest_upload.data, 16) &&
                 (!decoded.package_upload.data || std::memcmp(metadata.id, decoded.package_upload.data, 16)))) return Status::Busy;
            upload_runtime::destroyOwner(upload_runtime::owner);
            upload_runtime::owner = nullptr;
        }
        if (installing)
        {
            if (querying) work = package_install_runtime::createPackageQueryWork(decoded, request.session, author_hash, sd::sd_media_session(), now);
            else work = package_install_runtime::createPackageInstallWork(decoded, request.session, author_hash, sd::sd_media_session(), now, install_policy);
            if (!work) return Status::Busy;
        }
        else
        {
            auto* input = createPrepareOwner(decoded, sd::sd_media_session(), public_key, author_hash);
            if (!input) return Status::Busy;
            auto* created = createPrepareWork(input, request.session, now);
            if (!created)
            {
                destroyPrepareOwner(input);
                return Status::Busy;
            }
            const auto started = input->start(scratch, capacity);
            if (started != track_sharing::ManifestFileResult::Pending)
            {
                destroyPrepareWork(created);
                return started == track_sharing::ManifestFileResult::StorageFailure ? Status::StorageUnavailable : Status::Conflict;
            }
            work = created;
        }
    }
    // A retry can reseal uploads while this slot already holds a completed
    // result. Drop only this request's sealed owner; leave unrelated or active
    // transfers intact. The retained files and immutable result remain usable.
    if (!querying && work->result() && !work->holdsStages() && upload_runtime::owner)
    {
        const auto& metadata = upload_runtime::owner->resources.metadata();
        if (!upload_runtime::owner->discarding && metadata.phase == track_sharing::UploadPhase::Sealed &&
            upload_runtime::owner->session.matches(request.session, sd::sd_media_session(), now) && metadata.generation == generation &&
            !std::memcmp(metadata.author_hash, author_hash, 32) &&
            (!std::memcmp(metadata.id, decoded.kml_upload.data, 16) || !std::memcmp(metadata.id, decoded.manifest_upload.data, 16) ||
             (decoded.package_upload.data && !std::memcmp(metadata.id, decoded.package_upload.data, 16))))
        {
            upload_runtime::destroyOwner(upload_runtime::owner);
            upload_runtime::owner = nullptr;
        }
    }
    const auto advanced = work->advance(adapter, scratch, capacity);
    if (advanced != Status::Ok && advanced != Status::Busy) return advanced;
    if (!work->renewMatched(now)) return Status::Busy;
    const auto* result = work->result();
    if (installing)
    {
        auto* target = reinterpret_cast<char*>(scratch + capacity - 80);
        if (result && !track_sharing::routePackageTarget({result->hash, 32}, track_sharing::PackageRouteFile::Kml, target, 80)) return Status::Internal;
        geocaching::protocol::CmpWriter writer(scratch, capacity - 80);
        if (!writer.array(5) || !writer.binary(work->operationId()) || !writer.unsignedInteger(work->installationState())) return Status::Internal;
        if (result)
        {
            if (!writer.binary({result->object, 32}) || !writer.binary({result->hash, 32}) || !writer.text(target)) return Status::Internal;
        }
        else if (!writer.nil() || !writer.nil() || !writer.nil()) return Status::Internal;
        written = writer.size();
        return Status::Ok;
    }
    geocaching::protocol::CmpWriter writer(scratch, capacity);
    const bool signing = request.code == static_cast<uint16_t>(Operation::TrackSignPrepare) || request.code == uint16_t(Operation::TrackPackageSignPrepare);
    if (!writer.array(signing ? 6 : 5) || !writer.binary(work->operationId()) || !writer.unsignedInteger(result ? 1 : 0)) return Status::Internal;
    if (result)
    {
        if (!writer.binary({result->object, 32}) || !writer.binary({result->hash, 32}) || !writer.unsignedInteger(result->revision)) return Status::Internal;
    }
    else if (!writer.nil() || !writer.nil() || !writer.unsignedInteger(0)) return Status::Internal;
    if (signing && !(result ? writer.binary(work->preparedSignature()) : writer.nil())) return Status::Internal;
    written = writer.size();
    return Status::Ok;
}
} // namespace hostlink::management::prepare_runtime
