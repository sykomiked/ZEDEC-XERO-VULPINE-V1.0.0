// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// lockstep.cpp — deterministic lockstep for game engines (Unreal, Unity
// native plugins, custom C++ engines).
//
// In a lockstep game every peer runs the same simulation from the same
// inputs, exchanging only inputs. A companion AI fits in that model only
// if its inference is bit-identical everywhere. The ZXV engine runs in
// integer arithmetic, so the same model, seed and input text give the same
// tokens on every machine, OS and CPU. This program runs two independent
// "peers" frame by frame and checks that their companion lines match.
//
// Usage: zxv_lockstep <fixtures dir>   (tiny-qwen2.gguf, tiny-tokenizer.gguf)
#include <zxv.hpp>

#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

namespace {

/// One peer: its own context, capability, model, session and phase clock.
struct Peer {
    zxv::Context ctx;
    zxv::Capability cap = ctx.root_capability(zxv::caps::ai_inference | zxv::caps::phase | zxv::caps::budget);
    zxv::Model model;
    zxv::Session session;
    zxv::PhaseClock clock{cap};

    explicit Peer(const std::string& dir)
        : model(zxv::Model::open(cap, dir + "/tiny-qwen2.gguf", dir + "/tiny-tokenizer.gguf")),
          session(model.new_session(128))
    {
    }

    /// One simulation frame: advance the logical clock by one frame's worth
    /// of ticks, then let the companion react to this frame's inputs.
    std::vector<std::int32_t> step(std::uint64_t frame, const std::string& inputs)
    {
        clock.advance(zxv::phase_cycle / 60); // 60 frames per fundamental (462 ticks)
        zxv::GenerateOptions o;
        o.max_tokens = 6;
        o.temperature = zxv::Rational::of(7, 10); // exact 0.7, never a float
        o.top_k = 40;
        o.seed = 0xC0FFEEull ^ (frame * 0x9E3779B97F4A7C15ull); // derived from the frame, not the wall clock
        session.reset(); // each frame is a pure function of (frame, inputs)
        return session.generate("frame " + std::to_string(frame) + " inputs: " + inputs, o).tokens;
    }
};

std::uint64_t fold(std::uint64_t h, const std::vector<std::int32_t>& v)
{
    for (std::int32_t x : v) h = (h ^ static_cast<std::uint32_t>(x)) * 0x100000001B3ull; // FNV-1a style
    return h;
}

} // namespace

int main(int argc, char** argv)
{
    const std::string dir = argc > 1 ? argv[1] : "../conformance/fixtures";
    if (!zxv::has_feature(zxv::feature::tensor)) {
        std::cout << "libzxv was built without the engine\n";
        return 0;
    }
    Peer a(dir), b(dir);
    const char* inputs[] = {"move north", "jump", "open door", "move north", "talk to companion"};
    std::uint64_t ha = 0xCBF29CE484222325ull, hb = ha;
    for (std::uint64_t frame = 0; frame < 5; ++frame) {
        const auto ta = a.step(frame, inputs[frame]);
        const auto tb = b.step(frame, inputs[frame]);
        ha = fold(ha, ta);
        hb = fold(hb, tb);
        std::printf("frame %llu tick %llu: %zu tokens, %s\n", static_cast<unsigned long long>(frame),
                    static_cast<unsigned long long>(a.clock.now()), ta.size(), ta == tb ? "in sync" : "DESYNC");
        if (ta != tb) return 1;
    }
    std::printf("state checksum peer A %016llx, peer B %016llx\n", static_cast<unsigned long long>(ha),
                static_cast<unsigned long long>(hb));
    return ha == hb ? 0 : 1;
}
