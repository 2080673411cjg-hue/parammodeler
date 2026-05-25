/***************************************************************************
  parammodeler_pcdloader.cpp
  Point Cloud File Loader (PLY / LAS / LAZ)
  -------------------
         begin                : May 2026
         copyright            : (C) 2026 by Chai
         email                : 2080673411@qq.com
 ***************************************************************************/

#include "parammodeler_pcdloader.h"

#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <algorithm>
#include <cmath>

#include <qgspointcloudlayer.h>
#include <qgspointcloudindex.h>
#include <qgspointcloudblock.h>
#include <qgspointcloudrequest.h>
#include <qgspointcloudattribute.h>
#include <qgsvector3d.h>

// ====================================================================
// PLY 头部解析辅助
// ====================================================================
static int parsePlyHeader( QFile &file, int &vertexCount,
                            QMap<QString,int> &propOffset,
                            QMap<QString,int> &propSize,
                            QMap<QString,bool> &propIsDouble,
                            int &recordSize, bool &isAscii, bool &isBigEndian )
{
    vertexCount = 0;
    recordSize = 0;
    isAscii = false;
    isBigEndian = false;
    propOffset.clear();
    propSize.clear();
    propIsDouble.clear();

    file.seek( 0 );
    int curOff = 0;

    while ( !file.atEnd() )
    {
        QByteArray lineBA;
        char c;
        while ( file.getChar( &c ) )
        {
            if ( c == '\n' ) break;
            if ( c != '\r' ) lineBA.append( c );
        }
        QString line = QString::fromLatin1( lineBA ).trimmed();

        if ( line.startsWith( "format" ) )
        {
            if ( line.contains( "ascii" ) )                isAscii = true;
            else if ( line.contains( "binary_little_endian" ) ) isAscii = false;
            else if ( line.contains( "binary_big_endian" ) )    { isAscii = false; isBigEndian = true; }
        }
        else if ( line.startsWith( "element vertex" ) )
        {
            vertexCount = line.split( ' ', Qt::SkipEmptyParts ).last().toInt();
        }
        else if ( line.startsWith( "property" ) )
        {
            QStringList tok = line.split( ' ', Qt::SkipEmptyParts );
            if ( tok.size() >= 3 )
            {
                QString tname = tok[1];
                QString pname = tok[2];
                int sz = 4;
                bool isDbl = false;
                if      ( tname == "double" || tname == "float64" ) { sz = 8; isDbl = true; }
                else if ( tname == "float"  || tname == "float32" ) { sz = 4; isDbl = false; }
                else if ( tname == "int"    || tname == "int32" ||
                          tname == "uint"   || tname == "uint32"  ) { sz = 4; isDbl = false; }
                else if ( tname == "short"  || tname == "int16"   ||
                          tname == "ushort" || tname == "uint16"  ) { sz = 2; isDbl = false; }
                else if ( tname == "char"   || tname == "uchar"   ||
                          tname == "int8"   || tname == "uint8"   ) { sz = 1; isDbl = false; }
                propOffset[pname] = curOff;
                propSize[pname] = sz;
                propIsDouble[pname] = isDbl;
                curOff += sz;
            }
        }
        else if ( line == "end_header" )
        {
            break;
        }
    }
    recordSize = curOff;
    return 0;
}

// ====================================================================
// 加载点云
// ====================================================================
PointCloud PointCloudLoader::load( const QString &filePath )
{
    PointCloud pc;
    QFileInfo fi( filePath );
    QString suffix = fi.suffix().toLower();

    if ( suffix == "ply" )
    {
        QFile file( filePath );
        if ( !file.open( QIODevice::ReadOnly ) ) return pc;

        int vertexCount = 0, recordSize = 0;
        bool isAscii = false, isBigEndian = false;
        QMap<QString,int> propOffset, propSize;
        QMap<QString,bool> propIsDouble;

        parsePlyHeader( file, vertexCount, propOffset, propSize,
                         propIsDouble, recordSize, isAscii, isBigEndian );

        if ( vertexCount <= 0 || !propOffset.contains( "x" ) ||
              !propOffset.contains( "y" ) || !propOffset.contains( "z" ) )
        {
            file.close();
            return pc;
        }

        int xOff = propOffset["x"], xSz = propSize["x"]; bool xDbl = propIsDouble["x"];
        int yOff = propOffset["y"], ySz = propSize["y"]; bool yDbl = propIsDouble["y"];
        int zOff = propOffset["z"], zSz = propSize["z"]; bool zDbl = propIsDouble["z"];

        auto readDouble = [&]( const QByteArray &rec, int off, int sz, bool isDbl, bool bigEndian ) -> double
        {
            if ( isDbl )
            {
                double v;
                memcpy( &v, rec.constData() + off, 8 );
                if ( bigEndian ) { char *b = reinterpret_cast<char*>(&v); std::reverse( b, b + 8 ); }
                return v;
            }
            else
            {
                float v;
                memcpy( &v, rec.constData() + off, qMin( sz, 4 ) );
                if ( bigEndian ) { char *b = reinterpret_cast<char*>(&v); std::reverse( b, b + 4 ); }
                return static_cast<double>( v );
            }
        };

        pc.points.reserve( vertexCount );
        pc.originalCount = vertexCount;

        if ( isAscii )
        {
            // Determine column order by offset
            QList<QPair<int,QString>> orderList;
            for ( auto it = propOffset.begin(); it != propOffset.end(); ++it )
                orderList.append( { it.value(), it.key() } );
            std::sort( orderList.begin(), orderList.end() );
            QStringList propOrder;
            for ( auto &p : orderList ) propOrder << p.second;
            int xCol = propOrder.indexOf( "x" );
            int yCol = propOrder.indexOf( "y" );
            int zCol = propOrder.indexOf( "z" );
            int need = std::max( { xCol, yCol, zCol } ) + 1;

            int count = 0;
            while ( !file.atEnd() && count < vertexCount )
            {
                QByteArray lineBA = file.readLine().trimmed();
                if ( lineBA.isEmpty() ) continue;
                QList<QByteArray> parts = lineBA.split( ' ' );
                parts.removeAll( QByteArray() );
                if ( parts.size() < need ) continue;

                double x = parts[xCol].toDouble();
                double y = parts[yCol].toDouble();
                double z = parts[zCol].toDouble();
                pc.points.append( QVector3D( x, y, z ) );
                count++;
            }
        }
        else
        {
            for ( int i = 0; i < vertexCount; i++ )
            {
                QByteArray rec = file.read( recordSize );
                if ( rec.size() < recordSize ) break;
                double x = readDouble( rec, xOff, xSz, xDbl, isBigEndian );
                double y = readDouble( rec, yOff, ySz, yDbl, isBigEndian );
                double z = readDouble( rec, zOff, zSz, zDbl, isBigEndian );
                pc.points.append( QVector3D( x, y, z ) );
            }
        }
        file.close();
    }
    else if ( suffix == "las" || suffix == "laz" )
    {
        QgsPointCloudLayer *tmpLayer = new QgsPointCloudLayer( filePath, "tmp_classify", "pdal" );
        if ( !tmpLayer || !tmpLayer->isValid() )
        {
            delete tmpLayer;
            return pc;
        }

        QgsPointCloudIndex index = tmpLayer->dataProvider()->index();
        if ( !index.isValid() )
        {
            delete tmpLayer;
            return pc;
        }

        QgsPointCloudAttributeCollection attrs;
        attrs.push_back( QgsPointCloudAttribute( "X", QgsPointCloudAttribute::Int32 ) );
        attrs.push_back( QgsPointCloudAttribute( "Y", QgsPointCloudAttribute::Int32 ) );
        attrs.push_back( QgsPointCloudAttribute( "Z", QgsPointCloudAttribute::Int32 ) );
        QgsPointCloudRequest request;
        request.setAttributes( attrs );

        QgsVector3D scale = index.scale();
        QgsVector3D offset = index.offset();

        QList<QgsPointCloudNodeId> queue;
        queue.append( index.root() );

        while ( !queue.isEmpty() )
        {
            QgsPointCloudNodeId nodeId = queue.takeFirst();
            QgsPointCloudNode node = index.getNode( nodeId );
            for ( const QgsPointCloudNodeId &child : node.children() )
                queue.append( child );

            std::unique_ptr<QgsPointCloudBlock> block = index.nodeData( nodeId, request );
            if ( !block ) continue;

            const char *data = block->data();
            int ptCnt = block->pointCount();
            int recSz = block->pointRecordSize();

            for ( int i = 0; i < ptCnt; i++ )
            {
                const char *ptr = data + i * recSz;
                qint32 ix = *reinterpret_cast<const qint32*>( ptr );
                qint32 iy = *reinterpret_cast<const qint32*>( ptr + 4 );
                qint32 iz = *reinterpret_cast<const qint32*>( ptr + 8 );
                double x = ix * scale.x() + offset.x();
                double y = iy * scale.y() + offset.y();
                double z = iz * scale.z() + offset.z();
                pc.points.append( QVector3D( x, y, z ) );
            }
        }
        pc.originalCount = pc.points.size();
        delete tmpLayer;
    }

    // Compute bounding box
    if ( !pc.points.isEmpty() )
    {
        pc.bboxMin = pc.points[0];
        pc.bboxMax = pc.points[0];
        for ( const QVector3D &p : pc.points )
        {
            if ( p.x() < pc.bboxMin.x() ) pc.bboxMin.setX( p.x() );
            if ( p.y() < pc.bboxMin.y() ) pc.bboxMin.setY( p.y() );
            if ( p.z() < pc.bboxMin.z() ) pc.bboxMin.setZ( p.z() );
            if ( p.x() > pc.bboxMax.x() ) pc.bboxMax.setX( p.x() );
            if ( p.y() > pc.bboxMax.y() ) pc.bboxMax.setY( p.y() );
            if ( p.z() > pc.bboxMax.z() ) pc.bboxMax.setZ( p.z() );
        }
    }
    return pc;
}
