#include "track_sharing/upload_space_budget.h"
#include <cassert>
int main()
{
    using track_sharing::UploadKind;
    using track_sharing::UploadSpaceBudget;
    UploadSpaceBudget resources(1, UploadKind::AttachmentResource);
    for (unsigned i = 0; i < 189; ++i) assert(resources.add(0, false));
    assert(resources.complete() && !resources.add(0, false));
    UploadSpaceBudget resource_bytes(1, UploadKind::AttachmentResource);
    assert(resource_bytes.add(64ULL * 1024 * 1024 + 63 * 216 - 1, false));
    assert(!resource_bytes.add(1, false));
    UploadSpaceBudget index(294912, UploadKind::AttachmentIndex), index_large(294913, UploadKind::AttachmentIndex);
    assert(index.add(294912, false) && index.add(108, false) && index.add(108, false) && !index.add(0, false));
    assert(!index_large.complete());
    UploadSpaceBudget signed_manifest(4166,UploadKind::SignedManifest),signed_large(4167,UploadKind::SignedManifest);
    assert(signed_manifest.add(4166,false)&&signed_manifest.add(108,false)&&signed_manifest.add(108,false)&&!signed_manifest.add(0,false));
    assert(!signed_large.complete());
    UploadSpaceBudget full(8388608);
    assert(full.add(8388608, false) && full.add(108, false) && full.add(108, false) && full.complete());
    assert(!full.add(0, false) && !full.complete());
    UploadSpaceBudget excess(8388608);
    assert(!excess.add(8388608 + 217, false));
    UploadSpaceBudget directory(1);
    assert(!directory.add(0, true));
    UploadSpaceBudget zero(0), large(8388609);
    assert(!zero.complete() && !large.complete());
    UploadSpaceBudget overflow(1);
    assert(!overflow.add(UINT64_MAX, false));
    UploadSpaceBudget manifest(4096, 4096);
    assert(manifest.add(4096, false) && manifest.add(108, false) && manifest.add(108, false) && manifest.complete());
    assert(!manifest.add(0, false));
    UploadSpaceBudget manifest_excess(4096, 4096), invalid_limit(1, 4097), manifest_large(4097, 4096);
    assert(!manifest_excess.add(4096 + 217, false));
    assert(!invalid_limit.complete() && !manifest_large.complete());
    UploadSpaceBudget receipt(1100, 1100), receipt_large(1101, 1100);
    assert(receipt.add(1100, false) && receipt.add(108, false) && receipt.add(108, false) && receipt.complete());
    assert(!receipt.add(0, false) && !receipt_large.complete());
}
