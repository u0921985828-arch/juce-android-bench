# ci/

Build inputs for the Android APK workflow.

## colors-debug.keystore

The signing key for CI-built APKs. **It is a debug key**: alias
`androiddebugkey`, store and key password `android` — the canonical Android
debug credentials, deliberately not secret.

It lives in the repo because Android refuses to update an installed app whose
signing certificate changed. The workflow used to run `keytool -genkeypair` on
every build, and a fresh runner has no keystore, so every APK carried a
different certificate and each new build had to be uninstalled before it could
be installed. A fixed key makes builds install over one another.

Do **not** promote this to a Play Store upload key. A real release needs a key
that is generated once, kept out of version control (a GitHub Actions secret
holding the base64 keystore), and never rotated — losing it means losing the
ability to update the listing.

## icon.png / make_icon.py

The launcher icon, generated rather than hand-drawn so it stays in step with
the app's palette. `make_icon.py` reads the same colour values as
`ShardColours`/`Zati` and draws the pad matrix with four lit pads taken from the fixed
fragment palette in `Zati.h`, on the white chassis. Re-run it after any palette change:

    python3 ci/make_icon.py

`Shard.jucer` references `ci/icon.png` as both `smallIcon` and `bigIcon`;
Projucer generates the Android mipmap set from it.
