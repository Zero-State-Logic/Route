#pragma once
#include <atomic>

// Global demand surge multiplier, shared across the fare engine, the surge job,
// and the manager console. Auto-updated from demand unless a manager overrides it.
namespace route {

class Surge {
  public:
    static Surge &instance() { static Surge s; return s; }

    double current() const { return mult_.load(); }
    bool isManual() const { return manual_.load(); }

    // Auto job path: ignored while a manual override is in effect.
    void setAuto(double m) { if (!manual_.load()) mult_.store(clamp(m)); }
    // Manager override + kill-switch.
    void setManual(double m) { manual_.store(true); mult_.store(clamp(m)); }
    void resumeAuto() { manual_.store(false); }

  private:
    static double clamp(double m) { return m < 1.0 ? 1.0 : (m > 3.0 ? 3.0 : m); }
    std::atomic<double> mult_{1.0};
    std::atomic<bool> manual_{false};
};

}  // namespace route
