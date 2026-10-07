"""The copies that read the GPU's shared-memory buffer back to the game must come after the barrier that
makes a compute shader's write visible to a transfer. `Use(kRead)` only queues that barrier; this checks
the core's source for `SubmitBarriers` between it and each `CmdVkCopyBuffer` out of the shared memory.
(A software Vulkan cannot show the defect: it runs the commands in order. The console's GPU does not.)"""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
source = (root / ".deps/xenia-canary/src/xenia/gpu/vulkan/vulkan_command_processor.cc").read_text(encoding="utf-8")
copies = [m.start() for m in re.finditer(r"CmdVkCopyBuffer\(\s*shared_memory_(?:->buffer\(\)|buffer)", source)]
assert len(copies) == 2, f"expected the resolve and the memory-export readback copies, found {len(copies)}"
for at in copies:
    used = source.rfind("shared_memory_->Use(VulkanSharedMemory::Usage::kRead);", 0, at)
    assert used >= 0, "a readback copy without Use(kRead)"
    between = source[used:at]
    line = source.count("\n", 0, at) + 1
    assert "SubmitBarriers(" in between, f"line {line}: the barrier is still pending when the copy is recorded"
    # Nothing that writes the buffer may sit between the submitted barrier and the copy.
    after = between[between.index("SubmitBarriers("):]
    assert "CmdVkDispatch" not in after and "Usage::kComputeWrite" not in after, f"line {line}: a write after the barrier"
print("PASS: both readback copies are recorded after their barrier is submitted")
