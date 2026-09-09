# dreo-protocol
ESPhome component for Dreo fans via the wifi module<>MCU serial protocol.

Read https://www.davidc.net/content/hacking-dreo-htf-018s-serial-protocol for a description of this project
and instructions, particularly noting the warning to make sure your fan uses this protocol before flashing it.

Some sample packet captures and a python decoder are in [protocol/](protocol/).

An example full configuration is available in [example-dreo-htf018s-esp32c3.yaml](example-dreo-htf018s-esp32c3.yaml).
This is based on replacing the existing module with an ESP32C3 supermini.

There is also an example configuration for flashing the original MBL01 module in
[example-dreo-htf018s-mbl01.yaml](example-dreo-htf018s-mbl01.yaml).
without even having to open the case. To do this, compile and download a Beken OTA image. Then put the wifi module
into OTA mode (hold the oscillation button for 5 seconds until the countdown ends), connect to the captive access point,
and visit http://192.168.0.1 and upload the file. I have tested this allows me to install a functioning ESPhome
although I haven't tested the fan functionality - although that should be fine 

## Tested models

| Model      | Type  | Module  | Config | Status |
|------------|-------|---------|--------|--------|
| DR-HTF001S | Tower | -       | | Untested, but I believe this is an earlier version of HTF018S. Reports welcome. |
| DR-HTF004S | Tower | MBL02   | | Uses a different protocol, will not work. See [dreo-cloudcutter](https://github.com/ouaibe/dreo-cloudcutter) instead. |
| DR-HEC005S | Tower fan and humidifier | MBL01 (original) | [example-dreo-hec005s-mbl01.yaml](example-dreo-hec005s-mbl01.yaml) | Installed and verified on the original MBL01 module. |
| DR-HCF010S | Ceiling fan with main light and ambient RGB | ESP32-C3 (original CL187A carrier) | [example-dreo-hcf010s-esp32c3.yaml](example-dreo-hcf010s-esp32c3.yaml) | Installed and verified on the original carrier: fan, modes, reverse, both lights, timer, mute, remote convergence and over-the-air updates. |
| DR-HPF007S | Pedestal fan with presence sensing | MBL01 (original) | [example-dreo-hpf007s-mbl01.yaml](example-dreo-hpf007s-mbl01.yaml) | Installed and device-tested on the original MBL01: fan modes/speeds, both oscillation axes, calibration, presence, panel/remote and Wi-Fi indicator. Validation used a patched LibreTiny UART receive path; see the qualification below. |
| DR-HTF018S | Tower | MBL01 (original) | [example-dreo-htf018s-mbl01.yaml](example-dreo-htf018s-mbl01.yaml) | Tested ESPhome installs okay, not tested fan functionality. Reports welcome. |
| DR-HTF018S | Tower | ESP32C3 | [example-dreo-htf018s-esp32c3.yaml](example-dreo-htf018s-esp32c3.yaml) | Tested, works perfectly. |
| DR-HTF024S | Tower | MBL01 (original) | [example-dreo-htf024s-mbl01.yaml](example-dreo-htf024s-mbl01.yaml) | Untested but [may work](https://github.com/davidc/dreo-protocol/issues/1). |

The DR-HEC005S example includes its datapoint-backed RGB/effect light, validated humidity-threshold text, child lock,
and a model command policy that blocks state-dependent writes until the fan confirms the required power state.
It also drives the physical Wi-Fi icon from connection state: flashing after five seconds without Wi-Fi, off while
Wi-Fi is connected without a state-subscribing ESPHome API client, and solid while such a client is connected.

Compatibility evidence differs by model: DR-HTF018S on ESP32-C3 is behavior-tested,
DR-HTF018S on the original MBL01 is install-tested only, DR-HTF024S remains
untested, and DR-HEC005S is locally behavior-tested. The shared host suite and
compile fixtures provide regression evidence for those existing configurations;
they do not replace device testing. DR-HCF010S is device-tested on its original
carrier; its `dreo_ceiling_fan` coordinator models that fan's master power gate
and the remote's ambient preset cursor. DR-HPF007S is device-tested: its
`dreo_hpf007s` coordinator models the fan's modes and speed, the two
oscillation axes that share one datapoint, the structured sweep, head-position
and Custom-curve strings, the sensor-light gradient pairs, and presence
availability, and it derives the panel's Wi-Fi indicator state. Validation used
a patched LibreTiny UART receive path and does not establish reliability with
the unpatched framework. Occasional incomplete or unanswered reports recovered
during observation; this is not a zero-loss or maximum-latency guarantee.

## Protocol behavior the component implements

These are the observable behaviors of the shared `dreo` hub; each is covered by
the host suite in `tests/host/`.

- **Stream recovery.** A report the MCU abandons and retransmits is recovered:
  a candidate frame that fails its length or checksum is discarded from its
  first byte and the receiver rescans only for a complete `55 AA` header, so a
  retransmission that follows immediately is parsed. Nothing from a rejected
  candidate is published, a missing leading `55` is never reconstructed, and
  `55 AA` inside a valid body stays data.
- **Same-sequence replies.** With `acknowledge_reports: true` every valid
  datapoint report receives an empty `07` reply carrying the report's own
  sequence number; the reply creates no request state. The default is off,
  matching the models that were converted before this behavior was observed.
- **Module reset request.** An empty `10` from the MCU is acknowledged at the
  same sequence and restarts the protocol session (queue, pending writes,
  handshake) without touching stored configuration; `on_module_reset_request`
  fires.
- **Physical-input events.** A three-byte `0E` publishes `on_button_event`
  with numeric `origin`, `duration_seconds` and `button_id`; the meaning of
  each number is product-specific and belongs to the product package.
- **Atomic multi-datapoint writes.** `set_datapoint_values()` validates and
  authorizes every item before anything is recorded or sent, then emits one
  `06` frame with the items in order. Single-datapoint setters use the same
  path.
- **Configurable string adapters.** The writable `text` platform's length and
  pattern constraints are configurable (defaults unchanged), and the read-only
  `text_sensor` platform can clear its state so a client sees it as missing.
- **Per-product command encoding.** `command_datapoint_marker`,
  `integer_command_widths`, `enum_command_type`, `command_spacing` and
  `wifi_status_second_byte` let a package reproduce the exact frames its stock
  bridge sent without changing other products' defaults.

Please report back successes or failures. Please be careful and always ensure you have a way to restore the original firmware
if needed (e.g. UART), I accept no responsibility for bricked devices!
