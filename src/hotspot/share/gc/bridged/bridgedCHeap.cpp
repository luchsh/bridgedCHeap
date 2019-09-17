
#include "gc/bridged/bridgedCHeap.hpp"
#include <stdlib.h>
#include <dlfcn.h>
#include <sys/stat.h>

// ------------------------------ allocators ------------------------

// Abstract interface to delegate memory requests
// we have several back-ends
class CHeapAllocator : public CHeapObj<mtGC> {
public:
  virtual void* malloc(size_t size) = 0;
  virtual void free(void* buf) = 0;
};

// Prototype of libc's malloc/free
typedef void* (*malloc_prototype)(size_t size);
typedef void (*free_prototype)(void* ptr);

// Delegate directly to current in-use malloc/free
class DirectCHeapAllocator : public CHeapAllocator {
public:
  virtual void* malloc(size_t size)         { return ::malloc(size);    }
  virtual void free(void* ptr)              { ::free(ptr);              }
};

// Delegate to dynamically loaded libc implementation library
class DynLibCHeapAllocator : public CHeapAllocator {
private:
  char* _libc_path;   // path of dynamic libc
  void* _libc_handle;       // dlopen() handle
  // function pointers to malloc/free implementation
  malloc_prototype _malloc_impl;
  free_prototype _free_impl;

public:
  DynLibCHeapAllocator(const char* path);
  virtual ~DynLibCHeapAllocator();
  virtual void* malloc(size_t size) { return _malloc_impl(size);}
  virtual void free(void* ptr)      { _free_impl(ptr);          }

private:
  void initialize();
};

DynLibCHeapAllocator::DynLibCHeapAllocator(const char* path)
 : _libc_path(NULL),
   _libc_handle(NULL),
   _malloc_impl(NULL),
   _free_impl(NULL) {
  assert(UseBridgedCHeap && path != NULL && strlen(path) > 0, "Sanity");
  _libc_path = os::strdup(path, mtInternal);
  initialize();
}

DynLibCHeapAllocator::~DynLibCHeapAllocator() {
  if (_libc_handle != NULL) {
    dlclose(_libc_handle);
  }
  os::free(_libc_path);
}

void DynLibCHeapAllocator::initialize() {
  if (TraceBridgedCHeap) {
    tty->print_cr("Trying to load dynamic libc %s as BridgedCHeap delegation", _libc_path);
  }

  _libc_handle = dlopen(_libc_path, RTLD_LAZY);
  if (_libc_handle == NULL) {
    vm_exit_during_initialization(err_msg("Cannot load dynamic libc from %s", _libc_path));
  }

#define RESOLVE_SYMBOL(receiver, prototype, name, dl)  \
  do { \
    receiver = (prototype)dlsym(dl, name); \
    if (receiver == NULL) { \
      vm_exit_during_initialization("Symbol " name "() not found"); \
    } \
  } while(0)

  RESOLVE_SYMBOL(_malloc_impl, malloc_prototype, "malloc", _libc_handle);
  RESOLVE_SYMBOL(_free_impl, free_prototype, "free", _libc_handle);

#undef RESOLVE_SYMBOL

  if (TraceBridgedCHeap) {
    tty->print_cr("%s loaded successfully", _libc_path);
  }
}
// ------------------------------ BridgedCHeap ------------------------

BridgedCHeap::BridgedCHeap()
 : _used_bytes(0),
   _allocator(NULL) {
  BarrierSet::set_barrier_set(new BridgedCHeapBarrierSet());
  // pretend to have allocated the entire address space
  initialize_reserved_region((HeapWord*)0, (HeapWord*)max_jlong);
}


HeapWord* BridgedCHeap::mem_allocate(size_t word_size, bool* gc_overhead_limit_was_exceeded) {
  assert(UseBridgedCHeap && word_size > 0, "sanity");
  guarantee(_allocator != NULL, "Must have been initialized");
  size_t byte_size = (word_size << LogBytesPerWord);
  Atomic::add((intptr_t)byte_size, (volatile intptr_t*)&_used_bytes);
  if (_used_bytes > MaxHeapSize) {
    // will throw OOM when used bytes exceeds limit
    JavaThread* THREAD = JavaThread::current();
    THROW_OOP_(Universe::out_of_memory_error_java_heap(), NULL);
  } else {
    // Java objects have to be aligned
    size_t aligned_size = byte_size + MinObjAlignmentInBytes;
    HeapWord* raw_addr = (HeapWord*)_allocator->malloc(aligned_size);
    if (TraceBridgedAlloc) {
      tty->print_cr("[Birdged alloc: addr=" PTR_FORMAT ", size=" SIZE_FORMAT, p2i(raw_addr), byte_size);
    }
    return align_up(raw_addr, MinObjAlignmentInBytes);
  }
}

void BridgedCHeap::mem_deallocate(void* ptr) {
  assert(UseBridgedCHeap, "Sanity");
  if (ptr != NULL) {
    _allocator->free(ptr);
    if (TraceBridgedAlloc) {
      tty->print_cr("[Birdged free: addr=" PTR_FORMAT, p2i(ptr));
    }
  }
}

jint BridgedCHeap::initialize() {
  _allocator = create_allocator();
  if (_allocator == NULL) {
    return JNI_EINVAL;
  }
  return JNI_OK;
}

//
// allocator selection sequence:
// - DynLibCHeapAllocator
//   - Specified in BridgedLibcPath
//   - From jdk/lib/server/
// - DirectCHeapAllocator
//
CHeapAllocator* BridgedCHeap::create_allocator() {
  if (BridgedLibcPath != NULL && strlen(BridgedLibcPath) > 0) {
    if (TraceBridgedCHeap) {
      tty->print_cr("Using user-specified C library %s", BridgedLibcPath);
    }
    return new DynLibCHeapAllocator(BridgedLibcPath);
  }

  // search for libjemalloc.so
  if (AutoDetectJemalloc) {
    const size_t MAX_PATH_LEN = 4096;
    char lib_path[MAX_PATH_LEN];
    os::jvm_path(lib_path, MAX_PATH_LEN);
    char* p = strrchr(lib_path, (int)'/');
    if (p != NULL) {
      ++p; // move to 'libjvm.<extension>'
      assert(0 == strncmp(p, "libjvm.", 7), "sanity");
      sprintf(p, "libjemalloc%s", os::dll_file_extension());
      struct stat st;
      int ret = lstat(lib_path, &st);
      if (ret == 0 && S_ISREG(st.st_mode)) {
        if (TraceBridgedCHeap) {
          tty->print_cr("Auto detected jemalloc library in %s", lib_path);
        }
        return new DynLibCHeapAllocator(lib_path);
      }
    }
  }

  if (TraceBridgedCHeap) {
    tty->print_cr("Using hybrid C heap");
  }
  return new DirectCHeapAllocator();
}
