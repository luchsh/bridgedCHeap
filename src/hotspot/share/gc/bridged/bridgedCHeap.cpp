
#include "gc/bridged/bridgedCHeap.hpp"
#include <stdlib.h>

BridgedCHeap::BridgedCHeap() : _used_bytes(0) {
  set_barrier_set(new BridgedModRefBS());
  // pretend to have allocated the entire address space
  initialize_reserved_region((HeapWord*)0, (HeapWord*)max_jlong);
}

HeapWord* BridgedCHeap::mem_allocate(size_t word_size, bool* gc_overhead_limit_was_exceeded) {
  assert(UseBridgedCHeap && word_size > 0, "sanity");
  size_t byte_size = (word_size << LogBytesPerWord);
  Atomic::add((intptr_t)byte_size, (volatile intptr_t*)&_used_bytes);
  if (_used_bytes > MaxHeapSize) {
    // will throw OOM when used bytes exceeds limit
    JavaThread* THREAD = JavaThread::current();
    THROW_OOP_(Universe::out_of_memory_error_java_heap(), NULL);
  } else {
    // Java objects have to be aligned
    size_t aligned_size = byte_size + MinObjAlignmentInBytes;
    HeapWord* raw_addr = (HeapWord*)::malloc(byte_size);
    if (TraceBridgedAlloc) {
      tty->print_cr("[Birdged alloc: addr=" PTR_FORMAT ", size=" SIZE_FORMAT, raw_addr, byte_size);
    }
    return align_up(raw_addr, MinObjAlignmentInBytes);
  }
}