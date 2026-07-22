# Quest Device Notes

## Battery Drain During USB Development

A USB data connection does not guarantee that the computer supplies enough power to offset an active Quest 3's consumption. Passthrough, tracking, displays, CPU, and GPU workloads can therefore discharge the battery slowly even when the headset reports that it is charging.

Inspect the current battery and charging state with:

```bash
adb shell dumpsys battery
```

Relevant fields include `AC powered`, `USB powered`, `Max charging current`, `Max charging voltage`, `level`, and `temperature`. Input power is approximately the reported maximum current multiplied by the maximum voltage; actual charging power may be lower. Charging can also slow when the headset becomes warm.

For longer development sessions:

- use a power-capable USB-C port and a suitable power/data cable;
- use a powered USB-C hub with Power Delivery passthrough;
- use wireless ADB while powering the headset from its wall charger;
- close the XR application while compiling or when active testing is unnecessary;
- allow the headset to cool if its battery temperature remains elevated.

Slow discharge during demanding XR workloads usually indicates insufficient net input power, not a failed ADB connection.
