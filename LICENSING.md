# ABM AV Player licensing

## Current model

The native application checks licensing before media, streaming, torrent or casting components initialize.

- **Trial:** 10 days from first application launch, with the full feature set enabled.
- **Full licence:** one universal 16-character alphanumeric activation key works on every installation. There are no customer-specific keys, device IDs, activation limits or online activation calls.
- **Offline operation:** enter the shared key once. The application validates it locally and stores it under `%APPDATA%\\ABMAVPlayer\\license.key`.
- **Uninstall behaviour:** an explicit uninstall deletes the saved activation key. Reinstalling therefore requires the customer to enter the universal key again. The trial-state record is retained, so uninstalling does not restart the 10-day trial. Installing an update over the existing installation does not require reactivation.
- **Trial persistence:** first-launch and last-seen UTC timestamps are protected with Windows DPAPI and stored in the current user's AppData plus HKCU registry. A substantial clock rollback blocks trial continuation and requires the full activation key.

## Publisher activation key

Keep the universal key private and share it only with authorized customers or distributors. For the maintainer's current distribution workflow, the key is recorded in [`OWNER_DISTRIBUTION_KEY.md`](OWNER_DISTRIBUTION_KEY.md). This repository is public, so the key must be treated as publicly exposed and can be used by anyone who can view the repository. The same key can activate any number of installations. Because it is a shared offline key, it can be forwarded or published and does not provide per-customer revocation, device limits or strong anti-piracy protection. A determined user may also patch the executable or inspect the validation logic.

The application stores the accepted key locally so the customer does not need to enter it on every launch. A user who starts the trial can activate later from **Command Centre → License & Activation** without reinstalling. No publisher private key, signing certificate, customer device ID, key-generation script or licensing server is required.

## Build and test

Run `build.bat` from the x64 Native Tools Command Prompt for Visual Studio. The script compiles `src\\licensing.c` and links the Windows CNG, DPAPI and registry libraries used by the trial and key-validation code.

Before distribution, test at least:

- First launch shows the 10-day trial and starts the clock.
- Repeated launches preserve the remaining trial days.
- The universal 16-character key activates successfully.
- A user can open Command Centre → License & Activation during an active trial, activate without reinstalling, and see an already-activated status on subsequent visits.
- Incorrect, incomplete and malformed keys are rejected.
- A valid activation persists across application restarts, reboots and in-place application upgrades.
- Uninstalling removes the saved activation; reinstalling requires the key to be entered again.
- The expired trial blocks entry until the universal key is entered.
- The same key activates a second Windows PC.
- Moving the system clock backwards triggers the activation-only path.

**Important:** source changes have not been built or runtime-tested on the maintainer's Windows environment. Run the local build and manual acceptance tests before publishing an installer.
