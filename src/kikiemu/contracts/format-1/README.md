# Format-1 implementation contract

Draft; not a published/frozen package ABI yet. The three JSON files in this
directory are producer-owned canonical inputs. KikiEmu vendors their EXACT
bytes from a recorded device-repository commit, embeds the schemas in its
native reader, and runs the same metadata mutation fixtures. No runtime URL
fetch or requirement for an AOSP checkout on a user's Windows machine.

`manifest.schema.json` and `source-lock.schema.json` use a bounded subset of
standard JSON Schema 2020-12. The native implementation rejects unknown schema
keywords/references instead of silently ignoring a future extension. It is
not a general JSON Schema library. The producer uses python-jsonschema plus
the SAME normative semantic rules: integer tokens (no 1.0 byte/version
encoding), unique role/path/encoding mapping, exact one-MiB partition rounding,
required AOSP/kernel license records, maximum package size, minimum supported
launcher version and a complete explicitly pinned AOSP manifest.

Run on Linux with python-jsonschema installed:

```bash
python scripts/system-package.py --self-test
python scripts/system-package.py --validate /path/to/system.zip
```

The positive metadata fixture is SYNTHETIC and deliberately has placeholder
hashes. It must never be presented as a bootable image, installed publicly,
used as clean-build provenance or published as the user's test package. ZIP
fixtures for native reader tests are likewise small nonbootable fixtures.
Actual clean candidate packaging and full guest checks remain separate gates.

The XML in source-lock embeds the exact `repo manifest -r` output, rather than
introducing an additional archive role or depending on a development path.
Its digest, project count and each full project commit are checked. Package
metadata contains no arbitrary commands, QEMU paths or host installation
destinations. Provenance hashes/clean-build declarations are integrity and
audit records, not publisher authentication or evidence by themselves that
a clean build was actually performed. The tracked release build recipe and
release gates must supply that evidence.

New source here is GPL-2.0-or-later. JSON Schema behavior references the
[official object/required/additionalProperties documentation](https://json-schema.org/understanding-json-schema/reference/object).
