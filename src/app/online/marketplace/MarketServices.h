#pragma once

#include "app/mcp/tools/McpMarket.h"
#include "app/online/assets/DriftAssetStore.h"
#include "app/online/assets/SfxLibrary.h"
#include "app/online/free/FreeLibrary.h"
#include "app/online/marketplace/CommunityInstaller.h"
#include "app/online/marketplace/MarketInstallRouter.h"
#include "app/online/marketplace/MarketplaceManifest.h"
#include "app/online/marketplace/StockInstaller.h"

class AddonManager;
class AssetLibrary;
class EditorState;

// Every marketplace service wired to the app it installs into. The GUI and the headless entry
// point both build one, so the agent API sees the same installers QML does.
class MarketServices
{
public:
    MarketServices(AssetLibrary &library, EditorState &state, AddonManager &addons);

    McpMarketContext context();

    StockInstaller stock;
    DriftAssetStore driftAssets;
    SfxLibrary sfx;
    // Stock Media tab: free stock video / SFX / Lottie from the user-editable catalog.
    FreeLibrary freeLibrary;
    CommunityInstaller community;
    MarketplaceManifest manifest;
    MarketInstallRouter router;
};
