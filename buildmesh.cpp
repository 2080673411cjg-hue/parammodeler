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
MeshData BuildMesh::buildCuboid( ParamModelerDock *dock )
{
    MeshData m;
    double W = dock->cuboidLength();
    double D = dock->cuboidWidth();
    double H = dock->cuboidHeight();
    if ( W <= 0 || D <= 0 || H <= 0 ) return m;

    QVector3D v0(0,0,0), v1(W,0,0), v2(W,D,0), v3(0,D,0);
    QVector3D v4(0,0,H), v5(W,0,H), v6(W,D,H), v7(0,D,H);

    m.addQuad( v0, v1, v2, v3 ); // 底面（CCW从下方看）
    m.addQuad( v4, v5, v6, v7 ); // 顶面
    m.addQuad( v0, v1, v5, v4 ); // 前面
    m.addQuad( v1, v2, v6, v5 ); // 右面
    m.addQuad( v2, v3, v7, v6 ); // 后面
    m.addQuad( v3, v0, v4, v7 ); // 左面
    return m;
}

// ============================================================
// 圆柱
// ============================================================
MeshData BuildMesh::buildCylinder( ParamModelerDock *dock )
{
    MeshData m;
    double R = dock->cylinderRadius();
    double H = dock->cylinderHeight();
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
        m.addTriangle( bc, bot[i], bot[n] );       // 底面（法线朝下）
        m.addTriangle( tc, top[n], top[i] );       // 顶面（法线朝上）
        m.addTriangle( bot[i], bot[n], top[n] );   // 侧面
        m.addTriangle( bot[i], top[n], top[i] );
    }
    return m;
}

// ============================================================
// L型房子
// ============================================================
MeshData BuildMesh::buildLHouse( ParamModelerDock *dock )
{
    MeshData m;
    double Aw = dock->LMainLength();
    double Ad = dock->LMainWidth();
    double Bw = dock->LWingLength();
    double Bd = dock->LWingWidth();
    double H  = dock->LHeight();

    DEBUG_LOG( QString("[LHouse] Aw=%1 Ad=%2 Bw=%3 Bd=%4 H=%5\n")
        .arg(Aw).arg(Ad).arg(Bw).arg(Bd).arg(H).toStdWString().c_str() );
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

    // 后墙 y=Ad, 法线 +Y
    m.addTriangle( V(0,Ad,0),   V(Aw,Ad,0),   V(Aw,Ad,H) );
    m.addTriangle( V(0,Ad,0),   V(Aw,Ad,H),   V(0,Ad,H) );

    // 左墙 x=0, 法线 -X
    m.addTriangle( V(0,Ad,0),   V(0,0,0),     V(0,0,H) );
    m.addTriangle( V(0,Ad,0),   V(0,0,H),     V(0,Ad,H) );

    // 右墙上段 x=Aw, y∈[Bd,Ad], 法线 +X
    m.addTriangle( V(Aw,Bd,0), V(Aw,Ad,0), V(Aw,Ad,H) );
    m.addTriangle( V(Aw,Bd,0), V(Aw,Ad,H), V(Aw,Bd,H) );

    // 翼部后墙 y=Bd, 法线 +Y
    m.addTriangle( V(Aw,Bd,0),     V(Aw+Bw,Bd,0), V(Aw+Bw,Bd,H) );
    m.addTriangle( V(Aw,Bd,0),     V(Aw+Bw,Bd,H), V(Aw,Bd,H) );

    // 翼部右墙 x=Aw+Bw, 法线 +X
    m.addTriangle( V(Aw+Bw,0,0), V(Aw+Bw,Bd,0), V(Aw+Bw,Bd,H) );
    m.addTriangle( V(Aw+Bw,0,0), V(Aw+Bw,Bd,H), V(Aw+Bw,0,H) );

    return m;
}

// ============================================================
// 圆锥+圆柱
// ============================================================
MeshData BuildMesh::buildConeCylinder( ParamModelerDock *dock )
{
    MeshData m;
    double R     = dock->coneCylRadius();
    double Hcyl  = dock->coneCylCylHeight();
    double Hcone = dock->coneCylConeHeight();
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
        m.addTriangle( bc, bot[i], bot[n] );         // 底面
        m.addTriangle( bot[i], bot[n], mid[n] );     // 圆柱侧面
        m.addTriangle( bot[i], mid[n], mid[i] );
        m.addTriangle( mid[i], mid[n], apex );       // 圆锥侧面
    }
    return m;
}

// ============================================================
// 人字形屋顶
// ============================================================
MeshData BuildMesh::buildGabledRoof( ParamModelerDock *dock )
{
    MeshData m;
    double W  = dock->gabledRoofLength();
    double D  = dock->gabledRoofWidth();
    double HW = dock->gabledRoofWallHeight();
    double HR = dock->gabledRoofRoofHeight();
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

// ============================================================
// 金字塔屋顶
// ============================================================
MeshData BuildMesh::buildPyramidRoof( ParamModelerDock *dock )
{
    MeshData m;
    double W  = dock->pyramidLength();
    double D  = dock->pyramidWidth();
    double HW = dock->pyramidWallHeight();
    double HR = dock->pyramidRoofHeight();
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

// ============================================================
// 棱台屋顶
// ============================================================
MeshData BuildMesh::buildTruncatedPyramidRoof( ParamModelerDock *dock )
{
    MeshData m;
    double W  = dock->tpBottomLength();
    double D  = dock->tpBottomWidth();
    double HW = dock->tpWallHeight();
    double HR = dock->tpRoofHeight();
    double WT = dock->tpTopLength();
    double DT = dock->tpTopWidth();
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

// ============================================================
// 半圆柱屋顶
// ============================================================
MeshData BuildMesh::buildHalfCylinderRoof( ParamModelerDock *dock )
{
    MeshData m;
    double W  = dock->hcrLength();
    double D  = dock->hcrWidth();
    double HW = dock->hcrWallHeight();
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

// ============================================================
// 圆柱穹顶
// ============================================================
MeshData BuildMesh::buildCylinderHemisphere( ParamModelerDock *dock )
{
    MeshData m;
    double R    = dock->cylHemiRadius();
    double H    = dock->cylHemiHeight();
    double dH   = dock->cylHemiDomeHeight();
    double bulge= dock->cylHemiBulge();
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
        m.addTriangle(bc, botRing[i], botRing[n]);
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

// ============================================================
// 凹陷长方体
// ============================================================
MeshData BuildMesh::buildIndentedCuboid( ParamModelerDock *dock )
{
    MeshData m;
    double W  = dock->icOuterLength(),  D  = dock->icOuterWidth(),  H  = dock->icOuterHeight();
    double w  = dock->icInnerLength(),  d  = dock->icInnerWidth(),  h  = dock->icInnerHeight();
    double ox = dock->icOffsetX(),     oy = dock->icOffsetY();
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
    // 凹陷内部
    m.addQuad( b0, b1, b2, b3 ); // 凹陷底面
    m.addQuad( i0, i1, b1, b0 ); // 凹陷前侧
    m.addQuad( i1, i2, b2, b1 ); // 凹陷右侧
    m.addQuad( i2, i3, b3, b2 ); // 凹陷后侧
    m.addQuad( i3, i0, b0, b3 ); // 凹陷左侧
    return m;
}

// ============================================================
// 非对称人字形屋顶房屋
// ============================================================
MeshData BuildMesh::buildAsymmetricGableHouse( ParamModelerDock *dock )
{
  MeshData m;
  double W = dock->aghLength(), D = dock->aghWidth();
  double H = dock->aghWallHeight(), roofH = dock->aghRoofHeight();
  double ridgeL = dock->aghRidgeLength(), ridgeOff = dock->aghRidgeOffset();
  if ( W <= 0 || D <= 0 || H <= 0 || roofH <= 0 )
    return m;

  QVector3D v0( 0, 0, 0 ), v1( W, 0, 0 ), v2( W, D, 0 ), v3( 0, D, 0 );
  QVector3D v4( 0, 0, H ), v5( W, 0, H ), v6( W, D, H ), v7( 0, D, H );

  // 屋脊沿 X 轴（宽度方向）
  // ridgeOff  = 屋脊在 Y 方向的偏移（相对于 D/2）
  // ridgeL    = 屋脊长度（沿 X 方向）
  double rc = W / 2.0;            // 宽度中心
  double rs = rc - ridgeL / 2.0;  // 屋脊起点 X
  double re = rc + ridgeL / 2.0;  // 屋脊终点 X
  double ry = D / 2.0 + ridgeOff; // 屋脊 Y 位置

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
// ============================================================
// 四段式圆塔形：短圆柱底座 + 低坡弧面屋顶 + 顶部小尖锥
// ============================================================
MeshData BuildMesh::buildFourStageRoundTower( ParamModelerDock *dock )
{
    MeshData m;
    double baseR   = dock->ftBaseRadius();
    double baseH   = dock->ftBaseHeight();
    double roofH   = dock->ftMiddleHeight();
    double capR    = dock->ftMiddleTopRadius();
    double bulge   = dock->ftMiddleBulge();
    double coneH   = dock->ftConeHeight();
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
        m.addTriangle(bc, botRing[i], botRing[n]);
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

// ============================================================
// 双人字屋顶房屋：House 2 绕 House 1 的 C 点竖直轴旋转
// ============================================================
MeshData BuildMesh::buildTwoGableHouses( ParamModelerDock *dock )
{
    MeshData m;
    double L1    = dock->tgLength1();
    double L2    = dock->tgLength2();
    double W     = dock->tgWidth();
    double H     = dock->tgWallHeight();
    double roofH = dock->tgRoofHeight();
    double angle = dock->tgAngle();
    if ( L1<=0||L2<=0||W<=0||H<=0||roofH<=0 ) return m;
    if ( angle < 90.0 ) angle = 90.0;
    if ( angle > 180.0 ) angle = 180.0;

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
