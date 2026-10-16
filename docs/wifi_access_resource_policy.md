# Trail Mate Wi-Fi Access Resource Policy

This specification fixes the boundary for Wi-Fi resource usage on constrained
Trail Mate devices. It prevents HTTP downloads, MQTT, Reticulum Wi-Fi gateway,
OTA, SD writes, and UI wake rendering from competing as independent owners of
the same small device resources.

Runtime ownership rules for calls, protocol traffic, audio, font loading, and
UI projection are frozen by
`docs/specification/RUNTIME_OWNERSHIP_BOUNDARY_FREEZE.md`. This Wi-Fi policy is
the resource-specific child contract for the Wi-Fi owner.

## Distinctions

- `platform::ui::wifi` is the Wi-Fi control plane. It owns STA configuration,
  enable/disable, scanning, saved credentials, saved network profiles, and the
  platform connection API.
- `platform::ui::wifi_access` is the Wi-Fi business access policy. Any product
  feature that wants to use Wi-Fi for protocol traffic or downloads must ask it
  for permission, connection, and traffic budget.
- `platform::ui::http_client` is the only ESP HTTP/TLS byte-stream
  implementation. Product features must not create `esp_http_client` directly.
- MQTT and Reticulum Wi-Fi gateway are long-lived protocol users. They keep
  their own protocol socket state, but connection attempts and pump budgets are
  governed by `wifi_access`.
- Reticulum bearer strategy is selected above the Wi-Fi access layer. `Auto`
  means Wi-Fi-preferred terminal mode; once the gateway socket is ready, the
  Reticulum runtime must suppress shared LoRa RX instead of running LoRa and
  Wi-Fi as parallel receive paths.
- OTA is an exclusive Wi-Fi/HTTP activity. During OTA, long-lived protocol
  users must not consume pump budget beyond what `wifi_access` grants.

## Legal Access Paths

| Use case | Required path |
| --- | --- |
| Save Wi-Fi credentials, scan, manual settings connect | `platform::ui::wifi` control-plane APIs |
| HTTP metadata or small text fetch | `platform::ui::http_client::get_text` |
| HTTP file/asset/package download | `platform::ui::http_client::download` |
| Firmware OTA byte stream | `platform::ui::http_client::download` with `AccessKind::OtaDownload` |
| MQTT Wi-Fi connect | `platform::ui::wifi_access::ensure_connected` |
| MQTT socket connect/pump/send | `wifi_access::acquire` and `wifi_access::traffic_budget` |
| Reticulum gateway Wi-Fi connect | `platform::ui::wifi_access::ensure_connected` |
| Reticulum gateway socket connect/pump/send | `wifi_access::acquire` and `wifi_access::traffic_budget` |

## TLS Allocation Policy

TLS allocator policy belongs to platform startup, before network clients or
background services run. mbedTLS allocator hooks are process-wide and must not
be installed or changed by an individual HTTP request. On PSRAM-equipped
targets TLS allocations must remain external and fail normally if PSRAM is
exhausted, preserving internal memory for radio, DMA and task stacks. Arduino
uses the pinned SDK's allocator hook; native P4 IDF builds select the external
mbedTLS allocation configuration. See the [ESP RAM audit](engineering/esp-ram-audit.md)
for measured static allocations and the distinction from runtime heap peaks.

## Wi-Fi Connect Memory Gate

Starting an ESP STA connection is a resource boundary, not a harmless retry.
When the configured SSID is absent, the ESP-IDF Wi-Fi stack may enter scan and
connect paths that create internal timers and allocate internal RAM. On Trail
Mate devices this must be guarded before calling `esp_wifi_connect()`, because
low internal heap can otherwise abort the process inside the vendor Wi-Fi task
instead of returning a normal failure.

The Wi-Fi control plane must therefore check internal RAM and the largest
contiguous internal block before every `esp_wifi_connect()` call. If the gate
fails, it must log a deferred memory line and return a normal connection
failure to the caller; the caller's existing `wifi_access` backoff then governs
the next attempt. This rule applies equally to manual settings connect, MQTT,
Reticulum Wi-Fi gateway, HTTP pre-connect, and OTA pre-connect paths.

## Saved Wi-Fi Profiles

Trail Mate devices must remember up to ten Wi-Fi STA profiles. The legacy
`wifi_ssid` and `wifi_password` settings are retained only as the current
preferred-profile projection for existing UI and SD working-configuration
compatibility; they are
not the only source of Wi-Fi truth.

Settings must not expose the saved profile list as a separate management UI.
The Settings page shows one current SSID/password editor plus scan/connect
actions. When the user selects a scanned SSID that is already saved, the control
plane may restore its saved password into that single editor. When the scanned
SSID is new, the editor clears the password and waits for the user to enter it.

Saving Wi-Fi credentials must insert or update that SSID at the front of the
saved profile list and keep older unique SSIDs behind it. Background auto
connect must first run a bounded Wi-Fi scan under the granted connection budget
and prefer a saved profile whose SSID is currently visible. If no saved SSID is
visible, it may attempt one saved profile per granted connection attempt and
then advance to the next profile on failure. It must not synchronously try the
whole list in one call, because an absent SSID can consume a full connect
timeout.
Refreshing or loading Wi-Fi settings must preserve the pending auto-connect
profile cursor; otherwise UI status refreshes can pin auto connect to the first
saved SSID forever.

Manual connect with an explicit `Config` still targets only that profile. A
successful or newly saved manual profile becomes the preferred profile.

## Illegal Access Paths

- No product feature may include or call `esp_http_client` directly outside the
  ESP HTTP runtime.
- MQTT and Reticulum gateway must not call `platform::ui::wifi::connect`
  directly.
- New Wi-Fi-backed protocol runtimes must not invent their own reconnect,
  wake-protection, or pump-budget policy.
- New background downloads must not bypass `wifi_access` by using raw sockets or
  direct HTTP clients.

## Screen-State Policy

Trail Mate is a low-frequency terminal, not a desktop router node.

| Device phase | MQTT | Reticulum Wi-Fi gateway | HTTP/downloads |
| --- | --- | --- | --- |
| Screen on | Connected messaging is allowed with normal terminal budget. | Direct gateway traffic is allowed with terminal budget. | User foreground work is allowed; background work is deferred. |
| Wake protected | Existing sockets may use minimal budget. New reconnects and HTTP starts are deferred. | Existing sockets may use minimal budget. New reconnects are deferred. | Deferred. |
| Screen off | MQTT may connect and normally send/receive. | Gateway may connect and receive with expanded terminal budget. | Queued maintenance/background work may run, one transaction at a time. |
| Foreground HTTP pending | Suspended. No connect, read, write, publish, or downlink bridge budget. | Suspended. Existing gateway socket may be closed by its budget handler. | The pending foreground download owns the next HTTP opportunity while it waits for memory/SD/UI resources. |
| HTTP active | MQTT and Reticulum are reduced to minimal messaging budget. | Reduced to minimal gateway budget. | Exactly one HTTP transaction owns the HTTP resource. |
| OTA active | Protocol pump budget is suspended. | Protocol pump budget is suspended. | OTA is exclusive. |
| Reticulum call active | Suspended. MQTT sockets must not connect, pump, publish, or consume RX budget. | Exclusive traffic for the current LXST telephony Link only. Discovery, public announce ingest, route/path background noise, and non-call link work are deferred. | Denied. No HTTP, OTA, catalog, pack, language, image, or route download may start. |

The wake-protected phase shields the path from screen sleep to screen wake to
entering the UI. It is intentionally short and prevents reconnect storms, TLS
allocation, route image downloads, and catalog/update checks from causing
visible UI stalls.

## Reticulum Call Realtime Mode

Reticulum calls are a realtime resource mode, not a Contacts-page feature. The
Call Page is the visible entry point for this mode, but it is not the
owner of Wi-Fi, LoRa, GPS, BLE, or audio resources. Resource ownership is held by
runtime leases underneath the call runtime.

The runtime state is:

| Call realtime phase | UI ownership | Resource ownership |
| --- | --- | --- |
| `IncomingRinging` | The Call Page owns navigation and focus. | Soft preempt. New non-call Wi-Fi work is denied. Existing non-call Wi-Fi work reaches its normal poll/revoke point. LoRa and GPS runtime work are paused. BLE is not compiled into the ESP product runtime. |
| `AcceptedStarting` | The Call Page shows the call is connecting. | Hard preempt. The current LXST telephony Link starts the Reticulum call Wi-Fi exclusive lease. Non-call Wi-Fi leases are revoked. |
| `ActiveCall` | The active Call Page owns navigation and focus. | Exclusive call lease. Only Reticulum gateway traffic serving the current LXST telephony Link may read or write. |
| `ClosingCall` | The Call Page may show closing or hanging up. | The previous ownership remains in force until `LinkClose` is sent or cleanup finishes. Rejecting an incoming call that never entered hard preempt keeps only the soft preempt while close signalling is attempted. |
| `Idle` | The interruption navigator restores the previous page. | Normal Wi-Fi scheduling resumes and deferred work may be queued again. |

Outgoing calls enter `AcceptedStarting` immediately. The user explicitly asked to
place the call, so the call runtime must acquire the hard Wi-Fi preempt lease
before dialing proceeds.

Incoming calls enter `IncomingRinging` first. This phase still pauses LoRa and
GPS runtime work so the device is quiet for the user-visible interruption,
but it does not revoke already-running Wi-Fi work unless that work reaches a
normal lease or budget poll point. If the user declines, the runtime must not
escalate to hard Wi-Fi preempt.

When a Sideband-compatible `lxst.telephony` Link is accepted, active, or closing
after acceptance, the device must protect the audio path first:

- Wi-Fi access grants budget only to the Reticulum gateway client, and only for
  the call link's Reticulum packets.
- LoRa polling, GPS collection, public Reticulum discovery,
  periodic announces, MQTT, HTTP, OTA, route/image downloads, language-pack
  downloads, catalog refreshes, and SD-backed discovery persistence are paused,
  denied, or coalesced.
- Codec2/ES8311 media owns the audio hardware for the call. Other background
  hardware users must not start while the Call Page is active.
- Incoming calls may wake the screen and open the Call Page, but they must
  not trigger unrelated Wi-Fi reconnect storms, SD scans, or UI-heavy refreshes.

Wi-Fi users are grouped by preemption safety:

- `Preemptible`: MQTT, catalog refresh, route image downloads, language-pack
  downloads, ordinary HTTP metadata/downloads, ordinary Reticulum discovery,
  path requests, announces, and non-call LXMF pump work. These must stop,
  pause, or be denied after hard preempt.
- `GracefulAbort`: foreground HTTP downloads and larger resumable transfers.
  These receive revoke at a poll point, close their socket, and leave resumable
  state with their owner.
- `NonPreemptible`: OTA write/finalize/switch critical sections and short
  critical persistence commits. A call cannot directly interrupt this class.
  Accepting or placing the call fails while such an activity is active, and the
  UI reports the call as unavailable or busy.

After hangup, the runtime may flush deferred SD/cache work and resume normal
low-frequency terminal budgets. Hangup signalling itself remains inside the
call realtime mode so the LinkClose packet can be sent before other Wi-Fi users
resume.

## Enforcement

The CI boundary job runs `scripts/check_wifi_access_policy.py`. The check fails
when:

- `esp_http_client` appears outside `platform_ui_http_client_runtime.cpp`.
- ESP protocol/download business modules call `platform::ui::wifi::connect`
  directly instead of going through `wifi_access`.

Any exception must first update this specification and the checker allow-list.
