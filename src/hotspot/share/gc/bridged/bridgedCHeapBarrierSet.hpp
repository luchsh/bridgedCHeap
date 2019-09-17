#ifndef SHARE_VM_GC_IMPLEMENTATION_BRIDGED_BRIDGED_MOD_REF_BS_HPP
#define SHARE_VM_GC_IMPLEMENTATION_BRIDGED_BRIDGED_MOD_REF_BS_HPP

#include "gc/shared/barrierSet.hpp"

// Have to pretend to have a 'barrier' to make intepreter and compilers happy
class BridgedCHeapBarrierSet : public BarrierSet {
  friend class VMStructs;
public:
  BridgedCHeapBarrierSet();

  virtual bool is_aligned(HeapWord* addr) { return true; }
  virtual void print_on(outputStream* st) const { }
  // fake it!
  virtual BarrierSet::Name kind() const { return BarrierSet::BridgedCHeapBarrierSet; }

  template <DecoratorSet decorators, typename BarrierSetT = BridgedCHeapBarrierSet>
  class AccessBarrier: public BarrierSet::AccessBarrier<decorators, BarrierSetT> {};

protected:
  virtual void write_ref_array_work(MemRegion mr) { }
  virtual void write_region_work(MemRegion mr) { }
};

template<>
struct BarrierSet::GetName<BridgedCHeapBarrierSet> {
  static const BarrierSet::Name value = BarrierSet::BridgedCHeapBarrierSet;
};

template<>
struct BarrierSet::GetType<BarrierSet::BridgedCHeapBarrierSet> {
  typedef ::BridgedCHeapBarrierSet type;
};

#endif // #ifndef SHARE_VM_GC_IMPLEMENTATION_BRIDGED_BRIDGED_MOD_REF_BS_HPP
