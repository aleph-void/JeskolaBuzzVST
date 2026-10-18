# Code Signing (Windows)

How the `release` workflow signs the BuzzBridge VST3 so it installs without scary
SmartScreen warnings, and how to provision the service and secrets it depends on.

- **Windows** — Authenticode signature via **Azure Trusted Signing** (Microsoft's
  cloud signing service; the cert lives in Azure, never on disk).

Signing is wired into [`.github/workflows/release.yml`](../.github/workflows/release.yml)
and is **mandatory**: the `build-windows` job **fails** if the signing secrets
are missing or a signature fails to verify. We never publish an unsigned
installer — an unsigned one trips SmartScreen ("Windows protected your PC") on
download, which is worse than useless for the people you hand it to.

Two things get signed, in order:

1. the **inner binaries** — the two `BuzzBridge.vst3` plugin PEs (`x86-win` and
   `x86_64-win`) plus `BuzzBridgeHost32.exe` — signed **before** the installer
   wraps them, and
2. the **installer** — `BuzzBridge-Setup-<tag>.exe` — signed **after** it is
   built.

```
release.yml  build-windows job
  cmake build (32 + 64) ─▶ assemble dist/BuzzBridge.vst3 bundle
    ─▶ sign inner PEs + host exe (Azure Trusted Signing) ─▶ verify
    ─▶ ISCC build BuzzBridge-Setup-<tag>.exe
    ─▶ sign installer (Azure Trusted Signing) ─▶ verify ─▶ upload
```

There is no `.pfx`/`.p12` anywhere — the key never leaves Azure, and the runner
signs over an authenticated API. (The `ci.yml` workflow, which only produces
throwaway PR artifacts, is intentionally left unsigned.)

---

## Secrets reference

Add these under **GitHub → repo Settings → Secrets and variables → Actions →
New repository secret**. Names must match exactly — they're referenced by
`release.yml`.

| Secret | What it is |
| --- | --- |
| `AZURE_TENANT_ID` | Entra ID (Azure AD) tenant ID of the app registration. |
| `AZURE_CLIENT_ID` | App registration (service principal) client ID. |
| `AZURE_CLIENT_SECRET` | A client secret for that app registration. **Also the marker the workflow checks** to decide whether signing is enabled — if it's empty the release fails fast rather than shipping unsigned. |
| `AZ_TS_ENDPOINT` | Trusted Signing account endpoint URL for your region, e.g. `https://eus.codesigning.azure.net` (East US). |
| `AZ_TS_ACCOUNT` | Trusted Signing **account** name. |
| `AZ_TS_PROFILE` | Certificate **profile** name within that account. |

---

## Azure Trusted Signing setup

Azure Trusted Signing (formerly Azure Code Signing) is Microsoft's managed
signing service. It's the cheapest path that satisfies the post-2023
CA/Browser-Forum rule requiring code-signing keys to live on certified hardware
— the key never leaves Azure, and you sign over an authenticated API.

> **Eligibility:** identity validation requires either a 3+ year-old verifiable
> legal business (org validation) or individual validation. Allow a few business
> days for Microsoft's validation step.

### 1. Create the signing resources

1. In the [Azure portal](https://portal.azure.com), create (or pick) a
   subscription and resource group.
2. Create a **Trusted Signing account** (search the Marketplace for "Trusted
   Signing"). Pick the region closest to your CI — the region determines the
   **endpoint URL** you'll store in `AZ_TS_ENDPOINT`
   (e.g. East US → `https://eus.codesigning.azure.net`).
3. Complete the **Identity validation** request (Public Trust → Identity
   validations). Wait for it to reach *Completed*.
4. Create a **Certificate profile** of type **Public Trust** against the
   validated identity. Note the **account name** (`AZ_TS_ACCOUNT`) and the
   **profile name** (`AZ_TS_PROFILE`).

### 2. Create the CI service principal

The GitHub action authenticates as an Entra ID app registration:

1. **Entra ID → App registrations → New registration.** Name it e.g.
   `buzzbridge-ci-signing`. Note the **Application (client) ID**
   (`AZURE_CLIENT_ID`) and **Directory (tenant) ID** (`AZURE_TENANT_ID`).
2. **Certificates & secrets → New client secret.** Copy the secret **value**
   immediately → `AZURE_CLIENT_SECRET`.
3. Grant it signing rights: on the **Trusted Signing account → Access control
   (IAM) → Add role assignment**, assign **Trusted Signing Certificate Profile
   Signer** to the app registration.

### 3. Store the six secrets

Add `AZURE_TENANT_ID`, `AZURE_CLIENT_ID`, `AZURE_CLIENT_SECRET`,
`AZ_TS_ENDPOINT`, `AZ_TS_ACCOUNT`, `AZ_TS_PROFILE` (see the table above).

That's it — `azure/trusted-signing-action@v0` installs the signing tooling,
authenticates with the service principal, and Authenticode-signs the binaries
(timestamped against `http://timestamp.acs.microsoft.com`, SHA256 digest).

### 4. Verify a release

On a signed download — both the installer and the plugin inside it:

```powershell
# The installer — should report a valid signature with your org as signer.
Get-AuthenticodeSignature .\BuzzBridge-Setup-<tag>.exe

# The 64-bit plugin DLL inside the installed bundle.
Get-AuthenticodeSignature "C:\Program Files\Common Files\VST3\BuzzBridge.vst3\Contents\x86_64-win\BuzzBridge.vst3"

# The 32-bit bridge host that the 64-bit plugin launches.
Get-AuthenticodeSignature "C:\Program Files\Common Files\VST3\BuzzBridge.vst3\Contents\x86_64-win\BuzzBridgeHost32.exe"
```

---

## Operational notes

- **Rotation.** The Azure client secret expires per the lifetime you chose when
  creating it. When it does, create a new client secret on the app registration
  and update `AZURE_CLIENT_SECRET`. A revoked/expired credential makes the
  signing step **fail** the release — it does **not** silently ship unsigned
  (only a *missing* `AZURE_CLIENT_SECRET` marker does that, and the gate step
  catches even that).
- **Cost.** Azure Trusted Signing: a small monthly fee (~US$10) plus a
  per-signature cost; see current Azure pricing.
