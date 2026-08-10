# Symbol-backed reconstruction data

The reference PDB preserves the original Visual C++ 2005 compilation units,
source paths, file checksums, types, globals, function signatures, and RVAs.
This directory stores generated manifests derived from that information.

`GeneratePdbManifest.ps1` uses Microsoft's DIA SDK sample utility to create:

- `pdb_files.tsv`: every source/header path recorded by the PDB, its original
  MD5, and whether the current reconstruction is exact, different, or missing.
- `pdb_compilands.tsv`: the original object-to-primary-source mapping.

These manifests define file ownership. Existing Doom 3 filenames are retained
when the PDB agrees; ETQW-specific files use the exact PDB path instead of an
invented approximation.
