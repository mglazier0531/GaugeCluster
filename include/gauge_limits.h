#pragma once

constexpr float rpm_max = 5000.0f;
constexpr float rpm_redline = 4800.0f;
enum class Gauge { Rpm, Temperature, Oil, Fuel, Voltage };
enum class Alert { Normal, Warning, Critical };

constexpr Alert gauge_alert(Gauge gauge,float value) {
    switch(gauge) {
        case Gauge::Rpm: return value>rpm_redline?Alert::Critical:Alert::Normal;
        case Gauge::Temperature: return value>200?Alert::Critical:Alert::Normal;
        case Gauge::Oil: return value<15?Alert::Critical:Alert::Normal;
        case Gauge::Fuel: return value<10?Alert::Critical:(value<20?Alert::Warning:Alert::Normal);
        case Gauge::Voltage: return value<10?Alert::Critical:Alert::Normal;
    }
    return Alert::Normal;
}
