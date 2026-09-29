// X2AccessCamera_TileFollow
// The tactical default camera, made to follow the tile cursor.
//
// On mouse and keyboard the game's default camera (X2Camera_FollowMouseCursor, added by
// X2TacticalGameRuleset.AddDefaultPathingCamera) deliberately does NOT follow the 3D cursor: the
// mouse places the cursor by a ray through the camera, so a camera chasing the cursor would chase
// itself. It moves only on scroll input and on its unit-centring events. Only the controller camera
// (X2Camera_FollowCursor) follows, which is why "pushing the cursor moves the camera" held on a pad
// and never on the keyboard. This subclass keeps everything the mouse camera does and adds one
// thing: the tile reader tells it where the cursor landed (FocusOn), so it follows OUR moves and
// never the mouse, and cannot feed back on itself.
//
// Heading (player decision 2026-09-30): the vanilla diagonal headings stay, and the one nearest
// engine north is chosen. The game offers exactly HumanTurnYaw plus 90 degree steps (Ctrl+Q/E);
// with the shipped 48.5 that is the default heading itself, screen-up about 41.5 degrees west of
// north. It is applied when this camera first activates (mission start, and again after a load,
// which builds a fresh camera stack) and on the tile reader's recenter. Ctrl+Q/E stay live.
//
// Installation: the stack's camera list is private, so the vanilla default cannot be removed. It
// is not needed: AddCamera inserts before the first camera of STRICTLY higher priority, so a camera
// of equal priority added later lands above the earlier one, and the active camera is the topmost
// that does not yield. The vanilla default is added only while the ruleset builds or loads the
// mission (CreateTacticalGame, LoadTacticalGame, CreateChallengeGame), before the tile cursor can
// plant, so ours always ends up on top and the vanilla one sits dormant beneath it.
//
// Found by CameraTag each time, never held: the tile reader is a CDO (the GC rule), and the
// stack, owned by the XComCamera actor, is what keeps this instance alive.

class X2AccessCamera_TileFollow extends X2Camera_FollowMouseCursor;

// Engine north is +Y (X2AccessTileReader's NORTH_DX/NORTH_DY), and a rotator of yaw 90 degrees
// looks along +Y.
const NORTH_YAW_DEG = 90.0;

var private bool bEverActivated;
var private bool bPendingFocus;
var private vector PendingFocus;

// The installed instance, adding it when the stack has none. None outside tactical, and in
// controller mode, whose own default camera already follows the cursor.
static function X2AccessCamera_TileFollow FindOrInstall()
{
	local XComTacticalController TC;
	local XComCamera Cam;
	local X2AccessCamera_TileFollow Follow;

	TC = XComTacticalController(class'WorldInfo'.static.GetWorldInfo().GetALocalPlayerController());
	if (TC == none)
		return none;
	Cam = XComCamera(TC.PlayerCamera);
	if (Cam == none || Cam.CameraStack == none)
		return none;

	Follow = X2AccessCamera_TileFollow(Cam.CameraStack.FindCameraWithTag(default.CameraTag));
	if (Follow != none)
		return Follow;

	if (Cam.CameraStack.ContainsCameraOfClass('X2Camera_FollowCursor'))
		return none;

	Follow = new class'X2AccessCamera_TileFollow';
	Cam.CameraStack.AddCamera(Follow);
	return Follow;
}

// Centre on a world position. Called by the tile reader on every landing.
static function Follow(vector Target)
{
	local X2AccessCamera_TileFollow Camera;

	Camera = FindOrInstall();
	if (Camera != none)
		Camera.FocusOn(Target);
}

// Turn to the vanilla heading nearest north. Called by the tile reader's recenter.
static function FaceNorthmost()
{
	local X2AccessCamera_TileFollow Camera;

	Camera = FindOrInstall();
	if (Camera != none)
		Camera.FaceNorthmostHeading();
}

function FocusOn(vector Target)
{
	// Only X and Y matter: GetCameraLookat replaces Z with the cursor's floor.
	LookAt = Target;
	// Activation copies the look-at from the camera it takes over from, which would undo a focus
	// set between AddCamera and the stack's next update. Keep it for the first activation.
	if (!bEverActivated)
	{
		PendingFocus = Target;
		bPendingFocus = true;
	}
}

function Activated(TPOV CurrentPOV, X2Camera PreviousActiveCamera, X2Camera_LookAt LastActiveLookAtCamera)
{
	super.Activated(CurrentPOV, PreviousActiveCamera, LastActiveLookAtCamera);

	// Later activations (after a cutscene, an aim, the enemy turn) keep vanilla's hand-over: the
	// rotation and look-at come from the previous look-at camera, so the heading set here sticks.
	if (bEverActivated)
		return;
	bEverActivated = true;
	FaceNorthmostHeading();
	if (bPendingFocus)
	{
		LookAt = PendingFocus;
		bPendingFocus = false;
	}
}

// Of the headings the game itself offers (HumanTurnYaw plus 90 degree steps), the one whose
// screen-up is closest to engine north. Turns the short way from the current target.
function FaceNorthmostHeading()
{
	local float Heading, Best, BestError, Error, Delta;
	local int Step;

	BestError = 1000.0;
	for (Step = 0; Step < 4; Step++)
	{
		Heading = HumanTurnYaw + 90.0 * Step;
		Error = Abs(WrapDegrees(Heading - NORTH_YAW_DEG));
		if (Error < BestError)
		{
			BestError = Error;
			Best = Heading;
		}
	}

	Delta = Best * DegToUnrRot - TargetRotation.Yaw;
	Delta -= 65536.0 * Round(Delta / 65536.0);
	TargetRotation.Yaw += Round(Delta);
}

static function float WrapDegrees(float Degrees)
{
	return Degrees - 360.0 * Round(Degrees / 360.0);
}

defaultproperties
{
	CameraTag = X2AccessTileFollow
}
