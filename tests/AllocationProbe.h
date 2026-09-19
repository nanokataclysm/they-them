#pragma once

#include <algorithm>
#include <cstdlib>
#include <new>

// Count C++ heap use only on the processing thread. Test-fixture construction,
// diagnostic formatting, and timing-vector growth happen outside this region.
namespace allocationProbe {
thread_local bool active = false;
thread_local std::size_t allocations = 0;
thread_local std::size_t deallocations = 0;
void* allocate(std::size_t size, std::size_t alignment = 0) {
    if (active) ++allocations;
    void* result = nullptr;
    if (alignment != 0) {
        if (posix_memalign(&result, alignment, std::max<std::size_t>(size, 1)) != 0)
            result = nullptr;
    } else {
        result = std::malloc(std::max<std::size_t>(size, 1));
    }
    if (!result) throw std::bad_alloc();
    return result;
}
void release(void* pointer) noexcept {
    if (active && pointer) ++deallocations;
    std::free(pointer);
}
struct Scope {
    Scope() { allocations = deallocations = 0; active = true; }
    ~Scope() { active = false; }
};
}
void* operator new(std::size_t n) { return allocationProbe::allocate(n); }
void* operator new[](std::size_t n) { return allocationProbe::allocate(n); }
void operator delete(void* p) noexcept { allocationProbe::release(p); }
void operator delete[](void* p) noexcept { allocationProbe::release(p); }
void operator delete(void* p, std::size_t) noexcept { allocationProbe::release(p); }
void operator delete[](void* p, std::size_t) noexcept { allocationProbe::release(p); }
void* operator new(std::size_t n, std::align_val_t a) { return allocationProbe::allocate(n, static_cast<std::size_t>(a)); }
void* operator new[](std::size_t n, std::align_val_t a) { return allocationProbe::allocate(n, static_cast<std::size_t>(a)); }
void operator delete(void* p, std::align_val_t) noexcept { allocationProbe::release(p); }
void operator delete[](void* p, std::align_val_t) noexcept { allocationProbe::release(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { allocationProbe::release(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { allocationProbe::release(p); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept { try { return ::operator new(n); } catch (...) { return nullptr; } }
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { try { return ::operator new[](n); } catch (...) { return nullptr; } }
void operator delete(void* p, const std::nothrow_t&) noexcept { allocationProbe::release(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { allocationProbe::release(p); }
void* operator new(std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept { try { return ::operator new(n, a); } catch (...) { return nullptr; } }
void* operator new[](std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept { try { return ::operator new[](n, a); } catch (...) { return nullptr; } }
void operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept { allocationProbe::release(p); }
void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept { allocationProbe::release(p); }
