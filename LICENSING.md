# ABM AV Player licensing

## Current model

The native application now has a local licensing gate that runs before LibVLC, WebView2, torrent services or casting are initialized.

- **Trial:** 10 days from first application launch, with the full feature set enabled.
- **Perpetual licence:** an ECDSA P-256 signed token that has no expiry and is bound to the device ID shown by the activation window.
- **Offline operation:** activation and subsequent licence checks are local. No licensing server or recurring connection is required.
- **Trial persistence:** the first-launch and last-seen UTC timestamps are protected with Windows DPAPI and stored in the current user's AppData plus HKCU registry. A substantial clock rollback blocks trial continuation and requires activation.
- **Licence persistence:** the signed key is stored under `%APPDATA%\\ABMAVPlayer\\license.key`.

This is an offline licensing design, not a server-managed activation service. It does not provide remote deactivation, central device-count enforcement, purchase processing, or guaranteed anti-tamper protection. A determined user with administrator access can still manipulate local state or patch a native executable. The device fingerprint combines Windows MachineGuid and the C: volume serial; reinstalling Windows or changing the system volume can require a replacement key.

## Publisher signing key

The application contains only the public P-256 verification key. The matching private key is a separate publisher secret and must never be committed to Git, included in the installer, or sent to customers.

The initial private key for this repository's embedded public key is supplied separately to the maintainer. Keep multiple encrypted offline backups. If it is lost or compromised, a new public key must be shipped in an application update.

## Generate a customer key

1. Ask the customer to open ABM AV Player's activation window and choose **Copy device ID**.
2. On the publisher workstation, install Python 3 and the dependency:

   ```bat
   py -m pip install cryptography
   ```

3. Run the generator from the repository root, pointing to the separately stored private key:

   ```bat
   py tools\\licensing\\generate_license.py --private-key "D:\\ABM-Keys\\ABMAVPlayer-licensing-private.pem" --device-id CUSTOMER_DEVICE_ID
   ```

4. Send the generated `ABM1....` key to that customer. The key is perpetual and will activate only on the device whose ID was supplied.

The generator accepts encrypted PEM keys and prompts for their passphrase. Protect the publisher private key with a strong passphrase and keep it offline.

## Build and test

Run `build.bat` from the x64 Native Tools Command Prompt for Visual Studio. The script compiles `src\\licensing.c` and links the existing Windows CNG, DPAPI and registry libraries already used by the application.

Before distribution, test at least:

- First launch shows the 10-day trial and starts the clock.
- Repeated launches preserve the remaining days.
- A correctly signed key for the current device activates permanently.
- A modified, malformed or other-device key is rejected.
- A valid licence survives application upgrades and reboots.
- An expired trial blocks entry to the application until activation.
- Moving the system clock backwards triggers the activation-only path.
- A clean Windows user profile and a second physical/virtual machine get independent trial state.

**Important:** this implementation has not yet been built or tested on Windows in the maintainer's environment. Treat the first local build and the above manual tests as required before publishing an installer.
