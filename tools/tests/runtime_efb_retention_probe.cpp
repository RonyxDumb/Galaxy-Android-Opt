#include "gx/gx_recorder_internal.h"
#include <cassert>

static gpu::u8 memory[4096]{};
namespace gpu {
Recorder::Recorder() = default;
TexImage::~TexImage() = default;
void Recorder::draw(int, int, u32, const u8*, u32) {}
void Recorder::bpWrite(u32, u32, u32) {}
void Recorder::xfWrite(u32, u32) {}
void Recorder::cpWrite(u32, u32) {}
void Recorder::efbCopy(bool, bool) {}
void Recorder::loadTlut(u32) {}
bool Recorder::displayListBegin(u32, const u8*, u32) { return false; }
void Recorder::displayListEnd() {}
const u8* memPtr(u32 address) { assert(address < sizeof(memory) - 64); return memory + address; }
u64 hashRange(const u8* p, size_t n) {
    u64 h = 0; for (size_t i = 0; i < n; i++) h = (h * 31) + p[i]; return h;
}
}

int main() {
    gpu::Recorder recorder;
    const gpu::u32 address = 128;
    recorder.efbCopies[address] = {1, 16, 16, 6, 1, gpu::hashBytes(memory + address, 64)};
    for (int frame = 1; frame < 1000; frame++) {
        recorder.frameNumber = frame;
        recorder.snapshotEfbCopies();
        assert(recorder.efbCopies.size() == 1 && recorder.frame->liveEfbCopies.size() == 1);
        recorder.frame->clear(); assert(recorder.frame->liveEfbCopies.empty());
    }
    // A draw already recorded in this frame still needs its image even if
    // a subsequent CPU write invalidates the address for future frames.
    recorder.frame->liveEfbCopies.push_back(address);
    recorder.texDirty = false;
    auto epoch = recorder.bindEpoch;
    memory[address] = 42;
    recorder.snapshotEfbCopies();
    assert(recorder.efbCopies.empty() && recorder.texDirty && recorder.bindEpoch > epoch);
    assert(recorder.frame->liveEfbCopies.size() == 1 && recorder.frame->liveEfbCopies[0] == address);
    recorder.frame->clear(); recorder.snapshotEfbCopies();
    assert(recorder.frame->liveEfbCopies.empty());
}
