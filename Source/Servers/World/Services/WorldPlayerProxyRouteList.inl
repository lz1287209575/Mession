// Intentionally no include guard.
// This file is consumed multiple times with different X-macro definitions.

M_WORLD_PLAYER_PROXY_ROUTE(PlayerUpdateRoute, FPlayerUpdateRouteRequest, FPlayerUpdateRouteResponse, Controller, "PlayerUpdateRoute")
M_WORLD_PLAYER_PROXY_ROUTE(PlayerQueryProfile, FPlayerQueryProfileRequest, FPlayerQueryProfileResponse, Profile, "PlayerQueryProfile")
M_WORLD_PLAYER_PROXY_ROUTE(PlayerQueryInventory, FPlayerQueryInventoryRequest, FPlayerQueryInventoryResponse, Inventory, "PlayerQueryInventory")
M_WORLD_PLAYER_PROXY_ROUTE(PlayerQueryProgression, FPlayerQueryProgressionRequest, FPlayerQueryProgressionResponse, Progression, "PlayerQueryProgression")
M_WORLD_PLAYER_PROXY_ROUTE(PlayerChangeGold, FPlayerChangeGoldRequest, FPlayerChangeGoldResponse, Inventory, "PlayerChangeGold")
M_WORLD_PLAYER_PROXY_ROUTE(PlayerEquipItem, FPlayerEquipItemRequest, FPlayerEquipItemResponse, Inventory, "PlayerEquipItem")
M_WORLD_PLAYER_PROXY_ROUTE(PlayerGrantExperience, FPlayerGrantExperienceRequest, FPlayerGrantExperienceResponse, Progression, "PlayerGrantExperience")
M_WORLD_PLAYER_PROXY_ROUTE(PlayerModifyHealth, FPlayerModifyHealthRequest, FPlayerModifyHealthResponse, Progression, "PlayerModifyHealth")
