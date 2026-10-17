#pragma once

#include "registry.hpp"

#include <vector>

namespace base::bench {

// The benchmarks of each workload file, one spec per workload and implementation.
[[nodiscard]] std::vector<BenchmarkSpec> vector_benchmarks();
[[nodiscard]] std::vector<BenchmarkSpec> string_benchmarks();
[[nodiscard]] std::vector<BenchmarkSpec> map_benchmarks();
[[nodiscard]] std::vector<BenchmarkSpec> unordered_benchmarks();
[[nodiscard]] std::vector<BenchmarkSpec> locality_benchmarks();
[[nodiscard]] std::vector<BenchmarkSpec> churn_benchmarks();

} // namespace base::bench
