# Firmware test log

=== Test run 2026-08-23 16:21:07 ===
Port COM8 @ 115200
[PASS] G1  board already past welcome (state=@STATE:IDLE); welcome page shown at boot (MANUAL confirm on LCD)
[PASS] G2  160m -> 80m -> 60m -> 40m -> 30m -> 20m -> 17m -> 15m -> 12m -> 10m
[PASS] G3  seen: curve,numeric
[PASS] G4  98pts ok=True Rmed=50.0 Xmed=0.0 SWRmin=1.00 plausible=True passthrough_agree=True (R_fw=50.0 vs R_pt=50.0)
[PASS] G5  entered CALIBRATE; swept the single 50-ohm phase to CAL_DONE (phase telemetry=False band telemetry=True)
[PASS] G7  ext=True responsive=True local=True (b1=160m b2=80m)

## Summary

- Passed: 6
- Failed: 0
- Result: ALL PASS
