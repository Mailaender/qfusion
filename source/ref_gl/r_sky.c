/*
Copyright (C) 1999 Stephen C. Taylor
Copyright (C) 2002-2007 Victor Luchits

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

// r_sky.c

#include "r_local.h"


#define SIDE_SIZE   9
#define POINTS_LEN  ( SIDE_SIZE*SIDE_SIZE )
#define ELEM_LEN    ( ( SIDE_SIZE-1 )*( SIDE_SIZE-1 )*6 )

#define SPHERE_RAD  10.0
#define EYE_RAD     9.0

#define SCALE_S	    4.0  // Arbitrary (?) texture scaling factors
#define SCALE_T	    4.0

#define BOX_SIZE    1.0f
#define BOX_STEP    BOX_SIZE / ( SIDE_SIZE-1 ) * 2.0f

#define SKYDOME_VATTRIBS ( VATTRIB_TEXCOORDS_BIT | VATTRIB_NORMAL_BIT )

typedef struct visSkySide_s
{
	int index;
	int firstVert, numVerts;
	int firstElem, numElems;
} visSkySide_t;

typedef struct skydome_s
{
	mesh_t				*meshes;
	vec2_t				*sphereStCoords[6];
	mesh_vbo_t 			*sphereVbos[6];
	vec2_t				*linearStCoords[6];
	mesh_vbo_t 			*linearVbos[6];
} skydome_t;

static void Gen_BoxSide( skydome_t *skydome, int side, vec3_t orig, vec3_t drow, vec3_t dcol );
static void Gen_Box( skydome_t *skydome );

static void MakeSkyVec( float x, float y, float z, int axis, vec3_t v );

static drawSurfaceType_t r_skySurf = ST_SKY;

/*
* R_CreateSkydome
*/
skydome_t *R_CreateSkydome( model_t *model )
{
	int i, size;
	mesh_t *mesh;
	skydome_t *skydome;
	qbyte *buffer;

	size = sizeof( skydome_t ) + sizeof( mesh_t ) * 6 +
		sizeof( elem_t ) * ELEM_LEN * 6 +
		sizeof( vec3_t ) * POINTS_LEN * 6 + sizeof( vec3_t ) * POINTS_LEN * 6 +
		sizeof( vec2_t ) * POINTS_LEN * 11;
	buffer = Mod_Malloc( model, size );

	skydome = ( skydome_t * )buffer; buffer += sizeof( skydome_t );
	skydome->meshes = ( mesh_t * )buffer; buffer += sizeof( mesh_t ) * 6;

	for( i = 0, mesh = skydome->meshes; i < 6; i++, mesh++ )
	{
		mesh->numElems = ELEM_LEN;
		mesh->elems = ( elem_t * )buffer; buffer += sizeof( elem_t ) * ELEM_LEN;

		mesh->numVerts = POINTS_LEN;
		mesh->xyzArray = ( vec3_t * )buffer; buffer += sizeof( vec3_t ) * POINTS_LEN;
		mesh->normalsArray = ( vec3_t * )buffer; buffer += sizeof( vec3_t ) * POINTS_LEN;

		if( i != 5 ) {
			skydome->sphereStCoords[i] = ( vec2_t * )buffer; buffer += sizeof( vec2_t ) * POINTS_LEN;
			skydome->sphereVbos[i] = R_CreateMeshVBO( mesh, mesh->numVerts, mesh->numElems, 0,
				SKYDOME_VATTRIBS, VBO_TAG_WORLD );
		}

		skydome->linearStCoords[i] = ( vec2_t * )buffer; buffer += sizeof( vec2_t ) * POINTS_LEN;
		skydome->linearVbos[i] = R_CreateMeshVBO( mesh, mesh->numVerts, mesh->numElems, 0,
			SKYDOME_VATTRIBS, VBO_TAG_WORLD );
	}

	Gen_Box( skydome );

	return skydome;
}

/*
* R_TouchSkydome
*/
void R_TouchSkydome( skydome_t *skydome )
{
	int i;
	mesh_t *mesh;

	for( i = 0, mesh = skydome->meshes; i < 6; i++, mesh++ )
	{
		if( skydome->sphereVbos[i] )
			R_TouchMeshVBO( skydome->sphereVbos[i] );
		if( skydome->linearVbos[i] )
			R_TouchMeshVBO( skydome->linearVbos[i] );
	}
}

/*
* Gen_Box
*/
static void Gen_Box( skydome_t *skydome )
{
	int axis;
	vec3_t orig, drow, dcol;

	for( axis = 0; axis < 6; axis++ )
	{
		MakeSkyVec( -BOX_SIZE, -BOX_SIZE, BOX_SIZE, axis, orig );
		MakeSkyVec( 0, BOX_STEP, 0, axis, drow );
		MakeSkyVec( BOX_STEP, 0, 0, axis, dcol );

		Gen_BoxSide( skydome, axis, orig, drow, dcol );
	}
}

/*
* Gen_BoxSide
* 
* I don't know exactly what Q3A does for skybox texturing, but
* this is at least fairly close.  We tile the texture onto the
* inside of a large sphere, and put the camera near the top of
* the sphere. We place the box around the camera, and cast rays
* through the box verts to the sphere to find the texture coordinates.
*/
static void Gen_BoxSide( skydome_t *skydome, int side, vec3_t orig, vec3_t drow, vec3_t dcol )
{
	vec3_t pos, w, row, norm;
	float *v, *n, *st = NULL, *st2;
	int r, c;
	float t, d, d2, b, b2, q[2], s;
	elem_t *elem;

	s = 1.0 / ( SIDE_SIZE-1 );
	d = EYE_RAD; // sphere center to camera distance
	d2 = d * d;
	b = SPHERE_RAD; // sphere radius
	b2 = b * b;
	q[0] = 1.0 / ( 2.0 * SCALE_S );
	q[1] = 1.0 / ( 2.0 * SCALE_T );

	v = skydome->meshes[side].xyzArray[0];
	n = skydome->meshes[side].normalsArray[0];
	if( side != 5 )
		st = skydome->sphereStCoords[side][0];
	st2 = skydome->linearStCoords[side][0];

	VectorCopy( orig, row );

//	CrossProduct( dcol, drow, norm );
//	VectorNormalize( norm );
	VectorClear( norm );

	for( r = 0; r < SIDE_SIZE; r++ )
	{
		VectorCopy( row, pos );
		for( c = 0; c < SIDE_SIZE; c++ )
		{
			// pos points from eye to vertex on box
			VectorScale( pos, 1, v );
			VectorCopy( pos, w );

			// Normalize pos -> w
			VectorNormalize( w );

			// Find distance along w to sphere
			t = sqrt( d2 * ( w[2] * w[2] - 1.0 ) + b2 ) - d * w[2];
			w[0] *= t;
			w[1] *= t;

			if( st )
			{
				// use x and y on sphere as s and t
				// minus is here so skies scoll in correct (Q3A's) direction
				st[0] = -w[0] * q[0];
				st[1] = -w[1] * q[1];

				// avoid bilerp seam
				st[0] = ( bound( -1, st[0], 1 ) + 1.0 ) * 0.5;
				st[1] = ( bound( -1, st[1], 1 ) + 1.0 ) * 0.5;
			}

			st2[0] = c * s;
			st2[1] = 1.0 - r * s;

			VectorAdd( pos, dcol, pos );
			VectorCopy( norm, n );

			v += 3;
			n += 3;
			if( st ) st += 2;
			st2 += 2;
		}

		VectorAdd( row, drow, row );
	}

	// elements in tristrip order
	elem = skydome->meshes[side].elems;
	for( r = 0; r < SIDE_SIZE - 1; r++ )
	{
		for( c = 0; c < SIDE_SIZE - 1; c++ )
		{
			elem[0] = r * SIDE_SIZE + c;
			elem[1] = elem[4] = elem[0] + SIDE_SIZE;
			elem[2] = elem[3] = elem[0] + 1;
			elem[5] = elem[1] + 1;
			elem += 6;
		}
	}
	
	// upload two static VBO's for each side except for the bottom one
	// which only has 1 side for skybox
	if( side != 5 ) {
		skydome->meshes[side].stArray = skydome->sphereStCoords[side];
		R_UploadVBOVertexData( skydome->sphereVbos[side], 0, SKYDOME_VATTRIBS, &skydome->meshes[side], VBO_HINT_NONE );
		R_UploadVBOElemData( skydome->sphereVbos[side], 0, 0, &skydome->meshes[side], VBO_HINT_NONE );
	}

	skydome->meshes[side].stArray = skydome->linearStCoords[side];
	R_UploadVBOVertexData( skydome->linearVbos[side], 0, SKYDOME_VATTRIBS, &skydome->meshes[side], VBO_HINT_NONE );
	R_UploadVBOElemData( skydome->linearVbos[side], 0, 0, &skydome->meshes[side], VBO_HINT_NONE );
}

/*
* R_DrawSkyBoxSide
*/
static void R_DrawSkyBoxSide( const skydome_t *skydome, const visSkySide_t *visSide, const shader_t *shader, 
	int imageIndex )
{
	int side = visSide->index;

	if( ri.skyMins[0][side] >= ri.skyMaxs[0][side] ||
		ri.skyMins[1][side] >= ri.skyMaxs[1][side] )
		return;

	RB_BindShader( rsc.worldent, rf.skyShader, ri.skyFog );

	RB_BindVBO( skydome->linearVbos[side]->index, GL_TRIANGLES );

	RB_SetSkyboxShader( shader );

	RB_SetSkyboxSide( imageIndex );

	RB_DrawElements( visSide->firstVert, visSide->numVerts, visSide->firstElem, visSide->numElems );
}

/*
* R_DrawSkyBox
*/
static void R_DrawSkyBox( const skydome_t *skydome, const visSkySide_t *visSides, const shader_t *shader )
{
	int i;
	const int skytexorder[6] = { SKYBOX_RIGHT, SKYBOX_FRONT, SKYBOX_LEFT, SKYBOX_BACK, SKYBOX_TOP, SKYBOX_BOTTOM };

	for( i = 0; i < 6; i++ )
		R_DrawSkyBoxSide( skydome, visSides + i, shader, skytexorder[i] );
}

/*
* R_DrawBlackBottom
* 
* Draw dummy skybox side to prevent the HOM effect
*/
static void R_DrawBlackBottom( const skydome_t *skydome, const visSkySide_t *visSides )
{
	int side = 5;
	const visSkySide_t *visSide = visSides + side;

	if( ri.skyMins[0][side] >= ri.skyMaxs[0][side] ||
		ri.skyMins[1][side] >= ri.skyMaxs[1][side] )
		return;

	RB_BindShader( rsc.worldent, rf.envShader, ri.skyFog );

	RB_BindVBO( skydome->linearVbos[side]->index, GL_TRIANGLES );

	RB_DrawElements( visSide->firstVert, visSide->numVerts, visSide->firstElem, visSide->numElems );
}

/*
* R_DrawSkySurf
*/
qboolean R_DrawSkySurf( const entity_t *e, const shader_t *shader, const mfog_t *fog, drawSurfaceBSP_t *drawSurf )
{
	int i;
	int numVisSides;
	visSkySide_t visSkySides[6];
	vec3_t mins, maxs;
	int umin, umax, vmin, vmax;
	entity_t skyent;
	mat4_t projectionMatrix;
	mat4_t oldObjectMatrix, oldProjectionMatrix, oldModelviewMatrix;
	refdef_t *rd = &ri.refdef;
	skydome_t *skydome = r_worldbrushmodel->skydome;

	if( !skydome )
		return qfalse;

	numVisSides = 0;
	ClearBounds( mins, maxs );

	memset( visSkySides, 0, sizeof( visSkySides ) );

	for( i = 0; i < 6; i++ )
	{
		if( ri.skyMins[0][i] >= ri.skyMaxs[0][i] ||
			ri.skyMins[1][i] >= ri.skyMaxs[1][i] )
			continue;

		// increase the visible sides counter
		numVisSides++;

		umin = (int)( ( ri.skyMins[0][i]+1.0f )*0.5f*(float)( SIDE_SIZE-1 ) );
		umax = (int)( ( ri.skyMaxs[0][i]+1.0f )*0.5f*(float)( SIDE_SIZE-1 ) ) + 1;
		vmin = (int)( ( ri.skyMins[1][i]+1.0f )*0.5f*(float)( SIDE_SIZE-1 ) );
		vmax = (int)( ( ri.skyMaxs[1][i]+1.0f )*0.5f*(float)( SIDE_SIZE-1 ) ) + 1;

		clamp( umin, 0, SIDE_SIZE-1 );
		clamp( umax, 0, SIDE_SIZE-1 );
		clamp( vmin, 0, SIDE_SIZE-1 );
		clamp( vmax, 0, SIDE_SIZE-1 );

		visSkySides[i].index = i;
		visSkySides[i].firstVert = vmin * SIDE_SIZE + umin;
		visSkySides[i].numVerts = (vmax - vmin) * SIDE_SIZE + (umax - umin);
		visSkySides[i].firstElem = (vmin * (SIDE_SIZE-1) + umin) * 6;
		visSkySides[i].numElems = ((vmax - vmin) * (SIDE_SIZE-1) + (umax - umin)) * 6;

		AddPointToBounds( skydome->meshes[i].xyzArray[vmin*SIDE_SIZE+umin], mins, maxs );
		AddPointToBounds( skydome->meshes[i].xyzArray[vmax*SIDE_SIZE+umax], mins, maxs );

		skydome->meshes[i].numElems = visSkySides[i].numElems;
	}

	// no sides are truly visible, ignore
	if( !numVisSides )
		return qfalse;

	VectorAdd( mins, ri.viewOrigin, mins );
	VectorAdd( maxs, ri.viewOrigin, maxs );

	if( rd->rdflags & RDF_SKYPORTALINVIEW ) {
		R_DrawSkyPortal( e, &rd->skyportal, mins, maxs );
		return qfalse;
	}

	Matrix4_Copy( ri.objectMatrix, oldObjectMatrix );
	Matrix4_Copy( ri.projectionMatrix, oldProjectionMatrix );
	Matrix4_Copy( ri.modelviewMatrix, oldModelviewMatrix );

	if( rd->rdflags & RDF_USEORTHO ) {
		// FIXME?
	}
	else {
		Matrix4_InfinitePerspectiveProjection( rd->fov_x, rd->fov_y, 
			Z_NEAR, rf.cameraSeparation, projectionMatrix );

		RB_LoadProjectionMatrix( projectionMatrix );
	}

	// center skydome on camera to give the illusion of a larger space
	skyent = *rsc.worldent;
	skyent.scale = shader->skyHeight;
	VectorCopy( ri.viewOrigin, skyent.origin );
	R_TransformForEntity( &skyent );

	if( shader->skyboxImages[0] )
		R_DrawSkyBox( skydome, visSkySides, shader );
	else
		R_DrawBlackBottom( skydome, visSkySides );

	if( shader->numpasses )
	{
		RB_BindShader( rsc.worldent, shader, ri.skyFog );

		for( i = 0; i < 5; i++ )
		{
			const visSkySide_t *visSide = visSkySides + i;

			if( ri.skyMins[0][i] >= ri.skyMaxs[0][i] ||
				ri.skyMins[1][i] >= ri.skyMaxs[1][i] )
				continue;

			RB_BindVBO( skydome->sphereVbos[i]->index, GL_TRIANGLES );

			RB_DrawElements( visSide->firstVert, visSide->numVerts, visSide->firstElem, visSide->numElems );
		}
	}

	R_TransformForEntity( e );

	return qfalse;
}

//===================================================================

static const msurface_t *r_warpFace;
static qboolean	r_warpFaceVis;
static int r_warpFaceAxis;

vec3_t skyclip[6] = {
	{ 1, 1, 0 },
	{ 1, -1, 0 },
	{ 0, -1, 1 },
	{ 0, 1, 1 },
	{ 1, 0, 1 },
	{ -1, 0, 1 }
};

// 1 = s, 2 = t, 3 = 2048
int st_to_vec[6][3] =
{
	{ 3, -1, 2 },
	{ -3, 1, 2 },

	{ 1, 3, 2 },
	{ -1, -3, 2 },

	{ -2, -1, 3 },  // 0 degrees yaw, look straight up
	{ 2, -1, -3 }   // look straight down
};

// s = [0]/[2], t = [1]/[2]
int vec_to_st[6][3] =
{
	{ -2, 3, 1 },
	{ 2, 3, -1 },

	{ 1, 3, 2 },
	{ -1, 3, -2 },

	{ -2, -1, 3 },
	{ -2, 1, -3 }
};

/*
* DrawSkyPolygon
*/
static void DrawSkyPolygon( int nump, vec3_t vecs )
{
	int i, j;
	vec3_t v, av;
	float s, t, dv;
	int axis;
	float *vp;

	// decide which face it maps to
	VectorClear( v );

	for( i = 0, vp = vecs; i < nump; i++, vp += 3 )
		VectorAdd( vp, v, v );

	av[0] = fabs( v[0] );
	av[1] = fabs( v[1] );
	av[2] = fabs( v[2] );

	if( ( av[0] > av[1] ) && ( av[0] > av[2] ) )
		axis = ( v[0] < 0 ) ? 1 : 0;
	else if( ( av[1] > av[2] ) && ( av[1] > av[0] ) )
		axis = ( v[1] < 0 ) ? 3 : 2;
	else
		axis = ( v[2] < 0 ) ? 5 : 4;

	r_warpFaceVis = qtrue;
	r_warpFaceAxis = axis;

	// project new texture coords
	for( i = 0; i < nump; i++, vecs += 3 )
	{
		j = vec_to_st[axis][2];
		dv = ( j > 0 ) ? vecs[j - 1] : -vecs[-j - 1];

		if( dv < 0.001 )
			continue; // don't divide by zero

		dv = 1.0f / dv;

		j = vec_to_st[axis][0];
		s = ( j < 0 ) ? -vecs[-j -1] * dv : vecs[j-1] * dv;

		j = vec_to_st[axis][1];
		t = ( j < 0 ) ? -vecs[-j -1] * dv : vecs[j-1] * dv;

		if( s < ri.skyMins[0][axis] )
			ri.skyMins[0][axis] = s;
		if( t < ri.skyMins[1][axis] )
			ri.skyMins[1][axis] = t;
		if( s > ri.skyMaxs[0][axis] )
			ri.skyMaxs[0][axis] = s;
		if( t > ri.skyMaxs[1][axis] )
			ri.skyMaxs[1][axis] = t;
	}
}

#define	MAX_CLIP_VERTS	64

/*
* ClipSkyPolygon
*/
void ClipSkyPolygon( int nump, vec3_t vecs, int stage )
{
	float *norm;
	float *v;
	qboolean front, back;
	float d, e;
	float dists[MAX_CLIP_VERTS + 1];
	int sides[MAX_CLIP_VERTS + 1];
	vec3_t newv[2][MAX_CLIP_VERTS + 1];
	int newc[2];
	int i, j;

	if( nump > MAX_CLIP_VERTS )
		Com_Error( ERR_DROP, "ClipSkyPolygon: MAX_CLIP_VERTS" );

loc1:
	if( stage == 6 )
	{
		// fully clipped, so draw it
		DrawSkyPolygon( nump, vecs );
		return;
	}

	front = back = qfalse;
	norm = skyclip[stage];
	for( i = 0, v = vecs; i < nump; i++, v += 3 )
	{
		d = DotProduct( v, norm );
		if( d > ON_EPSILON )
		{
			front = qtrue;
			sides[i] = SIDE_FRONT;
		}
		else if( d < -ON_EPSILON )
		{
			back = qtrue;
			sides[i] = SIDE_BACK;
		}
		else
		{
			sides[i] = SIDE_ON;
		}
		dists[i] = d;
	}

	if( !front || !back )
	{	// not clipped
		stage++;
		goto loc1;
	}

	// clip it
	sides[i] = sides[0];
	dists[i] = dists[0];
	VectorCopy( vecs, ( vecs+( i*3 ) ) );
	newc[0] = newc[1] = 0;

	for( i = 0, v = vecs; i < nump; i++, v += 3 )
	{
		switch( sides[i] )
		{
		case SIDE_FRONT:
			VectorCopy( v, newv[0][newc[0]] );
			newc[0]++;
			break;
		case SIDE_BACK:
			VectorCopy( v, newv[1][newc[1]] );
			newc[1]++;
			break;
		case SIDE_ON:
			VectorCopy( v, newv[0][newc[0]] );
			newc[0]++;
			VectorCopy( v, newv[1][newc[1]] );
			newc[1]++;
			break;
		}

		if( sides[i] == SIDE_ON || sides[i+1] == SIDE_ON || sides[i+1] == sides[i] )
			continue;

		d = dists[i] / ( dists[i] - dists[i+1] );
		for( j = 0; j < 3; j++ )
		{
			e = v[j] + d * ( v[j+3] - v[j] );
			newv[0][newc[0]][j] = e;
			newv[1][newc[1]][j] = e;
		}
		newc[0]++;
		newc[1]++;
	}

	// continue
	ClipSkyPolygon( newc[0], newv[0][0], stage + 1 );
	ClipSkyPolygon( newc[1], newv[1][0], stage + 1 );
}

/*
* R_AddSkyToDrawList
*/
void R_AddSkyToDrawList( const msurface_t *fa )
{
	int i;
	vec3_t *vert;
	elem_t	*elem;
	mesh_t *mesh;
	vec3_t verts[4];

	// calculate vertex values for sky box
	r_warpFace = fa;
	r_warpFaceVis = qfalse;

	mesh = fa->mesh;
	elem = mesh->elems;
	vert = mesh->xyzArray;
	for( i = 0; i < mesh->numElems; i += 3, elem += 3 )
	{
		VectorSubtract( vert[elem[0]], ri.viewOrigin, verts[0] );
		VectorSubtract( vert[elem[1]], ri.viewOrigin, verts[1] );
		VectorSubtract( vert[elem[2]], ri.viewOrigin, verts[2] );
		ClipSkyPolygon( 3, verts[0], 0 );
	}

	if( r_warpFaceVis ) {
		if( fa->fog ) {
			ri.skyFog = fa->fog;
		} else if( r_worldbrushmodel->globalfog ) {
			ri.skyFog = r_worldbrushmodel->globalfog;
		}

		// there should be only one sky drawSurf in the list
		if( !ri.skyShader ) {
			ri.skyShader = fa->shader;
			R_AddDSurfToDrawList( rsc.worldent, NULL, fa->shader, 0, r_warpFaceAxis, NULL, &r_skySurf );
		}
	}
}

/*
* R_ClearSky
*/
void R_ClearSky( void )
{
	int i;

	ri.skyFog = NULL;
	ri.skyShader = NULL;
	for( i = 0; i < 6; i++ )
	{
		ri.skyMins[0][i] = ri.skyMins[1][i] = 9999999;
		ri.skyMaxs[0][i] = ri.skyMaxs[1][i] = -9999999;
	}
}

static void MakeSkyVec( float x, float y, float z, int axis, vec3_t v )
{
	int j, k;
	vec3_t b;

	b[0] = x;
	b[1] = y;
	b[2] = z;

	for( j = 0; j < 3; j++ )
	{
		k = st_to_vec[axis][j];
		if( k < 0 )
			v[j] = -b[-k - 1];
		else
			v[j] = b[k - 1];
	}
}
