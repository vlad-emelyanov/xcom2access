// X2AccessWallTones
// The tile cursor's WALL TONES: four looping voices, one per compass cardinal, whose volumes rise
// as a wall nears in that direction from the cursor tile — WH40KRTAccess's WallTones (itself
// WrathAccess's WallToneSystem), rebuilt on this engine's tile grid. East pans hard right, west
// hard left, north and south sit centred and differ by timbre, so a corridor reads as loud-left
// plus loud-right with a quiet front, a dead end swells the forward tone, and open ground goes
// near-silent on all four. It sonifies the SHAPE of the space, which no per-object readout can.
//
// Sighted parity: the walls, the cutaway and the map's edge are drawn on every frame; the bed is
// the geometry a sighted player takes in at a glance, delivered as a continuous aid. The wall
// definition is the rooms recon's (docs/rooms-recon.md): an edge that BLOCKS pathing and BLOCKS
// visibility across the same face. Cover — a crate, a sandbag, a car — blocks pathing and is
// vaultable or seen over, so it is not a wall and the cover ring already names it.
//
// Why an ACTOR: the four voices are AudioComponents, object references that a CDO must never hold
// (GC rule), and a timer is what the "when moving" mode needs. So one instance is spawned into the
// tactical world on first use, dies with the map, and is found again by iteration rather than by
// a stored reference (Get). The reader's SetCursorTile calls OnCursorTile on every landing.
//
// Playback is the engine's own: each voice is CreateAudioComponent over a cue that loops through
// a SoundNodeLooping (the `loop` tag in Sounds/, tools/make_sound_upk.py), spatialisation OFF so
// the channels baked into the wave stay where they are (the `left` / `right` tags — the listener
// is the camera, which rotates, and the compass must not), and the volume steered with
// AdjustVolume over GlideSeconds, the same slew RT uses so a whole-tile jump in wall distance
// never clicks. Between landings the voices idle at SILENT, never at zero: the device drops a
// wave instance at or under 0.0001 out of its active set and stops its source, and stops any
// source unheard for 60 ticks (docs/engine-notes.md, audio), so a true zero ends the loop and
// the next AdjustVolume steers a voice nobody is playing — which is what the first run heard:
// one landing, then nothing. A voice found stopped anyway (the 16x-duration cutoff, a device
// flush) is Play()ed again before it is aimed, and logged so the run says which path fired.
//
// No hotkey yet (player decision 2026-09-17): the mode is config only, When moving by default.

class X2AccessWallTones extends Actor config(Game);

// The cue keys, as registered under +SoundCuePaths (Config/XComGameData.ini), indexed by
// X2AccessTileReader's DIR_* cardinals.
const CUE_NORTH = "X2Access_Wall_North";
const CUE_SOUTH = "X2Access_Wall_South";
const CUE_EAST = "X2Access_Wall_East";
const CUE_WEST = "X2Access_Wall_West";

// The idle level. Above the device's 0.0001 cull threshold after every multiplier in the chain
// (the cue's 0.75, the sound class, the player's SFX slider down to a few percent), and 54 dB
// under full scale, which no mix surfaces.
const SILENT = 0.002;

// 0 off, 1 when moving (the bed rises on a cursor landing and fades HoldSeconds later),
// 2 continuous (the bed follows the cursor and never fades on its own).
var config int Mode;
// Full-scale multiplier at the wall itself; RT ships 25 %.
var config float Volume;
// A wall farther than this many tiles is silent; RT's 5 m is about 3.7 tiles.
var config float RangeTiles;
// How long a volume change takes to land. RT settles in about half a second.
var config float GlideSeconds;
// When moving: how long after a landing the bed stays up before gliding to silence.
var config float HoldSeconds;

var AudioComponent Voices[4];
var bool bBuilt;
// How many times a voice was found stopped and started again; the first few are logged.
var int Restarts;

// The one instance in the current world, spawned on demand. Iteration, not a cached reference:
// the caller is a CDO and the actor dies with the map.
static function X2AccessWallTones Get(bool bCreate)
{
	local WorldInfo WI;
	local X2AccessWallTones Tones;

	WI = class'WorldInfo'.static.GetWorldInfo();
	if (WI == none)
		return none;
	foreach WI.DynamicActors(class'X2AccessWallTones', Tones)
		return Tones;
	if (!bCreate)
		return none;
	Tones = WI.Spawn(class'X2AccessWallTones');
	if (Tones == none)
		`log("X2Access: wall tones actor failed to spawn", , 'X2Access');
	return Tones;
}

// The cursor landed on Tile: set every voice's target from the wall distance in its direction.
// Fog parity (RT's gate): a never-seen tile has no geometry on screen, so the bed goes silent
// there. The ray itself is not fog-checked per tile — RT's accepted residual, a short read into
// the fog from an explored tile beside it.
function OnCursorTile(XComWorldData WorldData, TTile Tile)
{
	local FOWTileStatus FOW;
	local byte GameplayFlags;
	local int Dir;
	local float Target;

	if (Mode == 0)
		return;
	if (!bBuilt && !Build())
		return;

	WorldData.GetTileFOWValue(Tile, FOW, GameplayFlags);
	if (FOW == eFOWTileStatus_NeverSeen)
	{
		Silence();
		return;
	}

	for (Dir = 0; Dir < 4; Dir++)
	{
		if (Voices[Dir] == none)
			continue;
		Revive(Dir);
		Target = FMax(Curve(DistanceToWall(WorldData, Tile, Dir)) * Volume, SILENT);
		Voices[Dir].AdjustVolume(GlideSeconds, Target);
	}

	if (Mode == 1)
		SetTimer(HoldSeconds, false, nameof(OnHoldExpired));
	else
		ClearTimer(nameof(OnHoldExpired));
}

// Glide every voice to the idle level and drop any pending hold. The voices keep looping,
// inaudible but never culled.
function Silence()
{
	local int Dir;

	ClearTimer(nameof(OnHoldExpired));
	for (Dir = 0; Dir < 4; Dir++)
	{
		if (Voices[Dir] != none)
			Voices[Dir].AdjustVolume(GlideSeconds, SILENT);
	}
}

// A voice the device stopped anyway is started again before it is aimed. Play() on a stopped
// component resets its playback clock and leaves the AdjustVolume state alone, so the glide
// that follows starts from the level the voice last landed on.
private function Revive(int Dir)
{
	if (Voices[Dir].IsPlaying())
		return;
	Voices[Dir].Play();
	Restarts++;
	if (Restarts <= 8)
		`log("X2Access: wall tone voice restarted:" @ CueKey(Dir) @ "(" $ Restarts $ ")", , 'X2Access');
}

function OnHoldExpired()
{
	Silence();
}

// Create the four voices, playing and silent. A cue that fails to resolve costs that direction
// its voice and is logged; the other three still play. Returns false only when nothing built.
private function bool Build()
{
	local int Dir;
	local SoundCue Cue;
	local AudioComponent AC;
	local bool bAny;

	for (Dir = 0; Dir < 4; Dir++)
	{
		Cue = FindCue(CueKey(Dir));
		if (Cue == none)
		{
			`log("X2Access: wall tone cue missing:" @ CueKey(Dir), , 'X2Access');
			continue;
		}
		// As XComSoundManager builds its own ambience channels: not auto-played, stopped with
		// the owner, attached to this actor. Not spatialised — the compass is in the samples.
		AC = CreateAudioComponent(Cue, false, true);
		if (AC == none)
		{
			`log("X2Access: wall tone component failed:" @ CueKey(Dir), , 'X2Access');
			continue;
		}
		AC.bAllowSpatialization = false;
		AC.bShouldRemainActiveIfDropped = true;
		AC.bAutoDestroy = false;
		// The idle level goes in BEFORE Play(), and as the CURRENT level, not through
		// AdjustVolume: the current level defaults to 1.0 and is only rewritten when a glide
		// lands, so an AdjustVolume(0, x) followed in the same frame by the first landing's
		// glide would start that glide from full scale — a burst, not a rise.
		AC.CurrAdjustVolumeTargetVolume = SILENT;
		AC.AdjustVolumeTargetVolume = SILENT;
		AC.Play();
		Voices[Dir] = AC;
		bAny = true;
	}
	bBuilt = bAny;
	return bAny;
}

// The cue by its registered key: the sound manager's table first (loaded at its Init from
// +SoundCuePaths), the package path as the fallback while that async load is still in flight.
private static function SoundCue FindCue(string Key)
{
	local XComSoundManager SoundMgr;
	local int Index;

	SoundMgr = `SOUNDMGR;
	if (SoundMgr != none)
	{
		Index = SoundMgr.SoundCues.Find('strKey', Key);
		if (Index != INDEX_NONE && SoundMgr.SoundCues[Index].Cue != none)
			return SoundMgr.SoundCues[Index].Cue;
	}
	return SoundCue(DynamicLoadObject("X2AccessSounds." $ Key, class'SoundCue'));
}

private static function string CueKey(int Cardinal)
{
	switch (Cardinal)
	{
	case class'X2AccessTileReader'.const.DIR_NORTH: return CUE_NORTH;
	case class'X2AccessTileReader'.const.DIR_SOUTH: return CUE_SOUTH;
	case class'X2AccessTileReader'.const.DIR_EAST:  return CUE_EAST;
	case class'X2AccessTileReader'.const.DIR_WEST:  return CUE_WEST;
	}
	return "";
}

// The grid raycast: walk the cardinal from Tile, at the cursor's own height, until an edge is a
// wall (blocked AND not seen through — docs/rooms-recon.md's `B--V`) or the next tile is off the
// map or outside the level volume, both of which are boundaries a sighted player sees drawn. The
// wall sits on the edge, half a tile past the last open tile. -1 when nothing within range.
private function float DistanceToWall(XComWorldData WorldData, TTile Tile, int Cardinal)
{
	local TTile Cur, Next;
	local vector CurPos, NextPos;
	local int DX, DY, Step, MaxSteps;

	if (!class'X2AccessTileReader'.static.CardinalDelta(Cardinal, DX, DY))
		return -1.0;
	MaxSteps = int(RangeTiles) + 1;
	Cur = Tile;
	for (Step = 1; Step <= MaxSteps; Step++)
	{
		Next = Cur;
		Next.X += DX;
		Next.Y += DY;
		if (Next.X < 0 || Next.Y < 0 || Next.X >= WorldData.NumX || Next.Y >= WorldData.NumY)
			return Step - 0.5;
		if (WorldData.IsTileOutOfRange(Next))
			return Step - 0.5;
		if (WorldData.IsAdjacentTileBlocked(Cur, Next))
		{
			// Out parameters, so the positions must be locals, not call expressions.
			CurPos = WorldData.GetPositionFromTileCoordinates(Cur);
			NextPos = WorldData.GetPositionFromTileCoordinates(Next);
			if (!WorldData.IsAdjacentTileVisible(CurPos, NextPos))
				return Step - 0.5;
		}
		Cur = Next;
	}
	return -1.0;
}

// 0 with no wall in range, rising to ~1 at the wall — WrathAccess's quadratic proximity curve,
// which bites close in: at RangeTiles 4, an adjacent wall is 0.77, two tiles out 0.39.
private function float Curve(float Dist)
{
	local float T;

	if (Dist < 0.0 || RangeTiles <= 0.0 || Dist >= RangeTiles)
		return 0.0;
	T = 1.0 - Dist / RangeTiles;
	return T * T;
}

defaultproperties
{
	Mode=1
	Volume=0.25
	RangeTiles=4.0
	GlideSeconds=0.5
	HoldSeconds=1.0

	bHidden=true
	bCollideActors=false
	bBlockActors=false
	bCollideWorld=false
	RemoteRole=ROLE_None
}
