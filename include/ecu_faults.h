#pragma once
#include <stdint.h>
#include <stddef.h>

// NMEA 2000 PGN 127489 status words (DD206/DD223). Operating indicators
// such as preheat, secondary throttle, and neutral-start protection are not faults.
constexpr uint16_t ecu_fault_mask1=0xfbff;
constexpr uint16_t ecu_fault_mask2=0x009f;
struct EcuFaultText { uint8_t word; uint16_t mask; const char* title; const char* meaning; const char* detail; };
constexpr EcuFaultText ecu_fault_texts[]={
    {1,0x0001,"ENGINE REPORTED A FAULT","NO SPECIFIC REASON WAS SUPPLIED","MORE DETAIL REQUIRES ENGINE DIAGNOSTICS"},
    {1,0x0002,"ENGINE IS TOO HOT","THE ECU REPORTS HIGH ENGINE TEMPERATURE","THIS WARNING CAME FROM THE ENGINE ECU"},
    {1,0x0004,"ENGINE OIL PRESSURE IS LOW","THE ECU REPORTS INSUFFICIENT OIL PRESSURE","THIS WARNING CAME FROM THE ENGINE ECU"},
    {1,0x0008,"ENGINE OIL LEVEL IS LOW","THE ECU REPORTS A LOW ENGINE OIL LEVEL","THIS WARNING CAME FROM THE ENGINE ECU"},
    {1,0x0010,"FUEL PRESSURE IS LOW","THE ECU REPORTS LOW FUEL SUPPLY PRESSURE","THIS WARNING CAME FROM THE ENGINE ECU"},
    {1,0x0020,"ELECTRICAL VOLTAGE IS LOW","THE ECU REPORTS LOW SYSTEM VOLTAGE","THIS WARNING CAME FROM THE ENGINE ECU"},
    {1,0x0040,"COOLANT LEVEL IS LOW","THE ECU REPORTS TOO LITTLE ENGINE COOLANT","THIS WARNING CAME FROM THE ENGINE ECU"},
    {1,0x0080,"COOLING WATER FLOW WARNING","THE ECU HAS FLAGGED COOLING WATER FLOW","THE MESSAGE DOES NOT IDENTIFY THE CAUSE"},
    {1,0x0100,"WATER DETECTED IN FUEL","THE ECU REPORTS WATER IN THE FUEL SYSTEM","THIS WARNING CAME FROM THE ENGINE ECU"},
    {1,0x0200,"CHARGING SYSTEM WARNING","THE ECU REPORTS A CHARGING SYSTEM PROBLEM","THE MESSAGE DOES NOT IDENTIFY THE CAUSE"},
    {1,0x0800,"BOOST PRESSURE IS TOO HIGH","THE ECU REPORTS HIGH INTAKE BOOST PRESSURE","THIS WARNING CAME FROM THE ENGINE ECU"},
    {1,0x1000,"ENGINE SPEED LIMIT EXCEEDED","THE ECU REPORTS ITS SPEED LIMIT WAS EXCEEDED","THIS IS THE LIMIT REPORTED BY THE ECU"},
    {1,0x2000,"EXHAUST RECIRCULATION WARNING","THE ECU REPORTS AN EGR SYSTEM PROBLEM","EGR MEANS EXHAUST GAS RECIRCULATION"},
    {1,0x4000,"THROTTLE SENSOR WARNING","THE ECU HAS FLAGGED THE THROTTLE SENSOR","THE MESSAGE DOES NOT IDENTIFY THE CAUSE"},
    {1,0x8000,"EMERGENCY STOP ACTIVE","THE ECU REPORTS ENGINE EMERGENCY STOP MODE","THIS WARNING CAME FROM THE ENGINE ECU"},
    {2,0x0001,"ENGINE WARNING REPORTED","THE ECU REPORTS A LEVEL ONE WARNING","NO MORE SPECIFIC REASON WAS SUPPLIED"},
    {2,0x0002,"ENGINE WARNING REPORTED","THE ECU REPORTS A LEVEL TWO WARNING","NO MORE SPECIFIC REASON WAS SUPPLIED"},
    {2,0x0004,"ENGINE POWER IS LIMITED","THE ECU REPORTS REDUCED ENGINE POWER","THE MESSAGE DOES NOT IDENTIFY THE CAUSE"},
    {2,0x0008,"ENGINE SERVICE IS DUE","THE ECU REPORTS MAINTENANCE IS NEEDED","THIS WARNING CAME FROM THE ENGINE ECU"},
    {2,0x0010,"ENGINE COMMUNICATION FAULT","THE ECU REPORTS A COMMUNICATION PROBLEM","THE MESSAGE DOES NOT IDENTIFY THE CAUSE"},
    {2,0x0080,"ENGINE SHUTDOWN REPORTED","THE ECU REPORTS THE ENGINE IS SHUTTING DOWN","THE MESSAGE DOES NOT IDENTIFY THE CAUSE"},
};

struct EcuFaults {
    uint16_t status1=0,status2=0;
    uint8_t engine_instance=0,source=255;
    bool simulated=false,open=false;
    unsigned page=0;
    constexpr bool lamp() const { return (status1&ecu_fault_mask1) || (status2&ecu_fault_mask2); }
    constexpr bool active(unsigned index) const {
        const auto& fault=ecu_fault_texts[index];
        if(index==0 && ((status1&(ecu_fault_mask1&~1u)) || (status2&ecu_fault_mask2))) return false;
        return ((fault.word==1?status1:status2)&fault.mask)!=0;
    }
    constexpr unsigned count() const {
        unsigned count=0;
        for(unsigned i=0;i<sizeof(ecu_fault_texts)/sizeof(ecu_fault_texts[0]);i++) if(active(i)) count++;
        return count;
    }
    constexpr const EcuFaultText* fault_at(unsigned page) const {
        for(unsigned i=0;i<sizeof(ecu_fault_texts)/sizeof(ecu_fault_texts[0]);i++) if(active(i)) {
            if(page==0) return &ecu_fault_texts[i];
            page--;
        }
        return nullptr;
    }
    constexpr bool receive_127489(const uint8_t* payload,size_t size,uint8_t sender,bool is_simulated=false) {
        if(!payload || size<26 || payload[0]!=engine_instance) return false;
        uint16_t first=payload[20]|(uint16_t(payload[21])<<8),second=payload[22]|(uint16_t(payload[23])<<8);
        // Unavailable fields must not manufacture faults, or clear an earlier report.
        if(first!=0xffff) status1=first;
        if(second!=0xffff) status2=second;
        source=sender; simulated=is_simulated;
        unsigned total=count();
        if(!total) { page=0; open=false; }
        else if(page>=total) page=total-1;
        return true;
    }
};
