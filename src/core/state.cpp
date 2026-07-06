#include "state.h"

#include <atomic>

namespace state {
namespace {

std::atomic<int> g_state{static_cast<int>(State::Discovering)};
std::atomic<uint64_t> g_mapping{0};     // (slotRva << 32) | rectOff; 0 = unset
std::atomic<uint64_t> g_backbuffer{0};  // (w << 32) | h; 0 = unknown
std::atomic<uint32_t> g_creatorSerial{0};
std::atomic<bool> g_scaled{false};

}  // namespace

State Get() { return static_cast<State>(g_state.load(std::memory_order_acquire)); }
void Set(State s) { g_state.store(static_cast<int>(s), std::memory_order_release); }

void PublishMapping(uint32_t slotRva, uint32_t rectOff) {
  g_mapping.store((static_cast<uint64_t>(slotRva) << 32) | rectOff,
                  std::memory_order_release);
}

bool GetMapping(uint32_t* slotRva, uint32_t* rectOff) {
  const uint64_t m = g_mapping.load(std::memory_order_acquire);
  if (!m) return false;
  if (slotRva) *slotRva = static_cast<uint32_t>(m >> 32);
  if (rectOff) *rectOff = static_cast<uint32_t>(m & 0xFFFFFFFFu);
  return true;
}

void ClearMapping() { g_mapping.store(0, std::memory_order_release); }

void SetBackbuffer(unsigned w, unsigned h) {
  if (!w || !h) return;
  g_backbuffer.store((static_cast<uint64_t>(w) << 32) | h,
                     std::memory_order_release);
}

bool GetBackbuffer(unsigned* w, unsigned* h) {
  const uint64_t b = g_backbuffer.load(std::memory_order_acquire);
  if (!b) return false;
  if (w) *w = static_cast<unsigned>(b >> 32);
  if (h) *h = static_cast<unsigned>(b & 0xFFFFFFFFu);
  return true;
}

void NoteCreatorRun() {
  g_creatorSerial.fetch_add(1, std::memory_order_acq_rel);
}

uint32_t CreatorSerial() {
  return g_creatorSerial.load(std::memory_order_acquire);
}

void NoteScaled() { g_scaled.store(true, std::memory_order_release); }
bool ScaledSinceAttach() { return g_scaled.load(std::memory_order_acquire); }

}  // namespace state
