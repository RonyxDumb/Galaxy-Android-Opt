#include "gx_recorder_internal.h"

namespace gpu {

void Recorder::snapshotEfbCopies() {
    // Retain addresses already referenced by this frame, even if the CPU
    // overwrote their backing memory after an earlier recorded draw.
    // A GPU-only copy has no equivalent pixels in host RAM. Age is not an
    // invalidation condition: effects may reuse a capture many frames later.
    for (auto it = efbCopies.begin(); it != efbCopies.end();) {
        if (hashBytes(memPtr(it->first), 64) != it->second.memHash) {
            it = efbCopies.erase(it);
            texDirty = true;
            bindEpoch++;
        } else {
            frame->liveEfbCopies.push_back(it->first);
            ++it;
        }
    }
}

}  // namespace gpu
