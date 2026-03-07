# Validate files on restore

This article describes what CRIU does to make sure it restores the correct set of files and how this file validation is implemented in CRIU. This project was completed under the [GSoC 2020 program](https://summerofcode.withgoogle.com/projects/#5773537320632320).

Note: This is NOT merged to CRIU, see https://github.com/checkpoint-restore/criu/pull/1148

## The previous implementation
Since CRIU doesn’t carry the contents of files into images while dumping (Except for ghost files), files that are being restored must be validated to make sure they are the “same” as they were during the dumping process (Especially true for ELF files since there is a risk of restoring executables or libraries of a different version). This was being done by only storing and comparing the size of the file. By itself, this isn’t a very strong check.

## The current implementation
The file size method is used as a preliminary method, if it fails there’s no need to do any of the more intensive checks, instead it will immediately give out an error and stop restoring. Stronger checks are used only if it passes.

The simplest and strongest check is to calculate the checksum for the entire file but this would be very intensive for large files and therefore not always feasible. A reasonable compromise would be to calculate the checksum only for certain parts of the file. This is the checksum method and it is one of the two methods that have been implemented in CRIU.

The other method is the build-ID method. The build-ID is a "strongly unique embedded identifier" that (If present) is stored in a particular note section of ELF files.

## Build-ID
The build-ID (If present) is stored in a note of type `NT_GNU_BUILD_ID` in the ELF file. All notes are in the note section which is a program header of type `PT_NOTE` in the ELF file. After the file has been mmap-ed, the first thing that needs to be done is to check whether the file is an ELF file or not. This is done by checking for the magic number. The next thing to do is to identify whether the file is a 32-bit ELF file or a 64-bit ELF file since the data types of the variables used to parse the ELF file will change depending on the bitness of the file (There are specific 32-bit and 64-bit variants of the data structures in `elf.h`) but the procedure will remain the same.

The position of the program headers is stored as an offset in `phoff`. Since all the program headers are stored in an arbitrary order, each program header needs to be checked. If a program header of type `PT_NOTE` is found, the position of this note section is stored as an offset in `p_offset`. The notes are stored in an arbitrary order as well, so each note needs to be checked. If a note of type `NT_GNU_BUILD_ID` is found, the build-ID is present in its description.

## Checksum
CRC32C is used to calculate the checksum. The only difference between CRC32C and CRC32 is the polynomial being used. CRC32C uses the Castagnoli polynomial (0x82F63B78 in little-endian notation) and CRC32 uses 0xEDB88320 (In little-endian notation).

The file is mapped 10 MB at a time and the checksum is calculated on the required bytes (Depending on the configuration set - Entire file, First N bytes of the file, or Every Nth byte of the file). N is the checksum parameter.

Adding a new configuration is quite simple and only requires the iterator to be moved to the necessary bytes (0 refers to the first byte of the file and 1 refers to the second byte and so on):
1. Input handling for the new configuration
1. The `checksum_iterator_init` function in *"criu/files-reg.c”* sets the initial iterator position (The first byte to calculate the checksum)
1. The `checksum_iterator_next` function in *“criu/files-reg.c”* moves the iterator to the next position (The next byte to calculate the checksum)
1. The `checksum_iterator_stop` function in *“criu/files-reg.c”* returns true when the iterator has reached its final position
There is a separate check in the `calculate_checksum` function to make sure the iterator refers to a valid byte (Not negative and smaller than the total number of bytes in the file). If the iterator is outside the mapped region but still valid, the required region of the file will be mapped.

## Using different validation methods and parameters
The build-ID method is much less intensive compared to the checksum method while still being a much stronger check than simply comparing the file size and is therefore the default. In other words, `--file-validation buildid` will not make a difference as this is the default method.

CRIU can also be configured to use the checksum method by default by using the `--file-validation option`:
- `--file-validation checksum-full` to calculate and use the checksum on the entire file.
- `--file-validation checksum` to calculate and use the checksum on the first N bytes of the file. The parameter N is set by using the `--checksum-parameter` option.
- `--file-validation checksum-period` to calculate and use the checksum on every Nth byte of the file (Including the first byte of the file). The parameter N is set by using the `--checksum-parameter option`.
By default, the checksum parameter N is set to 1024. If a method that doesn’t require the checksum parameter is being used, then the checksum parameter is simply ignored.
For example, to use the checksum method on the first 2048 bytes of the file: `--file-validation checksum --checksum-parameter 2048`

If the build-ID method is being used and is inconclusive (Maybe because the ELF file doesn’t contain a build-ID), then the checksum method on the first 1024 bytes of the file is used as a fallback. If the checksum method is being used and is inconclusive, then the build-ID method is used as a fallback. If both are inconclusive, only the file size check is used (And a warning is put out to inform the user that only a weak check has been used for that particular file).

To explicitly use only the file size check all the time, the following command-line option can be used: `--file-validation filesize` (This is the fastest and least intensive check).

## Performance impact
The values shown below are the average times it took to finish the ZDTM tests over multiple runs, and are only to indicate the impact each method has in general.

Each test has 3 files (Of sizes: 0.09 MB, 2 MB and 0.17 MB approximately) and each test is run 3 times (In Host, Namespace and User Namespace). For each file the checksum/build-ID is obtained twice (During dump and restore) therefore the function to find checksum/build-ID is called 18 times overall per test.

For reference, these tests were run on tmpfs (To remove any disk latency) and on an undervolted i5 4800H.

{| class="wikitable" style="text-align: center;
|+zdtm/transition/shmem:
|-
|File Size
|3.782s
|-
|Build-ID
|4.153s (~9% increase)
|-
|Checksum (First 1024 bytes)
|4.465s (~18% increase)
|-
|Checksum (Entire File)
|4.722s (~24% increase)
|-
|Checksum (Every 1024th byte)
|4.498s (~19% increase)
|}

{| class="wikitable" style="text-align: center;
|+zdtm/static/maps04:
|-
|File Size
|35.317s
|-
|Build-ID
|35.720s (~1% increase)
|-
|Checksum (First 1024 bytes)
|35.919s (~2% increase)
|-
|Checksum (Entire File)
|36.679s (~4% increase)
|-
|Checksum (Every 1024th byte)
|36.476s (~3% increase)
|}

## Scope for improvement and future work
- Calculating the checksum can be made faster by using a lookup table.


