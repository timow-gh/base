#include "harness.hpp"
#include "synthetic_data.hpp"
#include "workloads.hpp"

#include "base/core/types.hpp"

#include <benchmark/benchmark.h>

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace base::bench {

namespace {

struct VectorPushBack {
    using Container = VectorKind;
    static constexpr std::string_view name = "push_back";
    static constexpr std::string_view tags = "allocation";
    static constexpr std::string_view operation = "one push_back";
    static constexpr std::string_view description =
        "Appends n random u64 to an empty vector without reserve. The vector grows geometrically; the arena and "
        "pmr implementations keep every outgrown buffer, which shows in the memory chart.";
    static constexpr i64 maxElements = defaultMaxElements;

    template <typename Implementation>
    static void run(benchmark::State& state) {
        const auto count = static_cast<usize>(state.range(0));
        const std::span<const u64> keys = uniform_keys(count);
        run_workload<Implementation>(state,
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

struct VectorPushBackReserved {
    using Container = VectorKind;
    static constexpr std::string_view name = "push_back_reserved";
    static constexpr std::string_view tags = "allocation";
    static constexpr std::string_view operation = "one push_back";
    static constexpr std::string_view description =
        "The same appends after reserve(n): a single allocation, so the loop itself dominates.";
    static constexpr i64 maxElements = defaultMaxElements;

    template <typename Implementation>
    static void run(benchmark::State& state) {
        const auto count = static_cast<usize>(state.range(0));
        const std::span<const u64> keys = uniform_keys(count);
        run_workload<Implementation>(state,
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

template <StringLength length>
struct VectorOfStrings {
    using Container = VectorKind;
    static constexpr std::string_view name = length == StringLength::Short ? "short_strings" : "long_strings";
    static constexpr std::string_view tags = "allocation";
    static constexpr std::string_view operation = "one string appended";
    static constexpr std::string_view description =
        length == StringLength::Short
            ? "Copies n strings of 4 to 15 characters into a vector of strings that share the vector's allocator. "
              "They fit the small-string buffer, so only the vector allocates."
            : "Copies n strings of 32 to 128 characters into a vector of strings that share the vector's "
              "allocator: every string allocates, nested inside the vector.";
    static constexpr i64 maxElements = defaultMaxElements;

    template <typename Implementation>
    static void run(benchmark::State& state) {
        const auto count = static_cast<usize>(state.range(0));
        const std::span<const std::string> texts = random_strings(count, length);
        run_workload<Implementation>(state,
                              {.elements = count, .operations = count},
                              [texts]<typename Containers>(auto&& source, [[maybe_unused]] auto&& checkpoint) {
                                  using String = typename Containers::String;
                                  typename Containers::template Vector<String> lines(source);
                                  for (const std::string& text : texts) {
                                      // Constructed with the source and moved in: a pmr vector
                                      // would append its own allocator to emplace_back's
                                      // arguments, the other implementations would not.
                                      lines.emplace_back(String(text.data(), text.size(), source));
                                  }
                                  benchmark::DoNotOptimize(lines.data());
                                  benchmark::ClobberMemory();
                              });
    }
};

} // namespace

std::vector<BenchmarkSpec> vector_benchmarks() {
    return concat({specs_for<VectorPushBack>(AllImplementations{}),
                   specs_for<VectorPushBackReserved>(AllImplementations{}),
                   specs_for<VectorOfStrings<StringLength::Short>>(AllImplementations{}),
                   specs_for<VectorOfStrings<StringLength::Long>>(AllImplementations{})});
}

} // namespace base::bench
