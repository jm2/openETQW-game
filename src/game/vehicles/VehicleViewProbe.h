// SPDX-License-Identifier: MIT
//
// OpenETQW (M5 step 6): the vehicle view probe, g_logVehicleViews, a test diagnostic for
// the engine's vehicle-view-net test. For each player in a vehicle (an sdTransport seat) it
// prints the view state at the start and the end of each tick (VIEW_STATE) and around each
// area-of-relevance view calculation, idPlayer::GetAORView (AOR_VIEW), which the server's
// antilag and snapshots and a client's snapshots call between ticks. The test compares
// them to show whether those calls return the tick's view and leave it unchanged. It is
// read-only, and with the CVar off it prints nothing.

#ifndef __GAME_VEHICLES_VEHICLEVIEWPROBE_H__
#define __GAME_VEHICLES_VEHICLEVIEWPROBE_H__

class idPlayer;

// a player's first-person view and its seat view's third-person view
struct vehicleViewPose_t {
	idVec3							fpo;	// idPlayer::firstPersonViewOrigin
	idMat3							fpa;	// idPlayer::firstPersonViewAxis
	idVec3							tpo;	// sdVehicleView::GetThirdPersonViewOrigin
	idMat3							tpa;	// sdVehicleView::GetThirdPersonViewAxis
};

class sdVehicleViewProbe {
public:
	// with g_logVehicleViews, true for a player in an sdTransport seat, with its view now in
	// pose; otherwise false, and pose is not written
	static bool						Sample( const idPlayer* player, vehicleViewPose_t& pose );

	// AOR_VIEW: one GetAORView calculation, given the pose Sample took before it and the
	// origin and axis it returned
	static void						LogAORView( const idPlayer* player, const vehicleViewPose_t& before, const idVec3& origin, const idMat3& axis );
};

#endif // __GAME_VEHICLES_VEHICLEVIEWPROBE_H__
