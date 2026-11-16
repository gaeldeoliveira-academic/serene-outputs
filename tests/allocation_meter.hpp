// Test-only global allocation overrides used by capacity, document, bundle, and extension checks.
#pragma once
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>
// Test-only requested live C++ heap accounting; excludes allocator bookkeeping and
// separately admitted C allocations in the pinned codec.
namespace allocation_meter {
/** Header stored immediately before aligned test allocations; allocate/release use it to recover malloc ownership. */ struct Header {/** Original malloc block, including alignment and header overhead. */ void* base;/** Requested payload bytes charged to live accounting. */ std::size_t bytes;};
/* Current requested live bytes and high-water mark for one measurement window. */ inline std::atomic<std::size_t> live{},peak{};
/** Allocate n requested bytes with power-of-two alignment for test-only global new. Returns aligned storage or throws bad_alloc; the hidden header owns the malloc base. */
inline void* allocate(std::size_t n,std::size_t alignment){
    // Raise alignment for the accounting header, reject size overflow, and allocate backing storage.
    // Store the original base and requested size, then update current and maximum live charges.
    alignment=std::max(alignment,alignof(Header));
    if(n>SIZE_MAX-sizeof(Header)-alignment)throw std::bad_alloc();
    /* Malloc ownership before alignment. */ auto base=std::malloc(n+sizeof(Header)+alignment);if(!base)throw std::bad_alloc();
    /* Aligned payload address with room for its preceding header. */ auto address=(reinterpret_cast<std::uintptr_t>(base)+sizeof(Header)+alignment-1)&~(alignment-1);
    *reinterpret_cast<Header*>(address-sizeof(Header))={base,n};
    /* Updated live charge; previous is the compare/exchange observation of the peak. */ auto now=live.fetch_add(n)+n,previous=peak.load();while(now>previous&&!peak.compare_exchange_weak(previous,now)){}
    return reinterpret_cast<void*>(address);
}
/** Release a pointer previously returned by allocate; null is accepted. Deducts requested live bytes before freeing the original malloc base. */
inline void release(void*p)noexcept{
    // For a nonnull allocation, recover its preceding header, deduct its charge, and free its backing block.
    if(p){/* Header belonging to the released payload. */ auto*h=static_cast<Header*>(p)-1;live.fetch_sub(h->bytes);std::free(h->base);}}
/** Start a measurement window at the current live byte count. Returns that baseline; callers must avoid overlapping measurement windows. */
inline std::size_t start(){
    // Reset the high-water mark to the current live requested-byte count and return the baseline.
    /* Live requested bytes already retained when measurement starts. */ auto before=live.load();peak.store(before);return before;}
/** Return peak requested live bytes above the baseline returned by start; excludes allocator metadata and codec C allocations. */
inline std::size_t maximum_since(std::size_t before){
    // Read the shared peak and subtract the caller-retained baseline.
    return peak.load()-before;}
}
/** Allocate n bytes through test accounting; the aligned overload additionally accepts required alignment a. Array forms share the scalar allocation path. Returns owned storage or throws bad_alloc. */
void* operator new(std::size_t n){ /* Forward to the matching tracked allocation or release primitive. */ return allocation_meter::allocate(n,alignof(std::max_align_t));}
/** Allocate n bytes through test accounting; the aligned overload additionally accepts required alignment a. Array forms share the scalar allocation path. Returns owned storage or throws bad_alloc. */
void* operator new[](std::size_t n){ /* Forward to the matching tracked allocation or release primitive. */ return ::operator new(n);}
/** Allocate n bytes through test accounting; the aligned overload additionally accepts required alignment a. Array forms share the scalar allocation path. Returns owned storage or throws bad_alloc. */
void* operator new(std::size_t n,std::align_val_t a){ /* Forward to the matching tracked allocation or release primitive. */ return allocation_meter::allocate(n,static_cast<std::size_t>(a));}
/** Allocate n bytes through test accounting; the aligned overload additionally accepts required alignment a. Array forms share the scalar allocation path. Returns owned storage or throws bad_alloc. */
void* operator new[](std::size_t n,std::align_val_t a){ /* Forward to the matching tracked allocation or release primitive. */ return ::operator new(n,a);}
/** Release p through test accounting; null is accepted. Sized/aligned overload metadata is intentionally ignored because the stored header is authoritative. */
void operator delete(void*p)noexcept{ /* Forward to the matching tracked allocation or release primitive. */ allocation_meter::release(p);}
/** Release p through test accounting; null is accepted. Sized/aligned overload metadata is intentionally ignored because the stored header is authoritative. */
void operator delete[](void*p)noexcept{ /* Forward to the matching tracked allocation or release primitive. */ allocation_meter::release(p);}
/** Release p through test accounting; null is accepted. Sized/aligned overload metadata is intentionally ignored because the stored header is authoritative. */
void operator delete(void*p,std::size_t)noexcept{ /* Forward to the matching tracked allocation or release primitive. */ allocation_meter::release(p);}
/** Release p through test accounting; null is accepted. Sized/aligned overload metadata is intentionally ignored because the stored header is authoritative. */
void operator delete[](void*p,std::size_t)noexcept{ /* Forward to the matching tracked allocation or release primitive. */ allocation_meter::release(p);}
/** Release p through test accounting; null is accepted. Sized/aligned overload metadata is intentionally ignored because the stored header is authoritative. */
void operator delete(void*p,std::align_val_t)noexcept{ /* Forward to the matching tracked allocation or release primitive. */ allocation_meter::release(p);}
/** Release p through test accounting; null is accepted. Sized/aligned overload metadata is intentionally ignored because the stored header is authoritative. */
void operator delete[](void*p,std::align_val_t)noexcept{ /* Forward to the matching tracked allocation or release primitive. */ allocation_meter::release(p);}
/** Release p through test accounting; null is accepted. Sized/aligned overload metadata is intentionally ignored because the stored header is authoritative. */
void operator delete(void*p,std::size_t,std::align_val_t)noexcept{ /* Forward to the matching tracked allocation or release primitive. */ allocation_meter::release(p);}
/** Release p through test accounting; null is accepted. Sized/aligned overload metadata is intentionally ignored because the stored header is authoritative. */
void operator delete[](void*p,std::size_t,std::align_val_t)noexcept{ /* Forward to the matching tracked allocation or release primitive. */ allocation_meter::release(p);}
