// memory_info() reports what SDL can tell of the GPU's memory. The values
// differ from GPU to GPU, so this checks what holds on every one: an
// architecture and a kind from the MEMORY_ constants or 0, and bytes exactly
// when there is a kind; and that Vulkan, Direct3D 12 and Metal give an
// amount.
import gfx from "../..";
import std;
import os;

fn main() {
    guard gfx::open_headless(4, 4) else {
        write_stderr(str("gfx: ", gfx::error(), "\n"));
        return;
    }
    let m = gfx::memory_info();
    let arch_ok = m.architecture == 0 || m.architecture == gfx::MEMORY_UNIFIED || m.architecture == gfx::MEMORY_DEDICATED;
    let kind_ok = m.kind == gfx::MEMORY_BUDGET || m.kind == gfx::MEMORY_CAPACITY;
    print("architecture is unknown, unified or dedicated: ", arch_ok);
    print("an amount, as a budget or a capacity: ", kind_ok);
    print("bytes when there is an amount: ", (m.kind != 0) == (m.bytes > 0));
    print("at least 64 MB: ", m.bytes >= 64 << 20);
    gfx::check();
    gfx::close();
}
