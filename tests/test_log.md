# Firmware test log

=== Test run 2026-08-23 15:25:58 ===
Port COM8 @ 115200
[PASS] G1  in WELCOME state
[PASS] G2  160m -> 80m -> 60m -> 40m -> 30m -> 20m -> 17m -> 15m -> 12m -> 10m
[PASS] G3  seen: curve,numeric
[PASS] G4  98pts ok=True Ravg=50.0 Xavg=0.0 SWRmin=1.00 plausible=True passthrough_agree=True (R_fw=50.0 vs R_pt=50.0)
[PASS] G5  entered CALIBRATE and swept the single 50-ohm phase via START (see calibrate_guide.py for the hardware procedure)

## Summary

- Passed: 5
- Failed: 0
- Result: ALL PASS
