#pragma once

#include "hostlink/publication_ticket.h"
#include "platform/esp/arduino_common/chat/infra/store/fixed_slot_journal.h"
#include "platform/esp/arduino_common/storage/sd_card_runtime.h"

namespace hostlink::management
{
constexpr const char* kPublicationTicketPath = "/trailmate/geocaching.web.publish";
constexpr const char* kPublicationTicketNew = "/trailmate/geocaching.web.publish.new";
constexpr const char* kPublicationTicketBackup = "/trailmate/geocaching.web.publish.bak";

// Storage primitives only: PREPARE/COMMIT owns admission and phase transitions.
// No retained RAM; scratch belongs to the command owner and must be at least 231 bytes.
inline bool loadPublicationTicket(uint8_t* scratch, size_t capacity)
{
    if (!scratch || capacity < kPublicationTicketBytes ||
        !chat::storage::v2::recoverAtomicFile(kPublicationTicketPath, kPublicationTicketNew, kPublicationTicketBackup)) return false;
    platform::esp::arduino_common::storage::SdRuntimeFile file;
    return file.open(kPublicationTicketPath, "r") && file.size() == kPublicationTicketBytes &&
           file.read(scratch, kPublicationTicketBytes) == static_cast<int>(kPublicationTicketBytes) &&
           validatePublicationTicket(scratch, kPublicationTicketBytes);
}

inline bool savePublicationTicket(const uint8_t* bytes, size_t size)
{
    if (!validatePublicationTicket(bytes, size)) return false;
    platform::esp::arduino_common::storage::SdRuntimeFile file;
    if (!file.open(kPublicationTicketNew, "w") || file.write(bytes, size) != size || !file.flush()) return false;
    file.close();
    return chat::storage::v2::replaceFileAtomically(kPublicationTicketNew, kPublicationTicketPath, kPublicationTicketBackup);
}
} // namespace hostlink::management
