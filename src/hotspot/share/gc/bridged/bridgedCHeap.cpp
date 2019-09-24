
#include "gc/bridged/bridgedCHeap.hpp"
#include "logging/log.hpp"
#include <stdlib.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include "gc/shared/gcVMOperations.hpp"
#include "runtime/vmThread.hpp"

#include "precompiled.hpp"
#include "aot/aotLoader.hpp"
#include "classfile/classLoaderDataGraph.hpp"
#include "classfile/stringTable.hpp"
#include "classfile/systemDictionary.hpp"
#include "code/codeCache.hpp"
#include "memory/universe.hpp"
#include "runtime/mutex.hpp"
#include "services/management.hpp"
#include "utilities/macros.hpp"
#if INCLUDE_JVMCI
#include "jvmci/jvmci.hpp"
#endif
#include "oops/oop.inline.hpp"
#include "oops/oop.hpp"
#include "memory/iterator.inline.hpp"


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

  if (log_is_enabled(Info, bridged)) {
    ResourceMark rm;
    log_info(bridged)("%s loaded successfully", _libc_path);
  }
}
// ------------------------------ BridgedCHeap ------------------------

// compaction support
class CHeapChunk : public CHeapObj<mtGC> {
private:
  HeapWord* _bottom;
  size_t    _capacity; // in bytes
  HeapWord* _end;
  HeapWord* _top;
  CHeapChunk* _next;
public:
  CHeapChunk(HeapWord* bottom, size_t capacity)
    : _bottom(bottom), _capacity(capacity),
      _end(_bottom + (_capacity>>LogBytesPerWord)),
      _top(_bottom),
      _next(NULL)
  { }
  virtual ~CHeapChunk() {
    ((BridgedCHeap*)Universe::heap())->mem_deallocate(_bottom);
    _bottom = NULL;
    _capacity = 0;
  }
  CHeapChunk* next() { return _next; }
  void set_next(CHeapChunk* n) { _next = n; }
  HeapWord* bottom() { return _bottom; }
  HeapWord* end() { return _end; }
  HeapWord* top() { return _top; }
  bool contains(HeapWord* addr) {
    return (addr >= _bottom && addr < _top);
  }
  bool can_allocate(size_t word_size) {
    return (_top + word_size < _end);
  }
  HeapWord* allocate(size_t word_size) {
    if (can_allocate(word_size)) {
      HeapWord* res = _top;
      _top += word_size;
      return res;
    }
    return NULL;
  }
  size_t used_words() { return _top - _bottom; }
};


BridgedCHeap::BridgedCHeap()
 : _used_bytes(0),
   _chunk_list(NULL),
   _allocator(NULL),
   _segment_size(BridgedCHeapSegmentSize) {
  BarrierSet::set_barrier_set(new BridgedCHeapBarrierSet());
  // pretend to have allocated the entire address space
  initialize_reserved_region((HeapWord*)0, (HeapWord*)max_jlong);
}


HeapWord* BridgedCHeap::mem_allocate(size_t word_size, bool* gc_overhead_limit_was_exceeded) {
  assert(UseBridgedCHeap && word_size > 0, "sanity");
  guarantee(_allocator != NULL, "Must have been initialized");
  size_t byte_size = (word_size << LogBytesPerWord);
  Atomic::add((intptr_t)byte_size, (volatile intptr_t*)&_used_bytes);

  if (_used_bytes + byte_size >= MaxHeapSize) {
    if (log_is_enabled(Info, bridged)) {
      ResourceMark rm;
      log_info(bridged)("[Bridged collect used_bytes=" SIZE_FORMAT " MaxHeapSize=" SIZE_FORMAT
                        " current alloc_requst_byte_size=" SIZE_FORMAT,
                        _used_bytes, MaxHeapSize, byte_size);
    }
    collect(GCCause::_allocation_failure);
  }

  if (_used_bytes < MaxHeapSize) {
    // Java objects have to be aligned
    size_t aligned_size = MAX2(BridgedCHeapSegmentSize, byte_size) + MinObjAlignmentInBytes;
    HeapWord* raw_addr = (HeapWord*)_allocator->malloc(aligned_size);
    if (log_is_enabled(Trace, bridged)) {
      ResourceMark rm;
      log_trace(bridged)("[Bridged alloc: addr=" PTR_FORMAT ", size=" SIZE_FORMAT, p2i(raw_addr), aligned_size);
    }
    return align_up(raw_addr, MinObjAlignmentInBytes);
  } else {
    // will throw OOM when used bytes exceeds limit
    JavaThread* THREAD = JavaThread::current();
    THROW_OOP_(Universe::out_of_memory_error_java_heap(), NULL);
  }
}

void BridgedCHeap::mem_deallocate(void* ptr) {
  assert(UseBridgedCHeap, "Sanity");
  if (ptr != NULL) {
    _allocator->free(ptr);
    if (log_is_enabled(Trace, bridged)) {
      ResourceMark rm;
      log_trace(bridged)("[Bridged free: addr=" PTR_FORMAT, p2i(ptr));
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
    if (log_is_enabled(Info, bridged)) {
      ResourceMark rm;
      log_info(bridged)("Using user-specified C library %s", BridgedLibcPath);
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
        if (log_is_enabled(Info, bridged)) {
          ResourceMark rm;
          log_info(bridged)("Auto detected jemalloc library in %s", lib_path);
        }
        return new DynLibCHeapAllocator(lib_path);
      }
    }
  }

  if (log_is_enabled(Info, bridged)) {
    ResourceMark rm;
    log_info(bridged)("Using Bridged C heap memory management");
  }
  return new DirectCHeapAllocator();
}

HeapWord* BridgedCHeap::allocate_new_tlab(size_t min_size,
                                          size_t requested_size,
                                          size_t* actual_size) {
  if (requested_size < _segment_size) {
    requested_size = _segment_size;
  }
  *actual_size = requested_size;
  HeapWord* res = mem_allocate(requested_size, NULL);
  if (log_is_enabled(Trace, bridged)) {
    ResourceMark rm;
    log_trace(bridged)("Bridged TLAB alloc thread=" PTR_FORMAT " size=" SIZE_FORMAT
                  ", addr=" PTR_FORMAT,
                  p2i(Thread::current()),
                  requested_size, p2i(res));
  }
  CHeapChunk* chunk = new CHeapChunk(res, requested_size);
  CHeapChunk* head = (CHeapChunk*)_chunk_list;
  CHeapChunk* get = NULL;
  do {
    head = (CHeapChunk*)_chunk_list;
    chunk->set_next(head);
    get = NULL;
    get = (CHeapChunk*)Atomic::cmpxchg(chunk, &_chunk_list, head);
  } while (get != head);
  return res;
}

class VM_BridgedCompact : public VM_GC_Operation {
public:
  VM_BridgedCompact(uint gc_count_before,
                  GCCause::Cause _cause,
                  uint full_gc_count_before = 0,
                  bool full = false)
    : VM_GC_Operation(gc_count_before, _cause, full_gc_count_before, full)
  { }
  virtual void doit() {
    ((BridgedCHeap*)(Universe::heap()))->do_collection_pause(_gc_cause);
  }
  virtual VMOp_Type type() const { return VMOp_BridgedCompact; }
};

void BridgedCHeap::collect(GCCause::Cause cause) {
  VM_BridgedCompact op(0,
                       cause,
                       0,
                       true);
  VMThread::execute(&op);
}

CHeapChunk* BridgedCHeap::gc_chunk_of(HeapWord* addr) {
  for (CHeapChunk* chk = (CHeapChunk*)_gc_chunks;
       chk != NULL; chk = chk->next()) {
    if (chk->contains(addr)) {
      return chk;
    }
  }
  return NULL;
}

class CopyClosure : public BasicOopIterateClosure {
public:
  virtual void do_oop(oop* o) { do_oop_work(o); }
  virtual void do_oop(narrowOop* o) { do_oop_work(o); }
private:
  template<typename T>
  void do_oop_work(T* p) {
    oop o = RawAccess<>::oop_load(p);
    if (o != NULL) {
      oop dest = NULL;
      if (o->is_forwarded()) {
        dest = o->forwardee();
        DEBUG_ONLY(if (DebugBridgedCHeap) {
                    tty->print_cr("update ref " PTR_FORMAT "=>" PTR_FORMAT, p2i(o), p2i(dest));
                   });
      } else {
        dest = copy_obj(o);
        DEBUG_ONLY(if (DebugBridgedCHeap) {
                    tty->print_cr("copy object " PTR_FORMAT "=>" PTR_FORMAT, p2i(o), p2i(dest));
                   });
      }
      RawAccess<>::oop_store(p, o->forwardee());
    }
  }

  oop copy_obj(oop src) {
    if (src != NULL) {
      size_t word_sz = src->size();
      markOop old_mark = src->mark();
      HeapWord* obj_ptr = ((BridgedCHeap*)Universe::heap())->allocate_at_gc(word_sz);
      guarantee(obj_ptr != NULL, "never expects a NULL pointer");
      const oop obj = oop(obj_ptr);
      Copy::aligned_disjoint_words((HeapWord*) src, obj_ptr, word_sz);
      obj->set_mark(old_mark);
      src->forward_to(obj);
      return obj;
    }
    return NULL;
  }
};

void BridgedCHeap::do_collection_pause(GCCause::Cause cause) {
  guarantee(SafepointSynchronize::is_at_safepoint(), "Must in safepoint");
  if (log_is_enabled(Info, gc)) {
    ResourceMark rm;
    log_info(gc)("BridgedCHeap::collect() cause=%s", GCCause::to_string(cause));
  }

  double secs_start = os::elapsedTime();
  size_t used_bytes_before = _used_bytes;

  // initialization
  reset_gc_stat();
  ensure_parsability(true);

  CopyClosure copy_objs;

  process_roots(&copy_objs);

  // scan sequencially
  for (CHeapChunk* chk_scan = _gc_chunks;
       chk_scan != NULL; chk_scan = chk_scan->next()) {
    for (HeapWord* p = chk_scan->bottom();
         chk_scan->contains(p);) {
      if (log_is_enabled(Trace, bridged)) {
        ResourceMark rm;
        log_trace(bridged)("BridgedCHeap::collect() scanning address " PTR_FORMAT, p2i(p));
      }
      oop obj = (oop)p;
      size_t word_sz = obj->size();
      obj->oop_iterate(&copy_objs);
      p += word_sz;
    }
  }

  cleanup_after_gc();

  if (log_is_enabled(Info, gc)) {
    double secs_end = os::elapsedTime();
    ResourceMark rm;
    log_info(gc)("BridgedCHeap::collect() finsihed, time=%lf secs, " SIZE_FORMAT "->" SIZE_FORMAT,
                 (secs_end - secs_start),
                 used_bytes_before,
                 _used_bytes);
  }
}

void BridgedCHeap::cleanup_after_gc() {
  // release from chunks
  for (CHeapChunk* chk = (CHeapChunk*)_chunk_list;
       chk != NULL; ) {
    CHeapChunk* next = chk->next();
    DEBUG_ONLY(if (DebugBridgedCHeap) {
               tty->print_cr("Freed chunk [" PTR_FORMAT "-" PTR_FORMAT "-" PTR_FORMAT ")",
                             p2i(chk->bottom()), p2i(chk->top()), p2i(chk->end()));
               });
    delete chk;
    chk = next;
  }
  if (_gc_last_chunk != NULL) {
    _gc_used_bytes += (_gc_last_chunk->used_words() << LogBytesPerWord);
  }
  // swap to chunks
  _chunk_list = _gc_chunks;
  _used_bytes = _gc_used_bytes;
}

void BridgedCHeap::reset_gc_stat() {
  _gc_chunks = NULL;
  _gc_last_chunk = NULL;
  _gc_end = NULL;
  _gc_scan = NULL;
  _gc_used_bytes = 0;
}

class ParallelOopsDoThreadClosure : public ThreadClosure {
private:
  OopClosure* _f;
  CodeBlobClosure* _cf;
public:
  ParallelOopsDoThreadClosure(OopClosure* f, CodeBlobClosure* cf) : _f(f), _cf(cf) {}
  void do_thread(Thread* t) {
    t->oops_do(_f, _cf);
  }
};

// Single-threaded, do not be shy
void BridgedCHeap::process_roots(OopClosure* cl) {
  if (log_is_enabled(Info, gc)) {
    ResourceMark rm;
    log_info(gc)("Begin scanning GC roots for BridgedCHeap");
  }
  double secs_start = os::elapsedTime();

  CodeBlobToOopClosure cb_cl(cl, false);
  CLDToOopClosure cld_cl(cl, false);
  ParallelOopsDoThreadClosure thrd_cl(cl, &cb_cl);
  ClassLoaderDataGraph::roots_cld_do(&cld_cl, &cld_cl);
  Threads::possibly_parallel_oops_do(false,
                                     cl, &cb_cl);
  Universe::oops_do(cl);
  JNIHandles::oops_do(cl);
  ObjectSynchronizer::oops_do(cl);
  Management::oops_do(cl);
  JvmtiExport::oops_do(cl);
#if INCLUDE_AOT
  AOTLoader::oops_do(cl);
#endif

#if INCLUDE_JVMCI
  JVMCI::oops_do(cl);
#endif
  SystemDictionary::oops_do(cl);
  CodeCache::blobs_do(&cb_cl);
  StringTable::oops_do(cl);

  double secs_end = os::elapsedTime();
  if (log_is_enabled(Info, gc)) {
    ResourceMark rm;
    log_info(gc)("Finish scanning GC roots, time=%lf secs", (secs_end - secs_start));
  }
}

HeapWord* BridgedCHeap::allocate_at_gc(size_t word_size) {
  guarantee(_allocator != NULL, "sanity");
  if (_gc_last_chunk != NULL && _gc_last_chunk->can_allocate(word_size)) {
    return _gc_last_chunk->allocate(word_size);
  } else {
    // allocate whole chunk or single humongous object;
    size_t aligned_size = MAX2(BridgedCHeapSegmentSize, (word_size << LogBytesPerWord))
                            + MinObjAlignmentInBytes;
    HeapWord* raw_addr = (HeapWord*)_allocator->malloc(aligned_size);
    HeapWord* res = align_up(raw_addr, MinObjAlignmentInBytes);
    CHeapChunk* cur = new CHeapChunk(res, aligned_size);
    if (log_is_enabled(Trace, bridged)) {
      ResourceMark rm;
      log_trace(bridged)("GC chunks allocated size=" SIZE_FORMAT ", addr=" PTR_FORMAT,
                         aligned_size,
                         p2i(res));
    }
    if (_gc_last_chunk == NULL) {
      guarantee(_gc_chunks == NULL, "must be uninitialized");
      _gc_chunks = cur;
      _gc_last_chunk = cur;
      _gc_scan = _gc_chunks->bottom();
    } else {
      // find the last
      CHeapChunk* chk;
      _gc_last_chunk->set_next(cur);
      _gc_used_bytes += (_gc_last_chunk->used_words() << LogBytesPerWord);
      _gc_last_chunk = cur;
    }
    res = cur->allocate(word_size);
    guarantee(res != NULL, "should not be!");
    if (log_is_enabled(Trace, bridged)) {
      ResourceMark rm;
      log_trace(bridged)("[Bridged GC alloc: addr=" PTR_FORMAT ", byte_size=" SIZE_FORMAT,
                         p2i(raw_addr), (word_size << LogBytesPerWord));
    }
    _gc_end = cur->top();
    return res;
  }
}
