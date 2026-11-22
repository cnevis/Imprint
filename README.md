# Imprint

Imprint stamps version information, an icon and an application manifest from a YAML file into a copy of a Windows executable.

## Usage

```
imprint <manifest.yaml> <input.exe> [output.exe]
```

The output defaults to `<name>.published.exe` next to the input. An existing output file is never overwritten.

## Manifest

All fields are optional. Relative paths are resolved against the YAML file's folder.

```yaml
author: Example Author
company: Example Company
name: Example Application
description: Example application
internal_name: Example.Application
original_filename: Example.exe
copyright: Copyright 2026 Example Company
trademarks: Example trademark
comments: Example comments
version: 1.0.0.0
file_version: 1.0.0.0
product_version: 1.0.0.0
icon: Example.ico
application_manifest: Example.manifest
```

- `company` defaults to `author`.
- `version` sets both the file and product version; `file_version` and `product_version` override it. Missing components are zero.
- Version information is written in US English, and version resources in other languages are removed.
- The icon replaces the executable's main icon group, and the manifest replaces any existing one.

## Building

Requires Visual Studio 2026, CMake 3.25 and vcpkg with `VCPKG_ROOT` set.

```
cmake --preset x64
cmake --build --preset x64-release
```

## License

MIT. See [LICENSE](LICENSE).
