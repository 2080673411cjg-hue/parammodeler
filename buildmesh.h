/***************************************************************************
  buildmesh.h
  Mesh Construction Header
  -------------------
         begin                : Mar. 2026
         copyright            : (C) 2026 by Chai
         email                : 2080673411@qq.com
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/
#ifndef BUILDMESH_H
#define BUILDMESH_H

#include <QString>
#include "meshdata.h"

class ParamModelerDock;

class BuildMesh
{
public:
    static MeshData build( const QString &primitiveType, ParamModelerDock *dock );

    static MeshData buildCuboid( ParamModelerDock *dock );
    static MeshData buildCylinder( ParamModelerDock *dock );
    static MeshData buildLHouse( ParamModelerDock *dock );
    static MeshData buildConeCylinder( ParamModelerDock *dock );
    static MeshData buildGabledRoof( ParamModelerDock *dock );
    static MeshData buildPyramidRoof( ParamModelerDock *dock );
    static MeshData buildTruncatedPyramidRoof( ParamModelerDock *dock );
    static MeshData buildHalfCylinderRoof( ParamModelerDock *dock );
    static MeshData buildCylinderHemisphere( ParamModelerDock *dock );
    static MeshData buildIndentedCuboid( ParamModelerDock *dock );
    static MeshData buildAsymmetricGableHouse( ParamModelerDock *dock );
    static MeshData buildFourStageRoundTower( ParamModelerDock *dock );
    static MeshData buildTwoGableHouses( ParamModelerDock *dock );
};

#endif // BUILDMESH_H