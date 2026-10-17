#pragma once

namespace base::bench {

void register_vector_benchmarks();
void register_string_benchmarks();
void register_map_benchmarks();
void register_unordered_benchmarks();
void register_locality_benchmarks();
void register_churn_benchmarks();

} // namespace base::bench
