# Mac-Vlan

CRIU supports checkpointing and restoring network namespaces with macvlan devices.

## Dump

On dump, criu will automatically detect these devices and no extra arguments are needed. The name of macvlan device inside the checkpointed namespace is saved to [images](images.md).

## Restore

On restore, users *must* specify the master device in the host network namespace via `--external macvlan[*inner_dev*]:*outer_dev*`, where `*inner_dev*` is the device name in restored namespace, and `*outer_dev*` is a network device existing in the same namespace as CRIU.

## Implementation details

The restore process for macvlan interfaces is somewhat convoluted, since the actual macvlan interface lives inside the network namespace, but the master device lives outside. CRIU uses `IFLA_NET_NS_ID` to specify the network namespace that the master link lives in, and uses `IFLA_NET_NS_FD` to specify the network namespace the slave link should be created in. In the user namespace case, the netlink call is made from usernsd, since the caller needs to have CAP_NET_ADMIN in both network namespaces. In the non-userns case, we setns around to create a netlink socket in CRIU's netns, and then use that socket to actually create the macvlan link.




