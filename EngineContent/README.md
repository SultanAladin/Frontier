# Engine Content

- `Icons/`: shared SVG content used by the native interface.
- `FontArchives/<font name>/`: downloaded static fonts and original OFL licence notices.
- `FontArchives/FontCatalogue.toml`: generated metadata consumed by native C++ font discovery.
- `FontArchives/FontCatalogue.json`: equivalent inventory for automation and retained reports.

Recover ignored fonts with `python Automation/PrepareFonts.py` after installing
`Automation/FontRequirements.txt`. Regenerate metadata with `--scan-only` after adding fonts, then restart.
All engine-authored native runtime descriptors use TOML through toml++.
