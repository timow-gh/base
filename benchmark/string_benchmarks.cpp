#include "harness.hpp"
#include "workloads.hpp"

#include "base/core/types.hpp"

#include <benchmark/benchmark.h>

#include <string_view>

namespace base::bench {

namespace {

// A string built from small appends, e.g. a log line or generated code: geometric
// growth like a vector, with the small-string buffer in front.
struct StringAppend {
    static constexpr std::string_view name = "string_append";
    static constexpr i64 maxElements = defaultMaxElements;
    static constexpr std::string_view chunk = "0123456789abcdef";

    template <typename Variant>
    static void run(benchmark::State& state) {
        const auto length = static_cast<usize>(state.range(0));
        const usize appends = length / chunk.size();
        run_workload<Variant>(state,
                              {.elements = length, .operations = appends},
                              [appends]<typename Containers>(auto&& source, [[maybe_unused]] auto&& checkpoint) {
                                  typename Containers::String text(source);
                                  for (usize i = 0; i < appends; ++i) {
                                      text.append(chunk);
                                  }
                                  benchmark::DoNotOptimize(text.data());
                                  benchmark::ClobberMemory();
                              });
    }
};

} // namespace

void register_string_benchmarks() {
    register_workload<StringAppend>(AllVariants{});
}

} // namespace base::bench
