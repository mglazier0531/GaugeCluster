"""Observe a complete demo alarm cycle over UART; requires pyserial."""
import argparse
import re
import time
import serial

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--port',default='COM9')
parser.add_argument('--seconds',type=float,default=90)
args=parser.parse_args()
seen=set()
heaps=[]
faults=[]
pending=''
with serial.Serial(args.port,115200,timeout=.2) as uart:
    deadline=time.monotonic()+args.seconds
    while time.monotonic()<deadline:
        pending+=uart.read(8192).decode(errors='replace').replace('\r','')
        while '\n' in pending:
            line,pending=pending.split('\n',1)
            alarm=re.search(r'alarm: (RPM|TEMP|OIL|FUEL|VOLTAGE) (CRITICAL|WARNING|CLEAR)',line)
            if alarm:
                seen.add(alarm.groups())
                print(line,flush=True)
            heap=re.search(r'free heap=(\d+)',line)
            if heap: heaps.append(int(heap[1]))
            if any(word in line.lower() for word in ('panic','guru meditation','assert failed','watchdog')):
                faults.append(line)
                print(line,flush=True)
required={(name,state) for name in ('RPM','TEMP','OIL','FUEL','VOLTAGE') for state in ('CRITICAL','CLEAR')}
required.add(('FUEL','WARNING'))
missing=required-seen
if heaps: print(f'Free heap range: {min(heaps)}..{max(heaps)} bytes')
if missing or faults:
    raise SystemExit(f'FAIL: missing transitions={sorted(missing)}, faults={faults}')
print('PASS: every critical alarm triggered and cleared; fuel warning observed; no runtime faults.')
