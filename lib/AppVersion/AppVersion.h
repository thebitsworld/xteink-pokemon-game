#pragma once

// PlatformIO normally supplies these through build_flags/extra_scripts. Keep
// fallbacks here so editor indexers and simulator-like tools still parse files.
namespace AppVersion {
const char* version();
const char* versionLabel();
const char* userAgent();
const char* gitSha();
const char* supportVersion();
const char* sdkSha();
const char* gitDirtyFlag();
}  // namespace AppVersion

#ifndef CROSSINK_PRODUCT_NAME
#if defined(CROSSINK_PRODUCT_XTEINK_POKEMON)
#define CROSSINK_PRODUCT_NAME "Xteink Pokemon"
#else
#define CROSSINK_PRODUCT_NAME "CrossInk"
#endif
#endif

#ifndef CROSSINK_BUILD_ENV
#define CROSSINK_BUILD_ENV "unknown"
#endif

#ifndef CROSSINK_FIRMWARE_DEVICE_TYPE
#define CROSSINK_FIRMWARE_DEVICE_TYPE "unknown"
#endif
