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
static_assert(gauge_alert(Gauge::Trim,60,1000)==Alert::Normal);
static_assert(gauge_alert(Gauge::Trim,61,0)==Alert::Normal);
static_assert(gauge_alert(Gauge::Trim,61,-1)==Alert::Normal);
static_assert(gauge_alert(Gauge::Trim,61,1)==Alert::Critical);
static_assert(gauge_alert(Gauge::Afr,16)==Alert::Normal);
static_assert(gauge_alert(Gauge::Afr,16.01f)==Alert::Critical);
static_assert(gauge_alert(Gauge::IntakeAir,130)==Alert::Normal);
static_assert(gauge_alert(Gauge::IntakeAir,130.1f)==Alert::Critical);
static_assert(gauge_alert(Gauge::Accelerator,80)==Alert::Normal);
static_assert(gauge_alert(Gauge::Accelerator,80.1f)==Alert::Critical);
static_assert(gauge_alert(Gauge::FuelPressure,25)==Alert::Normal);
static_assert(gauge_alert(Gauge::FuelPressure,24.9f)==Alert::Critical);

#include "display_settings.h"
constexpr bool all_selections_work() {
    for(int slot=0;slot<5;slot++) for(int choice=0;choice<10;choice++) {
        DisplaySettings s;
        auto r=gauge_slots[slot];
        s.touch({true,r.left+r.width/2,r.top+r.height/2});
        if(s.chooser_slot!=slot) return false;
        s.touch({false,0,0});
        unsigned changes=s.touch({true,250+(choice%2)*304,180+(choice/2)*66});
        if(s.chooser_slot!=-1 || !(changes&DisplaySettings::Save) || !(changes&DisplaySettings::Layout)) return false;
        for(int i=0;i<5;i++) if(s.gauges[i]!=(i==slot?static_cast<Gauge>(choice):default_gauges[i])) return false;
    }
    return true;
}
static_assert(all_selections_work(),"Every gauge must be selectable independently in every position");

#include "hidden_alerts.h"
constexpr bool hidden_alert_checks() {
    Gauge shown[5]={Gauge::Rpm,Gauge::Temperature,Gauge::Oil,Gauge::Fuel,Gauge::Voltage};
    Alert levels[gauge_count]={};
    HiddenAlerts h;
    levels[0]=Alert::Critical; h.update(shown,levels);
    if(h.open || h.mask) return false; // A displayed gauge must not create a hidden alert.
    levels[5]=Alert::Critical; h.update(shown,levels);
    if(!h.open || h.mask!=(1u<<5)) return false;
    h.acknowledge(); h.update(shown,levels);
    if(h.open) return false;
    levels[6]=Alert::Critical; h.update(shown,levels);
    if(!h.open || h.mask!=((1u<<5)|(1u<<6))) return false;
    h.acknowledge(); levels[5]=Alert::Normal; h.update(shown,levels);
    if(h.open) return false;
    levels[5]=Alert::Critical; h.update(shown,levels);
    if(!h.open) return false; // Recovery rearms the next episode.
    shown[1]=Gauge::Trim; shown[2]=Gauge::Afr; h.update(shown,levels);
    if(h.open || h.mask) return false; // Showing all alerted gauges closes the popup.
    shown[3]=Gauge::Rpm; levels[3]=Alert::Warning; h.update(shown,levels);
    if(!h.open || h.critical_mask) return false;
    h.acknowledge(); h.update(shown,levels);
    if(h.open) return false;
    levels[3]=Alert::Critical; h.update(shown,levels);
    if(!h.open || h.critical_mask!=(1u<<3)) return false; // Warning->critical interrupts acknowledgement.
    levels[3]=Alert::Normal; h.update(shown,levels);
    return !h.open && !h.mask;
}
static_assert(hidden_alert_checks(),"Hidden alarms must acknowledge, rearm, escalate, and respect visible gauges");
