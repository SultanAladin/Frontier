# Engine Content

- `MaterialArchives/TextureStudio/`: exported 127-preset HTML catalogue and supported native physical definitions.
- `Icons/`: shared SVG content used by the native interface.
- `FontArchives/<font name>/`: downloaded static fonts and original OFL licence notices.
- `FontArchives/FontCatalogue.toml`: generated metadata consumed by native C++ font discovery.
- `FontArchives/FontCatalogue.json`: equivalent inventory for automation and retained reports.

Recover ignored fonts with `python Automation/PrepareFonts.py` after installing
`Automation/FontRequirements.txt`. Regenerate metadata with `--scan-only` after adding fonts, then restart.
Editable engine-authored descriptors use TOML through toml++; compiled runtime packages use the approved binary
envelope. The two are separate authorities.

## Default content and blank projects

`Defaults/BlankProject/Blank.projectspace` is the single editable project seed and engine preference-default source.
Appearance, input, notification and engine configuration are sections in this container, not standalone TOML files.
The internal editor encoding is TOML with a versioned discriminator; binary FSPC is a separate runtime contract.
New projects snapshot the template with fresh identity. The editor never saves preference edits into installed defaults.
Fonts/SVGs remain in FontArchives/Icons. See `Exhibits/ProjectSpace/README.md` for controls, tests and limitations.
