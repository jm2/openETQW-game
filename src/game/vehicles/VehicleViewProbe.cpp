// SPDX-License-Identifier: MIT
//
// OpenETQW (M5 step 6): the vehicle view probe (g_logVehicleViews); see VehicleViewProbe.h.

#include "../precompiled.h"
#pragma hdrstop

#if defined( _DEBUG ) && !defined( ID_REDIRECT_NEWDELETE )
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#include "VehicleViewProbe.h"
#include "SeatGate.h"
#include "Transport.h"
#include "VehicleView.h"
#include "../Player.h"

/*
===============================================================================

	Line fields

	Positions and angles are printed in full precision (%.9g reads back as the same
	float), so that the test can compare them exactly; only -0 becomes 0, as in SIM_VIEW.
	An axis is printed as its angles (pitch, yaw, roll).

===============================================================================
*/

static double ProbeNumber( float value ) {
	return value == 0.0f ? 0.0 : ( double )value;
}

static void AppendVector( idStr& line, const char* key, const idVec3& v ) {
	char buffer[ 160 ];
	idStr::snPrintf( buffer, sizeof( buffer ), "%s(%.9g,%.9g,%.9g)", key, ProbeNumber( v.x ), ProbeNumber( v.y ), ProbeNumber( v.z ) );
	line += buffer;
}

static void AppendAxis( idStr& line, const char* key, const idMat3& axis ) {
	const idAngles a = axis.ToAngles();
	char buffer[ 160 ];
	idStr::snPrintf( buffer, sizeof( buffer ), "%s(%.9g,%.9g,%.9g)", key, ProbeNumber( a.pitch ), ProbeNumber( a.yaw ), ProbeNumber( a.roll ) );
	line += buffer;
}

// " <key>=fpo(x,y,z),fpa(p,y,r),tpo(x,y,z),tpa(p,y,r)"
static void AppendPose( idStr& line, const char* key, const vehicleViewPose_t& pose ) {
	line += va( " %s=", key );
	AppendVector( line, "fpo", pose.fpo );
	AppendAxis( line, ",fpa", pose.fpa );
	AppendVector( line, ",tpo", pose.tpo );
	AppendAxis( line, ",tpa", pose.tpa );
}

// "<name>: side=srv|cli[ at=<at>] tick= client= ent= pos= mode= class= ud= new="
static void BeginLine( idStr& line, const char* name, const char* at, const idPlayer* player, const seatInfo_t& seat ) {
	line = va( "%s: side=%s", name, gameLocal.isClient ? "cli" : "srv" );
	if ( at != NULL ) {
		line += va( " at=%s", at );
	}
	line += va( " tick=%d client=%d ent=%d pos=%d mode=%d class=%s ud=%d new=%d",
		gameLocal.framenum, player->entityNumber, seat.proxy->entityNumber, seat.positionId, seat.viewMode,
		seat.view->GetTypeName(), gameLocal.unlock.unlockedDraw ? 1 : 0, gameLocal.isNewFrame ? 1 : 0 );
}

static void TakePose( const idPlayer* player, const sdVehicleView& view, vehicleViewPose_t& pose ) {
	pose.fpo = player->firstPersonViewOrigin;
	pose.fpa = player->firstPersonViewAxis;
	pose.tpo = view.GetThirdPersonViewOrigin();
	pose.tpa = view.GetThirdPersonViewAxis();
}

/*
================
sdVehicleViewProbe::Sample
================
*/
bool sdVehicleViewProbe::Sample( const idPlayer* player, vehicleViewPose_t& pose ) {
	if ( !g_logVehicleViews.GetBool() ) {
		return false;
	}
	seatInfo_t seat;
	if ( !sdSeatGate::FindSeat( player, seat ) ) {
		return false;
	}
	TakePose( player, *seat.view, pose );
	return true;
}

/*
================
sdVehicleViewProbe::LogAORView

AOR_VIEW, printed after the calculation: the view before it (before=), the origin and
angles it returned (result=), the view after it (after=), the interpolation fraction
(f), and the vehicle's rendered and physics origins (rent=, phys=).
================
*/
void sdVehicleViewProbe::LogAORView( const idPlayer* player, const vehicleViewPose_t& before, const idVec3& origin, const idMat3& axis ) {
	seatInfo_t seat;
	if ( !sdSeatGate::FindSeat( player, seat ) ) {
		return;
	}
	vehicleViewPose_t after;
	TakePose( player, *seat.view, after );

	idStr line;
	BeginLine( line, "AOR_VIEW", NULL, player, seat );
	line += va( " f=%.9g", ProbeNumber( gameLocal.unlock.interpFraction ) );
	AppendPose( line, "before", before );
	AppendVector( line, " result=org", origin );
	AppendAxis( line, ",ang", axis );
	AppendPose( line, "after", after );
	AppendVector( line, " rent=", seat.proxy->GetRenderEntity()->origin );
	AppendVector( line, " phys=", seat.proxy->GetPhysics()->GetOrigin() );
	common->Printf( "%s\n", line.c_str() );
}

/*
================
LogViewState
================
*/
static void LogViewState( const idPlayer* player, const char* at ) {
	seatInfo_t seat;
	if ( !sdSeatGate::FindSeat( player, seat ) ) {
		return;
	}
	vehicleViewPose_t pose;
	TakePose( player, *seat.view, pose );

	idStr line;
	BeginLine( line, "VIEW_STATE", at, player, seat );
	AppendPose( line, "pose", pose );
	AppendVector( line, " rvo=", player->renderView.vieworg );
	AppendVector( line, " phys=", seat.proxy->GetPhysics()->GetOrigin() );
	AppendVector( line, " vpush=", seat.proxy->GetLastPushedOrigin() );
	common->Printf( "%s\n", line.c_str() );
}

/*
================
idGameLocal::EmitVehicleViewState

OpenETQW (M5 step 6), with g_logVehicleViews: VIEW_STATE, the view (pose=) of each player
in a vehicle at the start of a tick, before entity Think (at=pre), and at its end
(at=post): on a server where the tick logs its pose, on a network client after each
predicted tick, a tick predicted again (new=0) included. A server prints every client in
a vehicle, a network client its local player. Also the player's render view origin
(rvo=) and the vehicle's physics origin (phys=) and last pushed origin (vpush=).
================
*/
void idGameLocal::EmitVehicleViewState( const char* at ) {
	if ( !g_logVehicleViews.GetBool() ) {
		return;
	}
	if ( isClient ) {
		LogViewState( GetLocalPlayer(), at );
		return;
	}
	for ( int i = 0; i < MAX_CLIENTS; i++ ) {
		LogViewState( GetClient( i ), at );
	}
}
