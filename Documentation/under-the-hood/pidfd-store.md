# Pidfd Store

The `pidfd` store increases the reliability of PID reuse detection during pre-dumps and dumps by using task `pidfds` instead of task creation times.

This feature is supported only via RPC and the C library.

## Usage

A connectionless UNIX socket is passed to CRIU during each pre-dump or dump operation using the `pidfd_store_sk` RPC option or the `criu_set_pidfd_store_sk` library routine.

**NOTE**: This feature is intended for migration tools like P.Haul, as the provided socket must remain active throughout all pre-dump and dump iterations.

## Feature Check

This feature requires the `pidfd_open` and `pidfd_getfd` system calls. Support can be verified via:

- **CLI**: `criu check --feature pidfd_store`
- **RPC**: Use `CRIU_REQ_TYPE__FEATURE_CHECK` and set `pidfd_store` to `true` in the `features` field of the request.

## How It Works

The `pidfd_store_sk` serves as a queue for task `pidfds`. CRIU sends task `pidfds` to this socket and retrieves them during the subsequent pre-dump or dump iteration. These `pidfds` are then used to verify if the task is still active. If the task is no longer alive, CRIU detects this as a PID reuse scenario and performs a full page dump.
