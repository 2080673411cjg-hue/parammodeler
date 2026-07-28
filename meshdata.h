/***************************************************************************
  meshdata.h
  网格数据结构与工具函数
  -------------------
          begin                : Mar. 2026
          copyright            : (C) 2026 by Chai
          email                : 2080673411@qq.com
***************************************************************************/

/***************************************************************************
 * This program is free software; you can redistribute it and/or modify  *
 * it under the terms of the GNU General Public License as published by  *
 * the Free Software Foundation; either version 2 of the License, or     *
 * (at your option) any later version.                                   *
 *                                                                       *
 ***************************************************************************/
#ifndef MESHDATA_H
#define MESHDATA_H

#include <QVector>
#include <QVector3D>

// ============================================================
// MeshData：顶点 + 三角面片索引
// 供 Qt3D 实时预览和 ExportOBJ 写文件共用
// ============================================================
struct MeshData
{
    QVector<QVector3D> vertices;  // 顶点列表
    QVector<int>       indices;   // 每3个为一个三角形（CCW 朝外）

    void clear()
    {
        vertices.clear();
        indices.clear();
    }

    bool isEmpty() const
    {
        return vertices.isEmpty() || indices.isEmpty();
    }

    // ---- 工具函数：添加三角形 ----
    void addTriangle( const QVector3D &a, const QVector3D &b, const QVector3D &c )
    {
        int base = vertices.size();
        vertices << a << b << c;
        indices  << base << base + 1 << base + 2;
    }

    // ---- 工具函数：四边形拆成两个三角形（a-b-c-d 逆时针） ----
    void addQuad( const QVector3D &a, const QVector3D &b,
                  const QVector3D &c, const QVector3D &d )
    {
        addTriangle( a, b, c );
        addTriangle( a, c, d );
    }
};

// ============================================================
// 各基元参数结构体（纯数据，供外部直接调用 BuildMesh）
// ============================================================

struct CuboidParams {
    double length = 10.0, width = 8.0, height = 5.0;
};

struct CylinderParams {
    double radius = 4.0, height = 8.0;
};

struct LHouseParams {
    double totalLength = 20.0, wingRatio = 0.4, totalWidth = 10.0,
           wingWidthRatio = 0.5, height = 5.0;
};

struct ConeCylinderParams {
    double radius = 4.0, cylHeight = 5.0, coneHeight = 3.0;
};

struct GabledRoofParams {
    double length = 12.0, width = 8.0, wallHeight = 4.0, roofHeight = 3.0;
};

struct PyramidRoofParams {
    double length = 12.0, width = 8.0, wallHeight = 4.0, roofHeight = 3.0;
};

struct TruncatedPyramidRoofParams {
    double bottomLength = 14.0, bottomWidth = 10.0, topLength = 8.0,
           topWidth = 6.0, wallHeight = 4.0, roofHeight = 3.0;
};

struct HalfCylinderRoofParams {
    double length = 12.0, width = 8.0, wallHeight = 4.0;  // radius = width/2
};

struct CylinderHemisphereParams {
    double radius = 4.0, cylHeight = 5.0, domeHeight = 2.0, bulge = 0.3;
};

struct IndentedCuboidParams {
    double outerLength = 15.0, outerWidth = 10.0, outerHeight = 6.0,
           innerLength = 6.0, innerWidth = 4.0, innerHeight = 3.0,
           offsetX = 0.5, offsetY = 0.5;
};

struct AsymmetricGableHouseParams {
    double length = 12.0, width = 8.0, wallHeight = 4.0, roofHeight = 3.0,
           ridgeLength = 7.0, ridgeRatio = 0.5;
};

struct FourStageRoundTowerParams {
    double baseRadius = 8.0, baseHeight = 2.0, middleHeight = 2.0,
           middleTopRadius = 1.0, middleBulge = 0.3, coneHeight = 1.5;
};

struct TwoGableHousesParams {
    double length1 = 14.0, length2 = 10.0, width = 8.0, wallHeight = 4.0,
           roofHeight = 3.0, angle = 180.0, ridgeRatio = 0.5;
};

struct TriPrismPyramidParams {
    double leg = 8.0, baseSide = 6.0, totalHeight = 8.0, pyramidRatio = 0.4;
};

#endif // MESHDATA_H
