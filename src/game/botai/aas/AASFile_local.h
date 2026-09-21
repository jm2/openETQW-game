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
	int							BoundsReachableAreaNum_r( int nodeNum, const idBounds &bounds, const int areaFlags, const int excludeTravelFlags ) const;
};

class idAASFileManagerLocal : public idAASFileManager {
public:
	virtual						~idAASFileManagerLocal() {}

	virtual idAASFile *			LoadAAS( const char *fileName, unsigned int mapFileCRC ) override;
	virtual void				FreeAAS( idAASFile *file ) override;
};

extern idAASFileManagerLocal	aasFileManagerLocal;

#endif /* !__AASFILE_LOCAL_H__ */
