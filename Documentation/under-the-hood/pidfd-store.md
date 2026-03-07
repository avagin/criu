# Pidfd store

Pidfd store increases the reliability of pid reuse detection during pre-dumps/dumps using task pidfd instead of task creation time.

It is only supported for RPC and the C library.

## Usage

A connectionless unix socket is passed to CRIU during each pre-dump/dump through
the RPC option `pidfd_store_sk` or `criu_set_pidfd_store_sk` routine in the the library.

<b>NOTE</b>: This is targeted at migration tools like P.Haul, because the passed socket must be kept alive throughout all pre-dump/dump iterations.

## Feature check

This feature requires `pidfd_open` and `pidfd_getfd` syscalls.
Support could be checked with:
  CLI: `criu check --feature pidfd_store`.
  RPC: `CRIU_REQ_TYPE__FEATURE_CHECK` and set `pidfd_store` to true in the "features" field of the request

## How it works

The `pidfd_store_sk` is used as a queue for task pidfds. CRIU sends tasks pidfds to this socket and receives them in the next pre-dump/dump iteration. Those pidfds could then be used to check whether the task is still alive, otherwise it is a case of pid reuse and CRIU should make a full page dump.



