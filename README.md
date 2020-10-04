# OS_loader
Shared-library which recreates ELF (Linux) and PE (Windows) Loaders (including virtual memory allocation).

The repo contains two versions of the project:

- one for Windows, which, at build, creates the libso_loader.dll dynamic-link library;
- one for Linux, which, at build, creates the libso_loader.so shared object library.

The loader,.. well, loads the executables into memory page by page, using demand paging mechanism: pages are loaded into memory only when needed, generating page faults for each page which is not mapped into memory.
mmap (Linux) and MapViewOfFile (Windows) are used to allocate virtual memory.
Pages are allocated using the permissions specified for each segment inside the executable structure.
