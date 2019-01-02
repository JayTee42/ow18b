# Bluetooth access to the OWON (c) OW18B multimeter from Linux

The OW18B multimeter by OWON (c) is a digital multimeter with various settings and perks. It can be accessed via low-energy (LE) Bluetooth from mobile devices (Android / iOS) and the Windows desktop. The manufacturer has released closed-source applications for those platforms and there is no documentation about the data interchange format. I haven't looked into the mobile apps, but the desktop software is a huge Java mess that causes headaches. This repo contains a thin C layer that allows you to receive data samples from the OW18B on Linux and incorporate the device into your own data logging projects.

## Installation

- Drop **ow18b.h** and **ow18b.c** into your C project.
- Make sure you have *Bluez* (the Linux Bluetooth stack) and its headers installed. E. g., on Debian-based distributions, you need *libbluetooth-dev*. On Arch, it is *bluez-libs*. If `/usr/include/bluetooth/bluetooth.h` exists, you are probably fine :)
- When compiling, link against *BlueZ* by passing `-lbluetooth` to your linker.

## Interface

### Configuration

This layer provides you with two functions that can be used to receive data from the multimeter. Both expect a pointer to a configuration struct of type `ow_config_t`, so let's discuss its members first:

- `int dev_id`: The device ID of the Bluetooth adapter in the host system that shall be used. You can set this to `OW_DEV_ID_AUTOMATIC`. In that case, the layer calls `hci_get_route(NULL)` for you to determine the device ID of the first usable adapter.
- `ow_scan_mode_t scan_mode`: Somehow, we have to obtain the hardware address (six bytes) of your multimeter. `ow_scan_mode_t` is an enum that provides you with three approaches:
    - `OW_SCAN_MODE_NONE`: Don't perform an address scan at all. Instead, use the `addr` member of the `ow_config_t` struct to provide the address. This can be useful if you know your device's address and want to hardcode it into your project.
    - `OW_SCAN_MODE_AUTOMATIC`: Perform an automatic LE scan for the multimeter. Use the hardware address of the first device that advertises under the name `BDM` (that's at least what my multimeter does).
    - `OW_SCAN_MODE_MANUAL`: Perform an LE scan with the parameters provided in the `scan_params` member of the `ow_config_t` struct. I don't want to go into details about those parameters. The whole LE scan procedure has been ripped from the sources of *hcitool*, a system tool to discover Bluetooth devices. You can find it e. g. [here](https://github.com/pauloborges/bluez/blob/master/tools/hcitool.c). The scan parameters I have exposed are basically the parameters for the functions `hci_le_set_scan_parameters(...)` and `hci_le_set_scan_enable(...)`. You might also want to use the manual scan mode if your device uses a different name than `BDM` (see the `name` parameter of the `ow_scan_params` struct).
- `ow_connect_mode_t connect_mode`: As soon as we know the hardware address of the device, we want to connect to it. You have two choices:
    - `OW_CONNECT_MODE_AUTOMATIC`: Use default parameters to connect to the device. The relevant code comes again from *hcitool*, see above.
    - `OW_CONNECT_MODE_MANUAL`: Provide your own connection parameters in the `connect_params` member of the `ow_config_t` struct. Those are the parameters to the system function `hci_le_create_conn(...)`.

*TL;DR*: Just set the `dev_id` member to `OW_DEV_ID_AUTOMATIC`, the `scan_mode`member to `OW_SCAN_MODE_AUTOMATIC` and the `connect_mode` member to `OW_CONNECT_MODE_AUTOMATIC` as I do in *example.c*. In most cases, you should be fine.

### Receive functions

Now that you know the configuration struct, we can discuss the two interface functions:

- `bool ow_recv(const ow_config_t* config, ow_sample_func_t callback, void* context)`:
   This function uses the provided configuration struct to establish a connection to the multimeter. It then calls the `callback` function with new data samples until `false` is returned from it. A potential callback function might look like this: `bool callback(ow_sample_t sample, void* context)`. It receives new measurement samples of type `ow_sample_t` and the user-provided context that is passed to `ow_recv(...)`. If you need more samples, return `true`. Otherwise, return `false`.
   In the successful case, `ow_recv(...)` itself returns `true`. If any kind of error occurs, `false` is returned and `errno` is set to an appropriate value.
   See *example.c* for the usage of `ow_recv(...)`.
- `bool ow_recv_n(const ow_config_t* config, ow_sample_t* samples, int n)`:
   This function uses the provided configuration struct to establish a connection to the multimeter. It then collects `n` data samples and stores them to `samples`. Errors are indicated via the return value and `errno`, as described above.

### Measurement samples

Measurement samples are represented by the `ow_sample_t` struct. It has the following members:

- `ow_unit_t unit`: The unit of the sample. Units are:
    - Voltage units (`OW_UNIT_MILLIVOLT`, `OW_UNIT_VOLT`)
    - Current units (`OW_UNIT_MICROAMPERE`, `OW_UNIT_MILLIAMPERE`, `OW_UNIT_AMPERE`)
    - Resistance units (`OW_UNIT_OHM`, `OW_UNIT_KILOOHM`, `OW_UNIT_MEGAOHM`)
    - Capacity units (`OW_UNIT_NANOFARAD`, `OW_UNIT_MICROFARAD`, `OW_UNIT_MILLIFARAD`, `OW_UNIT_FARAD`)
    - Frequency units (`OW_UNIT_HERTZ`, `OW_UNIT_PERCENT` (for the relative mode))
    - Temperature units (`OW_UNIT_CELSIUS`, `OW_UNIT_FAHRENHEIT`)
    - Voltage detection units (`OW_UNIT_NEARFIELD`)
    - Unknown units (`OW_UNIT_UNKNOWN`)
- `ow_current_type_t current_type`: Allows to differentiate between DC and AC. Only for voltage and current units, otherwise undefined.
- `double value`: The signed floating point value of the sample. If the multimeter displays an overflow of any kind, this will be NaN (not a number).
- `bool is_continuity_test`: If the unit is `OW_UNIT_OHM`, this flag indicates if continuity testing is active. Otherwise, it is undefined.
- `bool is_diode_test`: If the unit is `OW_UNIT_VOLT`, this flag indicates if diode testing is active. Otherwise, it is undefined.
- `bool is_data_hold`: Indicates if the data hold mode is active.
- `bool is_relative`: Indicates if the relative mode is active.
- `bool is_auto_range`: Indicates if auto ranging is active.
- `bool is_low_battery`: Indicates if the battery of the multimeter runs low (battery icon on display).

You can use the helper functions `ow_unit_to_str(...)`, `ow_unit_to_short_str(...)` and `ow_current_type_to_str(...)` to obtain string representations of the corresponding enum values.

## Typical problems and errors

- Some Bluetooth system functions (e. g. `hci_le_set_scan_parameters(...)`) need elevated privileges. If you end up with `errno == EPERM`, try `sudo`.
- As soon as `ow_recv(...)` and `ow_recv_n(...)` return, they disconnect from the multimeter and close the Bluetooth session gracefully. If you interrupt them (e. g. via *CTRL+C*), the Bluetooth stack might get confused. In that case, a solution can be to restart the Bluetooth service (e. g. via `sudo systemctl restart bluetooth`) or to unplug and reinsert your Bluetooth stick.

## Internal data format

The multimeter uses the *ATT_HandleValueNoti* notification, wrapped into an L2CAP header, wrapped into an asynchronous HCI packet (ACL, type 0x02) to provide its data samples. Taking away those stacked headers, we end up with six effective bytes. I have inferred the following interpretation by trial-and-error, more info is always welcome :)

- First two bytes: Unit and decimal places

   We read the first two bytes as a little-endian `uint16_t` value. Its uppermost 13 bytes encode the unit of the current value, including information about AC/DC, continuity and diode test. Check the comments on the `ow_unit_t` enum in **ow18b.h** for the constants. Bit 2 (OF) indicates if the value is an overflow (1) or not (0). In the overflow case, bit 0 and 1 seem to be always 1, but I don't check that explicitly. If we don't deal with an overflow, bit 0 and 1 indicate the position of the decimal point (no point / one place behind / two places behind / three places behind).
- Third byte: Flags

    - Bit 0: Data hold mode (DHM)
    - Bit 1: Relative mode (RM)
    - Bit 2: Auto ranging (AR)
    - Bit 3: Low battery (LB)
- Fourth byte: Unused / Always zero?

- Last two bytes: Value and sign

   If the sample is not an overflow, this little-endian `uint16_t` value represents the numerical sample value itself. The lowermost 14 bit encode the displayed data as unsigned integer. Bit 14 seems to be unused (0) and bit 15 (SGN) is used for the sign. As a consequence, *no one- or two-complement is used* for negative values!

Summary:

      0                           7   8                          15  16                          23  24                          31  32                          39  40                          47
    +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
    |Places |OF |            Unit, AC/DC, continuity, diode         |DHM|RM |AR |LB |    Unused?    |            Unused?            |                         Value                         | 0 |SGN|
    +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
