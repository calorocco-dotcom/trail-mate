#include "track_sharing/revision_reservation.h"
#include <cassert>
int main()
{
    using namespace track_sharing;
    RevisionReservation value, decoded;
    value.operation[0] = 1;
    value.object[0] = 2;
    value.author[0] = 3;
    value.candidate[0] = 4;
    value.generation = 7;
    value.revision = 1;
    value.sequence = 1;
    value.phase = ReservationPhase::Reserved;
    decoded = value;
    decoded.sequence = 2;
    decoded.phase = ReservationPhase::Checked;
    assert(reservationAdvanceValid(value, decoded));
    decoded.phase = ReservationPhase::Signed;
    assert(!reservationAdvanceValid(value, decoded));
    decoded.phase = ReservationPhase::Checked;
    decoded.candidate[31] ^= 1;
    assert(!reservationAdvanceValid(value, decoded));
    decoded.candidate[31] ^= 1;
    decoded.generation++;
    assert(!reservationAdvanceValid(value, decoded));
    decoded.generation--;
    value.phase = ReservationPhase::Signed;
    decoded.phase = ReservationPhase::Cancelled;
    assert(!reservationAdvanceValid(value, decoded));
    decoded.phase = ReservationPhase::Committed;
    assert(reservationAdvanceValid(value, decoded));
    value.phase = ReservationPhase::Committed;
    decoded.phase = ReservationPhase::Reserved;
    assert(!reservationAdvanceValid(value, decoded));
    value.sequence = UINT64_MAX;
    decoded.sequence = 1;
    decoded.phase = ReservationPhase::Committed;
    assert(!reservationAdvanceValid(value, decoded));
    value.phase = ReservationPhase::Reserved;
    value.sequence = 0x100000001ULL;
    uint8_t raw[kReservationBytes];
    assert(encodeReservation(value, raw, sizeof(raw)) && decodeReservation(raw, sizeof(raw), decoded));
    assert(decoded.sequence == value.sequence && decoded.revision == 1 && decoded.generation == 7);
    for (unsigned i = 0; i < sizeof(raw); ++i)
    {
        raw[i] ^= 1;
        assert(!decodeReservation(raw, sizeof(raw), decoded) && !decoded.sequence);
        raw[i] ^= 1;
    }
    value.parent[0] = 1;
    assert(!encodeReservation(value, raw, sizeof(raw)));
    value.revision = 2;
    for (unsigned phase = 0; phase <= 4; ++phase)
    {
        value.phase = static_cast<ReservationPhase>(phase);
        assert(encodeReservation(value, raw, sizeof(raw)) && decodeReservation(raw, sizeof(raw), decoded) && decoded.phase == value.phase);
    }
    value.phase = static_cast<ReservationPhase>(5);
    assert(!encodeReservation(value, raw, sizeof(raw)));
    value.phase = ReservationPhase::Reserved;
    value.generation = 0;
    assert(!encodeReservation(value, raw, sizeof(raw)));
    value.generation = 7;
    assert(!encodeReservation(value, reinterpret_cast<uint8_t*>(&value), sizeof(value)));
    assert(!decodeReservation(raw, sizeof(raw) - 1, decoded));
}
