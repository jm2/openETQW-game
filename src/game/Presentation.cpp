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
#include "physics/Physics_RigidBodyMultiple.h"

// the continuity guard's limits (vehicle-responsiveness stage 3): a pose that moved
// further than this between two ticks is drawn unblended
static const float	PRESENT_MIN_JUMP = 64.0f;		// u
static const float	PRESENT_MAX_TURN = 45.0f;		// degrees
// the camera-in-hull guard: a camera that moved this far in the hull's frame between two
// ticks (a pivot recentring, a seat's camera reset) is drawn unblended
static const float	PRESENT_MAX_CAMERA_MOVE = 32.0f;	// u
static const float	ORBIT_EPSILON = 0.5f;			// u: a chase camera this far off the ray from its focus was moved by a clip or damping

// the angle between two rotations, in degrees
static float AngleBetween( const idMat3& a, const idMat3& b );

// the cosine of half the angle between two rotations, from their quaternions
static float HalfAngleCos( const idMat3& a, const idMat3& b ) {
	const idQuat qa = a.ToQuat();
	const idQuat qb = b.ToQuat();
	return idMath::Fabs( qa.x * qb.x + qa.y * qb.y + qa.z * qb.z + qa.w * qb.w );
}

static float AngleBetween( const idMat3& a, const idMat3& b ) {
	return RAD2DEG( 2.0f * idMath::ACos( Min( 1.0f, HalfAngleCos( a, b ) ) ) );
}

/*
================
sdPresentation::Clear
================
*/
void sdPresentation::Clear( void ) {
	drawLeadAlpha = -1.0f;
	drawJointsValid = false;
	drawLeadVector.Zero();
	fadeFrame = -1;
	leadFrame = -1;
	fadeStartMsec = 0.0f;
	fadeOffset.Zero();
	fadeRot.Set( 0.0f, 0.0f, 0.0f, 1.0f );
	for ( int i = 0; i < 3; i++ ) {
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
	sample_t& s = samples[ gameLocal.framenum % 3 ];
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
	// review of #38: only where the class's clamp is the base one and has no rate limit
	// or hull-relative final clamp (ClampFinalAxis), and never in a seat that needs the
	// tophat (the pilots: their mouse flies the vehicle)
	const positionViewMode_t& parms = seat.view->GetViewParms();
	s.lateLook = recipe->lateLook && seat.view->HasPureClamp() && !parms.tophatRequired
		&& !parms.clampYaw.flags.limitRate && !parms.clampPitch.flags.limitRate
		&& !parms.clampDampedYaw.flags.enabled && !parms.clampDampedPitch.flags.enabled;
	s.ownerAxis = const_cast< sdTransport* >( seat.transport )->GetPhysics()->GetAxis();
	// a chase view orbits its focus: CalculateViewPos puts the camera at focus - forward x
	// length unless the world clip, the lift over the hull or the distance damping moved
	// it, and only then is a turn about the focus what the next tick would show
	s.orbitValid = false;
	if ( s.thirdPerson && s.lateLook && seat.view->GetOrbitFocus( s.focus ) ) {
		const idVec3 toFocus = s.focus - s.camOrigin;
		const float along = toFocus * s.camWorldAxis[ 0 ];
		s.orbitValid = along > 0.0f && ( toFocus - s.camWorldAxis[ 0 ] * along ).LengthSqr() < Square( ORBIT_EPSILON );
	}
	// whether the tick took its command's turn (sdTransport::UpdateViewAngles and
	// ClampViewAngles, idPlayer::UpdateViewAngles): if not, the command's angle change is
	// not input the tick showed, and the draws must not show it either
	s.turnAllowed = TurnAllowed( *seat.transport, seat.positionId, *viewPlayer );
	s.lookActive = seat.view->LateLookAllowed( viewPlayer, viewPlayer->usercmd );
	s.inputTaken = s.turnAllowed && s.lookActive && !( parms.tophatRequired && !viewPlayer->usercmd.buttons.btn.tophat );
	s.tickAngles = viewPlayer->clientViewAngles;
	for ( int i = 0; i < 3; i++ ) {
		s.cmdAngles[ i ] = viewPlayer->usercmd.angles[ i ];
	}
	s.lookBase = s.tickAngles.ToMat3().Transpose() * s.camAxis;
	s.speed = const_cast< sdTransport* >( seat.transport )->GetPhysics()->GetLinearVelocity().Length();
	s.valid = true;
	SampleLead( s, Sample( gameLocal.framenum - 1 ), Sample( gameLocal.framenum - 2 ), *seat.transport );
	SampleJoints( s, *seat.transport );
}

/*
================
sdPresentation::SampleJoints

gunner views (vehicle-responsiveness 3.6): the transport's joints as this tick poses them.
With SMP off the renderer builds an animator's frame lazily, in the model callback of the
first draw after the tick (idEntity::UpdateRenderEntity); this builds the same frame (the
same time) now, as Present does when SMP is on, and copies it. Tick code: a draw-time
CreateFrame would add animator writes the renderer's culling would otherwise skip.
================
*/
void sdPresentation::SampleJoints( sample_t& s, const sdTransport& transport ) const {
	s.joints.SetNum( 0, false );
	idAnimator* animator = const_cast< sdTransport& >( transport ).GetAnimator();
	if ( animator == NULL ) {
		return;
	}
	animator->CreateFrame( gameLocal.time, false );
	const renderEntity_t* re = transport.GetRenderEntity();
	if ( re == NULL || re->joints == NULL || re->numJoints <= 0 ) {
		return;
	}
	s.joints.SetNum( re->numJoints, false );
	memcpy( s.joints.Begin(), re->joints, re->numJoints * sizeof( idJointMat ) );
}

/*
================
sdPresentation::BlendJoints

this draw's joints: each joint's model-space rotation slerped and its translation lerped
from tick n-1's to tick n's at f; a joint that turned more than 90 degrees in the tick (a
wheel at speed) keeps tick n's. Without matching samples the hull copy keeps the tick's
joints (drawJointsValid false).
================
*/
void sdPresentation::BlendJoints( const sample_t& prev, const sample_t& cur, float f ) {
	drawJointsValid = false;
	const int num = cur.joints.Num();
	if ( num <= 0 || prev.joints.Num() != num ) {
		return;
	}
	drawJoints.SetNum( num, false );
	for ( int i = 0; i < num; i++ ) {
		const idJointMat& a = prev.joints[ i ];
		const idJointMat& b = cur.joints[ i ];
		const idMat3 ra = a.ToMat3();
		const idMat3 rb = b.ToMat3();
		idJointMat& out = drawJoints[ i ];
		if ( HalfAngleCos( ra, rb ) < 0.70710678f ) {	// more than 90 degrees
			out = b;
			continue;
		}
		idQuat q;
		q.Slerp( ra.ToQuat(), rb.ToQuat(), f );
		idVec3 t;
		t.Lerp( a.ToVec3(), b.ToVec3(), f );
		out.SetRotation( q.ToMat3() );
		out.SetTranslation( t );
	}
	drawJointsValid = true;
}

/*
================
sdPresentation::SampleLead

M5 step 14b (docs/hull-lead.md 3.1): the one-tick predictions made on tick n, for every
presented seat whether or not its class has a lead (VEH_PRED measures them all):
  S: the samples, lead = delta(n) + k x ( delta(n) - delta(n-1) ), k 0 for the spiking
     tick and 3 more (the impact guard, 3.3), and the last tick's rotation again;
  P: the physics state's own step with no external force (they are zeroed after Evaluate):
     the centre of mass moves by ( v + g x dt ) x dt, gravity added back only off the
     ground, the axis turns by w x dt, and the origin follows from the centre of mass; only
     the delta reaches the draw, on top of the post-decay render pose. It reads the physics
     pose at tick time, the stated exception to vehicle-responsiveness 3.1 rule 2.
Reads only.
================
*/
void sdPresentation::SampleLead( sample_t& s, const sample_t& prev, const sample_t& prevPrev, const sdTransport& transport ) const {
	const sdDeclVehicleScript* script = transport.GetVehicleScript();
	idStr::Copynz( s.vehicleClass, script != NULL ? script->GetName() : "", sizeof( s.vehicleClass ) );
	s.lead = 0.0f;
	s.predictor = 'S';
	if ( !sdSeatGate::LeadForClass( s.vehicleClass, s.lead, s.predictor ) ) {
		s.lead = 0.0f;
	}

	s.leadS.Zero();
	s.rotS.Identity();
	s.spikeHold = 0;
	if ( SamplesContinue( prev, s ) ) {
		const idVec3 delta = s.hullOrigin - prev.hullOrigin;
		s.leadS = delta;
		s.rotS = prev.hullAxis.Transpose() * s.hullAxis;	// axis(n) = axis(n-1) x rot
		if ( SamplesContinue( prevPrev, prev ) ) {
			const idVec3 change = delta - ( prev.hullOrigin - prevPrev.hullOrigin );
			s.spikeHold = change.LengthSqr() > Square( g_vehicleLeadSpike.GetFloat() ) ? 4 : Max( prev.spikeHold - 1, 0 );
			if ( s.spikeHold == 0 ) {
				s.leadS += change;
			}
		}
	}

	s.leadP.Zero();
	s.rotP.Identity();
	s.hasPhysics = false;
	s.grounded = true;
	s.lastCollision = -1;
	// the physics step only where it can be drawn or is measured (a listed class, VEH_PRED)
	const sdTransport_RB* rb = const_cast< sdTransport& >( transport ).Cast< sdTransport_RB >();
	const sdPhysics_RigidBodyMultiple* phys = rb != NULL ? rb->GetRBPhysics() : NULL;
	if ( phys != NULL && ( s.lead > 0.0f || g_logVehiclePred.GetBool() ) ) {
		const float dt = MS2SEC( USERCMD_MSEC );
		s.grounded = phys->HasGroundContacts();
		s.lastCollision = phys->GetLastCollision().time;
		const idVec3& origin = phys->GetOrigin();
		const idMat3& axis = phys->GetAxis();
		const idVec3& com = phys->GetMainCenterOfMass();
		const idVec3 w = phys->GetAngularVelocity();	// copies: both getters return a static
		const float speed = w.Length();
		idMat3 turn = mat3_identity;
		if ( speed > idMath::FLT_EPSILON ) {
			turn = idRotation( vec3_origin, w / speed, -RAD2DEG( speed * dt ) ).ToMat3();
		}
		idVec3 velocity = phys->GetLinearVelocity();
		if ( !s.grounded ) {
			velocity += phys->GetGravity() * dt;	// the momentum Evaluate adds back for its step
		}
		const idMat3 nextAxis = axis * turn;
		const idVec3 nextOrigin = origin + com * axis + velocity * dt - com * nextAxis;
		s.leadP = nextOrigin - origin;
		s.rotP = turn;
		s.hasPhysics = true;
	}

	if ( g_logVehiclePred.GetBool() && prev.valid && SamplesContinue( prev, s ) ) {
		// how far each predictor's guess for this tick, made on the last, landed from it
		const float errS = ( prev.hullOrigin + prev.leadS - s.hullOrigin ).Length();
		const float errP = prev.hasPhysics ? ( prev.hullOrigin + prev.leadP - s.hullOrigin ).Length() : -1.0f;
		const bool hybridP = prev.hasPhysics && !prev.grounded;
		const float errH = hybridP ? errP : errS;
		const float rotS = AngleBetween( prev.hullAxis * prev.rotS, s.hullAxis );
		const float rotP = prev.hasPhysics ? AngleBetween( prev.hullAxis * prev.rotP, s.hullAxis ) : -1.0f;
		gameLocal.Printf( "VEH_PRED: tick=%d class=%s L=%.2f pred=%c k=%d ground=%d errS=%.3f errP=%.3f errH=%.3f rotS=%.3f rotP=%.3f move=%.3f coll=%d\n",
			s.framenum, s.vehicleClass, prev.lead, prev.predictor, prev.spikeHold > 0 ? 0 : 1, prev.grounded ? 1 : 0,
			errS, errP, errH, rotS, rotP, ( s.hullOrigin - prev.hullOrigin ).Length(), s.lastCollision );
	}
}

/*
================
sdPresentation::LeadOf

the one-tick lead tick s predicts with its class's predictor, held to g_vehicleLeadMaxLength
================
*/
bool sdPresentation::LeadOf( const sample_t& s, idVec3& lead, idMat3& rot ) const {
	const bool physics = s.hasPhysics && ( s.predictor == 'P' || ( s.predictor == 'H' && !s.grounded ) );
	lead = physics ? s.leadP : s.leadS;
	rot = physics ? s.rotP : s.rotS;
	const float maxLength = g_vehicleLeadMaxLength.GetFloat();
	if ( maxLength > 0.0f && lead.LengthSqr() > Square( maxLength ) ) {
		lead *= maxLength / lead.Length();
	}
	return true;
}

/*
================
sdPresentation::HullAt

the hull t ticks past tick n-1 (prev): t <= 1 between the samples, as stage 3 blends them;
past tick n, alpha = t - 1 of tick n's lead
================
*/
void sdPresentation::HullAt( const sample_t& prev, const sample_t& cur, float t, idVec3& origin, idMat3& axis ) const {
	if ( t <= 1.0f ) {
		idQuat q;
		q.Slerp( prev.hullAxis.ToQuat(), cur.hullAxis.ToQuat(), t );
		axis = q.ToMat3();
		origin.Lerp( prev.hullOrigin, cur.hullOrigin, t );
		return;
	}
	const float alpha = t - 1.0f;
	idVec3 lead;
	idMat3 rot;
	LeadOf( cur, lead, rot );
	idQuat q;
	q.Slerp( mat3_identity.ToQuat(), rot.ToQuat(), alpha );
	origin = cur.hullOrigin + lead * alpha;
	axis = cur.hullAxis * q.ToMat3();
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
	drawLeadAlpha = -1.0f;
	drawJointsValid = false;
	const sample_t& prev = Sample( gameLocal.framenum - 1 );
	const sample_t& cur = Sample( gameLocal.framenum );
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
	// step 14b: the hull t = f + L ticks past tick n-1; a chase view has no lead (its camera
	// is damped behind the hull; docs/hull-lead.md 3.2)
	const float L = cur.thirdPerson ? 0.0f : cur.lead;
	const float t = f + L;
	HullAt( prev, cur, t, drawHullOrigin, drawHullAxis );
	if ( t > 1.0f ) {
		drawLeadAlpha = t - 1.0f;
		idMat3 unusedRot;
		LeadOf( cur, drawLeadVector, unusedRot );
	}
	ApplyLeadCorrection( prev, cur, L, f, t );

	// stage 5 (late look): the mouse turn made since the tick, every angle difference the
	// shortest arc (usercmd angles wrap in 16 bits)
	const bool lateLook = cur.lateLook && prev.lateLook && g_unlock_vehicleLook.GetBool();
	idAngles turn( 0.0f, 0.0f, 0.0f );
	if ( lateLook && cmd != NULL && cur.turnAllowed && cur.lookActive && seat.view->LateLookAllowed( viewPlayer, *cmd ) &&
		 !( seat.view->GetViewParms().tophatRequired && !cmd->buttons.btn.tophat ) ) {
		for ( int i = 0; i < 2; i++ ) {
			turn[ i ] = idMath::AngleNormalize180( SHORT2ANGLE( static_cast< short >( cmd->angles[ i ] - cur.cmdAngles[ i ] ) ) );
		}
	}

	if ( cur.thirdPerson ) {
		// a chase view: its own tick outputs blended in the world (vehicle-responsiveness
		// stage 3); it is not rigid to the hull
		drawViewOrigin.Lerp( prev.camOrigin, cur.camOrigin, f );
		idQuat camQuat;
		camQuat.Slerp( prev.camWorldAxis.ToQuat(), cur.camWorldAxis.ToQuat(), f );
		drawViewAxis = camQuat.ToMat3();
		if ( lateLook && cur.orbitValid && cur.lookActive ) {
			// stage 5 for a chase view: the blend shows tick n's own turn spread over the
			// tick, so the draw adds what of it is still to come, (1 - f) x the input the tick
			// took, and the clamped turn since the tick; as an orbit about the blended focus,
			// as CalculateViewPos places the camera while nothing shortened its boom
			idAngles base = cur.tickAngles;
			seat.view->ClampViewAnglesPure( base, cur.tickAngles, cur.ownerAxis, 0.0f );
			idAngles turned = cur.tickAngles + turn;
			seat.view->ClampViewAnglesPure( turned, cur.tickAngles, cur.ownerAxis, f * MS2SEC( USERCMD_MSEC ) );
			idAngles extra( 0.0f, 0.0f, 0.0f );
			for ( int i = 0; i < 2; i++ ) {
				const float change = idMath::AngleNormalize180( cur.tickAngles[ i ] - prev.tickAngles[ i ] );
				float input = cur.inputTaken ? idMath::AngleNormalize180( SHORT2ANGLE( static_cast< short >( cur.cmdAngles[ i ] - prev.cmdAngles[ i ] ) ) ) : 0.0f;
				// what the tick's clamps let through of it
				input = input * change > 0.0f ? ( idMath::Fabs( input ) < idMath::Fabs( change ) ? input : change ) : 0.0f;
				// the part of tick n's turn still to come only if the last interval drew its
				// own late turn (an orbit, the look active, the turn allowed): else this draw
				// would step by it as the late look comes back
				const bool lastDrewTurn = prev.orbitValid && prev.lookActive && prev.turnAllowed;
				extra[ i ] = ( lastDrewTurn ? ( 1.0f - f ) * input : 0.0f ) + idMath::AngleNormalize180( turned[ i ] - base[ i ] );
			}
			OrbitView( extra, cur, prev, f, *seat.view );
		}
	} else {
		// the camera in the hull at the frame's own fraction: the hull leads (step 14b) and the
		// camera rides it, but the damped view's own motion in the hull is blended as before,
		// since blending it at the hull's time would step it by L x its per-tick change at
		// each tick (review of #42)
		const float cf = f;
		idVec3 camOffset;
		camOffset.Lerp( prev.camOffset, cur.camOffset, cf );
		drawViewOrigin = drawHullOrigin + camOffset * drawHullAxis;
		if ( lateLook ) {
			// stage 5 (late look): A = A_n - (1 - f) r_n + clampPure( A_n + mouse turn since
			// the tick ), r_n the part of the tick's angle change its input did not cause
			// (auto-centre, clamps), spread over the tick so it does not step at its end
			idAngles drawn = cur.tickAngles;
			for ( int i = 0; i < 2; i++ ) {
				const float input = cur.inputTaken ? idMath::AngleNormalize180( SHORT2ANGLE( static_cast< short >( cur.cmdAngles[ i ] - prev.cmdAngles[ i ] ) ) ) : 0.0f;
				const float residual = idMath::AngleNormalize180( idMath::AngleNormalize180( cur.tickAngles[ i ] - prev.tickAngles[ i ] ) - input );
				drawn[ i ] = idMath::AngleNormalize180( cur.tickAngles[ i ] - ( 1.0f - f ) * residual + turn[ i ] );
			}
			seat.view->ClampViewAnglesPure( drawn, cur.tickAngles, cur.ownerAxis, f * MS2SEC( USERCMD_MSEC ) );
			idQuat baseQuat;
			baseQuat.Slerp( prev.lookBase.ToQuat(), cur.lookBase.ToQuat(), cf );
			drawViewAxis = drawn.ToMat3() * baseQuat.ToMat3() * drawHullAxis;
		} else {
			idQuat camQuat;
			camQuat.Slerp( prev.camAxis.ToQuat(), cur.camAxis.ToQuat(), cf );
			drawViewAxis = camQuat.ToMat3() * drawHullAxis;
		}
	}
	// the joints at the frame's own fraction, as the in-hull camera (they are in the hull's frame)
	BlendJoints( prev, cur, f );
	drawProxySpawnId = cur.proxySpawnId;
	blending = true;
	blendedDraws++;
}

/*
================
sdPresentation::ApplyLeadCorrection

step 14b (docs/hull-lead.md 3.2): at a new tick's first draw, the pose the last tick's lead
would draw now (with what is left of its own fade) against the pose this tick draws; the
difference is added back and faded out over g_vehicleLeadFadeMsec, so the hull does not
step at the tick. A difference over the jump guard snaps instead. Writes only this
presentation's members and the drawn hull.
================
*/
void sdPresentation::ApplyLeadCorrection( const sample_t& prev, const sample_t& cur, float L, float f, float t ) {
	const float fadeMsec = static_cast< float >( g_vehicleLeadFadeMsec.GetInteger() );
	const float nowMsec = static_cast< float >( gameLocal.time ) + f * USERCMD_MSEC;
	if ( L <= 0.0f || fadeMsec <= 0.0f ) {
		fadeFrame = -1;
		fadeOffset.Zero();
		fadeRot.Set( 0.0f, 0.0f, 0.0f, 1.0f );
		return;
	}
	if ( fadeFrame != cur.framenum ) {
		const float kept = idMath::ClampFloat( 0.0f, 1.0f, 1.0f - ( nowMsec - fadeStartMsec ) / fadeMsec );
		idQuat keptRot;
		keptRot.Slerp( idQuat( 0.0f, 0.0f, 0.0f, 1.0f ), fadeRot, kept );
		const idVec3 keptOffset = fadeOffset * kept;
		fadeOffset.Zero();
		fadeRot.Set( 0.0f, 0.0f, 0.0f, 1.0f );
		const sample_t& prevPrev = Sample( cur.framenum - 2 );
		if ( leadFrame == prev.framenum && SamplesContinue( prevPrev, prev ) ) {
			// the last tick's draw for this moment: t ticks past n-1 is t + 1 past n-2
			idVec3 oldOrigin;
			idMat3 oldAxis;
			HullAt( prevPrev, prev, t + 1.0f, oldOrigin, oldAxis );
			oldOrigin += keptOffset;
			oldAxis = oldAxis * keptRot.ToMat3();
			const idVec3 correction = oldOrigin - drawHullOrigin;
			if ( correction.LengthSqr() <= Square( PRESENT_MIN_JUMP ) ) {
				fadeOffset = correction;
				fadeRot = ( drawHullAxis.Transpose() * oldAxis ).ToQuat();
			}
		}
		fadeFrame = cur.framenum;
		fadeStartMsec = nowMsec;
	}
	leadFrame = cur.framenum;
	const float w = idMath::ClampFloat( 0.0f, 1.0f, 1.0f - ( nowMsec - fadeStartMsec ) / fadeMsec );
	if ( w > 0.0f ) {
		idQuat rot;
		rot.Slerp( idQuat( 0.0f, 0.0f, 0.0f, 1.0f ), fadeRot, w );
		drawHullOrigin += fadeOffset * w;
		drawHullAxis = drawHullAxis * rot.ToMat3();
	}
}

/*
================
sdPresentation::DrawLead
================
*/
bool sdPresentation::DrawLead( float& alpha, idVec3& lead ) const {
	if ( !blending || !IsPrepared() || drawLeadAlpha < 0.0f ) {
		return false;
	}
	alpha = drawLeadAlpha;
	lead = drawLeadVector;
	return true;
}

/*
================
sdPresentation::OrbitView

stage 5, a chase view: turns the drawn camera by extra (pitch and yaw, as its view angles
turn it) about the blended focus, then holds it to the class's view constraints in the
drawn hull's frame (ClampToViewConstraints), never further out than the blended camera
already is
================
*/
void sdPresentation::OrbitView( const idAngles& extra, const sample_t& cur, const sample_t& prev, float f, const sdVehicleView& view ) {
	idVec3 focus;
	focus.Lerp( prev.focus, cur.focus, f );
	idAngles forward = drawViewAxis[ 0 ].ToAngles();
	forward.roll = 0.0f;
	idAngles turned = forward;
	turned.pitch += extra.pitch;
	turned.yaw += extra.yaw;
	turned.Normalize180();

	angleClamp_t pitchClamp, yawClamp;
	view.GetOrbitConstraints( pitchClamp, yawClamp );
	const idMat3 hullT = drawHullAxis.Transpose();
	idAngles before = ( forward.ToMat3() * hullT ).ToAngles();
	idAngles after = ( turned.ToMat3() * hullT ).ToAngles();
	bool clamped = false;
	const angleClamp_t* clamps[ 2 ] = { &pitchClamp, &yawClamp };
	for ( int i = 0; i < 2; i++ ) {
		if ( !clamps[ i ]->flags.enabled ) {
			continue;
		}
		const float mid = ( clamps[ i ]->extents[ 0 ] + clamps[ i ]->extents[ 1 ] ) * 0.5f;
		const float range = ( clamps[ i ]->extents[ 1 ] - clamps[ i ]->extents[ 0 ] ) * 0.5f;
		const float was = idMath::AngleNormalize180( before[ i ] - mid );
		const float now = idMath::AngleNormalize180( after[ i ] - mid );
		const float limit = Max( range, idMath::Fabs( was ) );
		if ( idMath::Fabs( now ) > limit ) {
			after[ i ] = mid + ( now > 0.0f ? limit : -limit );
			clamped = true;
		}
	}
	idMat3 axis = turned.ToMat3();
	if ( clamped ) {
		idAngles world = ( after.ToMat3() * drawHullAxis )[ 0 ].ToAngles();
		world.roll = 0.0f;
		axis = world.ToMat3();
	}
	drawViewOrigin = focus + ( drawViewOrigin - focus ) * drawViewAxis.Transpose() * axis;
	drawViewAxis = axis;
}

/*
================
sdPresentation::TurnAllowed

stage 5: whether the tick lets a mouse turn change the view angles, apart from the
tophat: not while the player's turning is inhibited (a camera, turningFrozen, a
teleport), the transport teleports or the seat's weapon is EMPed (idPlayer::
UpdateViewAngles, sdTransport::UpdateViewAngles and ClampViewAngles). Latched at the
tick: these change only on ticks
================
*/
bool sdPresentation::TurnAllowed( const sdTransport& transport, int positionId, idPlayer& player ) const {
	if ( player.InhibitTurning() ) {
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
	if ( drawJointsValid && drawJoints.Num() == hull->numJoints ) {
		// the blended joints; no callback, or the renderer would pose the tick's frame again
		copy.joints = const_cast< idJointMat* >( drawJoints.Begin() );
		copy.callback = NULL;
	}
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
