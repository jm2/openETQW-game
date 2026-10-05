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

class sdPresentation {
public:
							sdPresentation( void ) { Clear(); }

	void					Clear( void );

	// on new ticks, after the tick's pose log (RunFrame; ClientPrediction on new frames)
	void					SampleTick( idPlayer* viewPlayer );

	// at draw time, for the draw about to be rendered: blends the last two samples at
	// fraction (0 on a base draw) when the seat gate's re-check holds and every
	// continuity guard passes; otherwise this draw shows the tick's own view
	void					PrepareDraw( float fraction );
	void					Invalidate( void ) { blending = false; }

	// RenderPlayerView: the presented view origin and axis for this draw, if any
	bool					ApplyView( renderView_t& view ) const;

	// idGameLocal::Draw: the hull and what it carries, at the presented pose, through
	// idGameLocal's presentation copies (restored after RenderPlayerView)
	void					SubmitCopies( void ) const;

	// the last PrepareDraw's outcome, for diagnostics (SEAT_PRESENT)
	bool					IsBlending( void ) const { return blending; }

private:
	struct sample_t {
		int					framenum;
		int					proxySpawnId;
		int					positionId;
		int					viewMode;
		idVec3				hullOrigin;		// the transport's render entity at the tick's end
		idMat3				hullAxis;
		idVec3				camOffset;		// the tick camera in the hull's frame
		idMat3				camAxis;
		float				speed;			// the transport's speed, for the jump guard
		bool				valid;
	};

	bool					SamplesContinue( const sample_t& a, const sample_t& b ) const;

	sample_t				samples[ 2 ];	// by framenum & 1

	bool					blending;		// this draw is presented from the samples
	int						drawFrame;		// the framenum PrepareDraw ran for
	int						drawProxySpawnId;
	idVec3					drawHullOrigin;
	idMat3					drawHullAxis;
	idVec3					drawViewOrigin;
	idMat3					drawViewAxis;
};

#endif // __GAME_PRESENTATION_H__
