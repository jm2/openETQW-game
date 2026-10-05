// SPDX-License-Identifier: MIT
//
// OpenETQW (M5 step 13): the vehicle seat gate. It decides which vehicle seats may
// later get a presentation-only view (step 14); it changes nothing drawn or simulated
// by itself. idPlayer::IsFPSUnlock stays as retail 1.5 has it (no per-frame view in any
// seat); idPlayer::IsSeatPresentationAllowed is the separate predicate, and this class
// holds its proxy part: the seat must be a transport's (never a mounted gun's), its
// current view class must have a recipe, and its key must be listed in
// g_unlock_vehicleSeats. See the engine's docs/decisions/0003-vehicle-seat-gate.md.

#ifndef __GAME_VEHICLES_SEATGATE_H__
#define __GAME_VEHICLES_SEATGATE_H__

class idEntity;
class idPlayer;
class idCmdArgs;
class sdTransport;
class sdVehiclePosition;
class sdVehicleView;

typedef enum seatProxyKind_e {
	SPK_NONE,						// no proxy: on foot
	SPK_OTHER,						// a proxy that is not a transport (a mounted gun, a script's proxy): never allowed
	SPK_TRANSPORT,					// an sdTransport other than the jetpack
	SPK_JETPACK,					// sdJetPack (the icarus): a transport, kept apart so that no transport recipe covers it
} seatProxyKind_t;

// what makes a seat presentable; keyed by (kind, view class). Step 14 adds its hooks here.
struct seatRecipe_t {
	seatProxyKind_t					kind;		// SPK_TRANSPORT or SPK_JETPACK only
	const char*						viewType;	// sdVehicleView::GetTypeName()
	bool							testOnly;	// from g_seatGateTestRecipe: opens the gate, is never presented
	bool							presentable;	// M5 step 14: sdPresentation can draw the class (first-person views)
};

// the seat a player views from; FindSeat fills it
struct seatInfo_t {
	seatProxyKind_t					kind;
	idEntity*						proxy;
	const sdTransport*				transport;	// NULL unless FindSeat returned true
	const sdVehiclePosition*		position;
	const sdVehicleView*			view;		// the current view: what the tick's camera uses
	int								positionId;	// the player's proxy position and view mode (-1 without a proxy)
	int								viewMode;
};

class sdSeatGate {
public:
	static seatProxyKind_t			ProxyKind( const idEntity* proxy );
	static const char*				KindName( seatProxyKind_t kind );

	// true for an occupied transport or jetpack seat that holds the player and has a view;
	// otherwise false, with the kind, proxy, position id and view mode still set
	static bool						FindSeat( const idPlayer* player, seatInfo_t& seat );

	// script:position:viewType, each field with every character outside [A-Za-z0-9_] made '_'
	static void						MakeKey( const sdTransport& transport, const sdVehiclePosition& position, const sdVehicleView& view, idStr& key );

	// NULL without one; a test recipe's pointer is valid until g_seatGateTestRecipe changes
	static const seatRecipe_t*		FindRecipe( seatProxyKind_t kind, const char* viewType );
	static int						NumRecipes( void );

	static bool						IsListed( const char* key );						// by g_unlock_vehicleSeats
	static bool						MatchItem( const char* item, const char* key );	// one list item against one key

	// the proxy part of idPlayer::IsSeatPresentationAllowed; reads state only
	static bool						AllowsSeat( const idPlayer* viewPlayer );

	static void						ListSeatViews_f( const idCmdArgs& args );
};

#endif // __GAME_VEHICLES_SEATGATE_H__
