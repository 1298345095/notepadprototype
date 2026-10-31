// Linux experiment: both UI backends perform text measurement on their UI thread.
#pragma once
#include <chrono>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <type_traits>

#define USE_STD_ASYNC_FUTURE 0
#define USE_WIN32_PTP_WORK 0

namespace Scintilla::Internal {
struct HeapPointerFreer {
    template<class T>
    void operator()(T *ptr) const noexcept { std::free(ptr); }

    template<class T> requires std::is_unbounded_array_v<T>
    static std::unique_ptr<T, HeapPointerFreer> make_unique(size_t count) noexcept {
        using Item = std::remove_extent_t<T>;
        return std::unique_ptr<T, HeapPointerFreer>{static_cast<Item *>(std::calloc(count, sizeof(Item)))};
    }
};
using NativeMutex = std::mutex;
template<class Mutex> using LockGuard = std::lock_guard<Mutex>;
}
