// SPDX-License-Identifier: MIT
//

#ifndef __AASFILE_LOCAL_H__
#define __AASFILE_LOCAL_H__

class idAASFileLocal : public idAASFile {
public:
								idAASFileLocal();
	virtual 					~idAASFileLocal();

	bool						Load( const char *fileName, unsigned int mapFileCRC );

	virtual int					FindReachabilityByName( const char *name ) const override;

	virtual size_t				MemorySize() const override;
	virtual size_t				MemoryUsed() const override;
	virtual size_t				MaxRoutingCacheSize() const override;
	virtual size_t				AreaTravelTimeCacheSize() const override;

	virtual idVec3				EdgeCenter( int edgeNum ) const override;
	virtual idVec3				AreaCenter( int areaNum ) const override;
	virtual idBounds			EdgeBounds( int edgeNum ) const override;
	virtual idBounds			AreaBounds( int areaNum ) const override;

	virtual int					PointAreaNum( const idVec3 &origin ) const override;
	virtual int					PointReachableAreaNum( const idVec3 &origin, const idBounds &searchBounds, const int areaFlags, const int excludeTravelFlags ) const override;
	virtual int					BoundsReachableAreaNum( const idBounds &bounds, const int areaFlags, const int excludeTravelFlags ) const override;
	virtual bool				PushPointIntoArea( int areaNum, idVec3 &point ) const override;

	virtual bool				Trace( aasTrace_t &trace, const idVec3 &start, const idVec3 &end ) const override;
	virtual bool				TraceHeight( aasTraceHeight_t &trace, const idVec3 &start, const idVec3 &end ) const override;
	virtual bool				TraceFloor( aasTraceFloor_t &trace, const idVec3 &start, int startAreaNum, const idVec3 &end, int endAreaNum, int travelFlags ) const override;

private:
	bool						AreaMatches( int areaNum, int areaFlags, int excludeTravelFlags ) const;
	int							BoundsReachableAreaNum_r( int nodeNum, const idBounds &bounds, int areaFlags, int excludeTravelFlags, int depth ) const;
	void						BoundsAreas_r( int nodeNum, const idBounds &bounds, int areaFlags, int excludeTravelFlags, int *areaList, int &numAreas, int maxAreas, int depth ) const;
	bool						TraceSegment_r( aasTrace_t &trace, const idVec3 &start, const idVec3 &delta, int nodeNum, float startFraction, float endFraction, int enterPlaneNum, int depth ) const;
	void						SetTraceBlocked( aasTrace_t &trace, const idVec3 &start, const idVec3 &delta, float fraction, int planeNum, int blockingAreaNum ) const;

	bool						ValidateIndices( const char *fileName ) const;
	void						BuildEdgeAreas();

								// edge of an area polygon in the direction the area uses it ( clockwise seen from above )
	void						GetAreaEdge( int edgeNum, const idVec3 *&v0, const idVec3 *&v1 ) const;
								// negative inside the area polygon ( minus the distance to its boundary ), positive outside
	float						AreaPolygonDistance2D( int areaNum, float x, float y, idVec2 *closest = NULL ) const;
								// floor height of the area boundary within maxDistance of ( x, y )
	bool						AreaBoundaryHeight( int areaNum, float x, float y, float maxDistance, float &height ) const;

	bool						AreaTravelAllowed( int areaNum, int travelFlags ) const;
	bool						ReachabilityAllowed( int fromAreaNum, int toAreaNum, int travelFlags ) const;
	int							FloorTraceNextArea( int curAreaNum, int edgeNum, const idVec3 &crossPoint, float dirX, float dirY, int travelFlags ) const;

	bool						ColumnHeight( float x, float y, float &height ) const;
	bool						PushPointIntoColumn( int areaNum, float x, float y, float z, float &pushedZ ) const;

	// the two areas using each edge: [ edgeNum * 2 + 0 ] and [ edgeNum * 2 + 1 ].
	// 0 = no area, the second entry is -1 when more than two areas use the edge
	idList<int>					edgeAreas;
	float						maxVertexHeight;
};

class idAASFileManagerLocal : public idAASFileManager {
public:
	virtual						~idAASFileManagerLocal() {}

	virtual idAASFile *			LoadAAS( const char *fileName, unsigned int mapFileCRC ) override;
	virtual void				FreeAAS( idAASFile *file ) override;
};

extern idAASFileManagerLocal	aasFileManagerLocal;

#endif /* !__AASFILE_LOCAL_H__ */
