# AM2320 Linux Driver

This is a Linux HWMON driver for the AM2320, AM2321, and AM2322 temperature and humidity sensors.

The [it87 Makefile](https://github.com/a1wong/it87/blob/master/Makefile) was also used as an example.

## Installation

```sh
sudo make dkms
sudo modprobe am2320
echo am2320 0x5c | sudo tee /sys/class/i2c-dev/i2c-1/device/new_device
```

## Uninstallation

```sh
echo 0x5c | sudo tee /sys/class/i2c-dev/i2c-1/device/delete_device
sudo modprobe -r am2320
sudo make dkms_clean
```
