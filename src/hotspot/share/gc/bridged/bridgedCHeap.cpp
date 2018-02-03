
#include "gc/bridged/bridgedCHeap.hpp"
#include <stdlib.h>
#include <dlfcn.h>

BridgedCHeap::BridgedCHeap()
 : _used_bytes(0),
   _malloc_impl(NULL),
   _free_impl(NULL) {
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
    HeapWord* raw_addr = NULL;
    if (_malloc_impl != NULL) {
      raw_addr = (HeapWord*)_malloc_impl(byte_size);
    } else {
      raw_addr = (HeapWord*)::malloc(byte_size);
    }
    if (TraceBridgedAlloc) {
      tty->print_cr("[Birdged alloc: addr=" PTR_FORMAT ", size=" SIZE_FORMAT, raw_addr, byte_size);
    }
    return align_up(raw_addr, MinObjAlignmentInBytes);
  }
}

jint BridgedCHeap::initialize() {
  if (BridgedLibcPath != NULL) {
    if (TraceBridgedCHeap) {
      tty->print_cr("BridgedLibcPath=%s specified, trying to load it", BridgedLibcPath);
    }

    void* je_handle = dlopen(BridgedLibcPath, RTLD_LAZY);
    if (je_handle == NULL) {
      vm_exit_during_initialization(err_msg("Cannot load jemalloc library from %s", BridgedLibcPath));
    }

    _malloc_impl = (malloc_prototype)dlsym(je_handle, "malloc");
    if (_malloc_impl == NULL) {
      vm_exit_during_initialization("malloc() symbol not found");
    }

    _free_impl = (free_prototype)dlsym(je_handle, "free");
    if (_free_impl == NULL) {
      vm_exit_during_initialization("free() symbol not found");
    }

    if (TraceBridgedCHeap) {
      tty->print_cr("BridgedLibcPath=%s loaded successfully", BridgedLibcPath);
      tty->print_cr("All allocation requests will happen in separate C-Heap space");
    }
  } else {
    if (TraceBridgedCHeap) {
      tty->print_cr("BridgedLibcPath not specified, Java heap will be mixed with JVM's CHeap");
    }
  }

  return JNI_OK;
}