#include "gauge_limits.h"

// Compile-time boundary checks exercise the same classifier used by rendering.
static_assert(gauge_alert(Gauge::Rpm,4800)==Alert::Normal);
static_assert(gauge_alert(Gauge::Rpm,4800.1f)==Alert::Critical);
static_assert(gauge_alert(Gauge::Temperature,200)==Alert::Normal);
static_assert(gauge_alert(Gauge::Temperature,200.1f)==Alert::Critical);
static_assert(gauge_alert(Gauge::Oil,15)==Alert::Normal);
static_assert(gauge_alert(Gauge::Oil,14.9f)==Alert::Critical);
static_assert(gauge_alert(Gauge::Fuel,20)==Alert::Normal);
static_assert(gauge_alert(Gauge::Fuel,19.9f)==Alert::Warning);
static_assert(gauge_alert(Gauge::Fuel,10)==Alert::Warning);
static_assert(gauge_alert(Gauge::Fuel,9.9f)==Alert::Critical);
static_assert(gauge_alert(Gauge::Fuel,0)==Alert::Critical);
static_assert(gauge_alert(Gauge::Voltage,10)==Alert::Normal);
static_assert(gauge_alert(Gauge::Voltage,9.9f)==Alert::Critical);
static_assert(rpm_redline/rpm_max==0.96f);
