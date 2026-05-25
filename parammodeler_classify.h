/***************************************************************************
  parammodeler_classify.h
  Point Cloud Primitive Classification (orchestrator)
  -------------------
         begin                : May 2026
         copyright            : (C) 2026 by Chai
         email                : 2080673411@qq.com
 ***************************************************************************/

#ifndef PARAMMODELER_CLASSIFY_H
#define PARAMMODELER_CLASSIFY_H

#include "parammodeler_pcdtypes.h"
#include <QString>
#include <QVector>

class PrimitiveClassifier
{
public:
    static PcdResult classify( const QString &filePath );

private:
    static QVector<TypeProfile> buildProfiles();
    static double scoreProfile( const FeatureVector &fv, const TypeProfile &tp );
    static PcdResult classifyByScore( const FeatureVector &fv );
};

#endif // PARAMMODELER_CLASSIFY_H
