# Mac-Vlan

CRIU supports checkpointing and restoring network namespaces that use `macvlan` devices.

## Dump

During a dump, CRIU automatically detects these devices; no additional arguments are required. The name of the `macvlan` device within the checkpointed namespace is saved in the [images](images.md).

## Restore

During restoration, users *must* specify the master device in the host network namespace using the following syntax:

`--external macvlan[*inner_dev*]:*outer_dev*`

Where `*inner_dev*` is the device name within the restored namespace, and `*outer_dev*` is the corresponding network device in CRIU's namespace.

## Implementation Details

Restoring `macvlan` interfaces is complex because the interface itself resides within the target network namespace, while its master device resides outside. CRIU uses `IFLA_NET_NS_ID` to specify the master link's namespace and `IFLA_NET_NS_FD` to specify the namespace where the slave link should be created.

In cases involving user namespaces, the `netlink` call is made from `usernsd`, as the caller must have `CAP_NET_ADMIN` in both network namespaces. In non-usernamespace cases, CRIU uses `setns` to create a `netlink` socket within the target namespace and then uses that socket to create the `macvlan` link.
