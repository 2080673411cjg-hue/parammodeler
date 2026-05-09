/***************************************************************************
  parammodeler_inverse.h
  Point Cloud Parameter Inversion
  -------------------
         begin                : May 2026
         copyright            : (C) 2026 by Chai
         email                : 2080673411@qq.com
 ***************************************************************************/

#ifndef PARAMMODELER_INVERSE_H
#define PARAMMODELER_INVERSE_H

#include <QString>
#include <QMap>
#include <QVector>
#include <QVector3D>

class ParamModelerDock;

class ParamInverter
{
public:
    static QMap<QString, double> invert( const QString &primitiveType,
                                          const QString &filePath );

    static void applyToUI( ParamModelerDock *dock,
                            const QMap<QString, double> &params );

private:
    static QVector<QVector3D> loadPoints( const QString &filePath );

    // Per-primitive inversion
    static QMap<QString, double> invertCuboid( const QVector<QVector3D> &pts );
    static QMap<QString, double> invertCylinder( const QVector<QVector3D> &pts );
    static QMap<QString, double> invertLHouse( const QVector<QVector3D> &pts );
    static QMap<QString, double> invertConeCylinder( const QVector<QVector3D> &pts );
    static QMap<QString, double> invertGabledRoof( const QVector<QVector3D> &pts );
    static QMap<QString, double> invertPyramidRoof( const QVector<QVector3D> &pts );
    static QMap<QString, double> invertTruncatedPyramidRoof( const QVector<QVector3D> &pts );
    static QMap<QString, double> invertHalfCylinderRoof( const QVector<QVector3D> &pts );
    static QMap<QString, double> invertCylinderHemisphere( const QVector<QVector3D> &pts );
    static QMap<QString, double> invertIndentedCuboid( const QVector<QVector3D> &pts );
    static QMap<QString, double> invertAsymmetricGableHouse( const QVector<QVector3D> &pts );
    static QMap<QString, double> invertFourStageRoundTower( const QVector<QVector3D> &pts );
    static QMap<QString, double> invertTwoGableHouses( const QVector<QVector3D> &pts );

    // Utility functions
    static void fitCircleRANSAC( const QVector<QVector3D> &pts,
                                  double &cx, double &cy, double &radius,
                                  int iterations = 200, double inlierThresh = 0.15 );
    static void fitLineRANSAC( const QVector<QVector3D> &pts,
                                QVector3D &origin, QVector3D &direction,
                                int iterations = 200, double inlierThresh = 0.15 );
    static int  findHeightSplit( const QVector<double> &zValues,
                                  double wallRatio );
    static void computeFootprintBounds( const QVector<QVector3D> &pts,
                                         double &minX, double &maxX,
                                         double &minY, double &maxY );
};

#endif // PARAMMODELER_INVERSE_H
