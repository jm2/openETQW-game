// SPDX-License-Identifier: MIT
//
// OpenETQW (M5 step 13): the vehicle seat gate; see SeatGate.h.

#include "../precompiled.h"
#pragma hdrstop

#if defined( _DEBUG ) && !defined( ID_REDIRECT_NEWDELETE )
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#include "SeatGate.h"
#include "Transport.h"
#include "TransportExtras.h"
#include "VehicleView.h"
#include "JetPack.h"
#include "Walker.h"
#include "../Player.h"

extern idCVar g_simHash;

/*
===============================================================================

	Tables

	Both are empty in step 13: no seat has a presentation yet, so none can be
	allowed. A seat's view class gets a recipe when step 14 can present it, and a
	seat joins the default list when it meets docs/vehicle-responsiveness.md 3.3
	(D10: on by default).

===============================================================================
*/

// (kind, view class) -> recipe; ends with a NULL viewType
static const seatRecipe_t seatRecipes[] = {
	{ SPK_NONE, NULL, false },
};

// what the 'default' item of g_unlock_vehicleSeats stands for; ends with NULL
static const char* const seatDefaultList[] = {
	NULL,
};

/*
===============================================================================

	Parsing and matching

===============================================================================
*/

/*
================
IsKeyChar

the characters a key field keeps; everything else becomes '_'
================
*/
static bool IsKeyChar( char c ) {
	return ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) || ( c >= '0' && c <= '9' ) || c == '_';
}

/*
================
StripSpace

all whitespace goes: the console lexer splits a typed badger:*:x into "badger: * : x"
(no name token starts with ':' or '*'), and the list is read back as one string
================
*/
static void StripSpace( const char* text, idStr& out ) {
	out.Clear();
	for ( const char* c = text; *c != '\0'; c++ ) {
		if ( *c != ' ' && *c != '\t' && *c != '\n' && *c != '\r' && *c != '\v' && *c != '\f' ) {
			out.Append( *c );
		}
	}
}

/*
================
SplitComma
================
*/
static void SplitComma( const idStr& text, idStrList& items ) {
	items.Clear();
	idStr item;
	for ( int i = 0; i <= text.Length(); i++ ) {
		if ( i == text.Length() || text[ i ] == ',' ) {
			if ( item.Length() > 0 ) {
				items.Append( item );
			}
			item.Clear();
		} else {
			item.Append( text[ i ] );
		}
	}
}

/*
================
SplitFields

exactly three ':'-separated fields, as pointers into text and their lengths
================
*/
static bool SplitFields( const char* text, const char* fields[ 3 ], int lengths[ 3 ] ) {
	int n = 0;
	fields[ 0 ] = text;
	for ( const char* c = text; ; c++ ) {
		if ( *c == ':' || *c == '\0' ) {
			if ( n >= 3 ) {
				return false;
			}
			lengths[ n ] = c - fields[ n ];
			n++;
			if ( *c == '\0' ) {
				break;
			}
			if ( n < 3 ) {
				fields[ n ] = c + 1;
			}
		}
	}
	return n == 3;
}

/*
================
GlobMatch

the whole of text against the whole of pattern, ignoring case: '*' matches any run of
characters (none included), '?' exactly one. idStr::Filter does not do this: it
accepts a text that only starts like the pattern, and its '*' does not backtrack.
================
*/
static bool GlobMatch( const char* pattern, int patternLength, const char* text, int textLength ) {
	int p = 0;
	int t = 0;
	int star = -1;
	int resume = 0;
	while ( t < textLength ) {
		if ( p < patternLength && pattern[ p ] == '*' ) {		// a wildcard, even against a '*' in the text
			star = p++;
			resume = t;
		} else if ( p < patternLength && ( pattern[ p ] == '?' || idStr::ToLower( pattern[ p ] ) == idStr::ToLower( text[ t ] ) ) ) {
			p++;
			t++;
		} else if ( star >= 0 ) {
			p = star + 1;
			t = ++resume;
		} else {
			return false;
		}
	}
	while ( p < patternLength && pattern[ p ] == '*' ) {
		p++;
	}
	return p == patternLength;
}

/*
================
IsWellFormedItem

'default', '*', or three non-empty ':'-separated fields of key characters and wildcards
================
*/
static bool IsWellFormedItem( const char* item ) {
	if ( !idStr::Icmp( item, "default" ) || !idStr::Cmp( item, "*" ) ) {
		return true;
	}
	const char* fields[ 3 ];
	int lengths[ 3 ];
	if ( !SplitFields( item, fields, lengths ) ) {
		return false;
	}
	for ( int i = 0; i < 3; i++ ) {
		if ( lengths[ i ] == 0 ) {
			return false;
		}
		for ( int j = 0; j < lengths[ i ]; j++ ) {
			const char c = fields[ i ][ j ];
			if ( !IsKeyChar( c ) && c != '*' && c != '?' ) {
				return false;
			}
		}
	}
	return true;
}

/*
================
seatList_t

g_unlock_vehicleSeats as parsed; parsed again whenever the string changes
================
*/
struct seatList_t {
	bool							parsed;
	idStr							value;			// the CVar string it was parsed from
	bool							all;			// a '*' item
	bool							useDefault;		// a 'default' item
	idStrList						patterns;		// the well-formed script:position:viewType items
	int								numItems;		// well-formed items, 'default' and '*' included
	int								numBad;			// malformed items, ignored
};

static seatList_t seatList;		// zero-initialized: not parsed yet

static const seatList_t& ParsedSeatList( void ) {
	const char* value = g_unlock_vehicleSeats.GetString();
	if ( seatList.parsed && !seatList.value.Cmp( value ) ) {
		return seatList;
	}
	seatList.parsed = true;
	seatList.value = value;
	seatList.all = false;
	seatList.useDefault = false;
	seatList.patterns.Clear();
	seatList.numItems = 0;
	seatList.numBad = 0;

	idStr stripped;
	StripSpace( value, stripped );
	idStrList items;
	SplitComma( stripped, items );
	for ( int i = 0; i < items.Num(); i++ ) {
		const idStr& item = items[ i ];
		if ( !IsWellFormedItem( item.c_str() ) ) {
			// once per change of value, since it is parsed once per value
			gameLocal.Warning( "g_unlock_vehicleSeats: ignoring '%s': an item is 'default', '*' or script:position:viewType (wildcards * and ? in each field)", item.c_str() );
			seatList.numBad++;
			continue;
		}
		seatList.numItems++;
		if ( !item.Icmp( "default" ) ) {
			seatList.useDefault = true;
		} else if ( !item.Cmp( "*" ) ) {
			seatList.all = true;
		} else {
			seatList.patterns.Append( item );
		}
	}
	return seatList;
}

/*
================
testRecipes_t

g_seatGateTestRecipe as parsed: transport recipes for the listed view classes, test only
================
*/
struct testRecipes_t {
	bool							parsed;
	idStr							value;
	idStrList						viewTypes;
	idList< seatRecipe_t >			recipes;
};

static testRecipes_t testRecipes;

static const idList< seatRecipe_t >& ParsedTestRecipes( void ) {
	const char* value = g_seatGateTestRecipe.GetString();
	if ( testRecipes.parsed && !testRecipes.value.Cmp( value ) ) {
		return testRecipes.recipes;
	}
	testRecipes.parsed = true;
	testRecipes.value = value;
	testRecipes.recipes.Clear();

	idStr stripped;
	StripSpace( value, stripped );
	SplitComma( stripped, testRecipes.viewTypes );
	// the list is complete before any recipe points into it
	for ( int i = 0; i < testRecipes.viewTypes.Num(); i++ ) {
		seatRecipe_t& recipe = testRecipes.recipes.Alloc();
		recipe.kind = SPK_TRANSPORT;		// never the jetpack
		recipe.viewType = testRecipes.viewTypes[ i ].c_str();
		recipe.testOnly = true;
	}
	return testRecipes.recipes;
}

/*
===============================================================================

	sdSeatGate

===============================================================================
*/

/*
================
sdSeatGate::ProxyKind
================
*/
seatProxyKind_t sdSeatGate::ProxyKind( const idEntity* proxy ) {
	// idClass::Cast guards with "this ?", which a compiler may drop: test NULL first
	if ( proxy == NULL ) {
		return SPK_NONE;
	}
	if ( proxy->IsType( sdJetPack::Type ) ) {
		return SPK_JETPACK;
	}
	if ( proxy->IsType( sdTransport::Type ) ) {
		return SPK_TRANSPORT;
	}
	return SPK_OTHER;
}

/*
================
sdSeatGate::KindName
================
*/
const char* sdSeatGate::KindName( seatProxyKind_t kind ) {
	switch ( kind ) {
		case SPK_NONE:		return "none";
		case SPK_OTHER:		return "other";
		case SPK_TRANSPORT:	return "transport";
		case SPK_JETPACK:	return "jetpack";
	}
	return "?";
}

/*
================
sdSeatGate::FindSeat

The checks sdTransport::GetViewForPlayer would otherwise make fatal: PositionForPlayer
calls Error on a bad position id, and GetViewParms asserts on an empty seat and calls
Error on a seat without views. Requiring the seat to hold the player also turns away
a proxy a script set on a transport the player does not sit in.
================
*/
bool sdSeatGate::FindSeat( const idPlayer* player, seatInfo_t& seat ) {
	seat.kind = SPK_NONE;
	seat.proxy = NULL;
	seat.transport = NULL;
	seat.position = NULL;
	seat.view = NULL;
	seat.positionId = -1;
	seat.viewMode = -1;

	if ( player == NULL ) {
		return false;
	}
	seat.proxy = player->GetProxyEntity();
	seat.kind = ProxyKind( seat.proxy );
	if ( seat.kind == SPK_NONE ) {
		return false;
	}
	seat.positionId = player->GetProxyPositionId();
	seat.viewMode = player->GetProxyViewMode();
	if ( seat.kind != SPK_TRANSPORT && seat.kind != SPK_JETPACK ) {
		return false;
	}

	const sdTransport* transport = static_cast< const sdTransport* >( seat.proxy );
	const sdTransportPositionManager& positions = transport->GetPositionManager();
	if ( seat.positionId < 0 || seat.positionId >= positions.NumPositions() ) {
		return false;
	}
	const sdVehiclePosition* position = positions.PositionForId( seat.positionId );
	if ( position->GetPlayer() != player ) {
		return false;
	}
	if ( position->GetNumViewModes() <= 0 ) {
		return false;
	}
	seat.transport = transport;
	seat.position = position;
	seat.view = &position->GetViewParms();	// views[ viewMode ], or views[ 0 ] out of range, as the tick uses
	return true;
}

/*
================
sdSeatGate::MakeKey
================
*/
void sdSeatGate::MakeKey( const sdTransport& transport, const sdVehiclePosition& position, const sdVehicleView& view, idStr& key ) {
	const sdDeclVehicleScript* script = transport.GetVehicleScript();
	const char* fields[ 3 ] = {
		script != NULL ? script->GetName() : "",	// vs_vehicleScript: "badger", not the entityDef
		position.GetName(),							// a quoted decl string, spaces allowed
		view.GetTypeName(),							// the class: "" in the decl is "standard"
	};
	key.Clear();
	for ( int i = 0; i < 3; i++ ) {
		if ( i > 0 ) {
			key.Append( ':' );
		}
		for ( const char* c = fields[ i ]; *c != '\0'; c++ ) {
			key.Append( IsKeyChar( *c ) ? *c : '_' );
		}
	}
}

/*
================
sdSeatGate::FindRecipe

The built-in recipes, then (for transports only) the view classes g_seatGateTestRecipe
names, as test-only recipes. No recipe covers the jetpack, so no list item, a '*'
included, can allow it.
================
*/
const seatRecipe_t* sdSeatGate::FindRecipe( seatProxyKind_t kind, const char* viewType ) {
	if ( viewType == NULL ) {
		return NULL;
	}
	for ( const seatRecipe_t* recipe = seatRecipes; recipe->viewType != NULL; recipe++ ) {
		if ( recipe->kind == kind && !idStr::Icmp( recipe->viewType, viewType ) ) {
			return recipe;
		}
	}
	if ( kind != SPK_TRANSPORT || g_seatGateTestRecipe.GetString()[ 0 ] == '\0' ) {
		return NULL;
	}
	const idList< seatRecipe_t >& tests = ParsedTestRecipes();
	for ( int i = 0; i < tests.Num(); i++ ) {
		if ( !idStr::Icmp( tests[ i ].viewType, viewType ) ) {
			return &tests[ i ];
		}
	}
	return NULL;
}

/*
================
sdSeatGate::NumRecipes
================
*/
int sdSeatGate::NumRecipes( void ) {
	int count = 0;
	for ( const seatRecipe_t* recipe = seatRecipes; recipe->viewType != NULL; recipe++ ) {
		count++;
	}
	if ( g_seatGateTestRecipe.GetString()[ 0 ] != '\0' ) {
		count += ParsedTestRecipes().Num();
	}
	return count;
}

/*
================
sdSeatGate::MatchItem

a '*' item matches every key; any other item is three globs, each matched against the
whole of its key field, ignoring case
================
*/
bool sdSeatGate::MatchItem( const char* item, const char* key ) {
	if ( !idStr::Cmp( item, "*" ) ) {
		return true;
	}
	const char* itemFields[ 3 ];
	int itemLengths[ 3 ];
	const char* keyFields[ 3 ];
	int keyLengths[ 3 ];
	if ( !SplitFields( item, itemFields, itemLengths ) || !SplitFields( key, keyFields, keyLengths ) ) {
		return false;
	}
	for ( int i = 0; i < 3; i++ ) {
		if ( !GlobMatch( itemFields[ i ], itemLengths[ i ], keyFields[ i ], keyLengths[ i ] ) ) {
			return false;
		}
	}
	return true;
}

/*
================
sdSeatGate::IsListed
================
*/
bool sdSeatGate::IsListed( const char* key ) {
	const seatList_t& list = ParsedSeatList();
	if ( list.all ) {
		return true;
	}
	if ( list.useDefault ) {
		for ( int i = 0; seatDefaultList[ i ] != NULL; i++ ) {
			if ( MatchItem( seatDefaultList[ i ], key ) ) {
				return true;
			}
		}
	}
	for ( int i = 0; i < list.patterns.Num(); i++ ) {
		if ( MatchItem( list.patterns[ i ].c_str(), key ) ) {
			return true;
		}
	}
	return false;
}

/*
================
sdSeatGate::AllowsSeat

The proxy part of idPlayer::IsSeatPresentationAllowed. The structural checks come first,
so that no string is built while no recipe exists. It only reads state: nothing here
presents, updates visuals, clips or clamps a view.
================
*/
bool sdSeatGate::AllowsSeat( const idPlayer* viewPlayer ) {
	if ( viewPlayer == NULL ) {
		return false;
	}
	const idEntity* proxy = viewPlayer->GetProxyEntity();
	const seatProxyKind_t kind = ProxyKind( proxy );
	if ( kind != SPK_TRANSPORT && kind != SPK_JETPACK ) {
		return false;
	}
	if ( static_cast< const sdTransport* >( proxy )->IsTeleporting() ) {
		return false;
	}
	seatInfo_t seat;
	if ( !FindSeat( viewPlayer, seat ) ) {
		return false;
	}
	if ( FindRecipe( kind, seat.view->GetTypeName() ) == NULL ) {
		return false;
	}
	idStr key;
	MakeKey( *seat.transport, *seat.position, *seat.view, key );
	return IsListed( key.c_str() );
}

/*
===============================================================================

	listSeatViews

===============================================================================
*/

struct seatIKJoint_t {
	jointHandle_t					joint;
	const char*						ikType;
	int								positionId;		// -1 for the vehicle's own (walker) IK
};

/*
================
IKJointKeys

the ikParms keys naming the joints each IK class writes (their Setup and Update in
VehicleIK.cpp); sdVehicleIK_Steering moves the player's arms, not the vehicle's joints
================
*/
static const char* const* IKJointKeys( const char* ikType ) {
	static const char* const arms[] = { "jointElbow", "jointWrist", NULL };
	static const char* const single[] = { "joint", NULL };
	static const char* const none[] = { NULL };
	if ( !idStr::Icmp( ikType, "sdVehicleIKArms" ) || !idStr::Icmp( ikType, "sdVehicleWeaponAimer" ) ) {
		return arms;
	}
	if ( !idStr::Icmp( ikType, "sdVehicleSwivel" ) || !idStr::Icmp( ikType, "sdVehicleJointAimer" ) ) {
		return single;
	}
	return none;
}

/*
================
WeaponJointKeys

the weapon parameters naming the joints a vehicle weapon's aimer writes: only the fixed
minigun aims its own joints (sdVehicleWeaponFixedMinigun::Spawn); the locked weapon only
reads them. Weapons update after the seat cameras (sdTransport::PostThink), as IK does.
================
*/
static const char* const* WeaponJointKeys( const char* weaponType ) {
	static const char* const minigun[] = { "gunJointYaw", "gunJointPitch", NULL };
	static const char* const none[] = { NULL };
	if ( !idStr::Icmp( weaponType, "sdVehicleWeaponFixedMinigun" ) ) {
		return minigun;
	}
	return none;
}

/*
================
AddDrivenJoints

one SEAT_IK line for an IK system or weapon of a position, and its joints to the table
================
*/
static void AddDrivenJoints( int ent, int positionId, const char* positionName, const char* type, const char* const* keys,
	const idDict& parms, idAnimator* animator, idList< seatIKJoint_t >& table ) {
	idStr names;
	for ( ; *keys != NULL; keys++ ) {
		const char* jointName = parms.GetString( *keys );
		if ( jointName[ 0 ] == '\0' ) {
			continue;
		}
		const jointHandle_t joint = animator != NULL ? animator->GetJointHandle( jointName ) : INVALID_JOINT;
		if ( joint != INVALID_JOINT ) {
			seatIKJoint_t& entry = table.Alloc();
			entry.joint = joint;
			entry.ikType = type;
			entry.positionId = positionId;
		}
		if ( names.Length() > 0 ) {
			names.Append( ',' );
		}
		names.Append( jointName );
		if ( joint == INVALID_JOINT ) {
			names.Append( '?' );
		}
	}
	gameLocal.Printf( "SEAT_IK: ent=%d pos=%d name=\"%s\" type=%s joints=%s\n", ent, positionId, positionName, type,
		names.Length() > 0 ? names.c_str() : "-" );
}

/*
================
FormatClamp

enabled, rate-limited, min, max, the two rates and the filter
================
*/
static const char* FormatClamp( const angleClamp_t& clamp ) {
	return va( "%d,%d,%g,%g,%g,%g,%g", clamp.flags.enabled ? 1 : 0, clamp.flags.limitRate ? 1 : 0,
		clamp.extents[ 0 ], clamp.extents[ 1 ], clamp.rate[ 0 ], clamp.rate[ 1 ], clamp.filter );
}

/*
================
JointName
================
*/
static const char* JointName( idAnimator* animator, jointHandle_t joint ) {
	if ( animator == NULL || joint == INVALID_JOINT ) {
		return "-";
	}
	const char* name = animator->GetJointName( joint );
	return ( name != NULL && name[ 0 ] != '\0' ) ? name : "-";
}

/*
================
ListTransport
================
*/
static void ListTransport( sdTransport* transport, int& numSeats, int& numViews ) {
	const sdDeclVehicleScript* script = transport->GetVehicleScript();
	const char* scriptName = script != NULL ? script->GetName() : "-";
	sdTransportPositionManager& positions = transport->GetPositionManager();
	idAnimator* animator = transport->GetAnimator();
	const int ent = transport->entityNumber;

	gameLocal.Printf( "SEAT_VEHICLE: ent=%d class=%s script=%s kind=%s positions=%d\n", ent,
		transport->spawnArgs.GetString( "classname" ), scriptName,
		sdSeatGate::KindName( sdSeatGate::ProxyKind( transport ) ), positions.NumPositions() );

	// the joints that the IK systems and weapon aimers named in the decl write (the vehicle
	// script's positions, and a walker's legs); joints moved by scripts, animation or
	// wheels are not in it
	idList< seatIKJoint_t > ikJoints;
	if ( script != NULL ) {
		for ( int i = 0; i < script->positions.Num(); i++ ) {
			const positionInfo_t& info = script->positions[ i ]->positionInfo;
			for ( int j = 0; j < info.ikSystems.Num(); j++ ) {
				const vehicleIKSystemInfo_t& ik = info.ikSystems[ j ];
				AddDrivenJoints( ent, i, info.name.c_str(), ik.ikType.c_str(), IKJointKeys( ik.ikType.c_str() ), ik.ikParms, animator, ikJoints );
			}
			for ( int j = 0; j < info.weapons.Num(); j++ ) {
				const vehicleWeaponInfo_t& weapon = info.weapons[ j ];
				const char* const* keys = WeaponJointKeys( weapon.weaponType.c_str() );
				if ( *keys != NULL && weapon.weaponDef != NULL ) {
					AddDrivenJoints( ent, i, info.name.c_str(), weapon.weaponType.c_str(), keys, weapon.weaponDef->GetDict(), animator, ikJoints );
				}
			}
		}
	}
	if ( transport->IsType( sdWalker::Type ) && transport->spawnArgs.GetInt( "ik_numLegs" ) > 0 && animator != NULL ) {
		// sdIK_Walker::Init: the waist, and each leg's hip, mid, knee and ankle
		idStrList keys;
		keys.Append( "ik_waist" );
		for ( int i = 1; i <= transport->spawnArgs.GetInt( "ik_numLegs" ); i++ ) {
			keys.Append( va( "ik_hip%d", i ) );
			keys.Append( va( "ik_mid%d", i ) );
			keys.Append( va( "ik_knee%d", i ) );
			keys.Append( va( "ik_ankle%d", i ) );
		}
		idStr names;
		for ( int i = 0; i < keys.Num(); i++ ) {
			const char* jointName = transport->spawnArgs.GetString( keys[ i ].c_str() );
			const jointHandle_t joint = animator->GetJointHandle( jointName );
			if ( joint != INVALID_JOINT ) {
				seatIKJoint_t& entry = ikJoints.Alloc();
				entry.joint = joint;
				entry.ikType = "sdIK_Walker";
				entry.positionId = -1;
				if ( names.Length() > 0 ) {
					names.Append( ',' );
				}
				names.Append( jointName );
			}
		}
		gameLocal.Printf( "SEAT_IK: ent=%d pos=- name=\"-\" type=sdIK_Walker joints=%s\n", ent, names.Length() > 0 ? names.c_str() : "-" );
	}

	for ( int i = 0; i < positions.NumPositions(); i++ ) {
		const sdVehiclePosition* position = positions.PositionForId( i );
		numSeats++;
		// the live views: AddView skips a type the factory does not know
		for ( int m = 0; m < position->GetNumViewModes(); m++ ) {
			const sdVehicleView* view = position->GetView( m );
			if ( view == NULL ) {
				continue;
			}
			numViews++;
			const positionViewMode_t& parms = view->GetViewParms();
			idStr key;
			sdSeatGate::MakeKey( *transport, *position, *view, key );
			const seatProxyKind_t kind = sdSeatGate::ProxyKind( transport );
			const bool recipe = sdSeatGate::FindRecipe( kind, view->GetTypeName() ) != NULL;

			// the eye joint the damped views follow, and their pivot: eyePivot, else the
			// eye's parent (sdDampedVehicleView::SetupEyes). Smooth views use the eye only
			// as the orbit origin; the player and icarus views do not use it.
			idStr eye = "-";
			idStr pivot = "-";
			idStr ikDriver = "-";
			if ( parms.eyes.Length() > 0 ) {
				const jointHandle_t eyeJoint = animator != NULL ? animator->GetJointHandle( parms.eyes.c_str() ) : INVALID_JOINT;
				eye = parms.eyes;
				if ( eyeJoint == INVALID_JOINT ) {
					eye.Append( '?' );		// the model has no such joint
				} else {
					jointHandle_t pivotJoint = parms.eyePivot.Length() > 0 ? animator->GetJointHandle( parms.eyePivot.c_str() ) : INVALID_JOINT;
					if ( pivotJoint == INVALID_JOINT ) {
						pivotJoint = animator->GetJointParent( eyeJoint );
					}
					pivot = JointName( animator, pivotJoint );
					// the first joint from the eye up that IK named in the decl writes
					jointHandle_t joint = eyeJoint;
					for ( int depth = 0; joint != INVALID_JOINT && depth < 256 && ikDriver == "-"; depth++ ) {
						for ( int k = 0; k < ikJoints.Num(); k++ ) {
							if ( ikJoints[ k ].joint == joint ) {
								ikDriver = va( "%s/%s@%d", JointName( animator, joint ), ikJoints[ k ].ikType, ikJoints[ k ].positionId );
								break;
							}
						}
						joint = animator->GetJointParent( joint );
					}
				}
			}

			idStr line = va( "SEAT_VIEW: ent=%d script=%s pos=%d name=\"%s\" mode=%d/%d type=%s decl=%s key=%s listed=%d recipe=%d",
				transport->entityNumber, scriptName, i, position->GetName(), m, position->GetNumViewModes(), view->GetTypeName(),
				parms.type.Length() > 0 ? parms.type.c_str() : "-", key.c_str(), sdSeatGate::IsListed( key.c_str() ) ? 1 : 0, recipe ? 1 : 0 );
			line += va( " third=%d tophat=%d center=%d", parms.thirdPerson ? 1 : 0, parms.tophatRequired ? 1 : 0, parms.autoCenter ? 1 : 0 );
			line += va( " cy=%s", FormatClamp( parms.clampYaw ) );
			line += va( " cp=%s", FormatClamp( parms.clampPitch ) );
			line += va( " cdy=%s", FormatClamp( parms.clampDampedYaw ) );
			line += va( " cdp=%s", FormatClamp( parms.clampDampedPitch ) );
			line += va( " eye=%s pivot=%s ik=%s\n", eye.c_str(), pivot.c_str(), ikDriver.c_str() );
			gameLocal.Printf( "%s", line.c_str() );
		}
	}
}

/*
================
sdSeatGate::ListSeatViews_f

listSeatViews [all|<filter>]: each seat's view classes, flags, clamps and eye joint,
which the binary vehicle decls hide, with its key and whether g_unlock_vehicleSeats
lists it. By default the first vehicle of each vehicle script; 'all' every vehicle;
a filter (wildcards * and ?, whole name, ignoring case) every vehicle whose entity
name, class or vehicle script matches. A cheat: it reads the positions of vehicles
the player may not see.
================
*/
void sdSeatGate::ListSeatViews_f( const idCmdArgs& args ) {
	// the lexer splits "*icarus*" into three tokens: join them again
	idStr filter;
	for ( int i = 1; i < args.Argc(); i++ ) {
		filter += args.Argv( i );
	}
	const bool all = !filter.Icmp( "all" );
	const bool filtered = filter.Length() > 0 && !all;

	const seatList_t& list = ParsedSeatList();
	gameLocal.Printf( "SEAT_VIEWS: begin filter=%s list=\"%s\" items=%d bad=%d recipes=%d\n",
		filter.Length() > 0 ? filter.c_str() : "-", list.value.c_str(), list.numItems, list.numBad, NumRecipes() );

	int numVehicles = 0;
	int numSeats = 0;
	int numViews = 0;
	idList< const sdDeclVehicleScript* > seen;
	for ( idEntity* ent = gameLocal.spawnedEntities.Next(); ent != NULL; ent = ent->spawnNode.Next() ) {
		if ( ProxyKind( ent ) != SPK_TRANSPORT && ProxyKind( ent ) != SPK_JETPACK ) {
			continue;
		}
		sdTransport* transport = static_cast< sdTransport* >( ent );
		const sdDeclVehicleScript* script = transport->GetVehicleScript();
		if ( filtered ) {
			const char* names[ 3 ] = { transport->name.c_str(), transport->spawnArgs.GetString( "classname" ), script != NULL ? script->GetName() : "" };
			bool match = false;
			for ( int i = 0; i < 3 && !match; i++ ) {
				match = GlobMatch( filter.c_str(), filter.Length(), names[ i ], idStr::Length( names[ i ] ) );
			}
			if ( !match ) {
				continue;
			}
		} else if ( !all ) {
			if ( seen.FindIndex( script ) >= 0 ) {
				continue;
			}
			seen.Append( script );
		}
		numVehicles++;
		ListTransport( transport, numSeats, numViews );
	}

	gameLocal.Printf( "SEAT_VIEWS: end vehicles=%d seats=%d views=%d\n", numVehicles, numSeats, numViews );
}

/*
===============================================================================

	idGameLocal: the latch and its evidence

===============================================================================
*/

/*
================
SeatIdentity

the proxy, position and view mode a latch is compared by; -1 each without a proxy
================
*/
static void SeatIdentity( idPlayer* viewPlayer, int& proxySpawnId, int& positionId, int& viewMode ) {
	idEntity* proxy = viewPlayer != NULL ? viewPlayer->GetProxyEntity() : NULL;
	if ( proxy == NULL ) {
		proxySpawnId = -1;
		positionId = -1;
		viewMode = -1;
		return;
	}
	proxySpawnId = gameLocal.GetSpawnId( proxy );
	positionId = viewPlayer->GetProxyPositionId();
	viewMode = viewPlayer->GetProxyViewMode();
}

/*
================
idGameLocal::LatchSeatPresentation

OpenETQW (M5 step 13): at each tick entry, next to canUnlockFrames, the seat predicate
and the seat it was taken for. Nothing reads it yet: step 14's seat view will, on
ticks, and re-check it at every draw (RecheckSeatPresentation).
================
*/
void idGameLocal::LatchSeatPresentation( idPlayer* localPlayer ) {
	if ( localPlayer == NULL ) {
		unlock.seatAllowed = false;
		unlock.seatProxySpawnId = -1;
		unlock.seatPositionId = -1;
		unlock.seatViewMode = -1;
		return;
	}
	unlock.seatAllowed = localPlayer->IsSeatPresentationAllowed();
	SeatIdentity( localPlayer->GetViewClient(), unlock.seatProxySpawnId, unlock.seatPositionId, unlock.seatViewMode );
}

/*
================
idGameLocal::RecheckSeatPresentation

OpenETQW (M5 step 13): the draw-time re-check (docs/vehicle-responsiveness.md 3.1, rule
6). True only while the latch is set, the live proxy, position and view mode are the
latched ones, and the predicate still holds; an entry, exit, seat swap, camera cycle or
teleport since the latch makes it false. Only EmitSeatGate calls it in step 13.
================
*/
bool idGameLocal::RecheckSeatPresentation( void ) {
	if ( !unlock.seatAllowed ) {
		return false;
	}
	idPlayer* localPlayer = GetLocalPlayer();
	if ( localPlayer == NULL ) {
		return false;
	}
	int proxySpawnId;
	int positionId;
	int viewMode;
	SeatIdentity( localPlayer->GetViewClient(), proxySpawnId, positionId, viewMode );
	if ( proxySpawnId != unlock.seatProxySpawnId || positionId != unlock.seatPositionId || viewMode != unlock.seatViewMode ) {
		return false;
	}
	return localPlayer->IsSeatPresentationAllowed();
}

/*
================
idGameLocal::EmitSeatGate

OpenETQW (M5 step 13), with g_simHash: each tick, after VEH_TRACE, the seat gate's
state for the engine's seat-gate test. cu and latch are canUnlockFrames and the seat
latch taken at this tick's entry; pred is the predicate now, at the tick's end; draw is
the draw-time re-check now. The seat is the local view player's; with a seat, listed
and recipe are given whatever the other checks say, so that key matching is tested
while no recipe exists. Without one, key is "-" and both are 0.
================
*/
void idGameLocal::EmitSeatGate( void ) {
	if ( !g_simHash.GetBool() ) {
		return;
	}
	idPlayer* localPlayer = GetLocalPlayer();
	idPlayer* viewPlayer = localPlayer != NULL ? localPlayer->GetViewClient() : NULL;
	const bool pred = localPlayer != NULL && localPlayer->IsSeatPresentationAllowed();
	const bool draw = RecheckSeatPresentation();

	seatInfo_t seat;
	idStr key = "-";
	bool listed = false;
	bool recipe = false;
	if ( sdSeatGate::FindSeat( viewPlayer, seat ) ) {
		sdSeatGate::MakeKey( *seat.transport, *seat.position, *seat.view, key );
		listed = sdSeatGate::IsListed( key.c_str() );
		recipe = sdSeatGate::FindRecipe( seat.kind, seat.view->GetTypeName() ) != NULL;
	}

	common->Printf( "SEAT_GATE: tick=%d cu=%d latch=%d pred=%d draw=%d kind=%s ent=%d pos=%d mode=%d key=%s listed=%d recipe=%d\n",
		framenum, unlock.canUnlockFrames ? 1 : 0, unlock.seatAllowed ? 1 : 0, pred ? 1 : 0, draw ? 1 : 0,
		sdSeatGate::KindName( seat.kind ), seat.proxy != NULL ? seat.proxy->entityNumber : -1,
		seat.positionId, seat.viewMode, key.c_str(), listed ? 1 : 0, recipe ? 1 : 0 );
}
