#pragma once
#include "gauge_limits.h"

struct HiddenAlerts {
    bool open=false;
    unsigned mask=0,critical_mask=0;
    Alert levels[gauge_count]={},acknowledged[gauge_count]={};
    constexpr bool update(const Gauge (&displayed)[5],const Alert (&current)[gauge_count]) {
        unsigned next_mask=0,next_critical=0;
        bool next_open=false;
        for(unsigned i=0;i<gauge_count;i++) {
            bool shown=false;
            for(Gauge gauge:displayed) if(static_cast<unsigned>(gauge)==i) shown=true;
            if(shown || current[i]==Alert::Normal) {
                levels[i]=Alert::Normal; acknowledged[i]=Alert::Normal;
                continue;
            }
            levels[i]=current[i]; next_mask|=1u<<i;
            if(current[i]==Alert::Critical) next_critical|=1u<<i;
            if(static_cast<unsigned>(current[i])>static_cast<unsigned>(acknowledged[i])) next_open=true;
        }
        bool changed=mask!=next_mask || critical_mask!=next_critical || open!=next_open;
        mask=next_mask; critical_mask=next_critical; open=next_open;
        return changed;
    }
    constexpr void acknowledge() {
        for(unsigned i=0;i<gauge_count;i++) if(mask&(1u<<i)) acknowledged[i]=levels[i];
        open=false;
    }
};
