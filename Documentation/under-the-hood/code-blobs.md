# Code blobs

## Summary

There are two moments in time where criu runs in a somewhat strange environment

- [Parasite code](parasite-code.md) execution
- [Restore](restorer-context.md) of page dumped contents and yield rt-sigreturn to continue execution of the original program

## Building PIE code blobs for criu

Parasite code executes in the dumpee process context thus it needs to be [PIE](http://en.wikipedia.org/wiki/Position-independent_code)
compiled and to have own stack. The same applies to restorer code, which takes place at the very end of restore procedure.

Thus we need to reserve stack, place it statically somewhere in criu and use it at dump/checkpoint stages. To achieve
this (and still have some human way to edit source code) we use the following tricks

- Parasite code has own bootstrap code laid in a pure assembler file (parasite_head.S)
- Restorer bootstrap code is done in a simpler way in restorer.c. 

For both cases we generate header files which consist of

- Functions offsets for export
- C array of binary data, for example

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

These headers we include in the criu compiled file and then use them for checkpoint/restore.

Generation of these files is done in several steps

- All object files needed by are linked into 'built-in.o'
- With help of ld script we move code and data to special layout, i.e. to sections with predefined names and addresses
- With help of objcopy we move the section(s) we need to one binary file
- With help of hexdump we generate C-styled array of data and put it into -blob.h header

## Example of building procedure

  LINK     pie/parasite.built-in.o
  GEN      pie/parasite.built-in.bin.o
  GEN      pie/parasite.built-in.bin
  GEN      pie/parasite-blob.h
  
  LINK     pie/restorer.built-in.o
  GEN      pie/restorer.built-in.bin.o
  GEN      pie/restorer.built-in.bin
  GEN      pie/restorer-blob.h


