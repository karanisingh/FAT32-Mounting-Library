# FAT32-Mounting-Library
C++ Library to read from any FAT32 filesystem.  
  
Currently has support for 128 open file descriptors at one time.  
  
Used to mount onto `.raw` files representing filesystem images. Current test suite based on an test filesystem named `testdisk1.raw` but can be changed to test on any filesystem image.


### References: 
- https://wiki.osdev.org/FAT
- https://www.cs.fsu.edu/~cop4610t/assignments/project3/spec/fatspec.pdf

Currently has support for 128 open file descriptors.
