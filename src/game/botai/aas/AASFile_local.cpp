// SPDX-License-Identifier: MIT
//

#include "AASFile_local.h"

idAASFileManagerLocal aasFileManagerLocal;

/*
===============================================================================

	idAASSettings

===============================================================================
*/

idAASSettings::idAASSettings() {
	type = AAS_PLAYER;
	fileExtensionAAS = "aas_player";
	boundingBox.Clear();
	primitiveModeBrush = AAS_PRIMITIVE_MODE_DEFAULT;
	primitiveModePatch = AAS_PRIMITIVE_MODE_EXPLICIT;
	primitiveModeModel = AAS_PRIMITIVE_MODE_NEVER;
	primitiveModeTerrain = AAS_PRIMITIVE_MODE_ALWAYS;
	gravity = idVec3( 0, 0, -1066.0f );
	gravityDir = idVec3( 0, 0, -1.0f );
	invGravityDir = idVec3( 0, 0, 1.0f );
	gravityValue = 1066.0f;
	maxStepHeight = 16.0f;
	maxBarrierHeight = 64.0f;
	maxWaterJumpHeight = 0.0f;
	maxFallHeight = 900.0f;
	minFloorCos = 0.7f;
	minHighCeiling = 80.0f;
	groundSpeed = 256.0f;
	waterSpeed = 150.0f;
	ladderSpeed = 50.0f;
	wallCornerEdgeRadius = 0.0f;
	ledgeCornerEdgeRadius = 0.0f;
	obstaclePVSRadius = 1024.0f;
	tt_barrierJump = 100;
	tt_waterJump = 100;
	tt_startWalkOffLedge = 100;
	tt_startLadderClimb = 200;
}

bool idAASSettings::ReadFromFileBinary( idFile *fp ) {
	fp->ReadInt( type );
	int extLen = 0;
	fp->ReadInt( extLen );
	if ( extLen > 0 && extLen < 1024 ) {
		char *buf = (char *)_alloca( extLen + 1 );
		fp->Read( buf, extLen );
		buf[ extLen ] = '\0';
		fileExtensionAAS = buf;
	} else {
		fileExtensionAAS = "";
	}

	fp->ReadVec3( boundingBox[0] );
	fp->ReadVec3( boundingBox[1] );

	fp->ReadInt( primitiveModeBrush );
	fp->ReadInt( primitiveModePatch );
	fp->ReadInt( primitiveModeModel );
	fp->ReadInt( primitiveModeTerrain );

	fp->ReadVec3( gravity );
	fp->ReadVec3( gravityDir );
	fp->ReadVec3( invGravityDir );
	gravityValue = gravity.Length();

	fp->ReadFloat( maxStepHeight );
	fp->ReadFloat( maxBarrierHeight );
	fp->ReadFloat( maxWaterJumpHeight );
	fp->ReadFloat( maxFallHeight );
	fp->ReadFloat( minFloorCos );
	fp->ReadFloat( minHighCeiling );
	fp->ReadFloat( groundSpeed );
	fp->ReadFloat( waterSpeed );
	fp->ReadFloat( ladderSpeed );

	fp->ReadFloat( wallCornerEdgeRadius );
	fp->ReadFloat( ledgeCornerEdgeRadius );
	fp->ReadFloat( obstaclePVSRadius );

	fp->ReadInt( tt_barrierJump );
	fp->ReadInt( tt_waterJump );
	fp->ReadInt( tt_startWalkOffLedge );
	fp->ReadInt( tt_startLadderClimb );

	return true;
}

bool idAASSettings::ReadFromDict( const char *name, const idDict *dict ) { return false; }
bool idAASSettings::WriteToFile( idFile *fp ) const { return false; }
bool idAASSettings::ReadFromFile( idLexer &src ) { return false; }
bool idAASSettings::WriteToFile( const char *fileName ) const { return false; }
bool idAASSettings::ReadFromFile( const char *fileName ) { return false; }
bool idAASSettings::WriteToFileBinary( idFile *fp ) const { return false; }
bool idAASSettings::ValidForBounds( const idBounds &bounds ) const { return bounds == boundingBox; }
bool idAASSettings::ValidEntity( const idMapFile *mapFile, const char *classname ) const { return true; }
bool idAASSettings::ParseBool( idLexer &src, bool &b ) { return false; }
bool idAASSettings::ParseInt( idLexer &src, int &i ) { return false; }
bool idAASSettings::ParseFloat( idLexer &src, float &f ) { return false; }
bool idAASSettings::ParseVector( idLexer &src, idVec3 &vec ) { return false; }
bool idAASSettings::ParseBounds( idLexer &src, idBounds &bounds ) { return false; }
bool idAASSettings::ParseAASTypeKey( const idDict *dict, const char *keyName, int &aasType ) { return false; }
bool idAASSettings::ParseBoundsKey( const idDict *dict, const char *keyName, idBounds &bounds ) { return false; }
bool idAASSettings::ParsePrimitiveModeKey( const idDict *dict, const char *keyName, int &primitiveMode ) { return false; }

/*
===============================================================================

	On-disk 32-bit representations

===============================================================================
*/

#pragma pack(push, 1)
struct diskReachability_t {
	unsigned short				travelFlags;
	unsigned short				travelTime;
	unsigned short				fromAreaNum;
	unsigned short				toAreaNum;
	short						start[3];
	short						end[3];
	unsigned int				areaTTOfsAndNumber;
	unsigned int				diskPad[2];
};

struct diskArea_t {
	unsigned short				travelFlags;
	unsigned short				flags;
	int							numEdges;
	int							firstEdge;
	short						cluster;
	unsigned short				clusterAreaNum;
	unsigned int				obstaclePVSOffset;
	unsigned int				diskPad[2];
};
#pragma pack(pop)

/*
===============================================================================

	idAASFileLocal

===============================================================================
*/

idAASFileLocal::idAASFileLocal() {
	crc = 0;
}

idAASFileLocal::~idAASFileLocal() {
}

bool idAASFileLocal::Load( const char *fileName, unsigned int mapFileCRC ) {
	idFile *fp = fileSystem->OpenFileRead( fileName );
	if ( !fp ) {
		return false;
	}

	int idLen = 0;
	fp->ReadInt( idLen );
	if ( idLen <= 0 || idLen > 64 ) {
		fileSystem->CloseFile( fp );
		return false;
	}
	char idBuf[65];
	fp->Read( idBuf, idLen );
	idBuf[ idLen ] = '\0';
	if ( idStr::Cmp( idBuf, AAS_FILE_ID_BINARY ) != 0 ) {
		gameLocal.Warning( "idAASFileLocal::Load: %s has invalid header '%s'", fileName, idBuf );
		fileSystem->CloseFile( fp );
		return false;
	}

	int verLen = 0;
	fp->ReadInt( verLen );
	if ( verLen <= 0 || verLen > 64 ) {
		fileSystem->CloseFile( fp );
		return false;
	}
	char verBuf[65];
	fp->Read( verBuf, verLen );
	verBuf[ verLen ] = '\0';
	if ( idStr::Cmp( verBuf, AAS_FILE_VERSION ) != 0 ) {
		gameLocal.Warning( "idAASFileLocal::Load: %s has invalid version '%s'", fileName, verBuf );
		fileSystem->CloseFile( fp );
		return false;
	}

	fp->ReadUnsignedInt( crc );

	if ( !settings.ReadFromFileBinary( fp ) ) {
		gameLocal.Warning( "idAASFileLocal::Load: %s failed to read settings", fileName );
		fileSystem->CloseFile( fp );
		return false;
	}

	// 1. Planes (16 bytes each)
	int numPlanes = 0;
	fp->ReadInt( numPlanes );
	if ( numPlanes < 0 ) { fileSystem->CloseFile( fp ); return false; }
	planes.SetNum( numPlanes );
	if ( numPlanes > 0 ) {
		fp->Read( planes.Begin(), numPlanes * sizeof(aasPlane_t) );
	}

	// 2. Vertices (12 bytes each)
	int numVerts = 0;
	fp->ReadInt( numVerts );
	if ( numVerts < 0 ) { fileSystem->CloseFile( fp ); return false; }
	vertices.SetNum( numVerts );
	if ( numVerts > 0 ) {
		fp->Read( vertices.Begin(), numVerts * sizeof(aasVertex_t) );
	}

	// 3. Edges (12 bytes each)
	int numEdges = 0;
	fp->ReadInt( numEdges );
	if ( numEdges < 0 ) { fileSystem->CloseFile( fp ); return false; }
	edges.SetNum( numEdges );
	if ( numEdges > 0 ) {
		fp->Read( edges.Begin(), numEdges * sizeof(aasEdge_t) );
	}

	// 4. EdgeIndex (4 bytes each)
	int numEdgeIndex = 0;
	fp->ReadInt( numEdgeIndex );
	if ( numEdgeIndex < 0 ) { fileSystem->CloseFile( fp ); return false; }
	edgeIndex.SetNum( numEdgeIndex );
	if ( numEdgeIndex > 0 ) {
		fp->Read( edgeIndex.Begin(), numEdgeIndex * sizeof(aasIndex_t) );
	}

	// 5. Reachabilities (32 bytes on disk, unpacked into 64-bit struct)
	int numReach = 0;
	fp->ReadInt( numReach );
	if ( numReach < 0 ) { fileSystem->CloseFile( fp ); return false; }
	reachabilities.SetNum( numReach );
	if ( numReach > 0 ) {
		idList<diskReachability_t> diskReach;
		diskReach.SetNum( numReach );
		fp->Read( diskReach.Begin(), numReach * sizeof(diskReachability_t) );
		for ( int i = 0; i < numReach; i++ ) {
			aasReachability_t &r = reachabilities[i];
			const diskReachability_t &dr = diskReach[i];
			r.travelFlags = dr.travelFlags;
			r.travelTime = dr.travelTime;
			r.fromAreaNum = dr.fromAreaNum;
			r.toAreaNum = dr.toAreaNum;
			r.start[0] = dr.start[0];
			r.start[1] = dr.start[1];
			r.start[2] = dr.start[2];
			r.end[0] = dr.end[0];
			r.end[1] = dr.end[1];
			r.end[2] = dr.end[2];
			r.areaTTOfsAndNumber = dr.areaTTOfsAndNumber;
			r.next = NULL;
			r.rev_next = NULL;
		}
	}

	// 6. Areas (28 bytes on disk, unpacked into 64-bit struct)
	int numAreas = 0;
	fp->ReadInt( numAreas );
	if ( numAreas < 0 ) { fileSystem->CloseFile( fp ); return false; }
	areas.SetNum( numAreas );
	if ( numAreas > 0 ) {
		idList<diskArea_t> diskAreas;
		diskAreas.SetNum( numAreas );
		fp->Read( diskAreas.Begin(), numAreas * sizeof(diskArea_t) );
		for ( int i = 0; i < numAreas; i++ ) {
			aasArea_t &a = areas[i];
			const diskArea_t &da = diskAreas[i];
			a.travelFlags = da.travelFlags;
			a.flags = da.flags;
			a.numEdges = da.numEdges;
			a.firstEdge = da.firstEdge;
			a.cluster = da.cluster;
			a.clusterAreaNum = da.clusterAreaNum;
			a.obstaclePVSOffset = da.obstaclePVSOffset;
			a.reach = NULL;
			a.rev_reach = NULL;
		}
	}

	// Link reachabilities into areas
	for ( int i = 0; i < reachabilities.Num(); i++ ) {
		aasReachability_t &r = reachabilities[ i ];
		if ( r.fromAreaNum < (unsigned short)areas.Num() ) {
			r.next = areas[ r.fromAreaNum ].reach;
			areas[ r.fromAreaNum ].reach = &r;
		}
		if ( r.toAreaNum < (unsigned short)areas.Num() ) {
			r.rev_next = areas[ r.toAreaNum ].rev_reach;
			areas[ r.toAreaNum ].rev_reach = &r;
		}
	}

	// 7. Nodes (12 bytes each)
	int numNodes = 0;
	fp->ReadInt( numNodes );
	if ( numNodes < 0 ) { fileSystem->CloseFile( fp ); return false; }
	nodes.SetNum( numNodes );
	if ( numNodes > 0 ) {
		fp->Read( nodes.Begin(), numNodes * sizeof(aasNode_t) );
	}

	// 8. Portals (12 bytes each)
	int numPortals = 0;
	fp->ReadInt( numPortals );
	if ( numPortals < 0 ) { fileSystem->CloseFile( fp ); return false; }
	portals.SetNum( numPortals );
	if ( numPortals > 0 ) {
		fp->Read( portals.Begin(), numPortals * sizeof(aasPortal_t) );
	}

	// 9. PortalIndex (4 bytes each)
	int numPortalIndex = 0;
	fp->ReadInt( numPortalIndex );
	if ( numPortalIndex < 0 ) { fileSystem->CloseFile( fp ); return false; }
	portalIndex.SetNum( numPortalIndex );
	if ( numPortalIndex > 0 ) {
		fp->Read( portalIndex.Begin(), numPortalIndex * sizeof(aasIndex_t) );
	}

	// 10. Clusters (16 bytes each)
	int numClusters = 0;
	fp->ReadInt( numClusters );
	if ( numClusters < 0 ) { fileSystem->CloseFile( fp ); return false; }
	clusters.SetNum( numClusters );
	if ( numClusters > 0 ) {
		fp->Read( clusters.Begin(), numClusters * sizeof(aasCluster_t) );
	}

	// 11. ObstaclePVS (1 byte each)
	int numObstaclePVS = 0;
	fp->ReadInt( numObstaclePVS );
	if ( numObstaclePVS < 0 ) { fileSystem->CloseFile( fp ); return false; }
	obstaclePVS.SetNum( numObstaclePVS );
	if ( numObstaclePVS > 0 ) {
		fp->Read( obstaclePVS.Begin(), numObstaclePVS * sizeof(aasObstaclePVS_t) );
	}

	// 12. ReachabilityNames (132 bytes each)
	int numReachNames = 0;
	fp->ReadInt( numReachNames );
	if ( numReachNames < 0 ) { fileSystem->CloseFile( fp ); return false; }
	reachabilityNames.SetNum( numReachNames );
	if ( numReachNames > 0 ) {
		fp->Read( reachabilityNames.Begin(), numReachNames * sizeof(aasName_t) );
	}

	this->name = fileName;
	fileSystem->CloseFile( fp );
	return true;
}

int idAASFileLocal::FindReachabilityByName( const char *name ) const {
	for ( int i = 0; i < reachabilityNames.Num(); i++ ) {
		if ( idStr::Icmp( reachabilityNames[i].name, name ) == 0 ) {
			return reachabilityNames[i].index;
		}
	}
	return 0;
}

size_t idAASFileLocal::MemorySize() const {
	size_t size = sizeof( *this );
	size += planes.MemoryUsed();
	size += vertices.MemoryUsed();
	size += edges.MemoryUsed();
	size += edgeIndex.MemoryUsed();
	size += reachabilities.MemoryUsed();
	size += areas.MemoryUsed();
	size += nodes.MemoryUsed();
	size += portals.MemoryUsed();
	size += portalIndex.MemoryUsed();
	size += clusters.MemoryUsed();
	size += obstaclePVS.MemoryUsed();
	size += reachabilityNames.MemoryUsed();
	return size;
}

size_t idAASFileLocal::MemoryUsed() const {
	return MemorySize();
}

size_t idAASFileLocal::MaxRoutingCacheSize() const {
	return 1024 * 1024;
}

size_t idAASFileLocal::AreaTravelTimeCacheSize() const {
	return 1024 * 1024;
}

idVec3 idAASFileLocal::EdgeCenter( int edgeNum ) const {
	int e = abs( edgeNum );
	if ( e < 0 || e >= edges.Num() ) {
		return vec3_origin;
	}
	return ( vertices[ edges[e].vertexNum[0] ] + vertices[ edges[e].vertexNum[1] ] ) * 0.5f;
}

idBounds idAASFileLocal::EdgeBounds( int edgeNum ) const {
	int e = abs( edgeNum );
	idBounds bounds;
	bounds.Clear();
	if ( e >= 0 && e < edges.Num() ) {
		bounds.AddPoint( vertices[ edges[e].vertexNum[0] ] );
		bounds.AddPoint( vertices[ edges[e].vertexNum[1] ] );
	}
	return bounds;
}

idVec3 idAASFileLocal::AreaCenter( int areaNum ) const {
	if ( areaNum <= 0 || areaNum >= areas.Num() ) {
		return vec3_origin;
	}
	const aasArea_t &area = areas[ areaNum ];
	if ( area.numEdges <= 0 ) {
		return vec3_origin;
	}
	idVec3 center = vec3_origin;
	for ( int i = 0; i < area.numEdges; i++ ) {
		int edgeNum = edgeIndex[ area.firstEdge + i ];
		const aasEdge_t &edge = edges[ abs( edgeNum ) ];
		center += vertices[ edge.vertexNum[ INT32_SIGNBITSET( edgeNum ) ] ];
	}
	return center / (float)area.numEdges;
}

idBounds idAASFileLocal::AreaBounds( int areaNum ) const {
	idBounds bounds;
	bounds.Clear();
	if ( areaNum <= 0 || areaNum >= areas.Num() ) {
		return bounds;
	}
	const aasArea_t &area = areas[ areaNum ];
	for ( int i = 0; i < area.numEdges; i++ ) {
		int edgeNum = edgeIndex[ area.firstEdge + i ];
		const aasEdge_t &edge = edges[ abs( edgeNum ) ];
		bounds.AddPoint( vertices[ edge.vertexNum[ 0 ] ] );
		bounds.AddPoint( vertices[ edge.vertexNum[ 1 ] ] );
	}
	return bounds;
}

int idAASFileLocal::PointAreaNum( const idVec3 &origin ) const {
	if ( nodes.Num() <= 1 ) {
		return 0;
	}
	int nodeNum = 1;
	do {
		const aasNode_t &node = nodes[ nodeNum ];
		if ( planes[ node.planeNum ].Side( origin ) == PLANESIDE_BACK ) {
			nodeNum = node.children[ 1 ];
		} else {
			nodeNum = node.children[ 0 ];
		}
		if ( nodeNum < 0 ) {
			return -nodeNum;
		}
	} while( nodeNum != 0 );

	return 0;
}

int idAASFileLocal::BoundsReachableAreaNum_r( int nodeNum, const idBounds &bounds, const int areaFlags, const int excludeTravelFlags ) const {
	while( nodeNum ) {
		if ( nodeNum < 0 ) {
			int areaNum = -nodeNum;
			if ( areaNum > 0 && areaNum < areas.Num() ) {
				if ( ( areas[areaNum].flags & areaFlags ) && ( ( areas[areaNum].travelFlags & excludeTravelFlags ) == 0 ) ) {
					return areaNum;
				}
			}
			return 0;
		}
		if ( nodeNum >= nodes.Num() ) {
			return 0;
		}
		const aasNode_t &node = nodes[nodeNum];
		int res = bounds.PlaneSide( planes[node.planeNum] );
		if ( res == PLANESIDE_BACK ) {
			nodeNum = node.children[1];
		}
		else if ( res == PLANESIDE_FRONT ) {
			nodeNum = node.children[0];
		}
		else {
			int found = BoundsReachableAreaNum_r( node.children[1], bounds, areaFlags, excludeTravelFlags );
			if ( found ) {
				return found;
			}
			nodeNum = node.children[0];
		}
	}
	return 0;
}

int idAASFileLocal::BoundsReachableAreaNum( const idBounds &bounds, const int areaFlags, const int excludeTravelFlags ) const {
	if ( nodes.Num() <= 1 ) {
		return 0;
	}
	return BoundsReachableAreaNum_r( 1, bounds, areaFlags, excludeTravelFlags );
}

int idAASFileLocal::PointReachableAreaNum( const idVec3 &origin, const idBounds &searchBounds, const int areaFlags, const int excludeTravelFlags ) const {
	int areaList[32], areaNum, i;
	idVec3 start, end, pointList[32];
	aasTrace_t trace;
	idBounds bounds;
	float frac;

	start = origin;

	trace.areas = areaList;
	trace.points = pointList;
	trace.maxAreas = sizeof( areaList ) / sizeof( int );
	trace.getOutOfSolid = true;

	areaNum = PointAreaNum( start );
	if ( areaNum > 0 && areaNum < areas.Num() ) {
		if ( ( areas[areaNum].flags & areaFlags ) && ( ( areas[areaNum].travelFlags & excludeTravelFlags ) == 0 ) ) {
			return areaNum;
		}
	}
	else {
		// trace up
		end = start;
		end[2] += 32.0f;
		Trace( trace, start, end );
		if ( trace.numAreas >= 1 ) {
			int a = areaList[0];
			if ( a > 0 && a < areas.Num() && ( areas[a].flags & areaFlags ) && ( ( areas[a].travelFlags & excludeTravelFlags ) == 0 ) ) {
				return a;
			}
			start = pointList[0];
			start[2] += 1.0f;
		}
	}

	// trace down
	end = start;
	end[2] -= 32.0f;
	Trace( trace, start, end );
	if ( trace.lastAreaNum > 0 && trace.lastAreaNum < areas.Num() ) {
		int a = trace.lastAreaNum;
		if ( ( areas[a].flags & areaFlags ) && ( ( areas[a].travelFlags & excludeTravelFlags ) == 0 ) ) {
			return a;
		}
		start = trace.endpos;
	}

	// expand bounds until an area is found
	for ( i = 1; i <= 12; i++ ) {
		frac = i * ( 1.0f / 12.0f );
		bounds[0] = origin + searchBounds[0] * frac;
		bounds[1] = origin + searchBounds[1] * frac;
		areaNum = BoundsReachableAreaNum( bounds, areaFlags, excludeTravelFlags );
		if ( areaNum > 0 && areaNum < areas.Num() && ( areas[areaNum].flags & areaFlags ) && ( ( areas[areaNum].travelFlags & excludeTravelFlags ) == 0 ) ) {
			return areaNum;
		}
	}
	return 0;
}

bool idAASFileLocal::PushPointIntoArea( int areaNum, idVec3 &point ) const {
	if ( areaNum <= 0 || areaNum >= areas.Num() ) {
		return false;
	}
	if ( PointAreaNum( point ) == areaNum ) {
		return true;
	}
	point = AreaCenter( areaNum );
	return true;
}

#define TRACEPLANE_EPSILON		0.125f

typedef struct aasTraceStack_s
{
	idVec3			start;
	idVec3			end;
	int				planeNum;
	int				nodeNum;
} aasTraceStack_t;

bool idAASFileLocal::Trace( aasTrace_t &trace, const idVec3 &start, const idVec3 &end ) const {
	int side, nodeNum, tmpPlaneNum;
	double front, back, frac;
	idVec3 cur_start, cur_end, cur_mid, v1, v2;
	aasTraceStack_t tracestack[AAS_MAX_TREE_DEPTH];
	aasTraceStack_t *tstack_p;
	const aasNode_t *node;
	const idPlane *plane;

	trace.numAreas = 0;
	trace.lastAreaNum = 0;
	trace.blockingAreaNum = 0;

	if ( nodes.Num() <= 1 ) {
		trace.fraction = 1.0f;
		trace.endpos = end;
		return false;
	}

	tstack_p = tracestack;
	tstack_p->start = start;
	tstack_p->end = end;
	tstack_p->planeNum = 0;
	tstack_p->nodeNum = 1;		// start with the root of the tree
	tstack_p++;
	
	while( 1 ) {

		tstack_p--;
		// if the trace stack is empty
		if ( tstack_p < tracestack ) {
			if ( !trace.lastAreaNum ) {
				// completely in solid
				trace.fraction = 0.0f;
				trace.endpos = start;
			}
			else {
				// nothing was hit
				trace.fraction = 1.0f;
				trace.endpos = end;
			}
			trace.planeNum = 0;
			return false;
		}

		// number of the current node to test the line against
		nodeNum = tstack_p->nodeNum;

		// if it is an area
		if ( nodeNum < 0 ) {
			int areaNum = -nodeNum;
			if ( areaNum > 0 && areaNum < areas.Num() ) {
				// if can't enter the area
				if ( ( areas[areaNum].flags & trace.flags ) || ( areas[areaNum].travelFlags & trace.travelFlags ) ) {
					if ( !trace.lastAreaNum ) {
						trace.fraction = 0.0f;
						v1 = vec3_origin;
					} else {
						v1 = end - start;
						v2 = tstack_p->start - start;
						float v1Len = v1.Length();
						trace.fraction = ( v1Len > 0.0f ) ? ( v2.Length() / v1Len ) : 0.0f;
					}
					trace.endpos = tstack_p->start;
					trace.blockingAreaNum = areaNum;
					trace.planeNum = tstack_p->planeNum;
					// always take the plane with normal facing towards the trace start
					plane = &planes[trace.planeNum];
					if ( v1 * plane->Normal() > 0.0f ) {
						trace.planeNum ^= 1;
					}
					return true;
				}
				trace.lastAreaNum = areaNum;
				if ( trace.numAreas < trace.maxAreas ) {
					if ( trace.areas ) {
						trace.areas[trace.numAreas] = areaNum;
					}
					if ( trace.points ) {
						trace.points[trace.numAreas] = tstack_p->start;
					}
					trace.numAreas++;
				}
			}
			continue;
		}

		// if it is a solid leaf
		if ( !nodeNum ) {
			if ( !trace.lastAreaNum ) {
				trace.fraction = 0.0f;
				v1 = vec3_origin;
			} else {
				v1 = end - start;
				v2 = tstack_p->start - start;
				float v1Len = v1.Length();
				trace.fraction = ( v1Len > 0.0f ) ? ( v2.Length() / v1Len ) : 0.0f;
			}
			trace.endpos = tstack_p->start;
			trace.blockingAreaNum = 0;	// hit solid leaf
			trace.planeNum = tstack_p->planeNum;
			// always take the plane with normal facing towards the trace start
			plane = &planes[trace.planeNum];
			if ( v1 * plane->Normal() > 0.0f ) {
				trace.planeNum ^= 1;
			}
			if ( !trace.lastAreaNum && trace.getOutOfSolid ) {
				continue;
			}
			else {
				return true;
			}
		}

		// the node to test against
		node = &nodes[nodeNum];
		// start point of current line to test against node
		cur_start = tstack_p->start;
		// end point of the current line to test against node
		cur_end = tstack_p->end;
		// the current node plane
		plane = &planes[node->planeNum];

		front = plane->Distance( cur_start );
		back = plane->Distance( cur_end );

		// if the whole to be traced line is totally at the front of this node
		// only go down the tree with the front child
		if ( front >= -ON_EPSILON && back >= -ON_EPSILON ) {
			tstack_p->nodeNum = node->children[0];
			tstack_p++;
			if ( tstack_p >= &tracestack[AAS_MAX_TREE_DEPTH] ) {
				common->Error("idAASFileLocal::Trace: stack overflow\n" );
				return false;
			}
		}
		// if the whole to be traced line is totally at the back of this node
		// only go down the tree with the back child
		else if ( front < ON_EPSILON && back < ON_EPSILON ) {
			tstack_p->nodeNum = node->children[1];
			tstack_p++;
			if ( tstack_p >= &tracestack[AAS_MAX_TREE_DEPTH] ) {
				common->Error("idAASFileLocal::Trace: stack overflow\n" );
				return false;
			}
		}
		// go down the tree both at the front and back of the node
		else {
			tmpPlaneNum = tstack_p->planeNum;
			// calculate the hit point with the node plane
			// put the cross point TRACEPLANE_EPSILON on the near side
			if (front < 0) {
				frac = (front + TRACEPLANE_EPSILON) / ( front - back );
			}
			else {
				frac = (front - TRACEPLANE_EPSILON) / ( front - back );
			}

			if (frac < 0) {
				frac = 0.001f;
			}
			else if (frac > 1) {
				frac = 0.999f;
			}

			cur_mid = cur_start + ( cur_end - cur_start ) * frac;

			// side the front part of the line is on
			side = front < 0;

			// first put the end part of the line on the stack (back side)
			tstack_p->start = cur_mid;
			tstack_p->planeNum = node->planeNum;
			tstack_p->nodeNum = node->children[!side];
			tstack_p++;
			if ( tstack_p >= &tracestack[AAS_MAX_TREE_DEPTH] ) {
				common->Error("idAASFileLocal::Trace: stack overflow\n" );
				return false;
			}
			// now put the part near the start of the line on the stack so we will
			// continue with that part first.
			tstack_p->start = cur_start;
			tstack_p->end = cur_mid;
			tstack_p->planeNum = tmpPlaneNum;
			tstack_p->nodeNum = node->children[side];
			tstack_p++;
			if ( tstack_p >= &tracestack[AAS_MAX_TREE_DEPTH] ) {
				common->Error("idAASFileLocal::Trace: stack overflow\n" );
				return false;
			}
		}
	}
	return false;
}

bool idAASFileLocal::TraceHeight( aasTraceHeight_t &trace, const idVec3 &start, const idVec3 &end ) const {
	trace.numPoints = 0;
	if ( trace.maxPoints <= 0 || trace.points == NULL ) {
		return false;
	}

	aasTrace_t bspTrace;
	bspTrace.areas = NULL;
	bspTrace.points = trace.points;
	bspTrace.maxAreas = trace.maxPoints;
	bspTrace.getOutOfSolid = true;

	Trace( bspTrace, start, end );
	trace.numPoints = bspTrace.numAreas;
	if ( trace.numPoints == 0 ) {
		trace.points[0] = start;
		trace.numPoints = 1;
	}
	return true;
}

bool idAASFileLocal::TraceFloor( aasTraceFloor_t &trace, const idVec3 &start, int startAreaNum, const idVec3 &end, int endAreaNum, int travelFlags ) const {
	aasTrace_t bspTrace;
	bspTrace.flags = AAS_AREA_CONTENTS_SOLID;
	bspTrace.travelFlags = travelFlags;
	bspTrace.getOutOfSolid = true;

	Trace( bspTrace, start, end );

	trace.fraction = bspTrace.fraction;
	trace.endpos = bspTrace.endpos;
	trace.lastAreaNum = bspTrace.lastAreaNum;
	trace.lastEdgeNum = 0;

	if ( bspTrace.fraction >= 1.0f ) {
		if ( endAreaNum > 0 && trace.lastAreaNum != endAreaNum ) {
			if ( PointAreaNum( end ) == endAreaNum ) {
				trace.lastAreaNum = endAreaNum;
			}
		}
		return true;
	}
	return false;
}

/*
===============================================================================

	idAASFileManagerLocal

===============================================================================
*/

idAASFile *idAASFileManagerLocal::LoadAAS( const char *fileName, unsigned int mapFileCRC ) {
	idAASFileLocal *file = new idAASFileLocal();
	if ( !file->Load( fileName, mapFileCRC ) ) {
		delete file;
		return NULL;
	}
	return file;
}

void idAASFileManagerLocal::FreeAAS( idAASFile *file ) {
	delete file;
}
