#pragma once

#include "port/hooks/impl/EventSystem.h"

// SaveManager
DEFINE_EVENT(OnSaveFileSave,
    void* saveData;
);

DEFINE_EVENT(OnSaveFileLoad,
    int32_t saveSlot;
);

DEFINE_EVENT(OnSaveFileErase,
    int32_t saveSlot;
);

DEFINE_EVENT(OnSaveGlobalsSave,
    void* saveGlobals;
);

DEFINE_EVENT(OnSaveGlobalsLoad,
    void* saveGlobals;
);

// Cheats
DEFINE_EVENT(OnPlayerDamageReceived,
    int32_t* damage;
);

DEFINE_EVENT(OnPlayerFPChange,
    int32_t fpCost;
);

DEFINE_EVENT(OnPlayerSPChange,
    int32_t spCost;
);

DEFINE_EVENT(OnPlayerBPCostCheck);

// Gameplay > CutsceneSkips
DEFINE_EVENT(OnPostSaveFileLoad,
    int32_t saveSlot;
);

// Battle
DEFINE_EVENT(OnBattleEffectsRemoved);

DEFINE_EVENT(OnBlockWindowCheck,
    int32_t* blockWindow;
    int32_t* mashWindow;
);

DEFINE_EVENT(OnActionCommandDifficulty,
    int32_t* difficultyLevel;
);

DEFINE_EVENT(OnPowerBounceChance,
    int32_t* hitChance;
    int32_t targetChance;
);

DEFINE_EVENT(OnStarPointDrop,
    int32_t* count;
);

DEFINE_EVENT(OnCoinDrop,
    int32_t* count;
);

// Battle > Actors
DEFINE_EVENT(OnActorBlueprintLoad,
    struct ActorBlueprint** blueprint;
);

DEFINE_EVENT(OnActorCreated,
    Actor* actor;
);

DEFINE_EVENT(OnEnemyAttackDamage,
    Actor* attacker;
    int32_t* damage;
);

DEFINE_EVENT(OnPartnerAttackDamage,
    Actor* partner;
    Actor* target;
    int32_t* damage;
);

DEFINE_EVENT(OnActorDefenseCheck,
    Actor* target;
    int32_t* defenseTable;
    int32_t elementFlags;
    int32_t* defense;
);

// World
// Start of a map change, before the fade-out: the destination map.
DEFINE_EVENT(OnMapChangeStart,
    const char* mapName;
);

DEFINE_EVENT(OnMapLoad,
    const char* mapName;
);

// Battles: when the pre-battle fade begins, once the battle is loaded, and when it ends.
DEFINE_EVENT(OnBattlePrepare,
    int32_t battleID;
);

DEFINE_EVENT(OnBattleStart,
    int32_t battleID;
);

DEFINE_EVENT(OnBattleEnd);

DEFINE_EVENT(OnPlayerSpeedUpdate,
    float* walkSpeed;
    float* runSpeed;
);

DEFINE_EVENT(OnMapReady,
    const char* mapName;
);

DEFINE_EVENT(OnHiddenBlockAlphaChange,
    int32_t* alpha;
    );
