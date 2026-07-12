/***************************************************************************
  parammodeler_datasetgen.h
  DL dataset batch generation helpers
  -------------------
         begin                : July 2026
         copyright            : (C) 2026 by Chai
         email                : 2080673411@qq.com
 ***************************************************************************/

#ifndef PARAMMODELER_DATASETGEN_H
#define PARAMMODELER_DATASETGEN_H

class ParamModelerDock;

void generateFullDataset( ParamModelerDock *dock );

void generateSinglePrimitiveDataset( ParamModelerDock *dock );

#endif // PARAMMODELER_DATASETGEN_H
