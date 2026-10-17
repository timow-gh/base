#include "harness.hpp"
#include "synthetic_data.hpp"
#include "workloads.hpp"

#include "base/core/types.hpp"

#include <benchmark/benchmark.h>

#include <span>
#include <string>
#include <string_view>

namespace base::bench {

namespace {

// Growth without reserve: the arena variants leave every outgrown buffer behind, which
// shows in peak_bytes.
struct VectorPushBack {
    static constexpr std::string_view name = "vector_push_back";
    static constexpr i64 maxElements = defaultMaxElements;

    template <typename Variant>
    static void run(benchmark::State& state) {
        const auto count = static_cast<usize>(state.range(0));
        const std::span<const u64> keys = uniform_keys(count);
        run_workload<Variant>(state,
                              {.elements = count, .operations = count},
                              [keys]<typename Containers>(auto&& source, [[maybe_unused]] auto&& checkpoint) {
                                  typename Containers::template Vector<u64> values(source);
                                  for (const u64 key : keys) {
                                      values.push_back(key);
                                  }
                                  benchmark::DoNotOptimize(values.data());
                                  benchmark::ClobberMemory();
                              });
    }
};

// One allocation of the final size: isolates the allocation path from the growth.
struct VectorPushBackReserved {
    static constexpr std::string_view name = "vector_push_back_reserved";
    static constexpr i64 maxElements = defaultMaxElements;

    template <typename Variant>
    static void run(benchmark::State& state) {
        const auto count = static_cast<usize>(state.range(0));
        const std::span<const u64> keys = uniform_keys(count);
        run_workload<Variant>(state,
                              {.elements = count, .operations = count},
                              [keys]<typename Containers>(auto&& source, [[maybe_unused]] auto&& checkpoint) {
                                  typename Containers::template Vector<u64> values(source);
                                  values.reserve(keys.size());
                                  for (const u64 key : keys) {
                                      values.push_back(key);
                                  }
                                  benchmark::DoNotOptimize(values.data());
                                  benchmark::ClobberMemory();
                              });
    }
};

// Nested containers: a vector of strings, all from the same source. Short strings fit
// the small-string buffer and only the vector allocates; long strings allocate each.
template <StringLength length>
struct VectorOfStrings {
    static constexpr std::string_view name =
        length == StringLength::Short ? "vector_of_short_strings" : "vector_of_long_strings";
    static constexpr i64 maxElements = defaultMaxElements;

    template <typename Variant>
    static void run(benchmark::State& state) {
        const auto count = static_cast<usize>(state.range(0));
        const std::span<const std::string> texts = random_strings(count, length);
        run_workload<Variant>(state,
                              {.elements = count, .operations = count},
                              [texts]<typename Containers>(auto&& source, [[maybe_unused]] auto&& checkpoint) {
                                  using String = typename Containers::String;
                                  typename Containers::template Vector<String> lines(source);
                                  for (const std::string& text : texts) {
                                      // Constructed with the source and moved in: a pmr vector
                                      // would append its own allocator to emplace_back's
                                      // arguments, the other variants would not.
                                      lines.emplace_back(String(text.data(), text.size(), source));
                                  }
                                  benchmark::DoNotOptimize(lines.data());
                                  benchmark::ClobberMemory();
                              });
    }
};

} // namespace

void register_vector_benchmarks() {
    register_workload<VectorPushBack>(AllVariants{});
    register_workload<VectorPushBackReserved>(AllVariants{});
    register_workload<VectorOfStrings<StringLength::Short>>(AllVariants{});
    register_workload<VectorOfStrings<StringLength::Long>>(AllVariants{});
}

} // namespace base::bench
