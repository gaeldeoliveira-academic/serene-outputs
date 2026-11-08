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
struct Header {void* base;std::size_t bytes;};
inline std::atomic<std::size_t> live{},peak{};
inline void* allocate(std::size_t n,std::size_t alignment){
    alignment=std::max(alignment,alignof(Header));
    if(n>SIZE_MAX-sizeof(Header)-alignment)throw std::bad_alloc();
    auto base=std::malloc(n+sizeof(Header)+alignment);if(!base)throw std::bad_alloc();
    auto address=(reinterpret_cast<std::uintptr_t>(base)+sizeof(Header)+alignment-1)&~(alignment-1);
    *reinterpret_cast<Header*>(address-sizeof(Header))={base,n};
    auto now=live.fetch_add(n)+n,previous=peak.load();while(now>previous&&!peak.compare_exchange_weak(previous,now)){}
    return reinterpret_cast<void*>(address);
}
inline void release(void*p)noexcept{if(p){auto*h=static_cast<Header*>(p)-1;live.fetch_sub(h->bytes);std::free(h->base);}}
inline std::size_t start(){auto before=live.load();peak.store(before);return before;}
inline std::size_t maximum_since(std::size_t before){return peak.load()-before;}
}
void* operator new(std::size_t n){return allocation_meter::allocate(n,alignof(std::max_align_t));}
void* operator new[](std::size_t n){return ::operator new(n);}
void* operator new(std::size_t n,std::align_val_t a){return allocation_meter::allocate(n,static_cast<std::size_t>(a));}
void* operator new[](std::size_t n,std::align_val_t a){return ::operator new(n,a);}
void operator delete(void*p)noexcept{allocation_meter::release(p);}
void operator delete[](void*p)noexcept{allocation_meter::release(p);}
void operator delete(void*p,std::size_t)noexcept{allocation_meter::release(p);}
void operator delete[](void*p,std::size_t)noexcept{allocation_meter::release(p);}
void operator delete(void*p,std::align_val_t)noexcept{allocation_meter::release(p);}
void operator delete[](void*p,std::align_val_t)noexcept{allocation_meter::release(p);}
void operator delete(void*p,std::size_t,std::align_val_t)noexcept{allocation_meter::release(p);}
void operator delete[](void*p,std::size_t,std::align_val_t)noexcept{allocation_meter::release(p);}
