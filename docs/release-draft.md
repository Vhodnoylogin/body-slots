# Unpublished Nexus draft packaging

The current file label is `0.1.0-alpha.1` for the Body Slots Nexus draft. This is a mod-author foundation snapshot, not a playable-slot release or released VRIK integration. The tested activation DLL is retained byte-for-byte; packaging does not rebuild it or change legacy PluginInfo version 2. Licensing is not finalized and no license grant is fabricated. Public publication and repository visibility changes require separate owner review.

`tools/package_draft.py` uses an explicit allowlist. The runtime ZIP contains only the existing DLL, a folder README and portable provenance. It enables no demonstration data. The SDK ZIP contains the seven own headers, offline Python author/compiler, optional JSON/.bslots example, a clean read-only discovery utility and API documents. It excludes all test executables/fake SkyrimVR hosts, build objects/PDBs/import libraries, machine paths, reconstructed VRIK code and third-party SDK sources. No Nexus/browser operation is performed by this tool.

```text
python tools/package_draft.py --dll <frozen-activation02-BodySlots.dll> --expected-dll-sha256 a4a5af3b028da5aecdffa6db66e7ef152e2787cd923dcb45518666344eb5a03b --source-commit 376ef62ff05c1e696eff72f11a06ab0469020cc0 --label 0.1.0-alpha.1 --output <new-external-directory>
```

The output is `BodySlots-runtime-0.1.0-alpha.1.zip`, `BodySlots-sdk-0.1.0-alpha.1.zip`, `SHA256SUMS.txt` and `draft-artifacts.json`. Archive entries are sorted with fixed timestamps and permissions; identical input bytes produce identical archive bytes. Each archive contains provenance with payload hashes, production commit and frozen DLL SHA256, without local source/build paths. Packaging-only source changes may remain uncommitted for review and are identified by the packaging tool hash; they do not masquerade as a production rebuild.
