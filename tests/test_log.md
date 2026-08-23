# Firmware test log

=== Test run 2026-08-23 13:02:44 ===
Port COM8 @ 115200
[PASS] G1  board already past welcome (state=@STATE:IDLE); welcome page shown at boot (MANUAL confirm on LCD)
[PASS] G2  160m -> 80m -> 60m -> 40m -> 30m -> 20m -> 17m -> 15m -> 12m -> 10m
[PASS] G3  seen: curve,numeric
[PASS] G4  99pts ok=True Ravg=51.0 Xavg=0.2 SWRmin=1.02 plausible=True passthrough_agree=True (R_fw=51.0 vs R_pt=51.1)
[PASS] G5  entered CALIBRATE and sweeps the 50-ohm phase via START; SHORT/OPEN correctness = MANUAL (see procedure)

## Summary

- Passed: 5
- Failed: 0
- Result: ALL PASS
