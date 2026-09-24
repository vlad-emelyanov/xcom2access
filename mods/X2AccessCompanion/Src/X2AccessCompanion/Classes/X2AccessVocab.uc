// X2AccessVocab
// Every word of OURS that reaches a player, in one place, on the vanilla localization system.
//
// The game's words stay the game's: a cover type, a hit result, a unit's name, an ability's name,
// an objective sentence are all read off the template or the screen that owns them. This class
// holds ONLY what the game has nowhere — the tile cursor's compass, the scanner's category names,
// "Press again to move". Before adding a key here, look for the game's own word first.
//
// ONE class, deliberately. A localized var declared on two classes and populated on one fails
// silently (docs/engine-notes.md, the double-space tell), so every localized var the companion
// owns is declared here and nowhere else, and every reader reads it through default. Native reads
// the same values off this class's CDO by property name (docs/localization.md).
//
// The values live in Localization/X2AccessCompanion.<lang> under [X2AccessVocab]. The tracked
// files are UTF-8; X2ModBuildCommon rewrites them to UTF-16 in staging, which is what the engine
// reads. A key missing from a language file reads as "" — the readers treat "" as "say nothing",
// so a missing key is a silent gap, and the .int file is the reference every other language is
// checked against.
//
// WORD ORDER is the translator's. A template carries vanilla's own tags — <XGParam:IntValue0/>,
// <XGParam:IntValue1/>, <XGParam:StrValue0/> — and the Fill functions below expand them through the
// game's XGParamTag exactly as UIArmory or SeqAct_ExpandLocalizedString do. The translator moves
// a tag, never the code.
//
// PLURALS. Vanilla has no rule (393 Singular/Plural pairs picked by == 1), so this class has one:
// a counted noun is THREE keys, <Name>_One, <Name>_Few and <Name>_Many, each a template over
// IntValue0, and Count() picks by PluralRule. Under the default English rule One is for exactly 1
// and everything else is Many; under "slavic" the last two digits decide (1, 21, 31 One; 2-4,
// 22-24 Few; 5-20, 25-30 and every 11-14 Many). English fills One and Many and leaves Few empty;
// an empty form falls to Many, and an empty Many falls to One, so a half-filled file still speaks.
//
// GC rule: this object is a CDO — value-typed state only, never object references. It holds no
// state at all; everything is static.

class X2AccessVocab extends Object;

// "" or "english": One is exactly 1. "slavic": the last-two-digits rule. Set once per language
// file, not per word.
var localized string PluralRule;
var localized string ScreenUnnamed;
var localized string SectionUnnamed;
var localized string KeyBindingsFallback;
var localized string CompanionNotLoaded;
var localized string KeyBindingPrompt;
var localized string KeyBound;
var localized string ItemFallback;
var localized string NarrationDropped_One;
var localized string NarrationDropped_Few;
var localized string NarrationDropped_Many;

// Native UI vocabulary, loaded from this class's CDO on the game tick.
var localized string StartupHookFailure;
var localized string StartupSdkFailure;
var localized string StartupActive;
var localized string HackCancelled;
var localized string EngineVolume;
var localized string EngineRate;
var localized string EngineBraille;
var localized string EngineSpeech;
var localized string EngineFeatures;
var localized string EngineLabel;
var localized string EngineBackendUnavailable;
var localized string EngineTtsUnavailable;
var localized string EngineNone;
var localized string NavHelpUnavailable;
var localized string SurfaceReportReader;
var localized string SurfaceBondReader;
var localized string SurfaceChosenReader;
var localized string SurfaceSkyrangerReader;
var localized string SurfaceUnitReader;
var localized string SurfaceTileReader;
var localized string SurfaceShotReader;
var localized string SurfaceObjectScanner;
var localized string SurfaceHackingReader;
var localized string SurfaceGeoscapeScanner;
var localized string SurfaceAimReader;
var localized string SurfaceAvengerReader;
var localized string ObjectiveOfFormat;
var localized string ObjectiveTurnsMany;
var localized string ObjectiveTurnsFew;
var localized string ObjectiveTurnsOne;
var localized string ObjectiveUnavailable;
var localized string ObjectiveLocked;
var localized string ObjectiveExpired;
var localized string ObjectiveUnlimited;
var localized string ObjectiveNoneListed;
var localized string ObjectiveFailed;
var localized string ObjectiveCompleted;
var localized string ObjectiveWarning;
var localized string ObjectivePickedUp;
var localized string ObjectiveRecovered;
var localized string ObjectiveDestroyed;
var localized string ObjectiveRescued;
var localized string ObjectiveDead;
var localized string ObjectiveAvailable;
var localized string ObjectiveCrates;
var localized string ObjectiveEvidence;
var localized string ObjectiveCivilian;
var localized string PercentManyFormat;
var localized string PercentFewFormat;
var localized string CovertActionStaffed;
var localized string PunctTilde;
var localized string PunctRightBrace;
var localized string PunctBar;
var localized string PunctLeftBrace;
var localized string PunctBacktick;
var localized string PunctUnderscore;
var localized string PunctCaret;
var localized string PunctRightBracket;
var localized string PunctBackslash;
var localized string PunctLeftBracket;
var localized string PunctAt;
var localized string PunctQuestion;
var localized string PunctGreaterThan;
var localized string PunctEquals;
var localized string PunctLessThan;
var localized string PunctSemicolon;
var localized string PunctColon;
var localized string PunctSlash;
var localized string PunctDot;
var localized string PunctDash;
var localized string PunctComma;
var localized string PunctPlus;
var localized string PunctStar;
var localized string PunctRightParen;
var localized string PunctLeftParen;
var localized string PunctApostrophe;
var localized string PunctAmpersand;
var localized string PunctPercent;
var localized string PunctDollar;
var localized string PunctHash;
var localized string PunctQuote;
var localized string PunctExclamation;
var localized string PunctSpace;
var localized string ColourGreyishPrefix;
var localized string ColourLightPrefix;
var localized string ColourDarkPrefix;
var localized string ColourPink;
var localized string ColourPurple;
var localized string ColourBlue;
var localized string ColourTeal;
var localized string ColourGreen;
var localized string ColourYellow;
var localized string ColourOrange;
var localized string ColourRed;
var localized string ColourGreyishBrown;
var localized string ColourBrown;
var localized string ColourGrey;
var localized string ColourWhite;
var localized string ColourBlack;
var localized string TabFallbackFormat;
var localized string KnowledgeChangeFormat;
var localized string PercentFormat;
var localized string PositionFormat;
var localized string SurfaceUnavailableFormat;
var localized string CommandFallbackFormat;
var localized string AbilityHotkeyFormat;
var localized string AbilityFallbackFormat;
var localized string DoomDownFormat;
var localized string DoomUpFormat;
var localized string FractionFormat;
var localized string MeterLevelFormat;
var localized string BondLevelFormat;
var localized string LadderRowFormat;
var localized string ScreenMainMenu;
var localized string ScreenOptionsFallback;
var localized string ScreenModConfigFallback;
var localized string ModConfigNoPage;
var localized string RoleButton;
var localized string RoleTicker;
var localized string RoleTab;
var localized string RoleTable;
var localized string TableBlank;
var localized string RoleCheckbox;
var localized string RoleSlider;
var localized string RoleSpinner;
var localized string RoleDropdown;
var localized string Disabled;
var localized string Attention;
var localized string My2KAccount;
var localized string My2KLinked;
var localized string My2KNotLinked;
var localized string My2KOffline;
var localized string ScreenDifficultyFallback;
var localized string ScreenSoundtrackFallback;
var localized string ScreenDialog;
var localized string ScreenLadderDifficultyFallback;
var localized string MedalEarned;
var localized string EmptySlot;
var localized string PCSLabel;
var localized string ScreenScoringFallback;
var localized string ScreenLadderUpgradeFallback;
var localized string ScreenLadderSquadFallback;
var localized string ScreenLadderMedalFallback;
var localized string ScreenMedalProgressFallback;
var localized string ScreenLadderEndFallback;
var localized string ScreenChallengePostFallback;
var localized string ScreenChallengeEventFallback;
var localized string RewardAlreadyUnlocked;
var localized string ScreenPopup;
var localized string PopupNoText;
var localized string PopupDismiss;
var localized string ScreenMissionBlade;
var localized string MissionBladeNoText;
var localized string ReportNoText;
var localized string ScreenResearchUnlocked;
var localized string ScreenSquadSelectFallback;
var localized string SoldierFallback;
var localized string UtilitySlotFallback;
var localized string ScreenLoadoutFallback;
var localized string LoadoutEmptySlot;
var localized string ScreenAfterActionFallback;
var localized string ScreenMissionSummaryFallback;
var localized string SummaryNoText;
var localized string SaveLoadEmpty;
var localized string SaveLoadBusy;
var localized string KeyBackspace;
var localized string ScreenRoomFallback;
var localized string RoomNoActions;
var localized string RoomFunctionFallback;
var localized string RoomUrgent;
var localized string RoomWarning;
var localized string RoomNoStaff;
var localized string RoomPickStaff;
var localized string RoomSlotLocked;
var localized string ScreenChooseUpgradeFallback;
var localized string ScreenCommodityFallback;
var localized string CommodityEmpty;
var localized string CommodityLeaveHint;
var localized string CommodityUnaffordable;
var localized string CommodityOwned;
var localized string CommodityShortfall;
var localized string ImplantCannotEquip;
var localized string ScreenBlackMarketSellFallback;
var localized string SellItemFallback;
var localized string ResourceLow;
var localized string ResourceCritical;
var localized string Resources;
var localized string ResourcesNotRead;
var localized string PolicyEmptySlot;
var localized string PolicyHeld;
var localized string PolicyReadOnly;
var localized string PolicyDropTarget;
var localized string PolicyCantPlace;
var localized string PolicyCantPick;
var localized string PolicyCantMove;
var localized string PolicyPutBack;
var localized string ScreenPersonnelFallback;
var localized string PersonnelEmpty;
var localized string PersonnelPromote;
var localized string ScreenPromotion;
var localized string PromoRankFallback;
var localized string PromoOwned;
var localized string PromoAvailable;
var localized string PromoUnaffordable;
var localized string PromoPrereq;
var localized string PromoSpent;
var localized string PersonnelSortBy;
var localized string SortAscending;
var localized string SortDescending;
var localized string ScreenChosenInfoFallback;
var localized string ScreenSkyrangerArrivesFallback;
var localized string BondsEmpty;
var localized string BondCreateFallback;
var localized string ScreenMonthlyReportFallback;
var localized string ScreenRecruitsFallback;
var localized string RecruitsEmpty;
var localized string ScreenCustomizeFallback;
var localized string ScreenCustomizeOptionsFallback;
var localized string CustomizeColours;
var localized string ColourUnknown;
var localized string ScreenCharacterPoolFallback;
var localized string ScreenPoolFilesFallback;
var localized string PoolEmptyFallback;
var localized string PoolEditFallback;
var localized string PoolFilesEmpty;
var localized string ScreenRewardsRecapFallback;
var localized string RecapNoText;
var localized string ScreenCovertReportFallback;
var localized string CovertReportNoText;
var localized string CovertReportCardFallback;
var localized string ScreenArmoryFallback;
var localized string ArmoryRowFallback;
var localized string ScreenWeaponUpgradeFallback;
var localized string ScreenWeaponPatternFallback;
var localized string StopWeaponStats;
var localized string WeaponSlotsMirror;
var localized string ScreenLootFallback;
var localized string LootEmpty;
var localized string CharInfoNoValue;
var localized string GeoscapeLeaveHint;
var localized string GeoscapeResNetOn;
var localized string GeoscapeResNetOff;
var localized string GeoscapeTrayEmpty;
var localized string GeoscapeUnmetFaction;
var localized string GeoscapeRegionActions;
var localized string GeoscapeCannotAfford;
var localized string GeoscapeNoComms;
var localized string TextInputRole;
var localized string TextInputEmpty;
var localized string TextInputStart;
var localized string TextInputEnd;
var localized string TextInputDeleted;
var localized string Loading;
var localized string GroupDifficulty;
var localized string GroupCampaignOptions;
var localized string ScreenTacticalHud;
var localized string TacticalWorldMode;
var localized string StopSoldier;
var localized string StopCommands;
var localized string StopTargets;
var localized string ScreenAvengerHud;
var localized string AvengerRoomsMode;
var localized string StopShortcuts;
var localized string StopToDo;
var localized string StopEvents;
var localized string EventNoEstimate;
var localized string IconLeftMouse;
var localized string IconRightMouse;
var localized string IconMouseWheelClick;
var localized string IconMouseWheelScroll;
var localized string IconMouseButton4;
var localized string IconMouseButton5;
var localized string IconArrowLeft;
var localized string IconArrowRight;
var localized string IconArrowUp;
var localized string IconArrowDown;
var localized string TutorialDismiss;
var localized string TutorialRejected;
var localized string TutorialNoMessage;
var localized string TutorialReaderUnavailable;


// ---------------------------------------------------------------------------
// Shared by the tile cursor and the object scanner
// ---------------------------------------------------------------------------

var localized string NoUnitSelected;

// A count of tiles, the distance clause of both surfaces.
var localized string Tiles_One;
var localized string Tiles_Few;
var localized string Tiles_Many;

// The reachability words — the blue/yellow move highlight, spoken. Same three words on a tile and
// on a marker, so the two surfaces agree by construction.
var localized string ReachOneAction;
var localized string ReachDash;
var localized string ReachOutOfRange;
var localized string ReachYourTile;

var localized string ViaSquadsight;
var localized string Flanked;

// A bearing from the soldier or the scan origin, one leg each; the legs are comma-joined.
var localized string OffsetNorth;
var localized string OffsetSouth;
var localized string OffsetEast;
var localized string OffsetWest;
var localized string OffsetUp;
var localized string OffsetDown;

// Interactive objects the game left unnamed, and their state.
var localized string Door;
var localized string HackableObject;
var localized string InteractiveObject;
var localized string Passage;
var localized string LockedStrength;
var localized string Hacked;
var localized string HasLoot;

var localized string Loot;
var localized string LootFrom;
var localized string PsiLoot;
var localized string LootExpires;
var localized string InTurns_One;
var localized string InTurns_Few;
var localized string InTurns_Many;

// Hazards on a tile, by type. The scanner's per-type ring and the cursor's per-tile line share them.
var localized string Fire;
var localized string Smoke;
var localized string Poison;
var localized string Acid;
var localized string Hazard;

var localized string EvacZone;
var localized string EvacZoneNoClearance;
var localized string EvacSize;
var localized string EvacInside;

var localized string DetectionZone;
var localized string TowerDetection;

var localized string Ally;
var localized string Enemy;

var localized string Ramp;
var localized string Ladder;
var localized string Water;

// ---------------------------------------------------------------------------
// The tile cursor
// ---------------------------------------------------------------------------

var localized string MapEdge;
var localized string TopFloor;
var localized string BottomFloor;
var localized string CursorOn;
var localized string CursorJumped;
var localized string NoMarkerSelected;

var localized string NoMoveAvailable;
var localized string AlreadyHere;
var localized string CannotStandThere;
var localized string CannotStopThere;
var localized string Nearest;
var localized string PressAgainToMove;
var localized string Moving;
var localized string CannotMoveThere;

var localized string Unexplored;
var localized string Impassable;
var localized string CannotStandHere;
var localized string Clear;
var localized string TileCoords;
var localized string SeenBy;

var localized string NotTargetable;
var localized string NoTargets;
var localized string TargetsHeading;

// Cover on a tile's edge, one key per edge and height so a translator declines the direction
// freely. Russian follows the game's TargetHighCover/TargetLowCover vocabulary.
var localized string NoCover;
var localized string FullCoverNorth;
var localized string FullCoverSouth;
var localized string FullCoverEast;
var localized string FullCoverWest;
var localized string HalfCoverNorth;
var localized string HalfCoverSouth;
var localized string HalfCoverEast;
var localized string HalfCoverWest;

// Shared tactical target readouts and the blocked landing warning.
var localized string TargetHealth;
var localized string TargetShield;
var localized string TargetArmor;
var localized string BlockedLands;
var localized string AbilityAtTarget;
var localized string DamageDealt;
var localized string ShieldAbsorbs;
var localized string ArmorBlocks;
var localized string NoShieldLeft;
var localized string SquadConcealmentBroken;
var localized string UnitRevealed;
var localized string UnitEvacuated;
var localized string EnemySighted;
var localized string NewEnemyType;
var localized string ReinforcementsTransport;
var localized string ReinforcementsPsiGate;
var localized string ReinforcementsDrop;
var localized string ChosenArrives;

// Tactical HUD, target strip, shot breakdown and free aim.
var localized string NotTargeting;
var localized string NoAbilitySelected;
var localized string NoFreeAimBreakdown;
var localized string NoShotBreakdown;
var localized string PercentValue;
var localized string PercentWord;
var localized string FreeAimLabel;
var localized string Cooldown_One;
var localized string Cooldown_Few;
var localized string Cooldown_Many;
var localized string Charges_One;
var localized string Charges_Few;
var localized string Charges_Many;
var localized string AbilityObjective;
var localized string FreeAimNoTarget;
var localized string NoTarget;
var localized string NumberRange;
var localized string BlastEmpty;
var localized string BlastLabel;
var localized string BlastArea;
var localized string BlastSize;
var localized string EnemiesVisible_One;
var localized string EnemiesVisible_Few;
var localized string EnemiesVisible_Many;
var localized string BonusesLabel;
var localized string PenaltiesLabel;
var localized string Targeted;
var localized string TargetFallback;
var localized string PlusValue;
var localized string MinusValue;
var localized string Unavailable;
var localized string NoLongerVisible;
var localized string AimUp;
var localized string AimDown;
var localized string NoFloorAbove;
var localized string NoFloorBelow;
var localized string Aiming;
var localized string ArrowsToAim;
var localized string NotFreeAiming;
var localized string NoCursor;
var localized string OnOwnTile;
var localized string BlastRadius_One;
var localized string BlastRadius_Few;
var localized string BlastRadius_Many;
var localized string WillEndTurn;
var localized string Yourself;
var localized string FriendlyFireWarning;
var localized string CiviliansInBlast;
var localized string NotAimedYet;
var localized string Checked;
var localized string Unchecked;
var localized string AmmoOf;
var localized string BondLevel;
var localized string BonusLabel;
var localized string PenaltyLabel;
var localized string NoActionsLeft;
var localized string Actions_One;
var localized string Actions_Few;
var localized string Actions_Many;
var localized string Overwatch;
var localized string NoHackingScreen;
var localized string HackInProgress;
var localized string Hacking;
var localized string GambleStaysOn;
var localized string NoReward;
var localized string NothingUnlocked;
var localized string MissedLabel;
var localized string LeftRightChoose;
var localized string EnterToHack;
var localized string EscapeToCancel;
var localized string NotOnAvengerGrid;
var localized string NoRoomSelected;
var localized string BeingCleared;
var localized string NeedsAttention;
var localized string StaffSlotsFilled;
var localized string ScientistSlotsFree;
var localized string EngineerSlotsFree;
var localized string SoldierSlotsFree;
var localized string PsiSlotsFree;
var localized string StaffSlotsFree;
var localized string UpgradeSlots;
var localized string GridRowColumn;
var localized string GridTopLeft;
var localized string GridTopMiddle;
var localized string GridTopRight;
var localized string GridUpperLeft;
var localized string GridFarLeft;
var localized string GridMiddleRight;
var localized string GridLowerRight;
var localized string NoGeoscape;
var localized string NothingOnMap;
var localized string NothingSelected;
var localized string SortingFrom;
var localized string SortingFromAvenger;
var localized string GeoHere;
var localized string Miles_One;
var localized string Miles_Few;
var localized string Miles_Many;
var localized string OriginGoneAvenger;
var localized string GeoNorth;
var localized string GeoSouth;
var localized string GeoEast;
var localized string GeoWest;
var localized string GeoNortheast;
var localized string GeoNorthwest;
var localized string GeoSoutheast;
var localized string GeoSouthwest;
var localized string RadioRelayBuilt;
var localized string ContactEstablished;
var localized string RadioRelayAvailable;
var localized string ContactAvailable;
var localized string NoContact;
var localized string SuppliesIncome;
var localized string MissionHere;
var localized string RegionsContacted;
var localized string RadioRelaysBuilt;
var localized string GeoCatRegions;
var localized string GeoCatMissions;
var localized string GeoCatScanSites;
var localized string GeoCatFacilities;
var localized string GeoCatFactions;
var localized string GeoCatContinents;
var localized string GeoEmptyRegions;
var localized string GeoEmptyMissions;
var localized string GeoEmptyScanSites;
var localized string GeoEmptyFacilities;
var localized string GeoEmptyFactions;
var localized string GeoEmptyContinents;
var localized string BondmateOnSquad;

var localized string BreaksConcealment;
var localized string Noise;

var localized string HeightUp;
var localized string HeightDown;
var localized string FloorOf;

var localized string Outside;
var localized string OnRoof;
var localized string InsideUFO;
var localized string InsideDropship;
var localized string InsideBuilding;
var localized string BuildingFloor;
var localized string BuildingFloorOf;
var localized string BuildingFloorChanged;
var localized string BuildingFloorChangedOf;

// ---------------------------------------------------------------------------
// The object scanner
// ---------------------------------------------------------------------------

var localized string NoScanOrigin;
var localized string NothingToScan;

var localized string CatMarkers;
var localized string CatEnemies;
var localized string CatSquad;
var localized string CatLoot;
var localized string CatReinforcements;
var localized string CatCivilians;
var localized string CatHazards;
var localized string CatObjects;
var localized string CatTowers;
var localized string CatConnectors;
var localized string CatCover;
var localized string CatUnexplored;
var localized string CatExits;

var localized string EmptyMarkers;
var localized string EmptyEnemies;
var localized string EmptySquad;
var localized string EmptyLoot;
var localized string EmptyReinforcements;
var localized string EmptyCivilians;
var localized string EmptyHazards;
var localized string EmptyObjects;
var localized string EmptyTowers;
var localized string EmptyConnectors;
var localized string EmptyCover;
var localized string EmptyUnexplored;
var localized string EmptyExits;
var localized string EmptyGeneric;

var localized string SpottedBySquad;
var localized string Selected;
var localized string Reinforcements;
var localized string Tutorial;
var localized string TutorialVerb;
var localized string Objective;
var localized string ObjectiveNamed;
var localized string MarkedByAdvent;
var localized string Marked;
var localized string Used;
var localized string CanBeCarried;
var localized string LostSwarm;
var localized string CountVisible;
var localized string TilesAcross_One;
var localized string TilesAcross_Few;
var localized string TilesAcross_Many;
var localized string TilesWide_One;
var localized string TilesWide_Few;
var localized string TilesWide_Many;
var localized string DetectsWithin_One;
var localized string DetectsWithin_Few;
var localized string DetectsWithin_Many;
var localized string UnexploredSpace;
var localized string PositionOf;

// The Chosen Avenger Defense's Mobile Power Generator, an XComInteractiveLevelActor whose
// FriendlyDisplayName Firaxis copy-pasted from the stronghold's sarcophagus. The game owns no noun
// for it anywhere but inside the objective SENTENCE, and a mod loc file under the map package's own
// name does not override the key (tried 2026-09-17, heard unchanged), so the word is ours.
var localized string ChosenADGeneratorName;

// ---------------------------------------------------------------------------
// Expansion
// ---------------------------------------------------------------------------

// One counted noun: the form PluralRule picks for N, with N filled in. A blank form falls through
// so a language file that filled only Many still speaks.
static function string Count(int N, string One, string Few, string Many)
{
	local string Template;

	switch (PluralForm(N))
	{
	case 0: Template = One; break;
	case 1: Template = Few; break;
	}
	if (Template == "")
		Template = Many;
	if (Template == "")
		Template = One;
	return Fill1(Template, N);
}

// 0 = One, 1 = Few, 2 = Many.
static function int PluralForm(int N)
{
	local int Tens, Units;

	N = Abs(N);
	if (default.PluralRule ~= "slavic")
	{
		Units = N % 10;
		Tens = N % 100;
		if (Units == 1 && Tens != 11)
			return 0;
		if (Units >= 2 && Units <= 4 && (Tens < 12 || Tens > 14))
			return 1;
		return 2;
	}
	return (N == 1) ? 0 : 2;
}

static function string Fill1(string Template, int A)
{
	return Expand(Template, A, 0, 0, "");
}

static function string Fill2(string Template, int A, int B)
{
	return Expand(Template, A, B, 0, "");
}

static function string Fill3(string Template, int A, int B, int C)
{
	return Expand(Template, A, B, C, "");
}

static function string FillS(string Template, string S)
{
	return Expand(Template, 0, 0, 0, S);
}

// The game's own expander over its own shared tag object. Every slot a template may name is
// written, the unused ones cleared, so a stale value from another caller can never leak into a
// line of ours. A template with no tags comes back unchanged; an empty one stays empty.
static function string Expand(string Template, int A, int B, int C, string S)
{
	local XGParamTag Tag;

	if (Template == "")
		return "";
	Tag = XGParamTag(`XEXPANDCONTEXT.FindTag("XGParam"));
	if (Tag == none)
		return Template;
	Tag.IntValue0 = A;
	Tag.IntValue1 = B;
	Tag.IntValue2 = C;
	Tag.StrValue0 = S;
	return `XEXPAND.ExpandString(Template);
}
