# Code blobs

## Summary

There are two scenarios where CRIU operates in a specialized environment:

- [Parasite code](parasite-code.md) execution.
- [Restoration](restorer-context.md) of dumped page contents and yielding `rt_sigreturn` to continue execution of the original program.

## Building PIE Code Blobs for CRIU

Parasite code executes within the context of the dumpee process; therefore, it must be compiled as a Position-Independent Executable (PIE) and maintain its own stack. The same requirements apply to the restorer code used at the end of the restoration process.

To accommodate this, we reserve a static stack within CRIU for use during the checkpoint and restoration stages. To keep the source code maintainable, we employ the following techniques:

- The parasite code uses its own bootstrap logic, defined in a pure assembly file (`parasite_head.S`).
- The restorer bootstrap code is implemented more simply within `restorer.c`.

For both cases, we generate header files that include:

- Function offsets for export.
- A C array of binary data.

Example:

```c
#define parasite_blob_offset____export_parasite_args 0x000000000000002c
#define parasite_blob_offset____export_parasite_cmd 0x0000000000000028
#define parasite_blob_offset____export_parasite_head_start 0x0000000000000000
#define parasite_blob_offset____export_parasite_stack 0x0000000000006034

static char parasite_blob[] = {
	0x48, 0x8d, 0x25, 0x2d, 0x60, 0x00, 0x00, 0x48,
	0x83, 0xec, 0x10, 0x48, 0x83, 0xe4, 0xf0, 0x6a,
	0x00, 0x48, 0x89, 0xe5, 0x8b, 0x3d, 0x0e, 0x00,
	0x00, 0x00, 0x48, 0x8d, 0x35, 0x0b, 0x00, 0x00,
...
};
```

These headers are included in the CRIU source files and used during checkpoint/restore.

Generation of these files involves several steps:

1. All required object files are linked into `built-in.o`.
1. Using a linker script, code and data are moved to a specialized layout (i.e., sections with predefined names and addresses).
1. Using `objcopy`, the required section(s) are moved into a single binary file.
1. Using `hexdump`, a C-style data array is generated and placed into a `-blob.h` header.

## Example Building Procedure

```
  LINK     pie/parasite.built-in.o
  GEN      pie/parasite.built-in.bin.o
  GEN      pie/parasite.built-in.bin
  GEN      pie/parasite-blob.h
  
  LINK     pie/restorer.built-in.o
  GEN      pie/restorer.built-in.bin.o
  GEN      pie/restorer.built-in.bin
  GEN      pie/restorer-blob.h
```
