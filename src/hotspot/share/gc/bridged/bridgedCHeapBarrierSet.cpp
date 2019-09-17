
#include "precompiled.hpp"
#include "runtime/thread.hpp"
#include "gc/bridged/bridgedCHeapBarrierSet.hpp"
#include "gc/shared/barrierSet.hpp"
#include "gc/shared/barrierSetAssembler.hpp"
#include "utilities/macros.hpp"
#ifdef COMPILER1
#include "gc/shared/c1/barrierSetC1.hpp"
#endif
#ifdef COMPILER2
#include "gc/shared/c2/barrierSetC2.hpp"
#endif

BridgedCHeapBarrierSet::BridgedCHeapBarrierSet() : BarrierSet(
          make_barrier_set_assembler<BarrierSetAssembler>(),
          make_barrier_set_c1<BarrierSetC1>(),
          make_barrier_set_c2<BarrierSetC2>(),
          NULL /* barrier_set_nmethod */,
          BarrierSet::FakeRtti(BarrierSet::BridgedCHeapBarrierSet)) {};
