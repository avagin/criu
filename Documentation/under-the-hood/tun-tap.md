# Tun-Tap

Tun-Tap devices, often used by software like OpenVPN, are supported by CRIU.

## Devices
The network device entry is stored in the `netdev-ID.img` image file and includes an optional `tun` field.

## Files
For every TUN device, there is a corresponding `reg-file` entry in the [images](images.md). Additionally, an entry is created in the `tunfile` image. CRIU performs device-to-file mapping based on the device name.
