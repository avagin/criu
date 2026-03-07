# Validating Files on Restore

This article describes how CRIU ensures it restores the correct set of files and how this validation is implemented. This project was completed as part of the [GSoC 2020 program](https://summerofcode.withgoogle.com/projects/#5773537320632320).

**Note**: This feature is currently maintained in a [pull request](https://github.com/checkpoint-restore/criu/pull/1148) and has not yet been merged into the main CRIU repository.

## Previous Implementation
Because CRIU does not include the full contents of files (except for ghost files) in its dump images, it must validate that the files present during restoration are identical to those captured during the dump. This is particularly important for ELF files to prevent restoring incompatible versions of executables or libraries. Previously, CRIU only stored and compared the file size, which is a relatively weak check.

## Current Implementation
The current implementation uses the file size check as an initial filter. If the size does not match, restoration is aborted immediately. If the size matches, CRIU proceeds with more rigorous validation methods.

### Checksum Method
The strongest check is calculating a checksum for the entire file. However, this can be performance-intensive for large files. As a compromise, CRIU can be configured to calculate checksums for specific portions of a file.

### Build-ID Method
The second method uses the Build-ID, a "strongly unique embedded identifier" found in many ELF files.

## Build-ID Details
If present, the Build-ID is stored in an `NT_GNU_BUILD_ID` note within a `PT_NOTE` program header. After `mmap`-ing the file, CRIU verifies the ELF magic number and determines if it is a 32-bit or 64-bit file to use the appropriate data structures from `elf.h`. 

CRIU then iterates through the program headers (located at the `phoff` offset) to find the `PT_NOTE` section. Within that section (at `p_offset`), it searches for the Build-ID note.

## Checksum Details
CRIU uses CRC32C (utilizing the Castagnoli polynomial `0x82F63B78`) for checksum calculation. The file is mapped in 10 MB increments, and the checksum is calculated based on the configuration:
- Entire file.
- First `N` bytes.
- Every `N`-th byte.

The `N` parameter defaults to 1024. The iterator logic in `criu/files-reg.c` (`checksum_iterator_init`, `next`, and `stop`) manages which bytes are processed. If an iterator moves outside the currently mapped region, CRIU maps the next required segment.

## Configuration and Fallbacks
The Build-ID method is the default because it is highly reliable and less resource-intensive than a full checksum. The `--file-validation` option allows users to customize this behavior:
- `--file-validation buildid`: Uses Build-ID (default).
- `--file-validation checksum-full`: Checksums the entire file.
- `--file-validation checksum`: Checksums the first `N` bytes (use `--checksum-parameter` to set `N`).
- `--file-validation checksum-period`: Checksums every `N`-th byte.
- `--file-validation filesize`: Uses only the file size check (fastest).

If the Build-ID method is selected but the file lacks a Build-ID, CRIU falls back to checksumming the first 1024 bytes. Conversely, if the checksum method is inconclusive, it falls back to Build-ID. If both fail, CRIU relies on the file size and issues a warning.

## Performance Impact
The following values represent the average time to complete ZDTM tests across multiple runs, indicating the general overhead of each method. Tests were performed on an undervolted i5 4800H using `tmpfs` to eliminate disk latency.

**Test: `zdtm/transition/shmem`**
| Method | Time | Increase |
| :--- | :--- | :--- |
| File Size | 3.782s | - |
| Build-ID | 4.153s | ~9% |
| Checksum (First 1024) | 4.465s | ~18% |
| Checksum (Entire File) | 4.722s | ~24% |
| Checksum (Every 1024th) | 4.498s | ~19% |

**Test: `zdtm/static/maps04`**
| Method | Time | Increase |
| :--- | :--- | :--- |
| File Size | 35.317s | - |
| Build-ID | 35.720s | ~1% |
| Checksum (First 1024) | 35.919s | ~2% |
| Checksum (Entire File) | 36.679s | ~4% |
| Checksum (Every 1024th) | 36.476s | ~3% |

## Future Work
- Implementing a lookup table to further accelerate CRC32C calculations.
