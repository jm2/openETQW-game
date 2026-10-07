// SPDX-License-Identifier: MIT
//
// OpenETQW (M5 step 14, stage 3 of the engine's docs/vehicle-responsiveness.md): the
// presentation-only view for allowed vehicle seats. On new ticks it samples the local
// view player's transport (its render entity, after prediction-error decay) and the tick
// camera, as the camera in the hull's frame; at draw time it blends the last two samples
// at the frame's fraction and draws the camera, the hull and what the hull carries (the
// riders, the cockpit) from that. It writes only its own members, the drawn view
// (idPlayerView::currentView) and renderer defs through copies that are restored after
// the draw (vehicle-responsiveness 3.1, rules 3 and 7); it reads tick state and changes
// none. Seats get it only through the seat gate (vehicles/SeatGate.h): a recipe that is
// presentable and a key listed in g_unlock_vehicleSeats.

#ifndef __GAME_PRESENTATION_H__
#define __GAME_PRESENTATION_H__

class idPlayer;
class sdTransport;
class sdVehicleView;

class sdPresentation {
public:
							sdPresentation( void ) { Clear(); }

	void					Clear( void );

	// on new ticks, after the tick's pose log (RunFrame; ClientPrediction on new frames)
	void					SampleTick( idPlayer* viewPlayer );

	// at draw time, for the draw about to be rendered: blends the last two samples at
	// fraction (0 on a base draw) when the seat gate's re-check holds and every
	// continuity guard passes; otherwise this draw shows the tick's own view
	void					PrepareDraw( float fraction, const usercmd_t* cmd = NULL );
	// after the draw: the next draw must be prepared again (PrepareDraw)
	void					Invalidate( void ) { blending = false; prepared = false; }
	bool					IsPrepared( void ) const;

	// RenderPlayerView: the presented view origin and axis for this draw, if any
	bool					ApplyView( renderView_t& view ) const;

	// idGameLocal::Draw: the hull and what it carries, at the presented pose, through
	// idGameLocal's presentation copies (restored after RenderPlayerView)
	void					SubmitCopies( void ) const;

	// the draws blended between the previous tick and this one (SEAT_PRESENT, g_simHash)
	int						BlendedDrawsBeforeTick( void ) const { return blendedBeforeTick; }
	int						PreparedDrawsBeforeTick( void ) const { return preparedBeforeTick; }

private:
	struct sample_t {
		int					framenum;
		int					proxySpawnId;
		int					positionId;
		int					viewMode;
		idVec3				hullOrigin;		// the transport's render entity at the tick's end
		idMat3				hullAxis;
		idVec3				camOffset;		// the tick camera in the hull's frame (first person)
		idMat3				camAxis;
		idVec3				camOrigin;		// the tick camera in the world (a chase view)
		idMat3				camWorldAxis;
		bool				thirdPerson;	// a chase view: blended in the world, not in the hull
		bool				lateLook;		// stage 5: the view angles below are shown with the mouse turn since the tick
		bool				turnAllowed;	// the tick let a mouse turn change the angles (the tophat aside)
		bool				lookActive;		// the class let the mouse turn its view at the tick (smooth_locked: the tophat)
		bool				inputTaken;		// and took its own command's turn
		idMat3				ownerAxis;		// the transport's physics axis (the smooth classes' clamps)
		idVec3				focus;			// a chase view's orbit focus, in the world
		bool				orbitValid;		// its camera lies on the ray back from the focus
		idAngles			tickAngles;		// the tick's view angles (clientViewAngles) and its command's
		short				cmdAngles[ 3 ];
		idMat3				lookBase;		// the camera without the view angles, in the hull's frame:
											// camera = tickAngles.ToMat3() x lookBase x hull
		float				speed;			// the transport's speed, for the jump guard
		bool				valid;
	};

	bool					SamplesContinue( const sample_t& a, const sample_t& b ) const;
	void					OrbitView( const idAngles& extra, const sample_t& cur, const sample_t& prev, float f, const sdVehicleView& view );
	bool					TurnAllowed( const sdTransport& transport, int positionId, idPlayer& player ) const;
	void					SubmitCarried( idLinkList< rvClientEntity >& carried, const idVec3& hullOrigin, const idMat3& inverseHull ) const;

	sample_t				samples[ 2 ];	// by framenum & 1

	bool					blending;		// this draw is presented from the samples
	bool					prepared;		// PrepareDraw ran for this draw
	int						blendedDraws;	// draws blended since the last tick sample
	int						blendedBeforeTick;	// the same, as the last tick sample found it
	int						preparedDraws;	// draws PrepareDraw ran for since the last tick sample
	int						preparedBeforeTick;
	int						drawFrame;		// the framenum PrepareDraw ran for
	int						drawProxySpawnId;
	idVec3					drawHullOrigin;
	idMat3					drawHullAxis;
	idVec3					drawViewOrigin;
	idMat3					drawViewAxis;
};

#endif // __GAME_PRESENTATION_H__
