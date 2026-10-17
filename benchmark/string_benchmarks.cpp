#include "harness.hpp"
#include "workloads.hpp"

#include "base/core/types.hpp"

#include <benchmark/benchmark.h>

#include <string_view>
#include <vector>

namespace base::bench {

namespace {

struct StringAppend {
    using Container = StringKind;
    static constexpr std::string_view name = "append";
    static constexpr std::string_view tags = "allocation";
    static constexpr std::string_view operation = "one 16-byte append";
    static constexpr std::string_view description =
        "Builds an n-byte string from 16-byte appends, like a log line or generated text: geometric growth like a "
        "vector, with the small-string buffer in front.";
    static constexpr i64 maxElements = defaultMaxElements;
    static constexpr std::string_view chunk = "0123456789abcdef";

    template <typename Implementation>
    static void run(benchmark::State& state) {
        const auto length = static_cast<usize>(state.range(0));
        const usize appends = length / chunk.size();
        run_workload<Implementation>(state,
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

std::vector<BenchmarkSpec> string_benchmarks() {
    return specs_for<StringAppend>(AllImplementations{});
}

} // namespace base::bench
