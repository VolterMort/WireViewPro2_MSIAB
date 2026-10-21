# WireView Pro II for MSI Afterburner

A free, MIT-licensed monitoring plugin that reads WireView Pro II directly over
USB and exposes its sensors in MSI Afterburner. Afterburner can display these
readings in the on-screen display (OSD) through RivaTuner Statistics Server
(RTSS).

**Version: v1.0 (Initial release).**
In-game OSD readings and the Pause > official application > Resume workflow
have been tested on one personal PC. See the tested configuration below.

<p align="center">
  <img width="406" height="628" alt="WireView Pro II Stats" src="https://github.com/user-attachments/assets/b2027a7a-db90-4b56-9ce1-1bf11fefc6fd" />
</p>

## Download

Download the ready-to-use `WireViewPro2.dll` from the **Releases** section
of this repository. The MIT license is provided alongside the DLL as `LICENSE`.
Follow the installation steps below; no archive extraction is needed.

The plugin is **32-bit (x86)** to match MSI Afterburner, including on 64-bit
Windows. You do not need a compiler or the Afterburner SDK to use the DLL.

## Sensors

- Total connector power (W), calculated as the sum of voltage times current
  across all six pins.
- Total current (A) and average voltage (V), reported by the firmware.
- Voltage, current, and power for each of the six 12 V pins.
- Internal IN/OUT temperatures and two external temperature probes.
- Fan duty cycle (%; not RPM).
- Connection status: 1 = recent valid reading, 0 = unavailable.
- Cable rating (W): the capability reported by the device, decoded as 600,
  450, 300, or 150 W. Unknown codes are reported as unavailable.

`WireView Cable rating` reads the capability field already present in the
sensor response. It does not expose or change the official application's
Auto/300W/600W selection. It represents a reported capability, not measured
power consumption or a user-configured power limit.

Power readings cover the monitored connector, not the graphics card's entire
power consumption. For example, power supplied through the PCIe slot is not
included. Temperature readings outside -40 to 200 degrees Celsius and data
older than 2.5 seconds are reported as unavailable using `FLT_MAX`.

## Requirements

- Windows, MSI Afterburner, and RTSS for the in-game OSD.
- A WireView Pro II connected to the PC **via USB as well as GPU power**.
- An available USB serial port. Close the official WireView application and
  disable WireView monitoring in other applications if they occupy the port.

## Tested configuration

Hardware testing has been performed on **one PC only: my personal PC**, with
the following configuration:

| Component | Tested version |
| --- | --- |
| Operating system | Windows 11 Pro 26H2, OS Build 26300.9550 |
| MSI Afterburner | v4.6.7.16935 Beta 2 |
| Device | WireView Pro II Black Reversed |
| Firmware | v05 (`TG-WV-PRO2-FW_20260902_0741`) |

In-game OSD readings and the Pause > official WireView application > Resume
workflow have been tested on this system. Other configurations have not yet
been tested.

## Installation and OSD setup

1. Exit MSI Afterburner completely using its **system tray icon > Exit**.
   Make sure it is no longer running in the background.
2. Copy `WireViewPro2.dll` into:

   ```text
   C:\Program Files (x86)\MSI Afterburner\Plugins\Monitoring
   ```

   Copying into Program Files may require administrator privileges. If
   Afterburner is installed elsewhere, use its `Plugins\Monitoring` folder.
3. Reopen MSI Afterburner.
4. Click **Settings > Monitoring > ...** to open the plugin selection window.
5. Enable the checkbox next to **WireViewPro2.dll**, then click **OK**.
6. Back on the **Monitoring** tab, select each `WireView ...` sensor you want
   to display. Enable its checkbox in **Active hardware monitoring graphs**
   and check **Show in On-Screen Display**.
7. Click **Apply**, then **OK**.
8. Make sure RTSS is running, then launch a game or another application that
   supports its OSD.

To uninstall, exit Afterburner and remove the DLL you copied.

To update, exit Afterburner and replace the installed DLL with the new build.
Existing source indices are preserved; `WireView Cable rating` is appended as
the 28th source. Restart Afterburner and enable the new sensor in Monitoring.

## Pause monitoring and release USB

Open **Settings > Monitoring > ...**, select `WireViewPro2.dll` in the plugin
list, and click **Setup**. Leave the plugin enabled.

1. Click **Pause / Release USB**.
2. Wait for **Paused. USB released.** before opening the official WireView app.
3. Use the official app, then exit it completely so it releases the port.
4. Open the plugin's Setup dialog again and click **Resume monitoring**.

You can close the dialog while paused; this does not resume polling. Pausing
does not close Afterburner or affect its other monitoring sources. WireView
readings become unavailable and `Connected` becomes 0. The worker closes the
USB handle before confirming release, and paused source reads do not reopen it.

If you resume while another app still owns USB, the plugin reports that it is
waiting for the device/port and retries approximately every three seconds.
Fresh readings resume after the port becomes available. The pause request is
asynchronous, so the dialog remains responsive while a read finishes or is
cancelled. With a malfunctioning driver, release may take longer; rely on the
confirmation rather than assuming the port is already free.

Pause is held for the lifetime of the loaded plugin instance. Restarting
Afterburner or unloading/reloading the DLL resets monitoring to enabled.
No device settings or firmware are changed.

## Connection and limitations

The plugin detects STM32 USB serial ports with VID `0483` and PID `5740`, then
checks the device identity (`EF 05` or `EF 06`) before reading sensors. It polls
approximately once per second and retries after errors. It sends only the
read-identity command `01` and read-sensors command `04`. It does not change
the device firmware, thresholds, or configuration.

The protocol has no CRC or framing. The plugin checks response length, padding,
and several physical limits to reject corrupt responses, but these checks
cannot detect every corruption. The plugin is intended for displaying readings,
not making protection decisions.

Multiple simultaneous devices and port sharing with the official application
are not implemented. If multiple compatible devices are connected, the first
one that responds is used. The Setup dialog allows manual USB handover through
pause/resume; it does not provide simultaneous access for both applications.

## Acknowledgements and technical references

Special thanks to [emaspa](https://github.com/emaspa) for
[wireview-hwmon](https://github.com/emaspa/wireview-hwmon). That project was
used as a reference and helped with understanding how to read WireView Pro II
data over USB.

This version of the plugin was developed with assistance from **OpenAI Codex**.

## License

This project is available under the [MIT License](LICENSE).

This is an unofficial project with no affiliation with Thermal Grizzly, MSI,
or the author of RTSS.
