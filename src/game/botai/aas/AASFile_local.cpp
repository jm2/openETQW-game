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
	maxVertexHeight = 0.0f;
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

	// Link reachabilities into areas ( indices are validated once the whole file has been read )
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
		for ( int i = 0; i < numReachNames; i++ ) {
			reachabilityNames[ i ].name[ AAS_MAX_NAME_LENGTH - 1 ] = '\0';
		}
	}

	this->name = fileName;
	fileSystem->CloseFile( fp );

	if ( !ValidateIndices( fileName ) ) {
		return false;
	}

	BuildEdgeAreas();

	return true;
}

/*
================
idAASFileLocal::ValidateIndices

The trace and push code indexes the lists directly, so reject a file whose cross references are out of range.
================
*/
// first..first+num-1 lies in 0..count-1, without int overflow on crafted counts
static bool AAS_SpanInRange( int first, int num, int count ) {
	return first >= 0 && num >= 0 && first <= count && num <= count - first;
}

bool idAASFileLocal::ValidateIndices( const char *fileName ) const {
	const char *problem = NULL;
	int index = 0;

	for ( int i = 0; i < edges.Num() && problem == NULL; i++ ) {
		if ( edges[ i ].vertexNum[ 0 ] < 0 || edges[ i ].vertexNum[ 0 ] >= vertices.Num() || edges[ i ].vertexNum[ 1 ] < 0 || edges[ i ].vertexNum[ 1 ] >= vertices.Num() ) {
			problem = "edge vertex"; index = i;
		}
	}
	for ( int i = 0; i < edgeIndex.Num() && problem == NULL; i++ ) {
		// signed edge number; compared without abs(), which overflows on INT_MIN
		if ( edgeIndex[ i ] >= edges.Num() || edgeIndex[ i ] <= -edges.Num() ) {
			problem = "edge index"; index = i;
		}
	}
	for ( int i = 0; i < areas.Num() && problem == NULL; i++ ) {
		if ( !AAS_SpanInRange( areas[ i ].firstEdge, areas[ i ].numEdges, edgeIndex.Num() ) ) {
			problem = "area edges"; index = i;
		}
	}
	for ( int i = 0; i < reachabilities.Num() && problem == NULL; i++ ) {
		if ( reachabilities[ i ].fromAreaNum >= areas.Num() || reachabilities[ i ].toAreaNum >= areas.Num() ) {
			problem = "reachability area"; index = i;
		}
	}
	// The routing code (AAS_routing.cpp) walks clusters and portals by index and sizes a
	// per-cluster cache slice from numReachableAreas, indexed by clusterAreaNum. Entry 0 of
	// the area, portal and cluster lists is an unused dummy, as in every idTech 4 AAS file;
	// retail cluster 0 has no portals or reachable areas, but its firstPortal is garbage.
	for ( int i = 0; i < portalIndex.Num() && problem == NULL; i++ ) {
		if ( portalIndex[ i ] <= 0 || portalIndex[ i ] >= portals.Num() ) {	// never the dummy portal 0
			problem = "portal index"; index = i;
		}
	}
	// the dummies name nothing (portal 0 is read by ClusterAreaNum for an area of cluster 0)
	if ( problem == NULL && portals.Num() > 0 && ( portals[ 0 ].areaNum != 0 || portals[ 0 ].clusters[ 0 ] != 0 || portals[ 0 ].clusters[ 1 ] != 0 ) ) {
		problem = "portal 0"; index = 0;
	}
	if ( problem == NULL && areas.Num() > 0 && areas[ 0 ].cluster != 0 ) {
		problem = "area 0 cluster"; index = 0;
	}
	long long totalReachable = 0;
	for ( int i = 0; i < clusters.Num() && problem == NULL; i++ ) {
		if ( clusters[ i ].numReachableAreas < 0 || clusters[ i ].numReachableAreas > clusters[ i ].numAreas || clusters[ i ].numAreas > areas.Num() ) {
			problem = "cluster reachable areas"; index = i;
		} else if ( i == 0 ? ( clusters[ i ].numPortals != 0 || clusters[ i ].numReachableAreas != 0 ) :
				!AAS_SpanInRange( clusters[ i ].firstPortal, clusters[ i ].numPortals, portalIndex.Num() ) ) {
			problem = "cluster portals"; index = i;
		} else if ( i > 0 ) {
			// a cluster's portals border it: ClusterAreaNum picks the portal side from that
			for ( int k = 0; k < clusters[ i ].numPortals; k++ ) {
				const aasPortal_t& portal = portals[ portalIndex[ clusters[ i ].firstPortal + k ] ];
				if ( portal.clusters[ 0 ] != i && portal.clusters[ 1 ] != i ) {
					problem = "cluster portal side"; index = i;
					break;
				}
			}
		}
		totalReachable += clusters[ i ].numReachableAreas;
	}
	// each area is reachable in its own cluster, a portal area in the two it joins
	if ( problem == NULL && totalReachable > ( long long )areas.Num() + 2LL * portals.Num() ) {
		problem = "cluster reachable area total"; index = 0;
	}
	for ( int i = 1; i < portals.Num() && problem == NULL; i++ ) {
		if ( portals[ i ].areaNum >= areas.Num() || areas[ portals[ i ].areaNum ].cluster != -i ) {	// the portal area points back
			problem = "portal area"; index = i;
		}
		for ( int j = 0; j < 2 && problem == NULL; j++ ) {
			const int cluster = portals[ i ].clusters[ j ];
			if ( cluster <= 0 || cluster >= clusters.Num() ) {
				problem = "portal cluster"; index = i;
			} else if ( portals[ i ].clusterAreaNum[ j ] >= clusters[ cluster ].numReachableAreas ) {
				problem = "portal cluster area"; index = i;
			}
		}
	}
	for ( int i = 1; i < areas.Num() && problem == NULL; i++ ) {
		// negative cluster = the area is portal -cluster
		if ( areas[ i ].cluster == 0 || ( areas[ i ].cluster > 0 && areas[ i ].cluster >= clusters.Num() ) || ( areas[ i ].cluster < 0 && -areas[ i ].cluster >= portals.Num() ) ) {
			problem = "area cluster"; index = i;
		} else if ( areas[ i ].cluster > 0 && areas[ i ].clusterAreaNum >= clusters[ areas[ i ].cluster ].numReachableAreas ) {
			// Kept to numReachableAreas for every area, not only reachable ones: the
			// routing cache has that many slots and GetAreaRoutingCache indexes it with
			// no bound of its own. Every retail file has numAreas == numReachableAreas.
			problem = "area cluster area"; index = i;
		}
	}
	for ( int i = 0; i < nodes.Num() && problem == NULL; i++ ) {
		if ( nodes[ i ].planeNum >= planes.Num() ) {
			problem = "node plane"; index = i;
		}
		for ( int j = 0; j < 2 && problem == NULL; j++ ) {
			const int child = nodes[ i ].children[ j ];
			if ( ( child > 0 && child >= nodes.Num() ) || ( child < 0 && child <= -areas.Num() ) ) {
				problem = "node child"; index = i;
			}
		}
	}

	if ( problem != NULL ) {
		gameLocal.Warning( "idAASFileLocal::Load: %s has an invalid %s reference ( entry %d )", fileName, problem, index );
		return false;
	}
	return true;
}

/*
================
idAASFileLocal::BuildEdgeAreas
================
*/
void idAASFileLocal::BuildEdgeAreas() {
	edgeAreas.SetNum( edges.Num() * 2 );
	for ( int i = 0; i < edgeAreas.Num(); i++ ) {
		edgeAreas[ i ] = 0;
	}

	for ( int i = 1; i < areas.Num(); i++ ) {
		const aasArea_t &area = areas[ i ];
		for ( int j = 0; j < area.numEdges; j++ ) {
			const int e = abs( edgeIndex[ area.firstEdge + j ] );
			if ( edgeAreas[ e * 2 + 0 ] == 0 ) {
				edgeAreas[ e * 2 + 0 ] = i;
			} else if ( edgeAreas[ e * 2 + 0 ] != i && edgeAreas[ e * 2 + 1 ] == 0 ) {
				edgeAreas[ e * 2 + 1 ] = i;
			} else if ( edgeAreas[ e * 2 + 0 ] != i && edgeAreas[ e * 2 + 1 ] != i ) {
				edgeAreas[ e * 2 + 1 ] = -1;	// used by more than two areas
			}
		}
	}

	// top of the navigable space, used to look down the floor columns
	maxVertexHeight = 0.0f;
	for ( int i = 0; i < vertices.Num(); i++ ) {
		if ( i == 0 || vertices[ i ].z > maxVertexHeight ) {
			maxVertexHeight = vertices[ i ].z;
		}
	}
}

int idAASFileLocal::FindReachabilityByName( const char *name ) const {
	if ( name == NULL ) {
		return -1;
	}
	for ( int i = 0; i < reachabilityNames.Num(); i++ ) {
		if ( idStr::Icmp( reachabilityNames[i].name, name ) == 0 ) {
			const int index = reachabilityNames[i].index;
			// a name that points outside the reachability list is as good as missing
			if ( index < 0 || index >= reachabilities.Num() ) {
				return -1;
			}
			return index;
		}
	}
	// -1: no such reachability. 0 is a real reachability, so it must never be
	// returned for a missing name (callers only reject negative indices).
	return -1;
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
	size += edgeAreas.MemoryUsed();
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

/*
===============================================================================

	BSP queries

	Node children: > 0 is a node, 0 is solid, < 0 is the negated area number.
	A point on a node plane belongs to the front ( children[ 0 ] ) side.

===============================================================================
*/

/*
================
idAASFileLocal::PointAreaNum
================
*/
int idAASFileLocal::PointAreaNum( const idVec3 &origin ) const {
	int nodeNum = ( nodes.Num() > 1 ) ? 1 : 0;
	for ( int depth = 0; nodeNum > 0; depth++ ) {
		if ( depth >= nodes.Num() ) {
			return 0;	// corrupt tree
		}
		const aasNode_t &node = nodes[ nodeNum ];
		nodeNum = node.children[ ( planes[ node.planeNum ].Distance( origin ) < 0.0f ) ? 1 : 0 ];
	}
	return -nodeNum;
}

/*
================
idAASFileLocal::AreaMatches
================
*/
bool idAASFileLocal::AreaMatches( int areaNum, int areaFlags, int excludeTravelFlags ) const {
	if ( areaNum <= 0 || areaNum >= areas.Num() ) {
		return false;
	}
	return ( areas[ areaNum ].flags & areaFlags ) != 0 && ( areas[ areaNum ].travelFlags & excludeTravelFlags ) == 0;
}

/*
================
idAASFileLocal::BoundsReachableAreaNum_r
================
*/
int idAASFileLocal::BoundsReachableAreaNum_r( int nodeNum, const idBounds &bounds, int areaFlags, int excludeTravelFlags, int depth ) const {
	if ( nodeNum < 0 ) {
		return AreaMatches( -nodeNum, areaFlags, excludeTravelFlags ) ? -nodeNum : 0;
	}
	if ( nodeNum == 0 || depth > AAS_MAX_TREE_DEPTH ) {
		return 0;
	}

	const aasNode_t &node = nodes[ nodeNum ];
	const int side = bounds.PlaneSide( planes[ node.planeNum ] );
	if ( side != PLANESIDE_BACK ) {
		const int areaNum = BoundsReachableAreaNum_r( node.children[ 0 ], bounds, areaFlags, excludeTravelFlags, depth + 1 );
		if ( areaNum != 0 ) {
			return areaNum;
		}
	}
	if ( side != PLANESIDE_FRONT ) {
		return BoundsReachableAreaNum_r( node.children[ 1 ], bounds, areaFlags, excludeTravelFlags, depth + 1 );
	}
	return 0;
}

/*
================
idAASFileLocal::BoundsReachableAreaNum

Returns the first area with the area flags and none of the travel flags that touches the bounds.
================
*/
int idAASFileLocal::BoundsReachableAreaNum( const idBounds &bounds, const int areaFlags, const int excludeTravelFlags ) const {
	if ( nodes.Num() <= 1 ) {
		return 0;
	}
	return BoundsReachableAreaNum_r( 1, bounds, areaFlags, excludeTravelFlags, 0 );
}

/*
================
idAASFileLocal::PointReachableAreaNum

Returns the area with the area flags and none of the travel flags that contains the origin, or else the
closest such area straight above or below the origin ( within the vertical search distance ), or else
the first such area touching the search bounds scaled around the origin ( in growing steps ).
================
*/
int idAASFileLocal::PointReachableAreaNum( const idVec3 &origin, const idBounds &searchBounds, const int areaFlags, const int excludeTravelFlags ) const {
	const float	REACHABLE_VERTICAL_SEARCH	= 32.0f;
	const int	REACHABLE_BOUNDS_STEPS		= 12;
	const int	MAX_COLUMN_AREAS			= 16;

	int areaNum = PointAreaNum( origin );
	if ( AreaMatches( areaNum, areaFlags, excludeTravelFlags ) ) {
		return areaNum;
	}

	int areaList[ MAX_COLUMN_AREAS ];
	idVec3 pointList[ MAX_COLUMN_AREAS ];
	int bestAreaNum = 0;
	float bestDist = idMath::INFINITY;
	for ( int dir = 0; dir < 2; dir++ ) {
		aasTrace_t trace;
		trace.areas = areaList;
		trace.points = pointList;
		trace.maxAreas = MAX_COLUMN_AREAS;
		trace.getOutOfSolid = true;
		const idVec3 end( origin.x, origin.y, origin.z + ( ( dir == 0 ) ? REACHABLE_VERTICAL_SEARCH : -REACHABLE_VERTICAL_SEARCH ) );
		Trace( trace, origin, end );

		for ( int i = 0; i < trace.numAreas; i++ ) {
			if ( AreaMatches( areaList[ i ], areaFlags, excludeTravelFlags ) ) {
				const float dist = idMath::Fabs( pointList[ i ].z - origin.z );
				if ( dist < bestDist ) {
					bestDist = dist;
					bestAreaNum = areaList[ i ];
				}
				break;
			}
		}
	}
	if ( bestAreaNum != 0 ) {
		return bestAreaNum;
	}

	// of the areas touching the smallest scaled search bounds, take the one closest to the origin seen from above
	if ( nodes.Num() <= 1 ) {
		return 0;	// no BSP tree to search (node 0 is the solid leaf)
	}
	const int MAX_BOUNDS_AREAS = 32;
	int boundsAreas[ MAX_BOUNDS_AREAS ];
	for ( int i = 1; i <= REACHABLE_BOUNDS_STEPS; i++ ) {
		const float scale = ( float )i / ( float )REACHABLE_BOUNDS_STEPS;
		const idBounds bounds( origin + searchBounds[ 0 ] * scale, origin + searchBounds[ 1 ] * scale );
		int numBoundsAreas = 0;
		BoundsAreas_r( 1, bounds, areaFlags, excludeTravelFlags, boundsAreas, numBoundsAreas, MAX_BOUNDS_AREAS, 0 );
		if ( numBoundsAreas == 0 ) {
			continue;
		}
		bestAreaNum = boundsAreas[ 0 ];
		bestDist = idMath::INFINITY;
		for ( int j = 0; j < numBoundsAreas; j++ ) {
			const float dist = Max( 0.0f, AreaPolygonDistance2D( boundsAreas[ j ], origin.x, origin.y ) );
			if ( dist < bestDist ) {
				bestDist = dist;
				bestAreaNum = boundsAreas[ j ];
			}
		}
		return bestAreaNum;
	}
	return 0;
}

/*
================
idAASFileLocal::BoundsAreas_r

Collects the distinct areas with the area flags and none of the travel flags that touch the bounds.
================
*/
void idAASFileLocal::BoundsAreas_r( int nodeNum, const idBounds &bounds, int areaFlags, int excludeTravelFlags, int *areaList, int &numAreas, int maxAreas, int depth ) const {
	if ( numAreas >= maxAreas ) {
		return;
	}
	if ( nodeNum < 0 ) {
		const int areaNum = -nodeNum;
		if ( AreaMatches( areaNum, areaFlags, excludeTravelFlags ) ) {
			for ( int i = 0; i < numAreas; i++ ) {
				if ( areaList[ i ] == areaNum ) {
					return;
				}
			}
			areaList[ numAreas++ ] = areaNum;
		}
		return;
	}
	if ( nodeNum == 0 || depth > AAS_MAX_TREE_DEPTH ) {
		return;
	}

	const aasNode_t &node = nodes[ nodeNum ];
	const int side = bounds.PlaneSide( planes[ node.planeNum ] );
	if ( side != PLANESIDE_BACK ) {
		BoundsAreas_r( node.children[ 0 ], bounds, areaFlags, excludeTravelFlags, areaList, numAreas, maxAreas, depth + 1 );
	}
	if ( side != PLANESIDE_FRONT ) {
		BoundsAreas_r( node.children[ 1 ], bounds, areaFlags, excludeTravelFlags, areaList, numAreas, maxAreas, depth + 1 );
	}
}

/*
================
idAASFileLocal::SetTraceBlocked
================
*/
void idAASFileLocal::SetTraceBlocked( aasTrace_t &trace, const idVec3 &start, const idVec3 &delta, float fraction, int planeNum, int blockingAreaNum ) const {
	const float TRACE_BACK_OFF = 0.125f;	// keep the end position a little before the plane that was hit

	if ( trace.lastAreaNum == 0 ) {
		// blocked before any area was entered
		trace.fraction = 0.0f;
		trace.endpos = start + delta * fraction;
	} else {
		const float length = delta.Length();
		if ( length > 0.0f ) {
			fraction = Max( 0.0f, fraction - TRACE_BACK_OFF / length );
		}
		trace.fraction = fraction;
		trace.endpos = start + delta * fraction;
	}
	trace.blockingAreaNum = blockingAreaNum;
	// report the side of the plane that faces the start of the trace ( planes are stored in opposite pairs )
	trace.planeNum = planeNum;
	if ( planeNum > 0 && planes[ planeNum ].Normal() * delta > 0.0f ) {
		trace.planeNum ^= 1;
	}
}

/*
================
idAASFileLocal::TraceSegment_r

The part of the line start + delta * f with f from startFraction to endFraction lies in the subtree of
nodeNum and was entered through plane enterPlaneNum. Returns true if the trace is blocked in it.
================
*/
bool idAASFileLocal::TraceSegment_r( aasTrace_t &trace, const idVec3 &start, const idVec3 &delta, int nodeNum, float startFraction, float endFraction, int enterPlaneNum, int depth ) const {
	if ( nodeNum < 0 ) {
		const int areaNum = -nodeNum;
		const aasArea_t &area = areas[ areaNum ];
		if ( ( area.flags & trace.flags ) != 0 || ( area.travelFlags & trace.travelFlags ) != 0 ) {
			SetTraceBlocked( trace, start, delta, startFraction, enterPlaneNum, areaNum );
			return true;
		}
		trace.lastAreaNum = areaNum;
		if ( trace.numAreas < trace.maxAreas ) {
			if ( trace.areas != NULL ) {
				trace.areas[ trace.numAreas ] = areaNum;
			}
			if ( trace.points != NULL ) {
				trace.points[ trace.numAreas ] = start + delta * startFraction;
			}
			trace.numAreas++;
		}
		return false;
	}

	if ( nodeNum == 0 ) {
		if ( trace.lastAreaNum == 0 && trace.getOutOfSolid ) {
			return false;	// still inside the solid the trace started in
		}
		SetTraceBlocked( trace, start, delta, startFraction, enterPlaneNum, 0 );
		return true;
	}

	if ( depth > AAS_MAX_TREE_DEPTH ) {
		SetTraceBlocked( trace, start, delta, startFraction, enterPlaneNum, 0 );
		return true;
	}

	const aasNode_t &node = nodes[ nodeNum ];
	const idPlane &plane = planes[ node.planeNum ];
	const float startDist = plane.Distance( start + delta * startFraction );
	const float endDist = plane.Distance( start + delta * endFraction );

	if ( startDist >= 0.0f && endDist >= 0.0f ) {
		return TraceSegment_r( trace, start, delta, node.children[ 0 ], startFraction, endFraction, enterPlaneNum, depth + 1 );
	}
	if ( startDist < 0.0f && endDist < 0.0f ) {
		return TraceSegment_r( trace, start, delta, node.children[ 1 ], startFraction, endFraction, enterPlaneNum, depth + 1 );
	}

	// the segment crosses the plane: trace the part on the start side first, then the rest
	const float crossFraction = startFraction + ( endFraction - startFraction ) * ( startDist / ( startDist - endDist ) );
	const int nearChild = ( startDist >= 0.0f ) ? node.children[ 0 ] : node.children[ 1 ];
	const int farChild = ( startDist >= 0.0f ) ? node.children[ 1 ] : node.children[ 0 ];
	if ( TraceSegment_r( trace, start, delta, nearChild, startFraction, crossFraction, enterPlaneNum, depth + 1 ) ) {
		return true;
	}
	return TraceSegment_r( trace, start, delta, farChild, crossFraction, endFraction, node.planeNum, depth + 1 );
}

/*
================
idAASFileLocal::Trace

Line trace through the areas. Areas with any of trace.flags or trace.travelFlags block the trace, as does
solid, unless trace.getOutOfSolid is set and no area has been entered yet. Returns true if the trace was
blocked. A trace that never enters an area ( completely in solid ) reports fraction 0.
================
*/
bool idAASFileLocal::Trace( aasTrace_t &trace, const idVec3 &start, const idVec3 &end ) const {
	trace.numAreas = 0;
	trace.lastAreaNum = 0;
	trace.blockingAreaNum = 0;
	trace.planeNum = 0;
	trace.fraction = 1.0f;
	trace.endpos = end;

	if ( nodes.Num() <= 1 ) {
		return false;
	}

	const idVec3 delta = end - start;
	if ( TraceSegment_r( trace, start, delta, 1, 0.0f, 1.0f, 0, 0 ) ) {
		return true;
	}

	if ( trace.lastAreaNum == 0 ) {
		trace.fraction = 0.0f;
		trace.endpos = start;
	}
	return false;
}


/*
===============================================================================

	Area polygons

	An area is a floor polygon: its edges, taken in the direction the edge index
	gives them, run clockwise when seen from above, so the inside of the area is
	on the right of every edge. The BSP tree splits space into vertical columns
	above these floors: a point belongs to the area with the highest floor below
	it at that x/y position.

===============================================================================
*/

/*
================
idAASFileLocal::GetAreaEdge
================
*/
void idAASFileLocal::GetAreaEdge( int edgeNum, const idVec3 *&v0, const idVec3 *&v1 ) const {
	const aasEdge_t &edge = edges[ abs( edgeNum ) ];
	if ( edgeNum < 0 ) {
		v0 = &vertices[ edge.vertexNum[ 1 ] ];
		v1 = &vertices[ edge.vertexNum[ 0 ] ];
	} else {
		v0 = &vertices[ edge.vertexNum[ 0 ] ];
		v1 = &vertices[ edge.vertexNum[ 1 ] ];
	}
}

/*
================
idAASFileLocal::AreaPolygonDistance2D

Returns minus the distance to the boundary for a point inside the area polygon ( seen from above ) and the
distance to the boundary for a point outside it. Optionally returns the closest boundary point.
================
*/
float idAASFileLocal::AreaPolygonDistance2D( int areaNum, float x, float y, idVec2 *closest ) const {
	const aasArea_t &area = areas[ areaNum ];
	bool inside = false;
	float bestDistSqr = idMath::INFINITY;

	for ( int i = 0; i < area.numEdges; i++ ) {
		const idVec3 *v0, *v1;
		GetAreaEdge( edgeIndex[ area.firstEdge + i ], v0, v1 );

		// crossing number test, correct for any simple polygon
		if ( ( v0->y > y ) != ( v1->y > y ) ) {
			const float crossX = v0->x + ( y - v0->y ) * ( v1->x - v0->x ) / ( v1->y - v0->y );
			if ( crossX > x ) {
				inside = !inside;
			}
		}

		const float ex = v1->x - v0->x;
		const float ey = v1->y - v0->y;
		const float lengthSqr = ex * ex + ey * ey;
		float u = 0.0f;
		if ( lengthSqr > 1e-6f ) {
			u = idMath::ClampFloat( 0.0f, 1.0f, ( ( x - v0->x ) * ex + ( y - v0->y ) * ey ) / lengthSqr );
		}
		const float px = v0->x + u * ex;
		const float py = v0->y + u * ey;
		const float distSqr = ( x - px ) * ( x - px ) + ( y - py ) * ( y - py );
		if ( distSqr < bestDistSqr ) {
			bestDistSqr = distSqr;
			if ( closest != NULL ) {
				closest->Set( px, py );
			}
		}
	}

	if ( area.numEdges <= 0 ) {
		return idMath::INFINITY;
	}
	const float dist = idMath::Sqrt( bestDistSqr );
	return inside ? -dist : dist;
}

/*
================
idAASFileLocal::AreaBoundaryHeight

Floor height of the area boundary closest to ( x, y ), if the boundary passes within maxDistance of it.
================
*/
bool idAASFileLocal::AreaBoundaryHeight( int areaNum, float x, float y, float maxDistance, float &height ) const {
	const aasArea_t &area = areas[ areaNum ];
	float bestDistSqr = Square( maxDistance );
	bool found = false;

	for ( int i = 0; i < area.numEdges; i++ ) {
		const idVec3 *v0, *v1;
		GetAreaEdge( edgeIndex[ area.firstEdge + i ], v0, v1 );
		const float ex = v1->x - v0->x;
		const float ey = v1->y - v0->y;
		const float lengthSqr = ex * ex + ey * ey;
		if ( lengthSqr < 1e-6f ) {
			continue;	// vertical edge
		}
		const float u = idMath::ClampFloat( 0.0f, 1.0f, ( ( x - v0->x ) * ex + ( y - v0->y ) * ey ) / lengthSqr );
		const float px = v0->x + u * ex;
		const float py = v0->y + u * ey;
		const float distSqr = ( x - px ) * ( x - px ) + ( y - py ) * ( y - py );
		if ( distSqr <= bestDistSqr ) {
			bestDistSqr = distSqr;
			height = v0->z + u * ( v1->z - v0->z );
			found = true;
		}
	}
	return found;
}

/*
================
idAASFileLocal::PushPointIntoColumn

Moves z the least amount needed to put ( x, y, z ) inside the column of the area.
================
*/
bool idAASFileLocal::PushPointIntoColumn( int areaNum, float x, float y, float z, float &pushedZ ) const {
	const float	PUSH_SEARCH_HEIGHT	= 4096.0f;
	const float	PUSH_INTO_COLUMN	= 0.5f;	// the trace reports area entry points on the splitting plane, move past it
	const int	MAX_COLUMN_AREAS	= 64;

	if ( PointAreaNum( idVec3( x, y, z ) ) == areaNum ) {
		pushedZ = z;
		return true;
	}

	int areaList[ MAX_COLUMN_AREAS ];
	idVec3 pointList[ MAX_COLUMN_AREAS ];
	bool found = false;
	float bestZ = z;

	for ( int dir = 0; dir < 2; dir++ ) {
		const float sign = ( dir == 0 ) ? 1.0f : -1.0f;

		aasTrace_t trace;
		trace.areas = areaList;
		trace.points = pointList;
		trace.maxAreas = MAX_COLUMN_AREAS;
		trace.getOutOfSolid = true;
		Trace( trace, idVec3( x, y, z ), idVec3( x, y, z + sign * PUSH_SEARCH_HEIGHT ) );

		for ( int i = 0; i < trace.numAreas; i++ ) {
			if ( areaList[ i ] != areaNum ) {
				continue;
			}
			const float candidateZ = pointList[ i ].z + sign * PUSH_INTO_COLUMN;
			if ( !found || idMath::Fabs( candidateZ - z ) < idMath::Fabs( bestZ - z ) ) {
				bestZ = candidateZ;
				found = true;
			}
			break;
		}
	}

	if ( found && PointAreaNum( idVec3( x, y, bestZ ) ) == areaNum ) {
		pushedZ = bestZ;
		return true;
	}
	return false;
}

/*
================
idAASFileLocal::PushPointIntoArea

Moves the point the least amount needed to get it inside the area: first into the area polygon seen from above
( onto the closest boundary point and slightly inwards ), then vertically into the column of the area.
Returns false and leaves the point untouched if no such position is found.
================
*/
bool idAASFileLocal::PushPointIntoArea( int areaNum, idVec3 &point ) const {
	const float	PUSH_FIRST_INSET	= 0.25f;
	const float	PUSH_MIN_INSIDE		= 0.05f;

	if ( areaNum <= 0 || areaNum >= areas.Num() ) {
		return false;
	}
	if ( PointAreaNum( point ) == areaNum ) {
		return true;
	}

	const aasArea_t &area = areas[ areaNum ];
	if ( area.numEdges < 3 ) {
		return false;
	}

	idVec2 center( 0.0f, 0.0f );
	for ( int i = 0; i < area.numEdges; i++ ) {
		const idVec3 *v0, *v1;
		GetAreaEdge( edgeIndex[ area.firstEdge + i ], v0, v1 );
		center.x += v0->x;
		center.y += v0->y;
	}
	center /= ( float )area.numEdges;

	idVec2 closest;
	const float polygonDist = AreaPolygonDistance2D( areaNum, point.x, point.y, &closest );

	// if the point is already inside the polygon only its height needs to change
	float pushedZ;
	if ( polygonDist < -PUSH_MIN_INSIDE ) {
		if ( PushPointIntoColumn( areaNum, point.x, point.y, point.z, pushedZ ) ) {
			point.z = pushedZ;
			return true;
		}
	}

	// step from the closest boundary point towards the centre of the polygon until the point is inside the area
	idVec2 toCenter = center - closest;
	const float centerDist = toCenter.Normalize();
	for ( float inset = PUSH_FIRST_INSET; inset < centerDist; inset *= 2.0f ) {
		const idVec2 candidate = closest + toCenter * inset;
		if ( AreaPolygonDistance2D( areaNum, candidate.x, candidate.y ) >= -PUSH_MIN_INSIDE ) {
			continue;
		}
		if ( PushPointIntoColumn( areaNum, candidate.x, candidate.y, point.z, pushedZ ) ) {
			point.Set( candidate.x, candidate.y, pushedZ );
			return true;
		}
	}

	if ( PushPointIntoColumn( areaNum, center.x, center.y, point.z, pushedZ ) ) {
		point.Set( center.x, center.y, pushedZ );
		return true;
	}

	return false;
}

/*
===============================================================================

	Height and floor traces

===============================================================================
*/

/*
================
idAASFileLocal::ColumnHeight

The compiler stores the floor height of a column in the flags of the BSP nodes that split off floors
( AAS_NODE_FLAG_COLUMN_HEIGHT ). Walking down the tree from above the world at ( x, y ), the last such node
gives the height of the highest floor in the column. The maximum encoded value marks a column without floor
data ( e.g. inside solid geometry ), which has no height to report, like a column outside the AAS.
================
*/
bool idAASFileLocal::ColumnHeight( float x, float y, float &height ) const {
	if ( nodes.Num() <= 1 ) {
		return false;
	}

	const idVec3 top( x, y, maxVertexHeight + 1.0f );
	int columnValue = -1;
	int nodeNum = 1;
	for ( int depth = 0; nodeNum > 0 && depth < nodes.Num(); depth++ ) {
		const aasNode_t &node = nodes[ nodeNum ];
		if ( node.flags & AAS_NODE_FLAG_COLUMN_HEIGHT ) {
			columnValue = node.flags >> AAS_NODE_FLAG_COLUMN_HEIGHT_SHIFT;
		}
		if ( planes[ node.planeNum ].Side( top ) == PLANESIDE_BACK ) {
			nodeNum = node.children[ 1 ];
		} else {
			nodeNum = node.children[ 0 ];
		}
	}

	if ( columnValue < 0 || columnValue >= AAS_NODE_FLAG_COLUMN_HEIGHT_MAX ) {
		return false;
	}
	height = ( float )( columnValue - AAS_NODE_FLAG_COLUMN_HEIGHT_OFFSET );
	return true;
}

/*
================
idAASFileLocal::TraceHeight

Samples the column heights ( highest floor ) below the line from start to end, seen from above. A point is
stored for the first sampled column and for every change of column height along the line, with the column
height as z. Columns without floor data are skipped. Returns false if the points did not fit, numPoints is
then maxPoints.
================
*/
bool idAASFileLocal::TraceHeight( aasTraceHeight_t &trace, const idVec3 &start, const idVec3 &end ) const {
	const float TRACEHEIGHT_SAMPLE_DISTANCE = 8.0f;		// columns are never narrower than the AAS bounding box

	trace.numPoints = 0;
	if ( trace.maxPoints <= 0 || trace.points == NULL ) {
		return false;
	}

	const float dx = end.x - start.x;
	const float dy = end.y - start.y;
	const float length = idMath::Sqrt( dx * dx + dy * dy );
	const int numSteps = idMath::Ftoi( length / TRACEHEIGHT_SAMPLE_DISTANCE ) + 1;

	float lastHeight = 0.0f;
	for ( int i = 0; i <= numSteps; i++ ) {
		const float f = ( float )i / ( float )numSteps;
		const float x = start.x + dx * f;
		const float y = start.y + dy * f;

		float height;
		if ( !ColumnHeight( x, y, height ) ) {
			continue;	// outside the AAS or no floor data
		}
		if ( trace.numPoints > 0 && height == lastHeight ) {
			continue;	// same column height
		}
		if ( trace.numPoints >= trace.maxPoints ) {
			return false;
		}
		trace.points[ trace.numPoints++ ].Set( x, y, height );
		lastHeight = height;
	}

	if ( trace.numPoints == 0 ) {
		// no column along the line has floor data
		trace.points[ 0 ] = start;
		trace.numPoints = 1;
	}
	return true;
}

/*
================
idAASFileLocal::AreaTravelAllowed

travelFlags is the set of allowed travel types, an area that needs any other travel type blocks.
================
*/
bool idAASFileLocal::AreaTravelAllowed( int areaNum, int travelFlags ) const {
	if ( areaNum <= 0 || areaNum >= areas.Num() ) {
		return false;
	}
	const aasArea_t &area = areas[ areaNum ];
	return ( area.travelFlags & ~travelFlags ) == 0 && ( area.flags & AAS_AREA_CONTENTS_SOLID ) == 0;
}

/*
================
idAASFileLocal::ReachabilityAllowed
================
*/
bool idAASFileLocal::ReachabilityAllowed( int fromAreaNum, int toAreaNum, int travelFlags ) const {
	for ( const aasReachability_t *reach = areas[ fromAreaNum ].reach; reach != NULL; reach = reach->next ) {
		if ( reach->toAreaNum == toAreaNum && ( reach->travelFlags & ~travelFlags ) == 0 ) {
			return true;
		}
	}
	return false;
}

/*
================
idAASFileLocal::FloorTraceNextArea

Returns the area a floor trace enters when it crosses edgeNum of curAreaNum at crossPoint ( z is the floor
height of the current area there ), curAreaNum if the trace stays in the current area, or 0 if it is blocked.
================
*/
int idAASFileLocal::FloorTraceNextArea( int curAreaNum, int edgeNum, const idVec3 &crossPoint, float dirX, float dirY, int travelFlags ) const {
	const float	FLOORTRACE_PROBE_DISTANCE	= 1.0f;		// how far past the edge the next area is looked up
	const float	FLOORTRACE_ALIGN_DISTANCE	= 1.5f;		// max distance between the edge and the boundary of the next area
	const float	FLOORTRACE_HEIGHT_EPSILON	= 0.5f;

	const int e = abs( edgeNum );
	const int area0 = edgeAreas[ e * 2 + 0 ];
	const int area1 = edgeAreas[ e * 2 + 1 ];
	int nextAreaNum;

	if ( area1 > 0 ) {
		// an edge shared by two areas: the floor continues at the same height
		nextAreaNum = ( area0 == curAreaNum ) ? area1 : area0;
	} else {
		// a wall, ledge or step: find the floor on the other side
		const int edgeFlags = edges[ e ].flags;
		float maxRise = settings.maxStepHeight;
		if ( edgeFlags & AAS_EDGE_WALL ) {
			if ( !( travelFlags & TFL_BARRIERJUMP ) ) {
				return 0;
			}
			maxRise = Max( maxRise, settings.maxBarrierHeight );
		}

		// the column just past the edge, at the highest floor that could be stepped onto
		const idVec3 probe( crossPoint.x + dirX * FLOORTRACE_PROBE_DISTANCE, crossPoint.y + dirY * FLOORTRACE_PROBE_DISTANCE, crossPoint.z + maxRise + 1.0f );
		nextAreaNum = PointAreaNum( probe );
		if ( nextAreaNum == curAreaNum || nextAreaNum <= 0 ) {
			return nextAreaNum;
		}

		float maxDrop = settings.maxStepHeight;
		if ( travelFlags & TFL_WALKOFFBARRIER ) {
			maxDrop = Max( maxDrop, settings.maxBarrierHeight );
		}
		if ( travelFlags & TFL_WALKOFFLEDGE ) {
			maxDrop = Max( maxDrop, settings.maxFallHeight );
		}

		float nextFloorHeight;
		if ( AreaBoundaryHeight( nextAreaNum, crossPoint.x, crossPoint.y, FLOORTRACE_ALIGN_DISTANCE, nextFloorHeight ) ) {
			const float deltaHeight = nextFloorHeight - crossPoint.z;
			if ( deltaHeight > maxRise + FLOORTRACE_HEIGHT_EPSILON || -deltaHeight > maxDrop + FLOORTRACE_HEIGHT_EPSILON ) {
				return 0;
			}
		} else if ( !( travelFlags & TFL_WALKOFFLEDGE ) ) {
			// the boundary of the area below is elsewhere: a drop of unknown height
			return 0;
		}
	}

	if ( nextAreaNum == curAreaNum ) {
		return curAreaNum;
	}
	// the compiled reachabilities decide whether this move is allowed with the given travel flags
	if ( !AreaTravelAllowed( nextAreaNum, travelFlags ) || !ReachabilityAllowed( curAreaNum, nextAreaNum, travelFlags ) ) {
		return 0;
	}
	return nextAreaNum;
}

/*
================
idAASFileLocal::TraceFloor

Traces along the floor from start to end, seen from above, walking from area polygon to area polygon.
travelFlags is the set of ALLOWED travel types ( e.g. TFL_VALID_WALK_GDF ), as used by the routing code: the
trace stops at walls ( unless TFL_BARRIERJUMP is allowed and the barrier is low enough ), at steps higher than
maxStepHeight, at drops deeper than maxStepHeight ( unless TFL_WALKOFFBARRIER / TFL_WALKOFFLEDGE are allowed )
and when entering an area or using a reachability that requires a travel type that is not allowed.
Returns true if the end was reached. lastAreaNum is the area the trace ended in.
================
*/
bool idAASFileLocal::TraceFloor( aasTraceFloor_t &trace, const idVec3 &start, int startAreaNum, const idVec3 &end, int endAreaNum, int travelFlags ) const {
	const int		FLOORTRACE_MAX_AREAS		= 1024;	// safety limit on the number of areas one trace may cross
	const double	FLOORTRACE_EDGE_EPSILON		= 0.1;	// a crossing may lie this far past the end of an edge
	const double	FLOORTRACE_LINE_OFFSET		= 0.01;	// keeps the line off the ( mostly integer ) vertex coordinates
	const float		FLOORTRACE_START_EPSILON	= 1.0f;	// the start may lie this far outside the start area
	const float		FLOORTRACE_MAX_START_OFFSET	= 64.0f;
	const float		FLOORTRACE_END_EPSILON		= 1.0f;	// the end may lie this far outside the area the trace ends in

	trace.fraction = 0.0f;
	trace.endpos = start;
	trace.lastAreaNum = 0;
	trace.lastEdgeNum = 0;

	if ( nodes.Num() <= 1 || edgeAreas.Num() != edges.Num() * 2 ) {
		return false;
	}

	int curAreaNum = ( startAreaNum > 0 && startAreaNum < areas.Num() ) ? startAreaNum : PointAreaNum( start );
	if ( !AreaTravelAllowed( curAreaNum, travelFlags ) ) {
		return false;
	}

	const double dx = ( double )end.x - ( double )start.x;
	const double dy = ( double )end.y - ( double )start.y;
	const double length = sqrt( dx * dx + dy * dy );
	if ( length < 0.01 ) {
		trace.fraction = 1.0f;
		trace.endpos = end;
		trace.lastAreaNum = curAreaNum;
		return true;
	}
	const float dirX = ( float )( dx / length );
	const float dirY = ( float )( dy / length );
	const double startX = start.x - dirY * FLOORTRACE_LINE_OFFSET;
	const double startY = start.y + dirX * FLOORTRACE_LINE_OFFSET;

	// the start can lie a little outside its area, e.g. an origin against a wall
	float startOffset = AreaPolygonDistance2D( curAreaNum, start.x, start.y );
	if ( startOffset > FLOORTRACE_START_EPSILON ) {
		const int pointAreaNum = PointAreaNum( start );
		if ( pointAreaNum != curAreaNum && AreaTravelAllowed( pointAreaNum, travelFlags ) && AreaPolygonDistance2D( pointAreaNum, start.x, start.y ) <= FLOORTRACE_START_EPSILON ) {
			curAreaNum = pointAreaNum;
			startOffset = 0.0f;
		} else if ( startOffset > FLOORTRACE_MAX_START_OFFSET ) {
			return false;
		}
	}

	double minFraction = -( Max( startOffset, FLOORTRACE_START_EPSILON ) + 0.1 ) / length;
	double lastFraction = 0.0;
	float lastFloorHeight = start.z;
	int lastEdgeNum = 0;

	for ( int i = 0; i < FLOORTRACE_MAX_AREAS; i++ ) {
		trace.lastAreaNum = curAreaNum;

		// find where the line leaves the current area: the first edge it crosses from the inside ( right )
		// to the outside ( left ) of the clockwise polygon
		const aasArea_t &area = areas[ curAreaNum ];
		int exitEdgeNum = 0;
		double exitFraction = 0.0;
		float exitEdgeFraction = 0.0f;
		bool exitShared = false;
		for ( int j = 0; j < area.numEdges; j++ ) {
			const int edgeNum = edgeIndex[ area.firstEdge + j ];
			const idVec3 *v0, *v1;
			GetAreaEdge( edgeNum, v0, v1 );

			const double ex = ( double )v1->x - ( double )v0->x;
			const double ey = ( double )v1->y - ( double )v0->y;
			const double edgeLengthSqr = ex * ex + ey * ey;
			if ( edgeLengthSqr < 1e-6 ) {
				continue;	// vertical edge
			}
			const double denom = ex * dy - ey * dx;
			if ( denom <= 0.0 ) {
				continue;	// the line does not leave the area through this edge
			}
			const double f = ( ey * ( startX - v0->x ) - ex * ( startY - v0->y ) ) / denom;
			if ( f < minFraction ) {
				continue;
			}
			const double crossX = startX + f * dx;
			const double crossY = startY + f * dy;
			const double u = ( ( crossX - v0->x ) * ex + ( crossY - v0->y ) * ey ) / edgeLengthSqr;
			const double uEpsilon = FLOORTRACE_EDGE_EPSILON / sqrt( edgeLengthSqr );
			if ( u < -uEpsilon || u > 1.0 + uEpsilon ) {
				continue;
			}
			const bool shared = edgeAreas[ abs( edgeNum ) * 2 + 1 ] > 0;
			// on a tie ( the line passes through a vertex ) prefer the edge that leads straight into a neighbour
			if ( exitEdgeNum == 0 || f < exitFraction || ( f == exitFraction && shared && !exitShared ) ) {
				exitEdgeNum = edgeNum;
				exitFraction = f;
				exitEdgeFraction = idMath::ClampFloat( 0.0f, 1.0f, ( float )u );
				exitShared = shared;
			}
		}

		const double endEpsilon = ( curAreaNum == endAreaNum ) ? FLOORTRACE_END_EPSILON / length : 0.0;
		if ( exitEdgeNum == 0 || exitFraction >= 1.0 - endEpsilon ) {
			if ( AreaPolygonDistance2D( curAreaNum, end.x, end.y ) <= FLOORTRACE_END_EPSILON ) {
				trace.fraction = 1.0f;
				trace.endpos = end;
				trace.lastEdgeNum = lastEdgeNum;
				return true;
			}
			// the line was lost ( degenerate area polygon ): report the trace as blocked where it entered this area
			trace.fraction = ( float )lastFraction;
			trace.endpos.Set( ( float )( startX + lastFraction * dx ), ( float )( startY + lastFraction * dy ), lastFloorHeight );
			trace.lastEdgeNum = lastEdgeNum;
			return false;
		}

		exitFraction = Max( exitFraction, lastFraction );
		const idVec3 *v0, *v1;
		GetAreaEdge( exitEdgeNum, v0, v1 );
		const idVec3 crossPoint( ( float )( startX + exitFraction * dx ), ( float )( startY + exitFraction * dy ), v0->z + ( v1->z - v0->z ) * exitEdgeFraction );

		const int nextAreaNum = FloorTraceNextArea( curAreaNum, exitEdgeNum, crossPoint, dirX, dirY, travelFlags );
		if ( nextAreaNum == curAreaNum ) {
			// the other side is still this area ( the area polygon folds over a vertical edge ), keep walking it
			minFraction = exitFraction + 1e-7;
			lastFraction = exitFraction;
			continue;
		}
		if ( nextAreaNum <= 0 ) {
			trace.fraction = ( float )exitFraction;
			trace.endpos = crossPoint;
			trace.lastEdgeNum = exitEdgeNum;
			return false;
		}

		minFraction = exitFraction - 1e-7;
		lastFraction = exitFraction;
		lastFloorHeight = crossPoint.z;
		lastEdgeNum = exitEdgeNum;
		curAreaNum = nextAreaNum;
	}

	// crossed too many areas, report the trace as blocked
	trace.fraction = ( float )lastFraction;
	trace.endpos.Set( ( float )( startX + lastFraction * dx ), ( float )( startY + lastFraction * dy ), lastFloorHeight );
	trace.lastEdgeNum = lastEdgeNum;
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
