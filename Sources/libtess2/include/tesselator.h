/*
** SGI FREE SOFTWARE LICENSE B (Version 2.0, Sept. 18, 2008) 
** Copyright (C) [dates of first publication] Silicon Graphics, Inc.
** All Rights Reserved.
**
** Permission is hereby granted, free of charge, to any person obtaining a copy
** of this software and associated documentation files (the "Software"), to deal
** in the Software without restriction, including without limitation the rights
** to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
** of the Software, and to permit persons to whom the Software is furnished to do so,
** subject to the following conditions:
** 
** The above copyright notice including the dates of first publication and either this
** permission notice or a reference to http://oss.sgi.com/projects/FreeB/ shall be
** included in all copies or substantial portions of the Software. 
**
** THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
** INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
** PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL SILICON GRAPHICS, INC.
** BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
** TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE
** OR OTHER DEALINGS IN THE SOFTWARE.
** 
** Except as contained in this notice, the name of Silicon Graphics, Inc. shall not
** be used in advertising or otherwise to promote the sale, use or other dealings in
** this Software without prior written authorization from Silicon Graphics, Inc.
*/
/*
** Author: Mikko Mononen, July 2009.
*/

#import <stdlib.h>
#import <stdbool.h>
#import "objc-clang.h"

NS_ASSUME_NONNULL_BEGIN

#ifndef TESSELATOR_H
#define TESSELATOR_H

// to support interpolation of some extra vectors.  NHP 1/31/22
#define MAX_DIMENSIONS 12

#ifdef __cplusplus
extern "C" {
#endif
    
/// See OpenGL Red Book for description of the winding rules
/// http://www.glprogramming.com/red/chapter11.html
enum TessWindingRule
{
    TESS_WINDING_ODD,
    TESS_WINDING_NONZERO,
    TESS_WINDING_POSITIVE,
    TESS_WINDING_NEGATIVE,
    TESS_WINDING_ABS_GEQ_TWO,
};

/// The contents of the tessGetElements() depends on element type being passed to tessTesselate().
/// Tesselation result element types:
///
/// \par TESS_POLYGONS
///
///   Each element in the element array is polygon defined as 'polySize' number of vertex indices.
///   If a polygon has than 'polySize' vertices, the remaining indices are stored as TESS_UNDEF.
///   Example, drawing a polygon:
///
/// \code
/// const int nelems = tessGetElementCount(tess);
/// const TESSindex* elems = tessGetElements(tess);
/// for (int i = 0; i < nelems; i++) {
///     const TESSindex* poly = &elems[i * polySize];
///     glBegin(GL_POLYGON);
///     for (int j = 0; j < polySize; j++) {
///         if (poly[j] == TESS_UNDEF) break;
///         glVertex2fv(&verts[poly[j]*vertexSize]);
///     }
///     glEnd();
/// }
/// \endcode
///
/// \par TESS_CONNECTED_POLYGONS
///
///   Each element in the element array is polygon defined as 'polySize' number of vertex indices,
///   followed by 'polySize' indices to neighour polygons, that is each element is 'polySize' * 2 indices.
///   If a polygon has than 'polySize' vertices, the remaining indices are stored as TESS_UNDEF.
///   If a polygon edge is a boundary, that is, not connected to another polygon, the neighbour index is TESS_UNDEF.
///   Example, flood fill based on seed polygon:
///
/// \code
/// const int nelems = tessGetElementCount(tess);
/// const TESSindex* elems = tessGetElements(tess);
/// unsigned char* visited = (unsigned char*)calloc(nelems);
/// TESSindex stack[50];
/// int nstack = 0;
/// stack[nstack++] = seedPoly;
/// visited[startPoly] = 1;
/// while (nstack > 0) {
///     TESSindex idx = stack[--nstack];
///        const TESSindex* poly = &elems[idx * polySize * 2];
///        const TESSindex* nei = &poly[polySize];
///      for (int i = 0; i < polySize; i++) {
///          if (poly[i] == TESS_UNDEF) break;
///          if (nei[i] != TESS_UNDEF && !visited[nei[i]])
///                stack[nstack++] = nei[i];
///              visited[nei[i]] = 1;
///          }
///      }
/// }
/// \endcode
///
/// \par TESS_BOUNDARY_CONTOURS
///
///   Each element in the element array is [base index, count] pair defining a range of vertices for a contour.
///   The first value is index to first vertex in contour and the second value is number of vertices in the contour.
///   Example, drawing contours:
///
/// \code
/// const int nelems = tessGetElementCount(tess);
/// const TESSindex* elems = tessGetElements(tess);
/// for (int i = 0; i < nelems; i++) {
///     const TESSindex base = elems[i * 2];
///     const TESSindex count = elems[i * 2 + 1];
///     glBegin(GL_LINE_LOOP);
///     for (int j = 0; j < count; j++) {
///         glVertex2fv(&verts[(base+j) * vertexSize]);
///     }
///     glEnd();
/// }
/// \endcode
///
enum TessElementType
{
    TESS_POLYGONS,
    TESS_CONNECTED_POLYGONS,
    TESS_BOUNDARY_CONTOURS,
};
    
typedef float TESSreal;
typedef int TESSindex;

typedef struct TESStesselator TESStesselator;
typedef struct TESSalloc TESSalloc;

#define TESS_UNDEF (~(TESSindex)0)

#define TESS_NOTUSED(v) do { (void)(1 ? (void)0 : ( (void)(v) ) ); } while(0)

/// Custom memory allocator interface.
/// The internal memory allocator allocates mesh edges, vertices and faces
/// as well as dictionary nodes and active regions in buckets and uses simple
/// freelist to speed up the allocation. The bucket size should roughly match your
/// expected input data. For example if you process only hundreds of vertices,
/// a bucket size of 128 might be ok, where as when processing thousands of vertices
/// bucket size of 1024 might be approproate. The bucket size is a compromise between
/// how often to allocate memory from the system versus how much extra space the system
/// should allocate. Reasonable defaults are show in commects below, they will be used if
/// the bucket sizes are zero.
///
/// The use may left the memrealloc to be null. In that case, the tesselator will not try to
/// dynamically grow int's internal arrays. The tesselator only needs the reallocation when it
/// has found intersecting segments and needs to add new vertex. This defency can be cured by
/// allocating some extra vertices beforehand. The 'extraVertices' variable allows to specify
/// number of expected extra vertices.
struct TESSalloc
{
    // Can return null, to indicate allocation failure
    void *_Nullable(*_Nonnull memalloc)( void *_Nullable userData, size_t size );
    void *_Nonnull(*_Nullable memrealloc)( void *_Nullable userData, void*_Nonnull ptr, size_t size );
    void (*_Nonnull memfree)( void *_Nullable userData, void *_Nonnull ptr );
    void*_Nullable userData;    // User data passed to the allocator functions.
    int meshEdgeBucketSize;		// 512
    int meshVertexBucketSize;	// 512
    int meshFaceBucketSize;		// 256
    int dictNodeBucketSize;		// 512
    int regionBucketSize;		// 256
    int extraVertices;			// Number of extra vertices allocated for the priority queue.
};

/// tessNewTess() - Creates a new tesselator.
/// Use tessDeleteTess() to delete the tesselator.
/// Parameters:
/// @param alloc pointer to a filled TESSalloc struct or NULL to use default malloc based allocator.
/// @returns new tesselator object.
SWIFT_COMPILE_NAME("Tesselator.create(allocator:)")
TESStesselator*_Nullable tessNewTess( TESSalloc*_Nullable alloc );

/// tessDeleteTess() - Deletes a tesselator.
/// Parameters:
/// @param tess pointer to tesselator object to be deleted.
SWIFT_COMPILE_NAME("Tesselator.destroy(self:)")
void tessDeleteTess( TESStesselator *_Nonnull tess );

/// tessAddContour() - Adds a contour to be tesselated.
/// The type of the vertex coordinates is assumed to be TESSreal.
/// Parameters:
/// @param tess pointer to tesselator object.
/// @param size number of coordinates per vertex. Must be 2 or 3.
/// @param pointer pointer to the first coordinate of the first vertex in the array.
/// @param stride defines offset in bytes between consecutive vertices.
/// @param count number of vertices in contour.
SWIFT_COMPILE_NAME("Tesselator.addContour(self:size:pointer:stride:count:)")
void tessAddContour( TESStesselator *_Nonnull tess, int size, const void*_Nonnull pointer, int stride, int count );

/// tessTesselate() - tesselate contours.
/// Parameters:
/// @param tess
///     pointer to tesselator object.
/// @param windingRule
///     winding rules used for tesselation, must be one of TessWindingRule.
/// @param elementType
///     defines the tesselation result element type, must be one of TessElementType.
/// @param polySize
///     defines maximum vertices per polygons if output is polygons.
/// @param vertexSize
///     defines the number of coordinates in tesselation result vertex, must be 2 or 3.
/// @param normal
///     defines the normal of the input contours, of null the normal is calculated automatically.
/// @returns 1 if succeed, 0 if failed.
SWIFT_COMPILE_NAME("Tesselator.tesselate(self:windingRule:elementType:polySize:vertexSize:normal:)")
int tessTesselate( TESStesselator *_Nonnull tess, int windingRule, int elementType, int polySize, int vertexSize, const TESSreal*_Nullable normal );

/// tessGetVertexCount() - Returns number of vertices in the tesselated output.
int tessGetVertexCount( TESStesselator *_Nonnull tess );

/// tessGetVertices() - Returns pointer to first coordinate of first vertex.
const TESSreal*_Nonnull tessGetVertices( TESStesselator *_Nonnull tess );

/// tessGetVertexIndices() - Returns pointer to first vertex index.
/// Vertex indices can be used to map the generated vertices to the original vertices.
/// Every point added using tessAddContour() will get a new index starting at 0.
/// New vertices generated at the intersections of segments are assigned value TESS_UNDEF.
const TESSindex*_Nonnull tessGetVertexIndices( TESStesselator *_Nonnull tess );

/// tessGetElementCount() - Returns number of elements in the the tesselated output.
int tessGetElementCount( TESStesselator *_Nonnull tess );

/// tessGetElements() - Returns pointer to the first element.
const TESSindex*_Nonnull tessGetElements( TESStesselator *_Nonnull tess );

/// tessGetNoEmptyPolygons() - Returns whether a tesselator is set to not output empty polygons in the output.
bool tessGetNoEmptyPolygons( TESStesselator *_Nonnull tess );

/// tessSetNoEmptyPoltgons() - Sets whether a tesselator should disallow empty polygons in the output.
/// Default is FALSE.
void tessSetNoEmptyPolygons( TESStesselator *_Nonnull tess, bool value );
    
#ifdef __cplusplus
};
#endif

#endif // TESSELATOR_H

NS_ASSUME_NONNULL_END
