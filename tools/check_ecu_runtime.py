"""Observe the simulated ECU status cycle over UART; requires pyserial."""
import argparse
import re
import time
import serial

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--port',default='COM9')
parser.add_argument('--seconds',type=float,default=90)
args=parser.parse_args()
seen=set(); faults=[]; pending=''; had_fault=False; cleared=False
with serial.Serial(args.port,115200,timeout=.2) as uart:
    deadline=time.monotonic()+args.seconds
    while time.monotonic()<deadline:
        pending+=uart.read(8192).decode(errors='replace').replace('\r','')
        while '\n' in pending:
            line,pending=pending.split('\n',1)
            state=re.search(r'ecu: SIMULATED lamp=(ON|OFF) status1=0x([0-9a-f]+) status2=0x([0-9a-f]+) faults=(\d+)',line)
            if state:
                lamp,s1,s2,count=state.groups()
                seen.add((int(s1,16),int(s2,16),lamp,int(count)))
                if lamp=='ON': had_fault=True
                elif had_fault: cleared=True
            if 'ecu:' in line: print(line,flush=True)
            if any(word in line.lower() for word in ('panic','guru meditation','assert failed','watchdog')): faults.append(line)
required={(0x400,0,'OFF',0),(0,0,'OFF',0),(3,0,'ON',1),(1,0,'ON',1),(0x221,4,'ON',3)}
missing=required-seen
if missing or faults or not cleared:
    raise SystemExit(f'FAIL: missing={sorted(missing)}, faults={faults}, fault cleared={cleared}')
print('PASS: healthy/preheat icon OFF, known/general/multiple ECU faults ON, and explicit clear OFF; no runtime faults.')
