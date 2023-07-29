# PS2-MCA-converter
Small utility to convert PS2 Memory Card Annihilator dumps to regular ps2 format used by PCSX2 and others

MCA dumps are essentially proper memory card images, except they are lacking the 16 byte ECC at the end of each page.

This utility assumes the dump is correct, and calculates the ECC for each page and inserts it at the end, where it is expected to be found.

After that, other utilities like PCSX2, mymc et al. should be able to read it like any other raw memory card dump.

# Usage
```
ps2_mca_converter [-h --help | -v --version] INPUT OUTPUT
Small utility to convert PS2 Memory Card Annihilator dumps to regular ps2 format used by PCSX2 and others

 -h --help
    Print detailed help screen

 -v --version
    Prints version
```
