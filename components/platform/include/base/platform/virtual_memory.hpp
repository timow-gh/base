#pragma once

#include <cstddef>

/**
 * \brief Thin wrappers over the operating system's virtual memory API.
 *
 * Memory is first *reserved* (address space only, no physical memory, any access
 * faults), then *committed* in page multiples before it is used. Freshly committed
 * pages read as zero, also after a decommit/commit cycle.
 */
namespace base::platform {

/**
 * \brief The size of a virtual memory page, e.g. 4 KiB. Commit and decommit ranges
 * must be multiples of it.
 */
[[nodiscard]] std::size_t page_size() noexcept;

/**
 * \brief The granularity of reservations: 64 KiB on Windows, the page size elsewhere.
 * Reserved ranges start at and have sizes that are multiples of it.
 */
[[nodiscard]] std::size_t allocation_granularity() noexcept;

/**
 * \brief Reserves `size` bytes of address space without committing memory.
 * \param size A non-zero multiple of allocation_granularity().
 * \return The start of the range, aligned to allocation_granularity(), or nullptr
 * if the address space is exhausted.
 */
[[nodiscard]] void* reserve(std::size_t size) noexcept;

/**
 * \brief Makes `[address, address + size)` of a reserved range readable and writable.
 * \param address Page aligned, inside a range returned by reserve().
 * \param size A non-zero multiple of page_size().
 * \return false if the system is out of memory (commit limit reached).
 */
[[nodiscard]] bool commit(void* address, std::size_t size) noexcept;

/**
 * \brief Returns the physical memory of a committed range to the system. The range
 * stays reserved and faults on access until it is committed again.
 * \param address Page aligned. \param size A non-zero multiple of page_size().
 */
void decommit(void* address, std::size_t size) noexcept;

/**
 * \brief Releases a whole range returned by reserve(), committed or not.
 * \param address The pointer returned by reserve(). \param size The size passed to reserve().
 */
void release(void* address, std::size_t size) noexcept;

} // namespace base::platform
