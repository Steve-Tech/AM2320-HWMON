# AM2320 Linux HWMON Driver

This is a Linux HWMON driver for the AM2320, AM2321, and AM2322 temperature and
humidity sensors, connected via I2C.

## Installation

This will install the driver as a DKMS module, which will allow it to be
recompiled when the kernel is updated.

```sh
sudo make dkms
sudo modprobe am2320
echo am2320 0x5c | sudo tee /sys/class/i2c-dev/i2c-1/device/new_device
```

### Install the Device Tree Overlay

If you are using a Raspberry Pi, you can install the device tree overlay to
automatically load the driver at boot time. You may need to adjust the I2C bus
target (`target = <&i2c1>;`) in the `am2320.dts` on other platforms.

```sh
sudo make dtoverlay
```

## Uninstallation

```sh
echo 0x5c | sudo tee /sys/class/i2c-dev/i2c-1/device/delete_device
sudo modprobe -r am2320
sudo make dkms_clean
```

### Remove the Device Tree Overlay

If you installed the device tree overlay, you can remove it with the following command:

```sh
sudo make dtoverlay_clean
```
