// SPDX-License-Identifier: MIT
//
// OpenETQW (M5 step 14, stage 3): see Presentation.h.

#include "precompiled.h"
#pragma hdrstop

#include "Presentation.h"
#include "Player.h"
#include "vehicles/Transport.h"
#include "vehicles/TransportExtras.h"
#include "vehicles/VehicleView.h"
#include "vehicles/SeatGate.h"
#include "client/ClientEntity.h"

// the continuity guard's limits (vehicle-responsiveness stage 3): a pose that moved
// further than this between two ticks is drawn unblended
static const float	PRESENT_MIN_JUMP = 64.0f;		// u
static const float	PRESENT_MAX_TURN = 45.0f;		// degrees
// the camera-in-hull guard: a camera that moved this far in the hull's frame between two
// ticks (a pivot recentring, a seat's camera reset) is drawn unblended
static const float	PRESENT_MAX_CAMERA_MOVE = 32.0f;	// u

// the cosine of half the angle between two rotations, from their quaternions
static float HalfAngleCos( const idMat3& a, const idMat3& b ) {
	const idQuat qa = a.ToQuat();
	const idQuat qb = b.ToQuat();
	return idMath::Fabs( qa.x * qb.x + qa.y * qb.y + qa.z * qb.z + qa.w * qb.w );
}

/*
================
sdPresentation::Clear
================
*/
void sdPresentation::Clear( void ) {
	for ( int i = 0; i < 2; i++ ) {
		samples[ i ].valid = false;
		samples[ i ].framenum = -1;
	}
	blending = false;
	prepared = false;
	drawFrame = -1;
	drawProxySpawnId = -1;
	blendedDraws = 0;
	blendedBeforeTick = 0;
	preparedDraws = 0;
	preparedBeforeTick = 0;
}

/*
================
sdPresentation::SampleTick

The tick's hull and camera, taken where the tick logs its pose. Only for a seat whose
presentation the seat gate allows at this tick's latch and whose recipe is presentable;
a sample is the transport's render entity (post-decay, as Present pushed it) and the
camera the tick computed, as an offset and axis in the hull's frame. Reads only.
================
*/
void sdPresentation::SampleTick( idPlayer* viewPlayer ) {
	blendedBeforeTick = blendedDraws;
	blendedDraws = 0;
	preparedBeforeTick = preparedDraws;
	preparedDraws = 0;
	sample_t& s = samples[ gameLocal.framenum & 1 ];
	s.valid = false;
	s.framenum = gameLocal.framenum;
	if ( viewPlayer == NULL || !gameLocal.unlock.seatAllowed ) {
		return;
	}
	seatInfo_t seat;
	if ( !sdSeatGate::FindSeat( viewPlayer, seat ) || seat.transport == NULL || seat.view == NULL ) {
		return;
	}
	const seatRecipe_t* recipe = sdSeatGate::FindRecipe( seat.kind, seat.view->GetTypeName() );
	// the recipe says how the class's camera is drawn: a view whose thirdPerson flag does
	// not match (a third-person standard view) draws the tick's view
	if ( recipe == NULL || !recipe->presentable || recipe->worldCamera != seat.view->GetViewParms().thirdPerson ) {
		return;
	}
	const renderEntity_t* hull = seat.transport->GetRenderEntity();
	const renderView_t* view = viewPlayer->GetRenderView();
	if ( hull == NULL || view == NULL ) {
		return;
	}
	const idMat3 inverseHull = hull->axis.Transpose();
	s.proxySpawnId = gameLocal.GetSpawnId( seat.proxy );
	s.positionId = seat.positionId;
	s.viewMode = seat.viewMode;
	s.hullOrigin = hull->origin;
	s.hullAxis = hull->axis;
	s.camOffset = ( view->vieworg - hull->origin ) * inverseHull;
	s.camAxis = view->viewaxis * inverseHull;
	s.camOrigin = view->vieworg;
	s.camWorldAxis = view->viewaxis;
	s.thirdPerson = recipe->worldCamera;
	// stage 5, the damped first-person view: camera = viewAngles x eye (VehicleView.cpp,
	// CalcViewAxes), so the camera without the angles is what the tick's eye and clamps left
	s.lateLook = recipe->lateLook && !s.thirdPerson;
	s.tickAngles = viewPlayer->clientViewAngles;
	for ( int i = 0; i < 3; i++ ) {
		s.cmdAngles[ i ] = viewPlayer->usercmd.angles[ i ];
	}
	s.lookBase = s.tickAngles.ToMat3().Transpose() * s.camAxis;
	s.speed = const_cast< sdTransport* >( seat.transport )->GetPhysics()->GetLinearVelocity().Length();
	s.valid = true;
}

/*
================
sdPresentation::SamplesContinue

The continuity guards between the samples of ticks n-1 (a) and n (b): consecutive ticks,
the same seat and view, and a move no larger than max( 64 u, 2 x speed x 33 ms ) and 45
degrees (a teleport, a respawn, a reset).
================
*/
bool sdPresentation::SamplesContinue( const sample_t& a, const sample_t& b ) const {
	if ( !a.valid || !b.valid || b.framenum - a.framenum != 1 ) {
		return false;
	}
	if ( a.proxySpawnId != b.proxySpawnId || a.positionId != b.positionId || a.viewMode != b.viewMode ||
		 a.thirdPerson != b.thirdPerson ) {
		return false;
	}
	const float maxMove = Max( PRESENT_MIN_JUMP, 2.0f * Max( a.speed, b.speed ) * MS2SEC( USERCMD_MSEC ) );
	if ( ( b.hullOrigin - a.hullOrigin ).LengthSqr() > Square( maxMove ) ) {
		return false;
	}
	const float minCosHalf = idMath::Cos( DEG2RAD( PRESENT_MAX_TURN ) * 0.5f );
	if ( HalfAngleCos( a.hullAxis, b.hullAxis ) < minCosHalf ) {
		return false;
	}
	if ( b.thirdPerson ) {
		// a chase camera follows the hull with its own damping: its move in the world is
		// held to the hull's limits (a reset or a cut snaps rather than sweeping)
		if ( ( b.camOrigin - a.camOrigin ).LengthSqr() > Square( maxMove + PRESENT_MAX_CAMERA_MOVE ) ||
			 HalfAngleCos( a.camWorldAxis, b.camWorldAxis ) < minCosHalf ) {
			return false;
		}
	} else if ( ( b.camOffset - a.camOffset ).LengthSqr() > Square( PRESENT_MAX_CAMERA_MOVE ) ||
		 HalfAngleCos( a.camAxis, b.camAxis ) < minCosHalf ) {
		// the camera in the hull's frame: a recentre or reset snaps rather than sweeping
		return false;
	}
	return true;
}

/*
================
sdPresentation::PrepareDraw

Blends tick n-1's sample into tick n's at the frame's fraction: the hull is lerped and
slerped, and the camera is the blended hull times the blended camera-in-hull, so the
eye stays fixed in the drawn hull and nothing steps at a tick boundary. The draw shows
the tick's own view (blending false) unless the seat gate's draw-time re-check holds for
the sampled seat, the game is not paused, the view has no fresh entry pending and the
continuity guards pass. The field of view is never blended (rule 5).
================
*/
void sdPresentation::PrepareDraw( float fraction, const usercmd_t* cmd ) {
	blending = false;
	prepared = true;
	preparedDraws++;
	drawFrame = gameLocal.framenum;
	const sample_t& prev = samples[ ( gameLocal.framenum + 1 ) & 1 ];
	const sample_t& cur = samples[ gameLocal.framenum & 1 ];
	if ( !cur.valid || cur.framenum != gameLocal.framenum || !SamplesContinue( prev, cur ) ) {
		return;
	}
	if ( gameLocal.IsPaused() || !gameLocal.RecheckSeatPresentation() ) {
		return;
	}
	if ( cur.proxySpawnId != gameLocal.unlock.seatProxySpawnId || cur.positionId != gameLocal.unlock.seatPositionId ||
		 cur.viewMode != gameLocal.unlock.seatViewMode ) {
		return;
	}
	idPlayer* viewPlayer = gameLocal.GetLocalViewPlayer();
	seatInfo_t seat;
	// a fresh entry's angles are not clamped yet (first person). A chase view
	// (sdSmoothVehicleView) keeps freshEntry set for good, so the flag says nothing there; an
	// entry is caught by the missing previous sample, a seat change or camera cycle by the
	// seat identity checks
	if ( viewPlayer == NULL || !sdSeatGate::FindSeat( viewPlayer, seat ) || seat.view == NULL ||
		 ( !cur.thirdPerson && seat.view->IsFreshEntry() ) ) {
		return;
	}

	const float f = idMath::ClampFloat( 0.0f, 1.0f, fraction );
	idQuat hullQuat;
	hullQuat.Slerp( prev.hullAxis.ToQuat(), cur.hullAxis.ToQuat(), f );
	drawHullAxis = hullQuat.ToMat3();
	drawHullOrigin.Lerp( prev.hullOrigin, cur.hullOrigin, f );

	if ( cur.thirdPerson ) {
		// a chase view: its own tick outputs blended in the world (vehicle-responsiveness
		// stage 3); it is not rigid to the hull
		drawViewOrigin.Lerp( prev.camOrigin, cur.camOrigin, f );
		idQuat camQuat;
		camQuat.Slerp( prev.camWorldAxis.ToQuat(), cur.camWorldAxis.ToQuat(), f );
		drawViewAxis = camQuat.ToMat3();
	} else {
		idVec3 camOffset;
		camOffset.Lerp( prev.camOffset, cur.camOffset, f );
		drawViewOrigin = drawHullOrigin + camOffset * drawHullAxis;
		if ( cur.lateLook && prev.lateLook && g_unlock_vehicleLook.GetBool() ) {
			// stage 5 (late look): A = A_n - (1 - f) r_n + clampPure( A_n + mouse turn since
			// the tick ), r_n the part of the tick's angle change its input did not cause
			// (auto-centre, clamps), spread over the tick so it does not step at its end;
			// every angle difference is the shortest arc, and usercmd angles wrap in 16 bits
			idAngles turn( 0.0f, 0.0f, 0.0f );
			if ( cmd != NULL && LookAllowed( *seat.transport, *seat.view, seat.positionId, *cmd ) ) {
				for ( int i = 0; i < 2; i++ ) {
					turn[ i ] = idMath::AngleNormalize180( SHORT2ANGLE( static_cast< short >( cmd->angles[ i ] - cur.cmdAngles[ i ] ) ) );
				}
			}
			idAngles drawn = cur.tickAngles;
			for ( int i = 0; i < 2; i++ ) {
				const float input = idMath::AngleNormalize180( SHORT2ANGLE( static_cast< short >( cur.cmdAngles[ i ] - prev.cmdAngles[ i ] ) ) );
				const float residual = idMath::AngleNormalize180( idMath::AngleNormalize180( cur.tickAngles[ i ] - prev.tickAngles[ i ] ) - input );
				drawn[ i ] = idMath::AngleNormalize180( cur.tickAngles[ i ] - ( 1.0f - f ) * residual + turn[ i ] );
			}
			seat.view->ClampViewAnglesPure( drawn, cur.tickAngles, f * MS2SEC( USERCMD_MSEC ) );
			idQuat baseQuat;
			baseQuat.Slerp( prev.lookBase.ToQuat(), cur.lookBase.ToQuat(), f );
			drawViewAxis = drawn.ToMat3() * baseQuat.ToMat3() * drawHullAxis;
		} else {
			idQuat camQuat;
			camQuat.Slerp( prev.camAxis.ToQuat(), cur.camAxis.ToQuat(), f );
			drawViewAxis = camQuat.ToMat3() * drawHullAxis;
		}
	}
	drawProxySpawnId = cur.proxySpawnId;
	blending = true;
	blendedDraws++;
}

/*
================
sdPresentation::LookAllowed

stage 5: when a draw may show the mouse turn made since the tick: not while a seat that
needs the tophat lacks it, the transport teleports, or the seat's weapon is EMPed (the
tick ignores the turn then too; Transport.cpp UpdateViewAngles and ClampViewAngles)
================
*/
bool sdPresentation::LookAllowed( const sdTransport& transport, const sdVehicleView& view, int positionId, const usercmd_t& cmd ) const {
	if ( view.GetViewParms().tophatRequired && !cmd.buttons.btn.tophat ) {
		return false;
	}
	if ( transport.IsTeleporting() ) {
		return false;
	}
	const sdVehiclePosition* position = transport.GetPositionManager().PositionForId( positionId );
	if ( position != NULL && position->GetWeaponIndex() != -1 && const_cast< sdTransport& >( transport ).IsWeaponEMPed() ) {
		return false;
	}
	return true;
}

/*
================
sdPresentation::IsPrepared
================
*/
bool sdPresentation::IsPrepared( void ) const {
	return prepared && drawFrame == gameLocal.framenum;
}

/*
================
sdPresentation::ApplyView
================
*/
bool sdPresentation::ApplyView( renderView_t& view ) const {
	if ( !blending || !IsPrepared() ) {
		return false;
	}
	view.vieworg = drawViewOrigin;
	view.viewaxis = drawViewAxis;
	return true;
}

/*
================
sdPresentation::SubmitCopies

The hull at the presented pose, and what it carries by the carrier rule: a rider's or the
cockpit's pose this tick, taken relative to the hull's pose this tick, applied to the
presented hull (presented x ( hull(n)^-1 x child(n) )). Nothing is written to the
entities; idGameLocal restores the replaced defs after the draw.
================
*/
void sdPresentation::SubmitCopies( void ) const {
	if ( !blending || !IsPrepared() ) {
		return;
	}
	idEntity* proxy = gameLocal.EntityForSpawnId( drawProxySpawnId );
	sdTransport* transport = proxy != NULL ? proxy->Cast< sdTransport >() : NULL;
	if ( transport == NULL || transport->GetModelDefHandle() < 0 || transport->IsHidden() ) {
		return;
	}
	const renderEntity_t* hull = transport->GetRenderEntity();
	const idMat3 inverseHull = hull->axis.Transpose();

	renderEntity_t copy = *hull;
	copy.origin = drawHullOrigin;
	copy.axis = drawHullAxis;
	gameLocal.SubmitPresentationCopy( transport->GetModelDefHandle(), copy, transport, NULL );
	SubmitCarried( transport->clientEntities, hull->origin, inverseHull );

	// riders
	const sdTransportPositionManager& positions = transport->GetPositionManager();
	for ( int i = 0; i < positions.NumPositions(); i++ ) {
		const sdVehiclePosition* position = positions.PositionForId( i );
		idPlayer* rider = position != NULL ? position->GetPlayer() : NULL;
		if ( rider == NULL || rider->GetModelDefHandle() < 0 || rider->IsHidden() ) {
			continue;
		}
		renderEntity_t riderCopy = *rider->GetRenderEntity();
		riderCopy.origin = drawHullOrigin + ( riderCopy.origin - hull->origin ) * inverseHull * drawHullAxis;
		riderCopy.axis = riderCopy.axis * inverseHull * drawHullAxis;
		gameLocal.SubmitPresentationCopy( rider->GetModelDefHandle(), riderCopy, rider, NULL );
		SubmitCarried( rider->clientEntities, hull->origin, inverseHull );
	}

	// the cockpit
	// (not when it is bound to the transport: SubmitCarried copied it with the hull's models)
	sdClientAnimated* cockpit = gameLocal.playerView.GetCockpit().GetEntity();
	if ( cockpit != NULL && cockpit->bindNode.ListHead() != &transport->clientEntities &&
		 cockpit->GetRenderEntityHandle() >= 0 && !cockpit->IsHidden() ) {
		renderEntity_t cockpitCopy = *cockpit->GetRenderEntity();
		cockpitCopy.origin = drawHullOrigin + ( cockpitCopy.origin - hull->origin ) * inverseHull * drawHullAxis;
		cockpitCopy.axis = cockpitCopy.axis * inverseHull * drawHullAxis;
		gameLocal.SubmitPresentationCopy( cockpit->GetRenderEntityHandle(), cockpitCopy, NULL, cockpit );
		SubmitCarried( cockpit->clientEntities, hull->origin, inverseHull );
	}
}

/*
================
sdPresentation::SubmitCarried

The client models bound to something the hull carries (a rider's third-person weapon, a
model on the hull or the cockpit), recursively, by the same rigid carrier rule. Client
effects and lights have no render entity of their own here and keep the tick's pose, as
do GUI surfaces (sdGuiSurface), which are positioned through their renderable.
================
*/
void sdPresentation::SubmitCarried( idLinkList< rvClientEntity >& carried, const idVec3& hullOrigin, const idMat3& inverseHull ) const {
	for ( rvClientEntity* cent = carried.Next(); cent != NULL; cent = cent->bindNode.Next() ) {
		const renderEntity_t* re = cent->GetRenderEntity();
		const int handle = cent->GetRenderEntityHandle();
		if ( re != NULL && handle >= 0 && !cent->IsHidden() ) {
			renderEntity_t copy = *re;
			copy.origin = drawHullOrigin + ( re->origin - hullOrigin ) * inverseHull * drawHullAxis;
			copy.axis = re->axis * inverseHull * drawHullAxis;
			gameLocal.SubmitPresentationCopy( handle, copy, NULL, cent );
		}
		SubmitCarried( cent->clientEntities, hullOrigin, inverseHull );
	}
}
