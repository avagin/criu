# Tun-Tap

Tun-Tap devices are used by e.g. OpenVPN software. CRIU has support for them.

## Devices

The netdevice entry sits in `netdev-ID.img` image file and has optional `tun` field.

## Files

Other than this there's always a reg-file entry in the [images](images.md) for tun deivce. In addition to this there's an entry in the `tunfile` image. The device-to-file mapping is performed by device name.


