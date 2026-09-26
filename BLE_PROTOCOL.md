# BLE protocol

The firmware advertises service `4fafc201-1fb5-459e-8fcc-c5c9c331914b`. Clients
must discover towers by this service UUID; the BLE local name is user-settable
and must not be treated as a stable device identifier.

## Device name

| Characteristic | UUID | Properties | Format |
| --- | --- | --- | --- |
| Device name | `4fafff0d-1fb5-459e-8fcc-c5c9c331914b` | Read, Notify | UTF-8, normalized name |
| Device name control | `4fafff0e-1fb5-459e-8fcc-c5c9c331914b` | Read, Write, Notify | UTF-8 write; status on read/notify |

The name is stored in NVS (`device` / `name`) and defaults to `Rheinturm` when
missing or invalid. A name write must use the encrypted, bonded link. The
device trims leading and trailing spaces, accepts 1-20 Unicode code points,
Latin letters (including German umlauts), ASCII digits, spaces, and hyphens,
and rejects names whose UTF-8 encoding exceeds 29 bytes. The byte limit leaves
the two-byte AD header inside the 31-byte legacy BLE scan-response limit.

After a valid write, the device-name characteristic contains the normalized
name and the control characteristic reports `name-ok`. Invalid writes leave
the stored and readable name unchanged and report `name-fail:<reason>` where
`<reason>` is one of `empty`, `invalid_utf8`, `invalid_character`,
`too_many_characters`, `too_many_bytes`, or `storage`.

The service UUID stays in the primary advertisement and the complete local
name is in the scan response. If a tower is connected while it is renamed, the
GAP name and GATT value update immediately; the refreshed scan response is
advertised when the current client disconnects. When no client is connected,
advertising restarts immediately with the new name.

Existing characteristic UUIDs and behavior are unchanged.
