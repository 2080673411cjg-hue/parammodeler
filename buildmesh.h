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
    // ---- 统一入口（通过 Dock UI） ----
    static MeshData build( const QString &primitiveType, ParamModelerDock *dock );

    // ============================================================
    // Dock 接口（供插件内部使用）
    // ============================================================
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
    static MeshData buildTriPrismPyramid( ParamModelerDock *dock );

    // ============================================================
    // 纯数据接口（供外部直接调用，不依赖 QGIS / Dock）
    // ============================================================
    static MeshData buildCuboid( const CuboidParams &p );
    static MeshData buildCylinder( const CylinderParams &p );
    static MeshData buildLHouse( const LHouseParams &p );
    static MeshData buildConeCylinder( const ConeCylinderParams &p );
    static MeshData buildGabledRoof( const GabledRoofParams &p );
    static MeshData buildPyramidRoof( const PyramidRoofParams &p );
    static MeshData buildTruncatedPyramidRoof( const TruncatedPyramidRoofParams &p );
    static MeshData buildHalfCylinderRoof( const HalfCylinderRoofParams &p );
    static MeshData buildCylinderHemisphere( const CylinderHemisphereParams &p );
    static MeshData buildIndentedCuboid( const IndentedCuboidParams &p );
    static MeshData buildAsymmetricGableHouse( const AsymmetricGableHouseParams &p );
    static MeshData buildFourStageRoundTower( const FourStageRoundTowerParams &p );
    static MeshData buildTwoGableHouses( const TwoGableHousesParams &p );
    static MeshData buildTriPrismPyramid( const TriPrismPyramidParams &p );
};

#endif // BUILDMESH_H
