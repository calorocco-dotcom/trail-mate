// Opt-in computer-directory transfer. Legacy and USB-facing paths stay 1 KiB.
// Requests and replies still fit the existing 8192-byte CMP1 envelope.
export const TRACK_BULK_OPERATION=19;
export const TRACK_BULK_CHUNK_BYTES=4096;
