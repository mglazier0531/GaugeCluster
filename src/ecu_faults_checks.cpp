#include "ecu_faults.h"
constexpr bool ecu_checks() {
    EcuFaults e;
    if(e.lamp() || e.count()) return false;
    uint8_t p[26]={};
    p[20]=3; // Check engine + high temperature: one specific explanation.
    if(!e.receive_127489(p,sizeof(p),17) || !e.lamp() || e.count()!=1 || e.fault_at(0)->mask!=2) return false;
    e.open=true;
    p[20]=0; p[21]=4; // Preheat is an operating indicator, not a fault.
    p[22]=0x60; // Secondary throttle and neutral-start protection are not faults.
    if(!e.receive_127489(p,sizeof(p),17) || e.lamp() || e.open) return false;
    p[20]=1; p[21]=0; p[22]=0;
    e.receive_127489(p,sizeof(p),17);
    if(!e.lamp() || e.count()!=1 || e.fault_at(0)->mask!=1) return false;
    p[20]=0x21; p[21]=2; p[22]=4;
    e.receive_127489(p,sizeof(p),17);
    if(e.count()!=3) return false;
    e.page=2;
    if(e.fault_at(e.page)->word!=2 || e.fault_at(e.page)->mask!=4) return false;
    p[20]=1; p[21]=0; p[22]=0;
    e.receive_127489(p,sizeof(p),17);
    if(e.page!=0 || e.count()!=1) return false;
    p[20]=0x21; p[21]=2; p[22]=4;
    e.receive_127489(p,sizeof(p),17);
    p[20]=255; p[21]=255; p[22]=255; p[23]=255;
    e.receive_127489(p,sizeof(p),17);
    if(e.count()!=3) return false; // Unavailable must preserve existing faults.
    p[0]=1; p[20]=0; p[21]=0; p[22]=0; p[23]=0;
    if(e.receive_127489(p,sizeof(p),18) || e.count()!=3) return false; // Another engine must not clear ours.
    p[0]=0;
    if(e.receive_127489(p,25,17) || e.count()!=3) return false;
    e.open=true;
    e.receive_127489(p,sizeof(p),17);
    if(e.lamp() || e.open) return false;
    EcuFaults unavailable;
    p[20]=p[21]=p[22]=p[23]=255;
    unavailable.receive_127489(p,sizeof(p),17);
    if(unavailable.lamp()) return false;
    p[20]=p[21]=0; p[22]=0; p[23]=0x80; // Reserved flags are not decoded as faults.
    unavailable.receive_127489(p,sizeof(p),17);
    return !unavailable.lamp();
}
static_assert(ecu_checks(),"ECU faults must decode, page, clear, reject malformed/other-engine data, and ignore operating indicators");
