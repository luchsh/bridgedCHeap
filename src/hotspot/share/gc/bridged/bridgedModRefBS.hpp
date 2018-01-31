#ifndef SHARE_VM_GC_IMPLEMENTATION_BRIDGED_BRIDGED_MOD_REF_BS_HPP
#define SHARE_VM_GC_IMPLEMENTATION_BRIDGED_BRIDGED_MOD_REF_BS_HPP

#include "gc/shared/modRefBarrierSet.hpp"
#include "gc/shared/barrierSet.hpp"

// Have to pretend to have a 'barrier' to make intepreter and compilers happy
class BridgedModRefBS : public ModRefBarrierSet {
public:
  BridgedModRefBS()
   : ModRefBarrierSet(
     BarrierSet::FakeRtti(BarrierSet::BridgedModRef)
     ) { }
  virtual void invalidate(MemRegion mr)   { }
  virtual void clear(MemRegion mr)        { }
  virtual void resize_covered_region(MemRegion new_region) { }
  virtual bool is_aligned(HeapWord* addr) { return true; }
  virtual void print_on(outputStream* st) const { }
  // fake it!
  virtual BarrierSet::Name kind() const { return BarrierSet::BridgedModRef; }

  template <DecoratorSet decorators, typename BarrierSetT = BridgedModRefBS>
  class AccessBarrier: public ModRefBarrierSet::AccessBarrier<decorators, BarrierSetT> {};

protected:
  virtual void write_ref_array_work(MemRegion mr) { }
  virtual void write_region_work(MemRegion mr) { }
};

template<>
struct BarrierSet::GetName<BridgedModRefBS> {
  static const BarrierSet::Name value = BarrierSet::BridgedModRef;
};

template<>
struct BarrierSet::GetType<BarrierSet::BridgedModRef> {
  typedef BridgedModRefBS type;
};

#endif // #ifndef SHARE_VM_GC_IMPLEMENTATION_BRIDGED_BRIDGED_MOD_REF_BS_HPP