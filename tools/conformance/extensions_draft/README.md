# Native 1.1 independent candidate corpus

Draft tooling only. No production writer, codec, reader, install, accepted fixture or receipt is modified. `author.py` imports the separately identified independent G0C author as a wire helper, never a Serene production library. New compression/decompression uses public zfpy 1.0.1.post0 (ZFP 1.0.1) in the existing eic12 interpreter. Generated artifacts are confined to `fixtures_extensions_draft`.

`audit.py` independently reads the emitted container envelopes and checks extension semantics and expected failure phases. This is draft corpus validation, not installed-reader or G3/G4 qualification. The original 1.0 corpus remains the regression authority. The manifest describes positive, negative, producer-only bound-oracle and bundle-import cases separately; a producer-side failed compression is never encoded as a legitimate completed archive.
