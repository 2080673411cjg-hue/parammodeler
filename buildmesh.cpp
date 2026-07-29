/***************************************************************************
  buildmesh.cpp
  Mesh Construction Functions
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
#include "buildmesh.h"
#include "parammodeler_dock.h"
#include <QtMath>
#include <cmath>
#include <windows.h>
#define DEBUG_LOG(msg) OutputDebugStringW(msg)

// ============================================================
// 统一后处理：所有建筑 mesh 以底面几何中心为原点
// 圆形建筑（Cylinder/Dome/Tower 等）底面中心已在 (0,0,0)，不受影响
// 矩形建筑（Cuboid/GabledRoof 等）从"左下角原点"统一平移到"底面中心原点"
// 效果：旋转绕模型自身中心、tx/ty 语义 = 建筑底面中心的世界坐标
// ============================================================
static void centerMeshOnBaseFace( MeshData &m )
{
    if ( m.vertices.isEmpty() )
        return;

    float minX = 1e9f, maxX = -1e9f;
    float minY = 1e9f, maxY = -1e9f;
    for ( const QVector3D &v : m.vertices )
    {
        if ( v.x() < minX ) minX = v.x();
        if ( v.x() > maxX ) maxX = v.x();
        if ( v.y() < minY ) minY = v.y();
        if ( v.y() > maxY ) maxY = v.y();
    }

    const float cx = ( minX + maxX ) * 0.5f;
    const float cy = ( minY + maxY ) * 0.5f;

    // 已经居中的（圆形建筑）跳过，避免无意义的遍历
    if ( std::abs( cx ) < 0.001f && std::abs( cy ) < 0.001f )
        return;

    for ( QVector3D &v : m.vertices )
    {
        v.setX( v.x() - cx );
        v.setY( v.y() - cy );
        // Z 保持不动，底面仍然在 Z=0
    }
}

// ============================================================
// 统一入口
// ============================================================
MeshData BuildMesh::build( const QString &primitiveType, ParamModelerDock *dock )
{
    MeshData m;
    if      ( primitiveType == "Cuboid" )              m = buildCuboid( dock );
    else if ( primitiveType == "Cylinder" )            m = buildCylinder( dock );
    else if ( primitiveType == "LHouse" )              m = buildLHouse( dock );
    else if ( primitiveType == "ConeCylinder" )        m = buildConeCylinder( dock );
    else if ( primitiveType == "GabledRoof" )          m = buildGabledRoof( dock );
    else if ( primitiveType == "PyramidRoof" )         m = buildPyramidRoof( dock );
    else if ( primitiveType == "TruncatedPyramidRoof") m = buildTruncatedPyramidRoof( dock );
    else if ( primitiveType == "HalfCylinderRoof" )    m = buildHalfCylinderRoof( dock );
    else if ( primitiveType == "CylinderDome" ||
              primitiveType == "CylinderHemisphere" )     m = buildCylinderHemisphere( dock );
    else if ( primitiveType == "IndentedCuboid" )        m = buildIndentedCuboid( dock );
    else if ( primitiveType == "AsymmetricGableHouse" )  m = buildAsymmetricGableHouse( dock );
    else if ( primitiveType == "FourStageRoundTower" )   m = buildFourStageRoundTower( dock );
    else if ( primitiveType == "TwoGableHouses" )        m = buildTwoGableHouses( dock );
    else if ( primitiveType == "TriPrismPyramid" )     m = buildTriPrismPyramid( dock );

    // 统一居中：矩形建筑从"左下角原点"修正为"底面中心原点"
    // 圆形建筑底面中心已在 (0,0,0)，centerMeshOnBaseFace 检测后自动跳过
    centerMeshOnBaseFace( m );

    DEBUG_LOG( QString( "[BuildMesh] %1 → 顶点=%2, 三角面=%3\n" )
      .arg( primitiveType )
      .arg( m.vertices.size() )
      .arg( m.indices.size() / 3 )
      .toStdWString().c_str() );
    return m;
}

// ============================================================
// 长方体
// ============================================================
MeshData BuildMesh::buildCuboid( const CuboidParams &p )
{
    MeshData m;
    double W = p.length, D = p.width, H = p.height;
    if ( W <= 0 || D <= 0 || H <= 0 ) return m;

    QVector3D v0(0,0,0), v1(W,0,0), v2(W,D,0), v3(0,D,0);
    QVector3D v4(0,0,H), v5(W,0,H), v6(W,D,H), v7(0,D,H);

    m.addQuad( v0, v3, v2, v1 ); //底面（CCW从下方看）
    m.addQuad( v4, v5, v6, v7 ); // 顶面
    m.addQuad( v0, v1, v5, v4 ); // 前面
    m.addQuad( v1, v2, v6, v5 ); // 右面
    m.addQuad( v2, v3, v7, v6 ); // 后面
    m.addQuad( v3, v0, v4, v7 ); // 左面
    return m;
}

MeshData BuildMesh::buildCuboid( ParamModelerDock *dock )
{
    CuboidParams p;
    p.length = dock->cuboidLength();
    p.width  = dock->cuboidWidth();
    p.height = dock->cuboidHeight();
    return buildCuboid( p );
}

// ============================================================
// 圆柱
// ============================================================
MeshData BuildMesh::buildCylinder( const CylinderParams &p )
{
    MeshData m;
    double R = p.radius, H = p.height;
    if ( R <= 0 || H <= 0 ) return m;

    const int seg = 64;
    QVector<QVector3D> bot, top;
    for ( int i = 0; i < seg; i++ )
    {
        double a = 2.0 * M_PI * i / seg;
        bot << QVector3D( R*cos(a), R*sin(a), 0 );
        top << QVector3D( R*cos(a), R*sin(a), H );
    }
    QVector3D bc(0,0,0), tc(0,0,H);
    for ( int i = 0; i < seg; i++ )
    {
        int n = (i+1)%seg;
        m.addTriangle( bc, bot[n], bot[i] );       // 底面（法线朝下）
        m.addTriangle( tc, top[i], top[n] );       // 顶面（法线朝上）
        m.addTriangle( bot[i], bot[n], top[n] );   // 侧面
        m.addTriangle( bot[i], top[n], top[i] );
    }
    return m;
}

MeshData BuildMesh::buildCylinder( ParamModelerDock *dock )
{
    CylinderParams p;
    p.radius = dock->cylinderRadius();
    p.height = dock->cylinderHeight();
    return buildCylinder( p );
}

// ============================================================
// L型房子
// ============================================================
MeshData BuildMesh::buildLHouse( ParamModelerDock *dock )
{
    MeshData m;
    double T   = dock->LTotalLength();
    double Rw  = dock->LWingRatio();
    double Aw  = T * ( 1.0 - Rw );
    double Bw  = T * Rw;
    double Ad  = dock->LTotalWidth();
    double Bdr = dock->LWingWidthRatio();
    double Bd  = Ad * Bdr;
    double H   = dock->LHeight();

    DEBUG_LOG( QString("[LHouse] T=%1 Rw=%2 Aw=%3 Ad=%4 Bw=%5 Bdr=%6 Bd=%7 H=%8\n")
        .arg(T).arg(Rw).arg(Aw).arg(Ad).arg(Bw).arg(Bdr).arg(Bd).arg(H).toStdWString().c_str() );
    if ( Aw<=0||Ad<=0||Bw<=0||Bd<=0||H<=0 ) return m;

    // L形拆成两个凸多边形，各自三角化，凹角不填充
    // 主体: (0,0)-(Aw,0)-(Aw,Bd)-(Aw,Ad)-(0,Ad)
    // 翼部: (Aw,0)-(Aw+Bw,0)-(Aw+Bw,Bd)-(Aw,Bd)

    auto V = []( double x, double y, double z ) { return QVector3D( (float)x, (float)y, (float)z ); };

    // ===== 底面 (法线 -Z, winding: 从下往上看顺时针) =====
    // 主体 6 边凹多边形，以凹角 (Aw,Bd) 为枢纽拆 3 个三角形
    m.addTriangle( V(0,0,0), V(Aw,Bd,0), V(Aw,0,0) );
    m.addTriangle( V(0,0,0), V(0,Ad,0),  V(Aw,Bd,0) );
    m.addTriangle( V(0,Ad,0), V(Aw,Ad,0), V(Aw,Bd,0) );
    // 翼部矩形 2 个三角形
    m.addTriangle( V(Aw,0,0), V(Aw+Bw,Bd,0), V(Aw+Bw,0,0) );
    m.addTriangle( V(Aw,0,0), V(Aw,Bd,0),     V(Aw+Bw,Bd,0) );

    // ===== 顶面 (法线 +Z, winding: 从上往下看逆时针) =====
    m.addTriangle( V(0,0,H), V(Aw,0,H),   V(Aw,Bd,H) );
    m.addTriangle( V(0,0,H), V(Aw,Bd,H),  V(0,Ad,H) );
    m.addTriangle( V(0,Ad,H), V(Aw,Bd,H), V(Aw,Ad,H) );
    m.addTriangle( V(Aw,0,H), V(Aw+Bw,0,H), V(Aw+Bw,Bd,H) );
    m.addTriangle( V(Aw,0,H), V(Aw+Bw,Bd,H), V(Aw,Bd,H) );

    // ===== 墙面 (每面2个三角形, 法线朝外) =====

    // 前墙 y=0, 覆盖全宽 [0, Aw+Bw], 法线 -Y
    m.addTriangle( V(0,0,0),     V(Aw,0,0),     V(Aw,0,H) );
    m.addTriangle( V(0,0,0),     V(Aw,0,H),     V(0,0,H) );
    m.addTriangle( V(Aw,0,0),     V(Aw+Bw,0,0), V(Aw+Bw,0,H) );
    m.addTriangle( V(Aw,0,0),     V(Aw+Bw,0,H), V(Aw,0,H) );

    // 后墙 y=Ad, 法线 +Y (CCW from +Y view)
    m.addTriangle( V(0,Ad,0),   V(Aw,Ad,H),   V(Aw,Ad,0) );
    m.addTriangle( V(0,Ad,0),   V(0,Ad,H),    V(Aw,Ad,H) );

    // 左墙 x=0, 法线 -X
    m.addTriangle( V(0,Ad,0),   V(0,0,0),     V(0,0,H) );
    m.addTriangle( V(0,Ad,0),   V(0,0,H),     V(0,Ad,H) );

    // 右墙上段 x=Aw, y∈[Bd,Ad], 法线 +X
    m.addTriangle( V(Aw,Bd,0), V(Aw,Ad,0), V(Aw,Ad,H) );
    m.addTriangle( V(Aw,Bd,0), V(Aw,Ad,H), V(Aw,Bd,H) );

    // 翼部后墙 y=Bd, 法线 +Y (CCW from +Y view)
    m.addTriangle( V(Aw,Bd,0),     V(Aw+Bw,Bd,H), V(Aw+Bw,Bd,0) );
    m.addTriangle( V(Aw,Bd,0),     V(Aw,Bd,H),     V(Aw+Bw,Bd,H) );

    // 翼部右墙 x=Aw+Bw, 法线 +X
    m.addTriangle( V(Aw+Bw,0,0), V(Aw+Bw,Bd,0), V(Aw+Bw,Bd,H) );
    m.addTriangle( V(Aw+Bw,0,0), V(Aw+Bw,Bd,H), V(Aw+Bw,0,H) );

    return m;
}

// ============================================================
// 圆锥+圆柱
// ============================================================
MeshData BuildMesh::buildConeCylinder( const ConeCylinderParams &p )
{
    MeshData m;
    double R     = p.radius;
    double Hcyl  = p.cylHeight;
    double Hcone = p.coneHeight;
    if ( R<=0||Hcyl<=0||Hcone<=0 ) return m;

    const int seg = 64;
    QVector<QVector3D> bot, mid;
    for ( int i = 0; i < seg; i++ )
    {
        double a = 2.0*M_PI*i/seg;
        bot << QVector3D(R*cos(a), R*sin(a), 0);
        mid << QVector3D(R*cos(a), R*sin(a), Hcyl);
    }
    QVector3D bc(0,0,0), apex(0,0,Hcyl+Hcone);

    for ( int i = 0; i < seg; i++ )
    {
        int n = (i+1)%seg;
        m.addTriangle( bc, bot[n], bot[i] );         // 底面（法线 -Z）
        m.addTriangle( bot[i], bot[n], mid[n] );     // 圆柱侧面
        m.addTriangle( bot[i], mid[n], mid[i] );
        m.addTriangle( mid[i], mid[n], apex );       // 圆锥侧面
    }
    return m;
}

MeshData BuildMesh::buildConeCylinder( ParamModelerDock *dock )
{
    ConeCylinderParams p;
    p.radius    = dock->coneCylRadius();
    p.cylHeight = dock->coneCylCylHeight();
    p.coneHeight = dock->coneCylConeHeight();
    return buildConeCylinder( p );
}

// ============================================================
// 人字形屋顶
// ============================================================
MeshData BuildMesh::buildGabledRoof( const GabledRoofParams &p )
{
    MeshData m;
    double W  = p.length;
    double D  = p.width;
    double HW = p.wallHeight;
    double HR = p.roofHeight;
    if ( W<=0||D<=0||HW<=0||HR<=0 ) return m;

    QVector3D v0(0,0,0), v1(W,0,0), v2(W,D,0), v3(0,D,0);
    QVector3D v4(0,0,HW), v5(W,0,HW), v6(W,D,HW), v7(0,D,HW);
    double Z = HW+HR;
    QVector3D r0(0, D/2, Z), r1(W, D/2, Z);

    m.addQuad( v0, v3, v2, v1 );   // 底面
    m.addQuad( v0, v1, v5, v4 );   // 前墙
    m.addQuad( v1, v2, v6, v5 );   // 右墙
    m.addQuad( v2, v3, v7, v6 );   // 后墙
    m.addQuad( v3, v0, v4, v7 );   // 左墙
    m.addQuad( v4, v5, r1, r0 );   // 前坡
    m.addQuad( v7, r0, r1, v6 );   // 后坡
    m.addTriangle( v4, r0, v7 );   // 左山墙
    m.addTriangle( v5, v6, r1 );   // 右山墙
    return m;
}

MeshData BuildMesh::buildGabledRoof( ParamModelerDock *dock )
{
    GabledRoofParams p;
    p.length     = dock->gabledRoofLength();
    p.width      = dock->gabledRoofWidth();
    p.wallHeight = dock->gabledRoofWallHeight();
    p.roofHeight = dock->gabledRoofRoofHeight();
    return buildGabledRoof( p );
}

// ============================================================
// 金字塔屋顶
// ============================================================
MeshData BuildMesh::buildPyramidRoof( const PyramidRoofParams &p )
{
    MeshData m;
    double W  = p.length;
    double D  = p.width;
    double HW = p.wallHeight;
    double HR = p.roofHeight;
    if ( W<=0||D<=0||HW<=0||HR<=0 ) return m;

    QVector3D v0(0,0,0), v1(W,0,0), v2(W,D,0), v3(0,D,0);
    QVector3D v4(0,0,HW), v5(W,0,HW), v6(W,D,HW), v7(0,D,HW);
    QVector3D apex(W/2, D/2, HW+HR);

    m.addQuad( v0, v3, v2, v1 );
    m.addQuad( v0, v1, v5, v4 );
    m.addQuad( v1, v2, v6, v5 );
    m.addQuad( v2, v3, v7, v6 );
    m.addQuad( v3, v0, v4, v7 );
    m.addTriangle( v4, v5, apex );
    m.addTriangle( v5, v6, apex );
    m.addTriangle( v6, v7, apex );
    m.addTriangle( v7, v4, apex );
    return m;
}

MeshData BuildMesh::buildPyramidRoof( ParamModelerDock *dock )
{
    PyramidRoofParams p;
    p.length     = dock->pyramidLength();
    p.width      = dock->pyramidWidth();
    p.wallHeight = dock->pyramidWallHeight();
    p.roofHeight = dock->pyramidRoofHeight();
    return buildPyramidRoof( p );
}

// ============================================================
// 棱台屋顶
// ============================================================
MeshData BuildMesh::buildTruncatedPyramidRoof( const TruncatedPyramidRoofParams &p )
{
    MeshData m;
    double W  = p.bottomLength;
    double D  = p.bottomWidth;
    double HW = p.wallHeight;
    double HR = p.roofHeight;
    double WT = p.topLength;
    double DT = p.topWidth;
    if ( W<=0||D<=0||HW<=0||HR<=0 ) return m;

    QVector3D v0(0,0,0), v1(W,0,0), v2(W,D,0), v3(0,D,0);
    QVector3D v4(0,0,HW), v5(W,0,HW), v6(W,D,HW), v7(0,D,HW);
    double cx=W/2, cy=D/2, Z=HW+HR;
    QVector3D t0(cx-WT/2, cy-DT/2, Z), t1(cx+WT/2, cy-DT/2, Z);
    QVector3D t2(cx+WT/2, cy+DT/2, Z), t3(cx-WT/2, cy+DT/2, Z);

    m.addQuad( v0, v3, v2, v1 );
    m.addQuad( v0, v1, v5, v4 );
    m.addQuad( v1, v2, v6, v5 );
    m.addQuad( v2, v3, v7, v6 );
    m.addQuad( v3, v0, v4, v7 );
    m.addQuad( v4, v5, t1, t0 ); // 前坡
    m.addQuad( v5, v6, t2, t1 ); // 右坡
    m.addQuad( v6, v7, t3, t2 ); // 后坡
    m.addQuad( v7, v4, t0, t3 ); // 左坡
    m.addQuad( t0, t1, t2, t3 ); // 顶面
    return m;
}

MeshData BuildMesh::buildTruncatedPyramidRoof( ParamModelerDock *dock )
{
    TruncatedPyramidRoofParams p;
    p.bottomLength = dock->tpBottomLength();
    p.bottomWidth  = dock->tpBottomWidth();
    p.topLength    = dock->tpTopLength();
    p.topWidth     = dock->tpTopWidth();
    p.wallHeight   = dock->tpWallHeight();
    p.roofHeight   = dock->tpRoofHeight();
    return buildTruncatedPyramidRoof( p );
}

// ============================================================
// 半圆柱屋顶
// ============================================================
MeshData BuildMesh::buildHalfCylinderRoof( const HalfCylinderRoofParams &p )
{
    MeshData m;
    double W  = p.length;
    double D  = p.width;
    double HW = p.wallHeight;
    double R  = D / 2.0;
    if ( W<=0||D<=0||HW<=0||R<=0 ) return m;

    const int N = 32;
    QVector3D v0(0,0,0), v1(W,0,0), v2(W,D,0), v3(0,D,0);
    QVector3D v4(0,0,HW), v5(W,0,HW), v6(W,D,HW), v7(0,D,HW);

    m.addQuad( v0, v3, v2, v1 );
    m.addQuad( v0, v1, v5, v4 );
    m.addQuad( v1, v2, v6, v5 );
    m.addQuad( v2, v3, v7, v6 );
    m.addQuad( v3, v0, v4, v7 );

    double cy = D/2.0;
    // 弧线顶点
    QVector<QVector3D> arcL, arcR;
    for ( int i = 0; i <= N; i++ )
    {
        double t = M_PI * i / N;
        double y = cy + R*cos(t);
        double z = HW + R*sin(t);
        arcL << QVector3D(0, y, z);
        arcR << QVector3D(W, y, z);
    }
    // 半圆柱表面
    for ( int i = 0; i < N; i++ )
    {
        m.addTriangle( arcL[i], arcL[i+1], arcR[i+1] );
        m.addTriangle( arcL[i], arcR[i+1], arcR[i] );
    }
    // 两端封面（三角扇形）
    QVector3D fcL(0, cy, HW), fcR(W, cy, HW);
    for ( int i = 0; i < N; i++ )
    {
        m.addTriangle( fcL, arcL[i+1], arcL[i] );
        m.addTriangle( fcR, arcR[i],   arcR[i+1] );
    }
    return m;
}

MeshData BuildMesh::buildHalfCylinderRoof( ParamModelerDock *dock )
{
    HalfCylinderRoofParams p;
    p.length     = dock->hcrLength();
    p.width      = dock->hcrWidth();
    p.wallHeight = dock->hcrWallHeight();
    return buildHalfCylinderRoof( p );
}

// ============================================================
// 圆柱穹顶
// ============================================================
MeshData BuildMesh::buildCylinderHemisphere( const CylinderHemisphereParams &p )
{
    MeshData m;
    double R     = p.radius;
    double H     = p.cylHeight;
    double dH    = p.domeHeight;
    double bulge = p.bulge;
    if ( R<=0||H<=0||dH<=0 ) return m;

    const int seg = 64, lat = 16;

    auto evalBezier = [&]( double t, double &r, double &z ) {
        double P0x=R, P0y=H;
        double P1x=R, P1y=H+dH*bulge*0.5;
        double P2x=R*(1.0-bulge), P2y=H+dH*0.9;
        double P3x=0.0, P3y=H+dH;
        double u=1.0-t, tt=t*t, uu=u*u;
        r=uu*u*P0x+3*uu*t*P1x+3*u*tt*P2x+tt*t*P3x;
        z=uu*u*P0y+3*uu*t*P1y+3*u*tt*P2y+tt*t*P3y;
    };

    auto ring = [&]( double r, double z ) {
        QVector<QVector3D> pts;
        for ( int i=0; i<seg; i++ ) {
            double a=2.0*M_PI*i/seg;
            pts << QVector3D(r*cos(a), r*sin(a), z);
        }
        return pts;
    };

    QVector<QVector3D> botRing = ring(R, 0);
    QVector<QVector3D> topRing = ring(R, H);

    // 底面
    QVector3D bc(0,0,0);
    for ( int i=0; i<seg; i++ ) {
        int n=(i+1)%seg;
        m.addTriangle(bc, botRing[n], botRing[i]);
    }
    // 圆柱侧面
    for ( int i=0; i<seg; i++ ) {
        int n=(i+1)%seg;
        m.addTriangle(botRing[i], botRing[n], topRing[n]);
        m.addTriangle(botRing[i], topRing[n], topRing[i]);
    }
    // 穹顶层
    QVector<QVector3D> prev = topRing;
    for ( int l=1; l<lat; l++ ) {
        double t = l / double(lat);
        double r, z;
        evalBezier(t, r, z);
        QVector<QVector3D> cur = ring(r, z);
        for ( int i=0; i<seg; i++ ) {
            int n=(i+1)%seg;
            m.addTriangle(prev[i], prev[n], cur[n]);
            m.addTriangle(prev[i], cur[n],  cur[i]);
        }
        prev = cur;
    }
    // 顶点封口
    QVector3D apex(0, 0, H+dH);
    for ( int i=0; i<seg; i++ ) {
        int n=(i+1)%seg;
        m.addTriangle(prev[i], prev[n], apex);
    }
    return m;
}

MeshData BuildMesh::buildCylinderHemisphere( ParamModelerDock *dock )
{
    CylinderHemisphereParams p;
    p.radius     = dock->cylHemiRadius();
    p.cylHeight  = dock->cylHemiHeight();
    p.domeHeight = dock->cylHemiDomeHeight();
    p.bulge      = dock->cylHemiBulge();
    return buildCylinderHemisphere( p );
}

// ============================================================
// 凹陷长方体
// ============================================================
MeshData BuildMesh::buildIndentedCuboid( const IndentedCuboidParams &p )
{
    MeshData m;
    double W  = p.outerLength,  D  = p.outerWidth,  H  = p.outerHeight;
    double w  = p.innerLength,  d  = p.innerWidth,  h  = p.innerHeight;
    double ox = p.offsetX,     oy = p.offsetY;
    if ( W<=0||D<=0||H<=0 ) return m;

    QVector3D v0(0,0,0), v1(W,0,0), v2(W,D,0), v3(0,D,0);
    QVector3D v4(0,0,H), v5(W,0,H), v6(W,D,H), v7(0,D,H);

    // 内部凹陷顶部轮廓（在顶面上）
    QVector3D i0(ox,   oy,   H), i1(ox+w, oy,   H);
    QVector3D i2(ox+w, oy+d, H), i3(ox,   oy+d, H);
    // 内部凹陷底部
    QVector3D b0(ox,   oy,   H-h), b1(ox+w, oy,   H-h);
    QVector3D b2(ox+w, oy+d, H-h), b3(ox,   oy+d, H-h);

    m.addQuad( v0, v3, v2, v1 ); // 外部底面
    m.addQuad( v0, v1, v5, v4 ); // 外部前面
    m.addQuad( v1, v2, v6, v5 ); // 外部右面
    m.addQuad( v2, v3, v7, v6 ); // 外部后面
    m.addQuad( v3, v0, v4, v7 ); // 外部左面
    // 顶面（围绕凹陷的4块）
    m.addQuad( v4, v5, i1, i0 ); // 前部顶面
    m.addQuad( v5, v6, i2, i1 ); // 右部顶面
    m.addQuad( v6, v7, i3, i2 ); // 后部顶面
    m.addQuad( v7, v4, i0, i3 ); // 左部顶面
    // 凹陷内部（法线均指向凹陷内部空间）
    m.addQuad( b0, b1, b2, b3 ); // 凹陷底面（法线 +Z，俯视可见）
    m.addQuad( i0, b0, b1, i1 ); // 凹陷前侧（法线 -Y，朝内）
    m.addQuad( i1, i2, b2, b1 ); // 凹陷右侧
    m.addQuad( i2, i3, b3, b2 ); // 凹陷后侧
    m.addQuad( i3, i0, b0, b3 ); // 凹陷左侧
    return m;
}

MeshData BuildMesh::buildIndentedCuboid( ParamModelerDock *dock )
{
    IndentedCuboidParams p;
    p.outerLength = dock->icOuterLength();
    p.outerWidth  = dock->icOuterWidth();
    p.outerHeight = dock->icOuterHeight();
    p.innerLength = dock->icInnerLength();
    p.innerWidth  = dock->icInnerWidth();
    p.innerHeight = dock->icInnerHeight();
    p.offsetX     = dock->icOffsetX();
    p.offsetY     = dock->icOffsetY();
    return buildIndentedCuboid( p );
}

// ============================================================
// 非对称人字形屋顶房屋
// ============================================================
MeshData BuildMesh::buildAsymmetricGableHouse( const AsymmetricGableHouseParams &p )
{
  MeshData m;
  double W = p.length, D = p.width;
  double H = p.wallHeight, roofH = p.roofHeight;
  double ridgeL = p.ridgeLength, ridgeRatio = p.ridgeRatio;
  if ( W <= 0 || D <= 0 || H <= 0 || roofH <= 0 )
    return m;
  if ( ridgeRatio < 0.2 ) ridgeRatio = 0.2;
  if ( ridgeRatio > 0.8 ) ridgeRatio = 0.8;

  QVector3D v0( 0, 0, 0 ), v1( W, 0, 0 ), v2( W, D, 0 ), v3( 0, D, 0 );
  QVector3D v4( 0, 0, H ), v5( W, 0, H ), v6( W, D, H ), v7( 0, D, H );

  // 屋脊沿 X 轴（宽度方向）
  // ridgeOff  = 屋脊在 Y 方向的偏移（相对于 D/2）
  // ridgeL    = 屋脊长度（沿 X 方向）
  double rc = W / 2.0;            // 宽度中心
  double rs = rc - ridgeL / 2.0;  // 屋脊起点 X
  double re = rc + ridgeL / 2.0;  // 屋脊终点 X
  double ry = D * ridgeRatio;     // 屋脊 Y 位置

  QVector3D r0( rs, ry, H + roofH ); // 屋脊左端
  QVector3D r1( re, ry, H + roofH ); // 屋脊右端

  // 屋脊两端在墙顶的投影边（用于山墙封闭）
  QVector3D e0( rs, 0, H ), e1( rs, D, H ); // 左端前/后墙顶点
  QVector3D e2( re, 0, H ), e3( re, D, H ); // 右端前/后墙顶点

  m.addQuad( v0, v3, v2, v1 ); // 底面
  m.addQuad( v0, v1, v5, v4 ); // 前墙
  m.addQuad( v1, v2, v6, v5 ); // 右墙
  m.addQuad( v2, v3, v7, v6 ); // 后墙
  m.addQuad( v3, v0, v4, v7 ); // 左墙

  // 左山墙（x=rs 以左）
  m.addTriangle( v4, e0, r0 ); // 前坡左山
  m.addTriangle( v7, r0, e1 ); // 后坡左山
  m.addTriangle( v4, r0, v7 ); // 左山墙三角

  // 右山墙（x=re 以右）
  m.addTriangle( e2, v5, r1 ); // 前坡右山
  m.addTriangle( e3, r1, v6 ); // 后坡右山
  m.addTriangle( v5, v6, r1 ); // 右山墙三角

  // 主屋顶：前坡（朝 -Y）
  m.addQuad( e0, e2, r1, r0 );
  // 主屋顶：后坡（朝 +Y）
  m.addQuad( e1, r0, r1, e3 );
  return m;
}

MeshData BuildMesh::buildAsymmetricGableHouse( ParamModelerDock *dock )
{
    AsymmetricGableHouseParams p;
    p.length      = dock->aghLength();
    p.width       = dock->aghWidth();
    p.wallHeight  = dock->aghWallHeight();
    p.roofHeight  = dock->aghRoofHeight();
    p.ridgeLength = dock->aghRidgeLength();
    p.ridgeRatio  = dock->aghRidgeRatio();
    return buildAsymmetricGableHouse( p );
}

// ============================================================
// 四段式圆塔形：短圆柱底座 + 低坡弧面屋顶 + 顶部小尖锥
// ============================================================
MeshData BuildMesh::buildFourStageRoundTower( const FourStageRoundTowerParams &p )
{
    MeshData m;
    double baseR   = p.baseRadius;
    double baseH   = p.baseHeight;
    double roofH   = p.middleHeight;
    double capR    = p.middleTopRadius;
    double bulge   = p.middleBulge;
    double coneH   = p.coneHeight;
    if ( baseR<=0||baseH<=0||roofH<=0||capR<=0||coneH<=0 ) return m;

    double roofR = baseR * 1.12;
    double eaveH = qBound( 0.25, baseH * 0.20, 0.9 );
    double bodyTopZ = baseH - eaveH;
    capR = qBound( roofR * 0.02, capR, roofR * 0.35 );
    bulge = qBound( 0.0, bulge, 0.6 );
    if ( bodyTopZ <= 0.0 ) return m;

    const int seg=64, layers=16;

    auto makeRing = [&]( double r, double z ) {
        QVector<QVector3D> pts;
        for ( int i=0; i<seg; i++ ) {
            double a=2.0*M_PI*i/seg;
            pts << QVector3D(r*cos(a), r*sin(a), z);
        }
        return pts;
    };

    auto evalBezier = [&]( double t, double &r, double &z ) {
        double P0x=roofR, P0y=baseH;
        double P1x=roofR*(1.0-0.08*bulge), P1y=baseH+roofH*0.12;
        double P2x=capR+(roofR-capR)*(0.35+0.25*bulge), P2y=baseH+roofH*0.85;
        double P3x=capR, P3y=baseH+roofH;
        double u=1.0-t;
        r=u*u*u*P0x+3*u*u*t*P1x+3*u*t*t*P2x+t*t*t*P3x;
        z=u*u*u*P0y+3*u*u*t*P1y+3*u*t*t*P2y+t*t*t*P3y;
    };

    auto connectRings = [&]( const QVector<QVector3D> &a, const QVector<QVector3D> &b ) {
        for ( int i=0; i<seg; i++ ) {
            int n=(i+1)%seg;
            m.addTriangle(a[i], a[n], b[n]);
            m.addTriangle(a[i], b[n], b[i]);
        }
    };

    // 底面封口
    QVector<QVector3D> botRing = makeRing(baseR, 0);
    QVector3D bc(0,0,0);
    for ( int i=0; i<seg; i++ ) {
        int n=(i+1)%seg;
        m.addTriangle(bc, botRing[n], botRing[i]);
    }

    // 圆柱段
    QVector<QVector3D> bodyTopRing = makeRing(baseR, bodyTopZ);
    connectRings(botRing, bodyTopRing);

    // 有厚度的外挑檐口
    QVector<QVector3D> eaveInnerTopRing = makeRing(baseR, baseH);
    QVector<QVector3D> eaveOuterBottomRing = makeRing(roofR, bodyTopZ);
    QVector<QVector3D> eaveRing = makeRing(roofR, baseH);
    connectRings(bodyTopRing, eaveOuterBottomRing);     // 檐口底面
    connectRings(eaveOuterBottomRing, eaveRing);        // 檐口外侧竖面
    connectRings(eaveInnerTopRing, eaveRing);           // 檐口顶面

    // 低坡弧面屋顶
    QVector<QVector3D> prev = eaveRing;
    for ( int l=1; l<=layers; l++ ) {
        double t=l/double(layers), r, z;
        evalBezier(t, r, z);
        QVector<QVector3D> cur = makeRing(r, z);
        connectRings(prev, cur);
        prev = cur;
    }

    // 顶部小尖锥
    QVector3D apex(0, 0, baseH+roofH+coneH);
    for ( int i=0; i<seg; i++ ) {
        int n=(i+1)%seg;
        m.addTriangle(prev[i], prev[n], apex);
    }
    return m;
}

MeshData BuildMesh::buildFourStageRoundTower( ParamModelerDock *dock )
{
    FourStageRoundTowerParams p;
    p.baseRadius      = dock->ftBaseRadius();
    p.baseHeight      = dock->ftBaseHeight();
    p.middleHeight    = dock->ftMiddleHeight();
    p.middleTopRadius = dock->ftMiddleTopRadius();
    p.middleBulge     = dock->ftMiddleBulge();
    p.coneHeight      = dock->ftConeHeight();
    return buildFourStageRoundTower( p );
}

// ============================================================
// 双人字屋顶房屋：House 2 绕 House 1 的 C 点竖直轴旋转
// ============================================================
MeshData BuildMesh::buildTwoGableHouses( const TwoGableHousesParams &p )
{
    MeshData m;
    double L1    = p.length1;
    double L2    = p.length2;
    double W     = p.width;
    double H     = p.wallHeight;
    double roofH = p.roofHeight;
    double angle = p.angle;
    double ridgeRatio = p.ridgeRatio;
    if ( L1<=0||L2<=0||W<=0||H<=0||roofH<=0 ) return m;
    if ( angle < 135.0 ) angle = 135.0;
    if ( angle > 180.0 ) angle = 180.0;
    if ( ridgeRatio < 0.2 ) ridgeRatio = 0.2;
    if ( ridgeRatio > 0.8 ) ridgeRatio = 0.8;

    {
        const double turnRad = ( 180.0 - angle ) * M_PI / 180.0;
        const double ct = cos( turnRad );
        const double st = sin( turnRad );
        const double roofZ = H + roofH;
        const double ridgeY = W * ridgeRatio;

        auto withZ = []( const QVector3D &p, double z ) {
            return QVector3D( p.x(), p.y(), z );
        };
        auto addWalls = [&]( const QVector<QVector3D> &poly )
        {
            for ( int i = 0; i < poly.size(); ++i )
            {
                const QVector3D p0 = poly[i];
                const QVector3D p1 = poly[( i + 1 ) % poly.size()];
                m.addQuad( p0, p1, withZ( p1, H ), withZ( p0, H ) );
            }
        };

        // Plan view: House 1 is rectangle A-B-C-D; House 2 is parallelogram B-E-G-C.
        const QVector3D A( 0, 0, 0 );
        const QVector3D B( L1, 0, 0 );
        const QVector3D C( L1, W, 0 );
        const QVector3D Dp( 0, W, 0 );
        const QVector3D dir( ct, st, 0 );
        const QVector3D E = B + dir * L2;
        const QVector3D G = C + dir * L2;

        QVector<QVector3D> footprint;
        footprint << A << B << E << G << C << Dp;

        m.addTriangle( C, B, A );
        m.addTriangle( Dp, C, A );
        m.addTriangle( G, E, B );
        m.addTriangle( C, G, B );
        addWalls( footprint );

        const QVector3D Aw = withZ( A, H );
        const QVector3D Bw = withZ( B, H );
        const QVector3D Cw = withZ( C, H );
        const QVector3D Dw = withZ( Dp, H );
        const QVector3D Ew = withZ( E, H );
        const QVector3D Gw = withZ( G, H );
        const QVector3D R0( 0, ridgeY, roofZ );
        const QVector3D R1( L1, ridgeY, roofZ );
        const QVector3D R2 = R1 + dir * L2;

        m.addTriangle( Aw, R0, Dw );
        m.addTriangle( Ew, Gw, R2 );
        m.addQuad( Aw, Bw, R1, R0 );
        m.addQuad( Dw, R0, R1, Cw );
        m.addQuad( Bw, Ew, R2, R1 );
        m.addQuad( Cw, R1, R2, Gw );

        return m;
    }

    double turnAngle = 180.0 - angle;
    double totalRad  = turnAngle * M_PI / 180.0;
    double ct = cos(totalRad), st = sin(totalRad);

    // ---- House 1: A-B-C-D, AB 为长度，BC 为宽度，C 为旋转轴底点 ----
    QVector3D A(0,0,0), B(L1,0,0), C(L1,W,0), Dp(0,W,0);
    QVector3D Aw(0,0,H), Bw(L1,0,H), Cw(L1,W,H), Dw(0,W,H);
    QVector3D R0(0,W/2,H+roofH), R1(L1,W/2,H+roofH);

    // ---- House 2: E-F-G-H, H 与 C 重合，整栋房屋绕 C 逆时针旋转 ----
    auto rotAroundC = [&]( const QVector3D &p ) -> QVector3D {
        double dx = p.x() - C.x();
        double dy = p.y() - C.y();
        return QVector3D(
            C.x() + dx * ct - dy * st,
            C.y() + dx * st + dy * ct,
            p.z()
        );
    };

    QVector3D E = rotAroundC( QVector3D(L1,0,0) );
    QVector3D F = rotAroundC( QVector3D(L1+L2,0,0) );
    QVector3D G = rotAroundC( QVector3D(L1+L2,W,0) );
    QVector3D H2 = C;
    QVector3D Ew = rotAroundC( QVector3D(L1,0,H) );
    QVector3D Fw = rotAroundC( QVector3D(L1+L2,0,H) );
    QVector3D Gw = rotAroundC( QVector3D(L1+L2,W,H) );
    QVector3D Hw = Cw;
    QVector3D R2 = rotAroundC( QVector3D(L1,W/2,H+roofH) );
    QVector3D R3 = rotAroundC( QVector3D(L1+L2,W/2,H+roofH) );

    auto addHouse1Exterior = [&]() {
        m.addQuad(A,Dp,C,B);        // 底面
        m.addQuad(A,B,Bw,Aw);       // AB 外墙
        m.addQuad(C,Dp,Dw,Cw);      // CD 外墙
        m.addQuad(Dp,A,Aw,Dw);      // DA 端墙
        m.addTriangle(Aw,R0,Dw);    // D-A 山墙
        m.addQuad(Aw,Bw,R1,R0);     // AB 侧屋坡
        m.addQuad(Dw,R0,R1,Cw);     // CD 侧屋坡
    };

    auto addHouse2Exterior = [&]() {
        m.addQuad(E,H2,G,F);        // 底面
        m.addQuad(E,F,Fw,Ew);       // EF 外墙
        m.addQuad(F,G,Gw,Fw);       // FG 端墙
        m.addQuad(G,H2,Hw,Gw);      // GH 外墙
        m.addTriangle(Fw,Gw,R3);    // F-G 山墙
        m.addQuad(Ew,Fw,R3,R2);     // 一侧屋坡
        m.addQuad(Hw,R2,R3,Gw);     // 另一侧屋坡
    };

    auto addGapConnector = [&]() {
        if ( angle >= 179.999 )
            return;

        m.addTriangle(B, C, E);       // 缺口底面
        m.addQuad(B, E, Ew, Bw);      // 缺口侧面
        m.addQuad(Bw, Ew, R2, R1);    // 屋脊前侧屋顶梯形面
        m.addTriangle(Cw, R1, R2);    // 屋脊背侧屋顶三角面
    };

    addHouse1Exterior();
    addHouse2Exterior();
    addGapConnector();

    return m;
}

MeshData BuildMesh::buildTwoGableHouses( ParamModelerDock *dock )
{
    TwoGableHousesParams p;
    p.length1     = dock->tgLength1();
    p.length2     = dock->tgLength2();
    p.width       = dock->tgWidth();
    p.wallHeight  = dock->tgWallHeight();
    p.roofHeight  = dock->tgRoofHeight();
    p.angle       = dock->tgAngle();
    p.ridgeRatio  = dock->tgRidgeRatio();
    return buildTwoGableHouses( p );
}

// ============================================================
// 三棱柱 + 三棱锥（等腰三角形底面）
// 参数：腰长 leg, 底边长 baseSide, 总高度 totalH, 锥高比 pyramidRatio
// ============================================================
MeshData BuildMesh::buildTriPrismPyramid( const TriPrismPyramidParams &p )
{
    MeshData m;
    double leg          = p.leg;
    double baseSide     = p.baseSide;
    double totalH       = p.totalHeight;
    double pyramidRatio = p.pyramidRatio;

    if ( leg <= 0 || baseSide <= 0 || totalH <= 0 ) return m;

    // 三角形存在条件：腰长 > 底边/2
    double halfBase = baseSide / 2.0;
    if ( leg <= halfBase ) return m;

    // 等腰三角形高（在 XY 平面内沿 Y 方向）
    double triH = std::sqrt( leg * leg - halfBase * halfBase );

    double prismZ  = totalH * ( 1.0 - pyramidRatio );
    double pyrH    = totalH * pyramidRatio;

    // ---- 底面三棱柱顶点 (Z=0) ----
    // 等腰三角形：底边沿 X 轴，顶点 C 在 -Y 方向（脊线朝上）
    QVector3D A( -halfBase, 0.0,  0.0 );
    QVector3D B(  halfBase, 0.0,  0.0 );
    QVector3D C( 0.0, -triH,     0.0 );

    // ---- 棱柱顶面 / 棱锥底面 (Z=prismZ) ----
    QVector3D A1( -halfBase, 0.0,  prismZ );
    QVector3D B1(  halfBase, 0.0,  prismZ );
    QVector3D C1( 0.0, -triH,     prismZ );

    // ---- 棱锥顶点 ----
    // 锥顶在等腰三角形顶点 C 的正上方（两条腰的交点）
    QVector3D apex( 0.0, -triH, totalH );

    // ---- 三棱柱网格 ----
    // 底面（法线 -Z，从下方看 CCW）
    m.addTriangle( A, B, C );

    // 三个侧面（均为四边形，法线朝外）
    m.addQuad( A, A1, B1, B );   // 前侧面 (底边 AB)
    m.addQuad( B, B1, C1, C );   // 右侧面 (边 BC)
    m.addQuad( C, C1, A1, A );   // 左侧面 (边 CA)

    // ---- 三棱锥网格（三个三角面，法线朝外） ----
    m.addTriangle( A1, apex, B1 );   // 前面
    m.addTriangle( B1, apex, C1 );   // 右面
    m.addTriangle( C1, apex, A1 );   // 左面

    return m;
}

MeshData BuildMesh::buildTriPrismPyramid( ParamModelerDock *dock )
{
    TriPrismPyramidParams p;
    p.leg          = dock->triPrismPyramidLeg();
    p.baseSide     = dock->triPrismPyramidBase();
    p.totalHeight  = dock->triPrismPyramidHeight();
    p.pyramidRatio = dock->triPrismPyramidRatio();
    return buildTriPrismPyramid( p );
}
