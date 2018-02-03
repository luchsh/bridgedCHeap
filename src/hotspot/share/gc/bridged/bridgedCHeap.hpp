
#ifndef SHARE_VM_GC_IMPLEMENTATION_BRIDGED_BRIDGED_CHEAP_HPP
#define SHARE_VM_GC_IMPLEMENTATION_BRIDGED_BRIDGED_CHEAP_HPP

#include "gc/shared/collectedHeap.hpp"
#include "gc/shared/gcArguments.hpp"
#include "gc/bridged/bridgedModRefBS.hpp"

//
// The Bridged CHeap
// Delegating all allocation requests to the standard C-Heap
//
// The 'C-Heap' used by this class is not the same one used by
// Java launcher and libjvm.so, but another separate copy of
// jemalloc, loaded dynamically during heap initialization.
//
class BridgedCHeap : public CollectedHeap {
public:
  typedef void* (*malloc_prototype)(size_t size);
  typedef void (*free_prototype)(void* ptr);

private:
  size_t _used_bytes;  // how many bytes have been allocated

  // function pointers to malloc/free implementation
  malloc_prototype _malloc_impl;
  free_prototype _free_impl;

public:
  BridgedCHeap();

  // The primary allocation method we want to implement
  virtual HeapWord* mem_allocate(size_t word_size, bool* gc_overhead_limit_was_exceeded);

  // do initialization
  virtual jint initialize();

  // requirements of HotSpot GC framework, actually we do not care at all
  virtual Name kind() const                     { return CollectedHeap::BridgedCHeap;   }
  virtual const char* name() const              { return "Bridged C-Heap";              }
  virtual size_t capacity() const               { return MaxHeapSize;                   }
  virtual size_t used() const                   { return _used_bytes;                   }
  virtual bool is_maximal_no_gc() const         { return false;                         }
  virtual size_t max_capacity() const           { return MaxHeapSize;                   }
  virtual bool is_in(const void* p) const       { return true;                          }
  virtual bool supports_tlab_allocation() const { return true;                          }
  virtual size_t tlab_capacity(Thread *thr) const     { return 0;                       }
  virtual size_t tlab_used(Thread *thr) const         { return 0;                       }
  virtual bool can_elide_tlab_store_barriers() const  { return false;                   }
  virtual bool can_elide_initializing_store_barrier(oop new_obj) { return false;        }
  virtual bool card_mark_must_follow_store() const    { return false;                   }
  virtual void collect(GCCause::Cause cause)    { Unimplemented();                      }
  virtual void do_full_collection(bool clear_all_soft_refs) { Unimplemented();          }
  virtual CollectorPolicy* collector_policy() const   { return NULL;                    }
  virtual GrowableArray<GCMemoryManager*> memory_managers() {
    return GrowableArray<GCMemoryManager*>();
  }
  virtual GrowableArray<MemoryPool*> memory_pools() {
    return GrowableArray<MemoryPool*>();
  }
  virtual void object_iterate(ObjectClosure* cl)      { }
  virtual void safe_object_iterate(ObjectClosure* cl) { }
  virtual HeapWord* block_start(const void* addr) const { return NULL;                  }
  virtual size_t block_size(const HeapWord* addr) const { return 0;                     }
  virtual bool block_is_obj(const HeapWord* addr) const { return false;                 }
  virtual jlong millis_since_last_gc()          { return 0;                             }
  virtual bool is_scavengable(oop obj)          { return false;                         }
  virtual void prepare_for_verify()             { }
  virtual void initialize_serviceability()      { }
  virtual void print_on(outputStream* st) const { }
  virtual void print_gc_threads_on(outputStream* st) const { }
  virtual void gc_threads_do(ThreadClosure* tc) const { }
  virtual void print_tracing_info() const       { }
  virtual void verify(VerifyOption option)      { }

  // not pure virtual, but have to implement as well
  virtual size_t unsafe_max_tlab_alloc(Thread *thr) const { return 0;                   }
};

// to work with JDK10's framework
class BridgedCHeapArguments : public GCArguments {
public:
  virtual size_t conservative_max_heap_alignment() {
    return CollectorPolicy::compute_heap_alignment();
  }
  virtual CollectedHeap* create_heap() {
    return new BridgedCHeap();
  }
};

#endif    // #ifndef SHARE_VM_GC_IMPLEMENTATION_BRIDGED_BRIDGED_CHEAP_HPP