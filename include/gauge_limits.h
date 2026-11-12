#pragma once

constexpr float rpm_max = 5000.0f;
constexpr float rpm_redline = 4800.0f;
enum class Gauge { Rpm, Temperature, Oil, Fuel, Voltage, Trim, Afr, IntakeAir, Accelerator, FuelPressure };
constexpr unsigned gauge_count = 10;
enum class Alert { Normal, Warning, Critical };

constexpr Alert gauge_alert(Gauge gauge,float value,float engine_rpm=0) {
    switch(gauge) {
        case Gauge::Rpm: return value>rpm_redline?Alert::Critical:Alert::Normal;
        case Gauge::Temperature: return value>200?Alert::Critical:Alert::Normal;
        case Gauge::Oil: return value<15?Alert::Critical:Alert::Normal;
        case Gauge::Fuel: return value<10?Alert::Critical:(value<20?Alert::Warning:Alert::Normal);
        case Gauge::Voltage: return value<10?Alert::Critical:Alert::Normal;
        case Gauge::Trim: return value>60 && engine_rpm>0?Alert::Critical:Alert::Normal;
        case Gauge::Afr: return value>16?Alert::Critical:Alert::Normal;
        case Gauge::IntakeAir: return value>130?Alert::Critical:Alert::Normal;
        case Gauge::Accelerator: return value>80?Alert::Critical:Alert::Normal;
        case Gauge::FuelPressure: return value<25?Alert::Critical:Alert::Normal;
    }
    return Alert::Normal;
}
