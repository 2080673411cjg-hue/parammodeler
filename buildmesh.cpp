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

// ============================================================
// 统一入口
// ============================================================
MeshData BuildMesh::build( const QString &primitiveType, ParamModelerDock *dock )
{
    if ( primitiveType == "Cuboid" )              return buildCuboid( dock );
    if ( primitiveType == "Cylinder" )            return buildCylinder( dock );
    if ( primitiveType == "LHouse" )              return buildLHouse( dock );
    if ( primitiveType == "ConeCylinder" )        return buildConeCylinder( dock );
    if ( primitiveType == "GabledRoof" )          return buildGabledRoof( dock );
    if ( primitiveType == "PyramidRoof" )         return buildPyramidRoof( dock );
    if ( primitiveType == "TruncatedPyramidRoof") return buildTruncatedPyramidRoof( dock );
    if ( primitiveType == "HalfCylinderRoof" )    return buildHalfCylinderRoof( dock );
    if ( primitiveType == "穹顶圆柱" )             return buildCylinderHemisphere( dock );
    if ( primitiveType == "凹陷长方体" )            return buildIndentedCuboid( dock );
    if ( primitiveType == "非对称人字形屋顶房屋" )   return buildAsymmetricGableHouse( dock );
    if ( primitiveType == "四段式圆塔形" )           return buildFourStageRoundTower( dock );
    if ( primitiveType == "双人字屋顶房屋" )         return buildTwoGableHouses( dock );
    return MeshData{};
}

// ============================================================
// 长方体
// ============================================================
MeshData BuildMesh::buildCuboid( ParamModelerDock *dock )
{
    MeshData m;
    double W = dock->cuboidWidth();
    double D = dock->cuboidDepth();
    double H = dock->cuboidHeight();
    if ( W <= 0 || D <= 0 || H <= 0 ) return m;

    QVector3D v0(0,0,0), v1(W,0,0), v2(W,D,0), v3(0,D,0);
    QVector3D v4(0,0,H), v5(W,0,H), v6(W,D,H), v7(0,D,H);

    m.addQuad( v0, v3, v2, v1 ); // 底面
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
    double Aw = dock->LMainWidth();
    double Ad = dock->LMainDepth();
    double Bw = dock->LWingWidth();
    double Bd = dock->LWingDepth();
    double H  = dock->LHeight();
    if ( Aw<=0||Ad<=0||Bw<=0||Bd<=0||H<=0 ) return m;

    // 7个底部顶点，7个顶部顶点
    QVector<QVector3D> bot = {
        {0,0,0}, {(float)Aw,0,0}, {(float)(Aw+Bw),0,0},
        {(float)(Aw+Bw),(float)Bd,0}, {(float)Aw,(float)Bd,0},
        {(float)Aw,(float)Ad,0}, {0,(float)Ad,0}
    };
    QVector<QVector3D> top;
    for ( auto &v : bot ) top << QVector3D(v.x(), v.y(), H);

    // 底面（三角扇形）
    for ( int i = 1; i < 6; i++ )
        m.addTriangle( bot[0], bot[i], bot[i+1] );
    // 顶面
    for ( int i = 1; i < 6; i++ )
        m.addTriangle( top[0], top[i+1], top[i] );
    // 侧面
    int n = bot.size();
    for ( int i = 0; i < n; i++ )
    {
        int j = (i+1)%n;
        m.addQuad( bot[i], bot[j], top[j], top[i] );
    }
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
    double W  = dock->gabledRoofWidth();
    double D  = dock->gabledRoofDepth();
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
    double W  = dock->pyramidWidth();
    double D  = dock->pyramidDepth();
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
    double W  = dock->tpBottomWidth();
    double D  = dock->tpBottomDepth();
    double HW = dock->tpWallHeight();
    double HR = dock->tpRoofHeight();
    double WT = dock->tpTopWidth();
    double DT = dock->tpTopDepth();
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
    double W  = dock->hcrWidth();
    double D  = dock->hcrDepth();
    double HW = dock->hcrWallHeight();
    double R  = dock->hcrRadius();
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
// 穹顶圆柱
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
    double W  = dock->icOuterWidth(),  D  = dock->icOuterDepth(),  H  = dock->icOuterHeight();
    double w  = dock->icInnerWidth(),  d  = dock->icInnerDepth(),  h  = dock->icInnerHeight();
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
  double W = dock->aghWidth(), D = dock->aghDepth();
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
// 四段式圆塔形
// ============================================================
MeshData BuildMesh::buildFourStageRoundTower( ParamModelerDock *dock )
{
    MeshData m;
    double baseR   = dock->ftBaseRadius();
    double baseH   = dock->ftBaseHeight();
    double midH    = dock->ftMiddleHeight();
    double midTopR = dock->ftMiddleTopRadius();
    double bulge   = dock->ftMiddleBulge();
    double coneH   = dock->ftConeHeight();
    if ( baseR<=0||baseH<=0||coneH<=0 ) return m;

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
        double P0x=baseR, P0y=baseH;
        double P1x=baseR*(1.0+bulge), P1y=baseH+midH*0.33;
        double P2x=midTopR*(1.0+bulge*0.5), P2y=baseH+midH*0.66;
        double P3x=midTopR, P3y=baseH+midH;
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
    QVector<QVector3D> topRing = makeRing(baseR, baseH);
    connectRings(botRing, topRing);

    // 贝塞尔中部
    QVector<QVector3D> prev = topRing;
    for ( int l=1; l<=layers; l++ ) {
        double t=l/double(layers), r, z;
        evalBezier(t, r, z);
        QVector<QVector3D> cur = makeRing(r, z);
        connectRings(prev, cur);
        prev = cur;
    }

    // 圆锥段
    QVector3D apex(0, 0, baseH+midH+coneH);
    for ( int i=0; i<seg; i++ ) {
        int n=(i+1)%seg;
        m.addTriangle(prev[i], prev[n], apex);
    }
    return m;
}

// ============================================================
// 双人字屋顶房屋
// ============================================================
MeshData BuildMesh::buildTwoGableHouses( ParamModelerDock *dock )
{
    MeshData m;
    double W1    = dock->tgWidth1();
    double W2    = dock->tgWidth2();
    double D     = dock->tgDepth();
    double H     = dock->tgWallHeight();
    double roofH = dock->tgRoofHeight();
    double angle = dock->tgAngle();
    if ( W1<=0||W2<=0||D<=0||H<=0||roofH<=0 ) return m;

    double turnAngle = 180.0 - angle;
    double bevelAngle = turnAngle / 2.0;
    double bevelRad = bevelAngle * M_PI / 180.0;
    double totalRad = turnAngle * M_PI / 180.0;

    // ---- 第一栋 ----
    QVector3D h1b0(0,0,0), h1b1(D,0,0), h1b2(D,W1,0), h1b3(0,W1,0);
    QVector3D h1w0(0,0,H), h1w1(D,0,H), h1w2(D,W1,H), h1w3(0,W1,H);
    QVector3D h1r0(D/2,0,H+roofH), h1r1(D/2,W1,H+roofH);

    m.addQuad(h1b3,h1b2,h1b1,h1b0); // 底
    m.addQuad(h1b0,h1b1,h1w1,h1w0); // 前墙
    m.addQuad(h1b1,h1b2,h1w2,h1w1); // 后墙
    m.addQuad(h1b2,h1b3,h1w3,h1w2); // 右墙
    m.addQuad(h1b3,h1b0,h1w0,h1w3); // 左墙
    m.addTriangle(h1w0,h1w1,h1r0);  // 前山墙
    m.addTriangle(h1w2,h1w3,h1r1);  // 后山墙
    m.addQuad(h1w0,h1w3,h1r1,h1r0); // 左坡
    m.addQuad(h1w1,h1w2,h1r1,h1r0); // 右坡（注意法线）
    m.addTriangle(h1w1,h1r0,h1r1);
    m.addTriangle(h1w1,h1r1,h1w2);

    // ---- 第二栋（旋转后） ----
    auto tr = [&]( double lx, double ly, double lz ) -> QVector3D {
        double rx = cos(totalRad)*lx - sin(totalRad)*ly;
        double ry = sin(totalRad)*lx + cos(totalRad)*ly;
        return QVector3D(0+rx, W1+ry, lz);
    };

    QVector3D h2b0=tr(0,0,0),    h2b1=tr(D,0,0);
    QVector3D h2b2=tr(D,W2,0),   h2b3=tr(0,W2,0);
    QVector3D h2w0=tr(0,0,H),    h2w1=tr(D,0,H);
    QVector3D h2w2=tr(D,W2,H),   h2w3=tr(0,W2,H);
    QVector3D h2r0=tr(D/2,0,H+roofH), h2r1=tr(D/2,W2,H+roofH);

    m.addQuad(h2b3,h2b2,h2b1,h2b0);
    m.addQuad(h2b0,h2b1,h2w1,h2w0);
    m.addQuad(h2b1,h2b2,h2w2,h2w1);
    m.addQuad(h2b2,h2b3,h2w3,h2w2);
    m.addQuad(h2b3,h2b0,h2w0,h2w3);
    m.addTriangle(h2w0,h2w1,h2r0);
    m.addTriangle(h2w2,h2w3,h2r1);
    m.addQuad(h2w0,h2w3,h2r1,h2r0);
    m.addTriangle(h2w1,h2r0,h2r1);
    m.addTriangle(h2w1,h2r1,h2w2);

    return m;
}
